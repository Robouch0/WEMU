#include "Gx2.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <unordered_map>

#include "AsyncCallbacks.hpp"
#include "Coreinit.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "gfx/Gx2BufferSnapshots.hpp"
#include "gfx/Gx2CommandStream.hpp"
#include "gfx/Gx2Replayer.hpp"
#include "gfx/LatteProgram.hpp"
#include "gfx/LatteSurfaceLayout.hpp"
#include "gfx/Renderer.hpp"
#include "gfx/SurfaceLayout.hpp"
#include "gfx/VulkanRasterBackend.hpp"
#include "utils/Diagnostics.hpp"
#include "utils/DisplayTiming.hpp"
#include "utils/FrameProfiler.hpp"
#include "utils/Logger.hpp"

// GX2 high-level emulation.
//
// Two responsibilities:
//  (1) Query/sizing functions write real values into guest structs (zeros -> zero-size allocations).
//  (2) Render functions (draw/clear/state/shader/present) are CAPTURED into an ordered command
//      stream (gfx/Gx2CommandStream) for the upcoming GX2->Vulkan replay stage. Everything else is a
//      no-op because the title fills its own GX2 structs and only the GPU consumes the registers.

namespace {

    // Replays one captured frame at a time on its own host thread, so GX2 lowering and Vulkan
    // submission overlap the guest CPU's next frame, the way the Wii U GPU runs behind the CPU.
    class ReplayWorker {
        public:
            ReplayWorker()
            {
#ifdef __APPLE__
                // Darwin can only name the calling thread.
                m_thread = std::thread([this] {
                    pthread_setname_np("wemu-replay");
                    loop();
                });
#else
                m_thread = std::thread([this] { loop(); });
                pthread_setname_np(m_thread.native_handle(), "wemu-replay");
#endif
            }
            ~ReplayWorker()
            {
                {
                    std::lock_guard lock(m_mutex);
                    m_stop = true;
                }
                m_wake.notify_all();
                m_thread.join();
            }
            ReplayWorker(const ReplayWorker &) = delete;
            ReplayWorker &operator=(const ReplayWorker &) = delete;

            // Callers wait() first: one frame is in flight at most.
            void submit(std::function<void()> job)
            {
                {
                    std::lock_guard lock(m_mutex);
                    m_job = std::move(job);
                }
                m_wake.notify_all();
            }
            // Blocks until the submitted frame finished; its failure is rethrown on the guest thread.
            void wait()
            {
                std::unique_lock lock(m_mutex);
                m_done.wait(lock, [&] { return !m_job; });
                if (auto error = std::exchange(m_error, nullptr))
                    std::rethrow_exception(error);
            }

        private:
            void loop()
            {
                std::unique_lock lock(m_mutex);
                for (;;) {
                    m_wake.wait(lock, [&] { return m_stop || m_job; });
                    if (!m_job)
                        return;
                    const auto job = m_job;
                    lock.unlock();
                    std::exception_ptr error;
                    try {
                        job();
                    } catch (...) {
                        error = std::current_exception();
                    }
                    lock.lock();
                    m_error = error;
                    m_job = nullptr;
                    m_done.notify_all();
                }
            }

            std::mutex m_mutex;
            std::condition_variable m_wake, m_done;
            std::function<void()> m_job;
            std::exception_ptr m_error;
            bool m_stop{};
            std::thread m_thread;
    };

    struct HostReplayState {
            struct BufferAllocatorCall {
                    std::uint32_t stack, lr, buffer;
                    bool freeing;
            };
            std::unordered_map<std::uint32_t, std::vector<BufferAllocatorCall>> bufferCalls;
            std::unique_ptr<Core::Gfx::Gx2Replayer> replayer;
            bool nativeInitialized{};
            // Asynchronous replay: the frame owned by the worker while it is in flight.
            std::unique_ptr<ReplayWorker> worker;
            std::unique_ptr<Core::Gfx::Gx2BufferSnapshots> bufferSnapshots;
            Core::Gfx::Gx2CommandStream frame;
    };
    HostReplayState &hostReplayState()
    {
        static HostReplayState state;
        return state;
    }

    // Cafe exports its default allocators as writable data pointers. Application
    // preinit may replace them after reserving the base heap for its own arena.
    bool callBufferAllocator(Core::Interpreter &cpu, const char *name, std::uint32_t buffer, bool freeing)
    {
        for (const auto &symbol: cpu.m_binary.symbols) {
            if (symbol.name != name || symbol.meta.type != STT_OBJECT)
                continue;
            const auto function = cpu.m_memory.read<std::uint32_t>(symbol.meta.virtAddress);
            if (!function || function >= 0xC0000000u || (function & 3))
                return false;
            const auto stack = cpu.m_gpr[1];
            hostReplayState().bufferCalls[cpu.m_scheduler.currentHandle()].push_back({stack, cpu.m_lr, buffer, freeing});
            cpu.m_gpr[1] = stack - 0x20;
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[1], stack);
            cpu.m_lr = Core::Gx2::bufferAllocatorReturn - Core::Memory::ApplicationCode;
            cpu.m_nextPc = function - Core::Memory::ApplicationCode;
            cpu.m_hle_redirected = true;
            return true;
        }
        return false;
    }

    constexpr std::uint32_t alignUp(std::uint32_t v, std::uint32_t a) { return (v + a - 1) & ~(a - 1); }
    constexpr std::uint32_t GX2_BUFFER_ALIGN = 0x800;

    constexpr std::uint32_t SURF_WIDTH = 0x04, SURF_HEIGHT = 0x08, SURF_DEPTH = 0x0C, SURF_FORMAT = 0x14;
    constexpr std::uint32_t SURF_IMAGE_SIZE = 0x20, SURF_MIPMAP_SIZE = 0x28, SURF_ALIGNMENT = 0x38, SURF_PITCH = 0x3C;

    // Bytes per element, where the element of a BC (block-compressed) format is a 4x4 texel block.
    // Getting this wrong inflates every guest texture allocation (MK8's 3 MB UI texture pool
    // overflows and the decoder writes through a null buffer).
    bool isBlockCompressed(std::uint32_t format) { return (format & 0x3F) >= 0x31 && (format & 0x3F) <= 0x35; }

    std::uint32_t bytesPerElement(std::uint32_t format)
    {
        switch (format & 0x3F) {
            case 0x01:
            case 0x02:
            case 0x03:
                return 1; // 8 / 4_4 / 3_3_2
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0C:
                return 2;
            case 0x1C: // X24_8_32_FLOAT
            case 0x1D: // 32_32
            case 0x1E: // 32_32_FLOAT
            case 0x1F: // 16_16_16_16
            case 0x20:
                return 8; // 16_16_16_16_FLOAT
            case 0x22: // 32_32_32_32
            case 0x23: // 32_32_32_32_FLOAT (GX2 0x823, RGBA32F)
            case 0x30:
                return 16;
            case 0x31: // BC1
            case 0x34:
                return 8; // BC4
            case 0x32: // BC2
            case 0x33: // BC3
            case 0x35:
                return 16; // BC5
            default:
                return 4;
        }
    }

    bool gx2SizeTrace()
    {
        static const bool enabled = []() {
            const char *env = std::getenv("WEMU_GX2_SIZE_TRACE");
            return env && env[0] == '1';
        }();
        return enabled;
    }

    std::uint32_t gx2SizeTraceMax()
    {
        static const std::uint32_t maxHits = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_GX2_SIZE_TRACE_MAX");
            return env ? std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0))) : 256;
        }();
        return maxHits;
    }

    std::uint32_t gx2SizeTraceMin()
    {
        static const std::uint32_t minSize = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_GX2_SIZE_TRACE_MIN");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0)) : 0;
        }();
        return minSize;
    }

    bool gx2SizeTraceTake()
    {
        static std::uint32_t hits = 0;
        if (++hits > gx2SizeTraceMax())
            return false;
        return true;
    }

    // ---- display lists -------------------------------------------------------------------------
    //
    // GX2BeginDisplayListEx(buffer, size, profiling) redirects every following GX2 call into
    // `buffer` instead of the GPU; GX2EndDisplayList closes it and returns the bytes used;
    // GX2CallDisplayList / GX2DirectCallDisplayList(buffer, size) execute a recorded list, often
    // once per frame and often many times. MK8 draws nearly all of its scene and nw::lyt UI this
    // way (a boot records ~48k lists and calls them ~18k times), so treating the calls as no-ops
    // dropped almost every real draw — only the handful of direct post-process passes survived.
    //
    // We record into a per-buffer command vector and splice it into the live stream on call.
    // Recording can nest (a list may call another), so pushes always go to the active sink.
    // The open list belongs to the recording thread (GX2 keeps it per core): MK8 builds lists on
    // worker threads while the render thread submits, and a shared sink would interleave their
    // commands whenever the scheduler switches threads mid-recording.
    constexpr std::uint32_t kDlBytesPerCmd = 32; // nominal PM4 bytes per captured call
    struct OpenDisplayList {
            std::uint32_t buffer{0};
            std::uint32_t capacity{0};
            std::vector<Core::Gfx::Gx2Command> commands;
    };
    std::unordered_map<std::uint32_t, std::vector<OpenDisplayList>> g_openLists; // by OSThread
    std::vector<OpenDisplayList> &openLists(const Core::Interpreter &cpu) { return g_openLists[cpu.m_scheduler.currentHandle()]; }
    using CapturedList = std::vector<Core::Gfx::Gx2Command>;
    std::unordered_map<std::uint32_t, std::shared_ptr<const CapturedList>> g_displayLists;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> g_fetchLayouts;

    // Fetch shaders loaded from a title's shader archives never pass through GX2InitFetchShaderEx,
    // so rebuild their GX2AttribStream layout (location, buffer, offset, format, type, divisor,
    // mask, endian) from the VTX_FETCH microcode itself.
    std::vector<std::uint32_t> decodeFetchShader(Core::Interpreter &cpu, std::uint32_t shader)
    {
        using namespace Core::Gfx::Latte;
        std::vector<std::uint32_t> layout;
        try {
            const auto size = cpu.m_memory.read<std::uint32_t>(shader + 8);
            const auto program = cpu.m_memory.read<std::uint32_t>(shader + 0xC);
            if (!program || size < 16 || size > 0x10000)
                return layout;
            // Latte microcode is stored little-endian in guest memory.
            const auto word = [&](std::uint32_t index) { return std::byteswap(cpu.m_memory.read<std::uint32_t>(program + index * 4)); };
            // VTX DATA_FORMAT -> GX2AttribFormat component/type code (low byte).
            const auto attribType = [](std::uint32_t dataFormat) -> int {
                switch (dataFormat) {
                    case 0x01:
                        return 0x00; // 8
                    case 0x02:
                        return 0x01; // 4_4
                    case 0x05:
                        return 0x02; // 16
                    case 0x06:
                        return 0x03; // 16 float
                    case 0x07:
                        return 0x04; // 8_8
                    case 0x0D:
                        return 0x05; // 32
                    case 0x0E:
                        return 0x06; // 32 float
                    case 0x0F:
                        return 0x07; // 16_16
                    case 0x10:
                        return 0x08; // 16_16 float
                    case 0x19:
                        return 0x0B; // 2_10_10_10 (X in the low bits)
                    case 0x1A:
                        return 0x0A; // 8_8_8_8
                    case 0x1D:
                        return 0x0C; // 32_32
                    case 0x1E:
                        return 0x0D; // 32_32 float
                    case 0x1F:
                        return 0x0E; // 16_16_16_16
                    case 0x20:
                        return 0x0F; // 16_16_16_16 float
                    case 0x2F:
                        return 0x10; // 32_32_32
                    case 0x30:
                        return 0x11; // 32_32_32 float
                    case 0x22:
                        return 0x12; // 32_32_32_32
                    case 0x23:
                        return 0x13; // 32_32_32_32 float
                    default:
                        return -1;
                }
            };
            for (std::uint32_t cf = 0; cf + 1 < size / 4 && cf < 64; cf += 2) {
                const auto w0 = word(cf), w1 = word(cf + 1);
                const auto inst = cfInst(w1);
                if (inst == CF_VTX || inst == CF_VTX_TC) {
                    const auto count = (((w1 >> 10) & 7) | ((w1 >> 16) & 8)) + 1;
                    for (std::uint32_t i = 0; i < count; ++i) {
                        const auto base = w0 * 2 + i * 4;
                        if (base + 3 > size / 4)
                            return {};
                        const auto v0 = word(base), v1 = word(base + 1), v2 = word(base + 2);
                        const auto type = attribType((v1 >> 22) & 63);
                        if (type < 0)
                            return {};
                        const auto numFormat = (v1 >> 28) & 3;
                        const bool isFloat =
                                type == 0x03 || type == 0x06 || type == 0x08 || type == 0x0D || type == 0x0F || type == 0x11 || type == 0x13;
                        std::uint32_t format = std::uint32_t(type);
                        if (numFormat == 1)
                            format |= 0x100; // integer
                        if ((v1 >> 30) & 1)
                            format |= 0x200; // signed
                        if (isFloat || numFormat == 2)
                            format |= 0x800; // float / scaled
                        const auto bufferId = (v0 >> 8) & 255;
                        const auto location = (v0 & 31) == 1 ? (v1 & 255) : (v1 & 127); // semantic id or destination GPR
                        const auto mask = (((v1 >> 9) & 7) << 24) | (((v1 >> 12) & 7) << 16) | (((v1 >> 15) & 7) << 8) | ((v1 >> 18) & 7);
                        layout.insert(layout.end(), {location, bufferId >= 160 ? bufferId - 160 : bufferId, v2 & 0xFFFF, format, (v0 >> 5) & 3, 0,
                                                     mask, (v2 >> 16) & 3});
                    }
                }
                if (cfEop(w1) || inst == CF_RETURN)
                    break;
            }
        } catch (const Core::MemoryException &) {
            layout.clear();
        }
        return layout;
    }

    // Bring-up telemetry: how much geometry actually flows through the display-list path.
    struct DlStats {
            std::uint64_t begins = 0, ends = 0, recorded = 0, emptyLists = 0;
            std::uint64_t calls = 0, hits = 0, misses = 0, spliced = 0, nested = 0;
            std::uint64_t recDraws = 0, splicedDraws = 0, directDraws = 0;
    } g_dlStats;

    bool isDrawCmd(Core::Gfx::Gx2Cmd t) { return t == Core::Gfx::Gx2Cmd::DrawEx || t == Core::Gfx::Gx2Cmd::DrawIndexedEx; }

    void dlReport()
    {
        std::fprintf(stderr,
                     "[GX2DL] begin=%llu end=%llu rec=%llu empty=%llu | call=%llu hit=%llu miss=%llu spliced=%llu nested=%llu lists=%zu"
                     " | draws rec=%llu spliced=%llu toStream=%llu\n",
                     (unsigned long long) g_dlStats.begins, (unsigned long long) g_dlStats.ends, (unsigned long long) g_dlStats.recorded,
                     (unsigned long long) g_dlStats.emptyLists, (unsigned long long) g_dlStats.calls, (unsigned long long) g_dlStats.hits,
                     (unsigned long long) g_dlStats.misses, (unsigned long long) g_dlStats.spliced, (unsigned long long) g_dlStats.nested,
                     g_displayLists.size(),
                     // toStream counts every draw that reached the frame stream, spliced ones included.
                     (unsigned long long) g_dlStats.recDraws, (unsigned long long) g_dlStats.splicedDraws,
                     (unsigned long long) g_dlStats.directDraws);
    }

    void pushCmd(const Core::Interpreter &cpu, const Core::Gfx::Gx2Command &c)
    {
        if (auto &lists = openLists(cpu); !lists.empty()) {
            if (isDrawCmd(c.type))
                g_dlStats.recDraws++;
            lists.back().commands.push_back(c);
        } else {
            if (isDrawCmd(c.type))
                g_dlStats.directDraws++;
            Core::Gfx::gx2Stream().push(c);
        }
    }

    // ---- command capture ----------------------------------------------------------------------

    // Snapshot the argument registers (r3..r10, f1..f8) for a captured GX2 call.
    void recordCmd(Core::Interpreter &cpu, Core::Gfx::Gx2Cmd type)
    {
        Core::Gfx::Gx2Command c;
        c.type = type;
        for (std::size_t i = 0; i < 8; i++) {
            c.gpr[i] = cpu.m_gpr[3 + i];
            c.fpr[i] = cpu.m_fpr[1 + i];
        }
        c.callerLr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
        if (type == Core::Gfx::Gx2Cmd::SetFetchShader && c.gpr[0]) {
            const auto program = cpu.m_memory.read<std::uint32_t>(c.gpr[0] + 0xC);
            auto it = g_fetchLayouts.find(program);
            if (it == g_fetchLayouts.end()) {
                it = g_fetchLayouts.emplace(program, decodeFetchShader(cpu, c.gpr[0])).first;
                static const bool trace = std::getenv("WEMU_FETCH_TRACE") != nullptr;
                if (trace && it->second.empty()) {
                    std::string words;
                    try {
                        const auto size = cpu.m_memory.read<std::uint32_t>(c.gpr[0] + 8);
                        for (std::uint32_t i = 0; i < 12; ++i)
                            words += std::format(" {:08X}", cpu.m_memory.read<std::uint32_t>(program + i * 4));
                        Utils::Log::error("[FETCHDECODE] program={:08X} size={} undecoded:{}", program, size, words);
                    } catch (const Core::MemoryException &) {
                        Utils::Log::error("[FETCHDECODE] program={:08X} unreadable", program);
                    }
                }
                if (trace)
                    for (std::size_t i = 0; i + 7 < it->second.size(); i += 8)
                        Utils::Log::error("[FETCHDECODE] program={:08X} loc={} buf={} off={} fmt={:X} type={} mask={:08X} endian={}", program,
                                          it->second[i], it->second[i + 1], it->second[i + 2], it->second[i + 3], it->second[i + 4],
                                          it->second[i + 6], it->second[i + 7]);
            }
            c.payload = it->second;
        }
        // Uniform data lives in stack/ring memory the game reuses immediately — copy it now.
        std::uint32_t ptr = 0, words = 0;
        if (type == Core::Gfx::Gx2Cmd::SetStreamOutBuffer || type == Core::Gfx::Gx2Cmd::SetStreamOutContext ||
            type == Core::Gfx::Gx2Cmd::SaveStreamOutContext) {
            ptr = c.gpr[1];
            words = 8;
        } else if (type == Core::Gfx::Gx2Cmd::SetVertexUniformReg || type == Core::Gfx::Gx2Cmd::SetPixelUniformReg) {
            ptr = c.gpr[2]; // (offset, count, data)
            words = std::min<std::uint32_t>(c.gpr[1], 0x400);
        } else if (type == Core::Gfx::Gx2Cmd::SetVertexUniformBlock || type == Core::Gfx::Gx2Cmd::SetPixelUniformBlock) {
            ptr = c.gpr[2]; // (location, size, data)
            words = std::min<std::uint32_t>(c.gpr[1] / 4, 0x1000);
        } else if (type == Core::Gfx::Gx2Cmd::SetPixelSampler || type == Core::Gfx::Gx2Cmd::SetVertexSampler) {
            ptr = c.gpr[0];
            words = 3;
        } else if (type == Core::Gfx::Gx2Cmd::SetColorBuffer || type == Core::Gfx::Gx2Cmd::SetDepthBuffer ||
                   type == Core::Gfx::Gx2Cmd::SetPixelTexture || type == Core::Gfx::Gx2Cmd::SetVertexTexture ||
                   type == Core::Gfx::Gx2Cmd::ClearColor || type == Core::Gfx::Gx2Cmd::ClearDepthStencilEx ||
                   type == Core::Gfx::Gx2Cmd::ClearBuffersEx || type == Core::Gfx::Gx2Cmd::CopyColorBufferToScanBuffer) {
            // r3 points at a GX2ColorBuffer/DepthBuffer/Texture whose leading GX2Surface fields
            // the replay stage parses. The title often builds these structs on its STACK, so the
            // pointer is stale by frame-end replay — snapshot the struct now, at call time.
            ptr = c.gpr[0];
            if (type == Core::Gfx::Gx2Cmd::SetPixelTexture || type == Core::Gfx::Gx2Cmd::SetVertexTexture)
                words = 0x9C / 4; // retain the texture view, component map, and resource registers
            else
                words = 0x80 / 4; // color/depth viewMip, viewFirstSlice and viewNumSlices
        }
        if (ptr && words) {
            if (const auto *source = std::as_const(cpu.m_memory).hostPtr(ptr, std::size_t(words) * 4)) {
                auto &payload = c.payload.overwrite(words);
                for (std::uint32_t i = 0; i < words; i++) {
                    std::uint32_t word;
                    std::memcpy(&word, source + std::size_t(i) * 4, 4);
                    payload[i] = std::byteswap(word);
                }
            } else {
                c.payload.reserve(words);
                try {
                    for (std::uint32_t i = 0; i < words; i++)
                        c.payload.push_back(cpu.m_memory.read<std::uint32_t>(ptr + i * 4));
                } catch (const Core::MemoryException &) {
                    c.payload.clear();
                }
            }
        }
        if (type == Core::Gfx::Gx2Cmd::ClearBuffersEx && c.gpr[1]) {
            // Both buffers may be stack allocated. Keep their snapshots together.
            c.payload.resize(32);
            try {
                for (unsigned i = 0; i < 32; ++i)
                    c.payload.push_back(cpu.m_memory.read<std::uint32_t>(c.gpr[1] + i * 4));
            } catch (const Core::MemoryException &) {
                c.payload.resize(32);
            }
        }
        static const bool depthTrace = [] {
            const auto *value = std::getenv("WEMU_DEPTH_TRACE");
            return value && value[0] == '1';
        }();
        if (depthTrace && (type == Core::Gfx::Gx2Cmd::SetDepthBuffer || type == Core::Gfx::Gx2Cmd::SetDepthStencilControl ||
                           type == Core::Gfx::Gx2Cmd::ClearDepthStencilEx || type == Core::Gfx::Gx2Cmd::ClearBuffersEx)) {
            Utils::Log::error("[DEPTH_CAPTURE] cmd={} args={:X},{:X},{:X},{:X},{:X},{:X},{:X},{:X} "
                              "floats={},{},{},{},{},{} size={}x{} fmt={:X} tile={} pitch={}",
                              unsigned(type), c.gpr[0], c.gpr[1], c.gpr[2], c.gpr[3], c.gpr[4], c.gpr[5], c.gpr[6], c.gpr[7], c.fpr[0], c.fpr[1],
                              c.fpr[2], c.fpr[3], c.fpr[4], c.fpr[5], c.payload.size() >= 16 ? c.payload[1] : 0,
                              c.payload.size() >= 16 ? c.payload[2] : 0, c.payload.size() >= 16 ? c.payload[5] : 0,
                              c.payload.size() >= 16 ? c.payload[12] : 0, c.payload.size() >= 16 ? c.payload[15] : 0);
        }
        // Boot-long probe: does the title EVER upload a nonzero projection (regs c7/c8, i.e.
        // words 28..35 of the vertex uniform-register file)? Logs the first hits only.
        if (type == Core::Gfx::Gx2Cmd::SetVertexUniformReg && !c.payload.empty()) {
            const std::uint32_t off = c.gpr[0];
            static int hits = 0;
            if (hits < 12 && off <= 32 && off + c.payload.size() > 28) {
                bool nz = false;
                for (std::uint32_t w = 28; w < 36 && w >= off && w - off < c.payload.size(); w++)
                    nz |= (c.payload[w - off] != 0 && c.payload[w - off] != 0x80000000u);
                if (nz) {
                    hits++;
                    Utils::Log::error("[GX2PROBE] nonzero c7/c8 upload! off={} cnt={} ptr=0x{:08X} LR=0x{:08X}", off, c.gpr[1], c.gpr[2], c.callerLr);
                }
            }
        }
        pushCmd(cpu, c); // into the open display list when recording, else straight into the frame
    }

    // One distinct function-pointer per command type (templates instantiate separate symbols).
    template<Core::Gfx::Gx2Cmd C>
    void gx2_rec(Core::Interpreter &cpu)
    {
        recordCmd(cpu, C);
        cpu.m_gpr[3] = 0;
    }

    // Swap/flip accounting: our "GPU" presents synchronously inside GX2SwapScanBuffers, so the
    // flip always trails the swap by zero frames — but the counters must ADVANCE, because titles
    // frame-pace by spinning on GX2GetSwapStatus until flipCount catches up with swapCount.
    std::uint32_t g_swapCount = 0;
    std::uint32_t g_flipCount = 0;
    std::uint64_t g_lastFlip = 0;
    constexpr std::uint64_t kVsyncPeriod = Core::Timing::swapTicks(1);

    std::uint64_t lastVsync(const Core::Interpreter &cpu)
    {
        const auto now = cpu.m_scheduler.now();
        return now - now % kVsyncPeriod;
    }

    // GX2SwapScanBuffers: end of a frame. Record it, replay the accumulated stream into a framebuffer,
    // present it through the Vulkan renderer, then start a fresh frame.
    void gx2_swap(Core::Interpreter &cpu)
    {
        recordCmd(cpu, Core::Gfx::Gx2Cmd::SwapScanBuffers);
        auto &frameHost = hostReplayState();
        static const bool asyncReplay = [] {
            const char *e = std::getenv("WEMU_ASYNC_REPLAY");
            return !e || e[0] != '0';
        }();
        if (asyncReplay) {
            if (!frameHost.worker)
                frameHost.worker = std::make_unique<ReplayWorker>();
            Core::Profile::Scope waiting(Core::Profile::Stage::GpuWait);
            frameHost.worker->wait();
        }
        static const bool snapshotBuffers = [] {
            const auto *setting = std::getenv("WEMU_FRAME_BUFFER_SNAPSHOTS");
            return !setting || setting[0] != '0';
        }();
        if (snapshotBuffers) {
            if (!frameHost.bufferSnapshots)
                frameHost.bufferSnapshots = std::make_unique<Core::Gfx::Gx2BufferSnapshots>();
            frameHost.bufferSnapshots->capture(Core::Gfx::gx2Stream(), cpu.m_memory);
        }
        // Uniform BLOCKS are bound before the game fills them (the GPU reads at submission), so
        // their call-time payload snapshot is premature — refresh from memory now, at frame end.
        static const bool regProbe = std::getenv("WEMU_GX2_REG_PROBE") != nullptr;
        // Every refresh reads memory at this same point, so commands binding the same block
        // (address and size) share one snapshot: draws rebind a few blocks hundreds of times.
        static std::unordered_map<std::uint64_t, Core::Gfx::Gx2Payload> refreshed;
        for (auto &c: Core::Gfx::gx2Stream().mutableCommands()) {
            // Probe (WEMU_GX2_REG_PROBE): if a reg upload's source memory holds different data at
            // frame end than it did at call time, the title fills the buffer after binding (would
            // need frame-end snapshots for regs too). Logs first few diffs.
            if ((c.type == Core::Gfx::Gx2Cmd::SetVertexUniformReg || c.type == Core::Gfx::Gx2Cmd::SetPixelUniformReg) && !c.payload.empty()) {
                if (!regProbe)
                    continue;
                static int regDiffs = 0;
                if (regDiffs < 8) {
                    try {
                        for (std::uint32_t i = 0; i < c.payload.size(); i++) {
                            const auto now = cpu.m_memory.read<std::uint32_t>(c.gpr[2] + i * 4);
                            if (now != c.payload[i]) {
                                regDiffs++;
                                Utils::Log::error("[GX2PROBE] reg upload off={} ptr=0x{:08X} changed after call: word{} {:08X}->{:08X}", c.gpr[0],
                                                  c.gpr[2], i, c.payload[i], now);
                                break;
                            }
                        }
                    } catch (const Core::MemoryException &e) {
                        Utils::Log::debug("[GX2PROBE] Cannot inspect guest memory: {}", e.what());
                    }
                }
                continue;
            }
            if (c.type != Core::Gfx::Gx2Cmd::SetVertexUniformBlock && c.type != Core::Gfx::Gx2Cmd::SetPixelUniformBlock)
                continue;
            const std::uint32_t words = std::min<std::uint32_t>(c.gpr[1] / 4, 0x1000);
            if (!c.gpr[2] || !words)
                continue;
            if (const auto *source = std::as_const(cpu.m_memory).hostPtr(c.gpr[2], std::size_t(words) * 4)) {
                const auto key = (std::uint64_t(c.gpr[2]) << 32) | words;
                if (const auto found = refreshed.find(key); found != refreshed.end()) {
                    c.payload = found->second;
                    continue;
                }
                // Blocks spliced from display lists share their recorded words: detach once.
                auto &payload = c.payload.overwrite(words);
                for (std::uint32_t i = 0; i < words; i++) {
                    std::uint32_t word;
                    std::memcpy(&word, source + std::size_t(i) * 4, 4);
                    payload[i] = std::byteswap(word);
                }
                refreshed.emplace(key, c.payload);
                continue;
            }
            c.payload.clear();
            try {
                for (std::uint32_t i = 0; i < words; i++)
                    c.payload.push_back(cpu.m_memory.read<std::uint32_t>(c.gpr[2] + i * 4));
            } catch (const Core::MemoryException &) {
                c.payload.clear();
            }
        }
        refreshed.clear(); // The stream owns this frame's snapshots now.
        auto &host = hostReplayState();
        if (!host.replayer)
            host.replayer = std::make_unique<Core::Gfx::Gx2Replayer>();
        auto &replayer = *host.replayer;
        // WEMU_GPU rasterises the frame on the GPU (Vulkan) instead of the software rasteriser.
        //   =1 -> the general multi-target render graph (offscreen buffers + render-to-texture)
        //   =2 -> the legacy single-target quad compositor (flattens everything to the scan target)
        static const int gpuMode = []() {
            const char *e = std::getenv("WEMU_GPU");
            return e ? std::atoi(e) : 0;
        }();
        if (gpuMode) {
            replayer.setGpuRenderer(cpu.m_renderer);
            replayer.setGpuRenderGraph(gpuMode == 1);
        }
        if (!host.nativeInitialized) {
            const auto *enabled = std::getenv("WEMU_NATIVE_RASTER");
            if (!gpuMode && enabled && enabled[0] == '1') {
                const char *textureReuse = std::getenv("WEMU_NATIVE_TEXTURE_REUSE");
                const char *lazyReadback = std::getenv("WEMU_NATIVE_LAZY_READBACK");
                replayer.setRasterBackend(std::make_shared<Core::Gfx::VulkanRasterBackend>(!textureReuse || std::string_view(textureReuse) != "0",
                                                                                           lazyReadback && std::string_view(lazyReadback) == "1"));
                Utils::Log::error("[GX2] experimental translated Vulkan raster backend enabled; unsupported draws remain software");
            }
            host.nativeInitialized = true;
        }
        // Frame-content telemetry: how much GX2 work the title recorded this frame.
        if (g_swapCount < 3 || (g_swapCount % 300) == 0)
            Utils::Log::error("[GX2] swap #{}: {} cmds, {} draws this frame", g_swapCount, Core::Gfx::gx2Stream().size(),
                              Core::Gfx::gx2Stream().drawCount());
        // WEMU_SCENE_STATE=N: every N swaps, dump MK8's scene-manager transition state. The per-scene
        // update is skipped while a transition is "in progress" (0x024CE528): the freeze is a
        // transition stuck mid-flight. singleton ptr @0x101D693C -> obj; obj+0x130 = transition state
        // (3/4 = active), +0x134 flags, +0x144 flag; gate word @0x1018D4F0+0x2F0.
        static const std::uint32_t sceneStateEvery = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_SCENE_STATE");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 0;
        }();
        if (sceneStateEvery && (g_swapCount % sceneStateEvery) == 0) {
            try {
                const std::uint32_t gate = cpu.m_memory.read<std::uint32_t>(0x1018D4F0);
                const std::uint32_t gateWord = gate ? cpu.m_memory.read<std::uint32_t>(gate + 0x2F0) : 0;
                const std::uint32_t sm = cpu.m_memory.read<std::uint32_t>(0x101D693C);
                const std::uint32_t obj = sm ? cpu.m_memory.read<std::uint32_t>(sm) : 0;
                std::uint32_t st = 0, f134 = 0, f144 = 0;
                if (obj) {
                    st = cpu.m_memory.read<std::uint32_t>(obj + 0x130);
                    f134 = cpu.m_memory.read<std::uint32_t>(obj + 0x134);
                    f144 = cpu.m_memory.read<std::uint8_t>(obj + 0x144);
                }
                std::uint32_t f143 = 0, f140 = 0, f150 = 0, f145 = 0, f138 = 0;
                if (obj) {
                    f138 = cpu.m_memory.read<std::uint32_t>(obj + 0x138); // pending next-scene code (decision fn 0x025467D0)
                    f143 = cpu.m_memory.read<std::uint8_t>(obj + 0x143); // advance-requested flag (driver 0x024D6460)
                    f140 = cpu.m_memory.read<std::uint8_t>(obj + 0x140);
                    f145 = cpu.m_memory.read<std::uint8_t>(obj + 0x145);
                    f150 = cpu.m_memory.read<std::uint8_t>(obj + 0x150);
                }
                Utils::Log::error(
                        "[SCENE] swap #{}: state={} pending138={} f134=0x{:08X} f144={} | adv143={} f140={} f145={} f150={} gateWord=0x{:08X}",
                        g_swapCount, st, f138, f134, f144, f143, f140, f145, f150, gateWord);
                // WEMU_FORCE_ADVANCE=1: while a transition is stuck at state 3, force the advance-
                // requested flag (smObj+0x143=1) each swap, to test whether pushing the transition
                // completes it (revealing the next scene) or exposes the next gate.
                static const bool forceAdv = []() {
                    const char *e = std::getenv("WEMU_FORCE_ADVANCE");
                    return e && e[0] == '1';
                }();
                if (forceAdv && obj && st == 3)
                    cpu.m_memory.write<std::uint8_t>(obj + 0x143, 1);
                // WEMU_KICK_DECISION=<hex this-ptr>: MK8 title->menu is stuck because the scene-
                // sequence task's decision fn (0x025467D0) runs exactly once (with pending138==2, so
                // it takes the no-advance path) and is never re-invoked after pending138 becomes 3.
                // With pending138==3 that fn would enqueue the incoming-scene construction tasks AND
                // call requestAdvance (sets adv143 + kicks the resource load) -- the proper path
                // (unlike WEMU_FORCE_ADVANCE, which skips construction and crashes on +0x34 NULL).
                // This re-invokes it ONCE on the async pump thread when we detect the stuck state.
                // The value is the task 'this' (observed stable at 0x51EDD6B8); experimental.
                static const std::uint32_t kickThis = []() -> std::uint32_t {
                    const char *e = std::getenv("WEMU_KICK_DECISION");
                    return e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 16)) : 0;
                }();
                // One-shot dump of the sequence-director handler table ([[0x1018A18C]+0xc]): the
                // decision fn schedules incoming-scene construction via 0x253a368(this, code) which
                // appends table[code] to the task's handler list ONLY if table[code]!=0. If codes
                // 5/6/0xB are 0 here, construction is never scheduled -> menu scene stays unbuilt.
                static bool dumpedDir = false;
                if (!dumpedDir && st == 3) {
                    dumpedDir = true;
                    try {
                        const std::uint32_t dir = cpu.m_memory.read<std::uint32_t>(0x1018A18C);
                        const std::uint32_t tbl = dir ? cpu.m_memory.read<std::uint32_t>(dir + 0xC) : 0;
                        const std::uint32_t cnt = tbl ? cpu.m_memory.read<std::uint32_t>(tbl + 0) : 0;
                        const std::uint32_t base = tbl ? cpu.m_memory.read<std::uint32_t>(tbl + 4) : 0;
                        const std::uint32_t dpause = dir ? cpu.m_memory.read<std::uint8_t>(dir + 0x28) : 0;
                        Utils::Log::error("[SCENE] director=0x{:08X} table=0x{:08X} count={} base=0x{:08X} pause[+0x28]={}", dir, tbl, cnt, base,
                                          dpause);
                        for (std::uint32_t i = 0; base && i < cnt && i < 16; i++)
                            Utils::Log::error("[SCENE]   table[{}] = 0x{:08X}", i, cpu.m_memory.read<std::uint32_t>(base + i * 4));
                    } catch (const Core::MemoryException &) {
                        Utils::Log::error("[SCENE] director table <unreadable>");
                    }
                }
                // WEMU_UNPAUSE_DIRECTOR=1: the sequence-director update (0x0253F048) SKIPS the
                // sequence-task's calc (vtbl[0x24]=0x02547174, which re-invokes the decision fn and
                // runs the incoming-scene construction handlers) when task->vtbl[0x8c]()!=0 AND
                // [director+0x28]!=0. That flag stays set during the transition, so the calc runs
                // exactly once and construction never proceeds. Clearing it each frame lets the game
                // drive its own construction + advance. director = [0x1018A18C].
                static const bool unpauseDir = []() {
                    const char *e = std::getenv("WEMU_UNPAUSE_DIRECTOR");
                    return e && e[0] == '1';
                }();
                if (unpauseDir && st == 3) {
                    try {
                        const std::uint32_t dir = cpu.m_memory.read<std::uint32_t>(0x1018A18C);
                        if (dir && cpu.m_memory.read<std::uint8_t>(dir + 0x28) != 0)
                            cpu.m_memory.write<std::uint8_t>(dir + 0x28, 0);
                    } catch (const Core::MemoryException &e) {
                        Utils::Log::debug("[GX2PROBE] Cannot inspect guest memory: {}", e.what());
                    }
                }
                static bool kicked = false;
                if (kickThis && !kicked && obj && st == 3 && f138 == 3 && f143 == 0) {
                    Utils::Log::error("[SCENE] WEMU_KICK_DECISION: re-invoking decision fn 0x025467D0 (this=0x{:08X})", kickThis);
                    Core::Async::enqueue(0x025467D0, kickThis);
                    kicked = true;
                }
                // WEMU_DRIVE_MENU=<hex seqtask-this>: drive the title->menu transition to completion.
                // The decision fn (this=seqtask) is one-shot on HW; re-run it every frame to keep the
                // incoming-scene construction handlers (codes 4/6, marked active via 0x253a2a0)
                // scheduled, but SUPPRESS the premature advance (clear O2.adv143) until the incoming
                // scene (observed at seqtask+0xDC) has actually built its +0x34 sub-object -- that is
                // exactly the field whose NULL crashes the forced advance at 0x02562298. Once +0x34
                // is non-NULL we stop suppressing and let the game complete the transition.
                static const std::uint32_t driveThis = []() -> std::uint32_t {
                    const char *e = std::getenv("WEMU_DRIVE_MENU");
                    return e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 16)) : 0;
                }();
                static const bool drainActive = []() {
                    const char *e = std::getenv("WEMU_DRAIN_ACTIVE");
                    return e && e[0] == '1';
                }();
                static const bool inlineScene = []() {
                    const char *e = std::getenv("WEMU_INLINE_SCENE");
                    return e && e[0] == '1';
                }();
                if (driveThis && obj && st == 3 && f138 == 3) {
                    const std::uint32_t incoming = driveThis + 0xDC;
                    std::uint32_t sub34 = 0;
                    try {
                        sub34 = cpu.m_memory.read<std::uint32_t>(incoming + 0x34);
                    } catch (const Core::MemoryException &) {
                        sub34 = 0; // An unreadable sub-object is not ready for the transition.
                    }
                    if (!inlineScene) {
                        Core::Async::enqueue(0x025467D0, driveThis); // progress construction each frame
                        if (drainActive && (g_swapCount % 30) == 0)
                            Core::Async::enqueue(0x0253A3C4, driveThis); // diagnostic: drain late active-list entries
                    }
                    if (sub34 == 0 && f143 != 0) // not built yet -> hold the advance
                        cpu.m_memory.write<std::uint8_t>(obj + 0x143, 0);
                    if ((g_swapCount % 30) == 0)
                        Utils::Log::error("[SCENE] drive: incoming=0x{:08X} +0x34=0x{:08X} adv143={} state={}", incoming, sub34, f143, st);
                }
            } catch (const Core::MemoryException &) {
                Utils::Log::error("[SCENE] swap #{}: <unreadable>", g_swapCount);
            }
        }
        // WEMU_THREAD_DUMP=N: every N swaps, snapshot all scheduler threads (state + wait key +
        // symbolized PC/LR). Surfaces what a stalled `main` is blocked on when the title won't advance.
        static const std::uint32_t threadDumpEvery = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_THREAD_DUMP");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 0;
        }();
        if (threadDumpEvery && (g_swapCount % threadDumpEvery) == 0) {
            std::cout << "[DIAG] threads @ swap #" << g_swapCount << ":" << std::endl;
            Core::Diag::dumpThreads(cpu, "    ");
        }
        // WEMU_GX2_DUMP=N: print the full captured command stream at swap #N (design aid for the
        // draw-replay stage).
        static const std::uint32_t dumpAt = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_GX2_DUMP");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 0xFFFFFFFFu;
        }();
        if (g_swapCount == dumpAt) {
            static constexpr const char *kCmdNames[] = {"Init",
                                                        "SetContextState",
                                                        "SetColorBuffer",
                                                        "SetDepthBuffer",
                                                        "ClearColor",
                                                        "ClearDepthStencilEx",
                                                        "ClearBuffersEx",
                                                        "SetViewport",
                                                        "SetScissor",
                                                        "SetFetchShader",
                                                        "SetVertexShader",
                                                        "SetPixelShader",
                                                        "SetGeometryShader",
                                                        "SetAttribBuffer",
                                                        "SetVertexUniformBlock",
                                                        "SetPixelUniformBlock",
                                                        "SetPixelTexture",
                                                        "SetPixelSampler",
                                                        "SetBlendControl",
                                                        "SetColorControl",
                                                        "SetDepthStencilControl",
                                                        "DrawEx",
                                                        "DrawIndexedEx",
                                                        "CopyColorBufferToScanBuffer",
                                                        "SwapScanBuffers",
                                                        "SetVertexUniformReg",
                                                        "SetPixelUniformReg",
                                                        "SetBlendConstantColor",
                                                        "SetTargetChannelMasks",
                                                        "SetVertexTexture",
                                                        "SetVertexSampler",
                                                        "SetPixelSamplerBorderColor",
                                                        "SetVertexSamplerBorderColor",
                                                        "SetPolygonControl",
                                                        "SetRasterizerClipControl",
                                                        "SetPolygonOffset",
                                                        "DrawImage",
                                                        "SetStencilMask",
                                                        "SetStreamOutEnable",
                                                        "SetStreamOutBuffer",
                                                        "SetStreamOutContext",
                                                        "SaveStreamOutContext"};
            std::size_t idx = 0;
            for (const auto &c: Core::Gfx::gx2Stream().commands()) {
                const auto ti = static_cast<std::size_t>(c.type);
                Utils::Log::error("[GX2DUMP] #{:04} {} r3..r10= {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} f1..f4= {} {} {} {}", idx++,
                                  ti < std::size(kCmdNames) ? kCmdNames[ti] : "?", c.gpr[0], c.gpr[1], c.gpr[2], c.gpr[3], c.gpr[4], c.gpr[5],
                                  c.gpr[6], c.gpr[7], static_cast<float>(c.fpr[0]), static_cast<float>(c.fpr[1]), static_cast<float>(c.fpr[2]),
                                  static_cast<float>(c.fpr[3]));
                try {
                    if (c.type == Core::Gfx::Gx2Cmd::SetAttribBuffer) {
                        // GX2SetAttribBuffer(index, size, stride, data): peek the first 16 words.
                        std::string words;
                        for (std::uint32_t w = 0; w < 16; w++) {
                            const std::uint32_t v = cpu.m_memory.read<std::uint32_t>(c.gpr[3] + w * 4);
                            words += std::format(" {:08X}", v);
                        }
                        Utils::Log::error("[GX2DUMP]       attrib[{}] stride={} data@{:08X}:{}", c.gpr[0], c.gpr[2], c.gpr[3], words);
                    } else if (c.type == Core::Gfx::Gx2Cmd::DrawIndexedEx) {
                        // GX2DrawIndexedEx(mode, count, indexType, indices, offset, instances)
                        std::string idxs;
                        for (std::uint32_t w = 0; w < std::min<std::uint32_t>(c.gpr[1], 12); w++) {
                            const std::uint16_t v = cpu.m_memory.read<std::uint16_t>(c.gpr[3] + w * 2);
                            idxs += std::format(" {}", v);
                        }
                        Utils::Log::error("[GX2DUMP]       indices@{:08X} (u16):{}", c.gpr[3], idxs);
                    }
                } catch (const Core::MemoryException &) {
                    Utils::Log::error("[GX2DUMP]       <data unreadable>");
                }
            }
        }
        // Frame-identical skip: rebuilding the framebuffer costs ~0.8s of CPU on this software
        // rasteriser and blocks the guest for the whole GX2SwapScanBuffers call. During static
        // stretches (loading, menus at rest) the title submits a byte-identical command stream for
        // hundreds of presents, so we hash the (already frame-end-refreshed) stream and, when it
        // matches the last presented frame, re-present the cached framebuffer instead of rebuilding
        // it. The guest then advances at the flip_tv-paced ~60 fps rather than ~1.25 fps — i.e. much
        // closer to real Wii U speed, which is what actually makes the menu appear quickly. Set
        // WEMU_NO_FRAMESKIP=1 to force a rebuild every present (debugging). See Gx2CommandStream::
        // contentHash for what the identity check does and does not cover.
        static const bool noSkip = []() {
            const char *e = std::getenv("WEMU_NO_FRAMESKIP");
            return e && e[0] == '1';
        }();
        static std::uint64_t s_lastHash = 0;
        static bool s_haveFrame = false;
        // The hash only serves frame skipping; it walks every command and payload word.
        const std::uint64_t hash = noSkip ? 0 : Core::Gfx::gx2Stream().contentHash();
        // WEMU_ASYNC_REPLAY=0 replays inside this call. Otherwise the previous frame, replayed
        // while the guest built this one, is finished and presented here, and this frame goes
        // to the replay thread: presentation trails the guest by one frame. Replay only reads
        // guest memory (uniform blocks were snapshotted above), like the GPU it stands for.
        const auto present = [&] {
            const bool drcShown = cpu.m_renderer && cpu.m_renderer->drc_wanted();
            if (drcShown && !replayer.drcFramebuffer().empty())
                cpu.m_renderer->flip_drc(replayer.drcFramebuffer().data(), Core::Gfx::Gx2Replayer::kDrcWidth, Core::Gfx::Gx2Replayer::kDrcHeight);
            if (cpu.m_renderer)
                cpu.m_renderer->flip_tv(replayer.framebuffer().data(), Core::Gfx::Gx2Replayer::kWidth, Core::Gfx::Gx2Replayer::kHeight,
                                        cpu.m_scheduler.swapInterval());
        };
        if (asyncReplay && s_haveFrame)
            present();
        // A remote GamePad attached during a static stretch needs one rebuild to get its first image.
        const bool drcWanted = cpu.m_renderer && cpu.m_renderer->drc_wanted();
        replayer.setDrcCapture(drcWanted);
        if (noSkip || !s_haveFrame || hash != s_lastHash || (drcWanted && replayer.drcFramebuffer().empty())) {
            if (asyncReplay) {
                std::swap(host.frame, Core::Gfx::gx2Stream());
                host.worker->submit([&replayer, &frame = host.frame, memory = &cpu.m_memory, presents = g_swapCount] {
                    Core::Profile::timeline().reset();
                    replayer.replay(frame, memory);
                    Core::Profile::reportReplay(presents);
                });
            } else {
                replayer.replay(Core::Gfx::gx2Stream(), &cpu.m_memory);
            }
            s_lastHash = hash;
            s_haveFrame = true;
        }
        if (!asyncReplay)
            present();
        Core::Gfx::gx2Stream().clear();
        g_swapCount++;
        g_flipCount = g_swapCount;
        g_lastFlip = cpu.m_scheduler.now();
        cpu.m_gpr[3] = 0;
        Core::advanceGuestFrameClock(cpu);
    }

    // GX2GetSwapStatus(u32* swapCount, u32* flipCount, OSTime* lastFlip, OSTime* lastVsync)
    void gx2_GetSwapStatus(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3], g_swapCount);
        if (cpu.m_gpr[4])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[4], g_flipCount);
        if (cpu.m_gpr[5])
            cpu.m_memory.write<std::uint64_t>(cpu.m_gpr[5], g_lastFlip);
        if (cpu.m_gpr[6])
            cpu.m_memory.write<std::uint64_t>(cpu.m_gpr[6], lastVsync(cpu));
        cpu.m_gpr[3] = 0;
    }

    void gx2_WaitForVsync(Core::Interpreter &cpu)
    {
        cpu.m_gpr[3] = 0;
        // All waiters share the next 60 Hz boundary. Sleeping (not yielding) keeps a
        // high-priority waiter out of the runnable set until that display tick.
        cpu.m_scheduler.sleep(cpu, kVsyncPeriod - cpu.m_scheduler.now() % kVsyncPeriod);
    }

    void gx2_WaitForFlip(Core::Interpreter &cpu)
    {
        // This backend completes each flip synchronously in GX2SwapScanBuffers.
        // There is no outstanding flip to wait for, so do not switch threads.
        cpu.m_gpr[3] = 0;
    }

    // ---- init / sizing ------------------------------------------------------------------------

    void gx2_Init(Core::Interpreter &cpu)
    {
        static std::uint32_t cmdBuffer = 0;
        if (!cmdBuffer)
            cmdBuffer = cpu.m_memory.heapAllocate(0x400000, GX2_BUFFER_ALIGN);
        Utils::Log::debug("[GX2] Init (cmd buffer @0x{:08X})", cmdBuffer);
        recordCmd(cpu, Core::Gfx::Gx2Cmd::Init);
        cpu.m_gpr[3] = 0;
    }

    void gx2_noop(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

    template<bool Stack>
    void gx2_ShaderResources(Core::Interpreter &cpu)
    {
        // All three GX2 shader headers begin with their SQ_PGM_RESOURCES word.
        const auto resources = cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[3]);
        cpu.m_gpr[3] = (resources >> (Stack ? 8 : 0)) & 0xFF;
    }

    void gx2_CopySurface(Core::Interpreter &cpu)
    {
        const auto read = [&](std::uint32_t surface, std::uint32_t offset) { return cpu.m_memory.read<std::uint32_t>(surface + offset); };
        const std::uint32_t src = cpu.m_gpr[3], dst = cpu.m_gpr[6];
        const std::uint32_t srcFormat = read(src, SURF_FORMAT), dstFormat = read(dst, SURF_FORMAT);
        const std::uint32_t srcTile = read(src, 0x30), dstTile = read(dst, 0x30);
        const std::uint32_t width = std::min(read(src, SURF_WIDTH), read(dst, SURF_WIDTH));
        const std::uint32_t height = std::min(read(src, SURF_HEIGHT), read(dst, SURF_HEIGHT));
        const auto supportedTile = [](std::uint32_t tile) { return tile <= 2 || tile == 4 || tile == 16; };
        static const bool trace = std::getenv("WEMU_GX2_COPY_TRACE") != nullptr;
        if (trace)
            Utils::Log::error("[GX2COPY] {}x{} f{:X}/f{:X} t{}/t{} pitch={}/{} image={:08X}/{:08X} level={}/{} slice={}/{}", width, height, srcFormat,
                              dstFormat, srcTile, dstTile, read(src, SURF_PITCH), read(dst, SURF_PITCH), read(src, 0x24), read(dst, 0x24),
                              cpu.m_gpr[4], cpu.m_gpr[7], cpu.m_gpr[5], cpu.m_gpr[8]);
        // Initial transfer coverage: matching uncompressed base-level 2D images.
        // Keep unsupported subresource/AA transfers visible instead of reading the wrong layout.
        if (!width || !height || width > 8192 || height > 8192 || srcFormat != dstFormat || isBlockCompressed(srcFormat) || !supportedTile(srcTile) ||
            !supportedTile(dstTile) || cpu.m_gpr[4] || cpu.m_gpr[5] || cpu.m_gpr[7] || cpu.m_gpr[8] || read(src, 0x18) || read(dst, 0x18)) {
            static unsigned warnings = 0;
            if (warnings++ < 8)
                Utils::Log::error("[GX2COPY] unsupported transfer {}x{} f{:X}/f{:X} t{}/t{}", width, height, srcFormat, dstFormat, srcTile, dstTile);
            cpu.m_gpr[3] = 0;
            return;
        }
        const std::uint32_t bytes = bytesPerElement(srcFormat);
        const std::uint32_t srcPitch = read(src, SURF_PITCH), dstPitch = read(dst, SURF_PITCH);
        const std::uint32_t srcSwizzle = read(src, 0x34), dstSwizzle = read(dst, 0x34);
        const std::uint32_t srcImage = read(src, 0x24), dstImage = read(dst, 0x24);
        const std::uint32_t srcSize = read(src, SURF_IMAGE_SIZE), dstSize = read(dst, SURF_IMAGE_SIZE);
        const auto offset = [&](std::uint32_t x, std::uint32_t y, bool source) {
            return Core::Gfx::tiledElementOffset(x, y, source ? srcPitch : dstPitch, bytes * 8, source ? srcTile : dstTile,
                                                 source ? srcSwizzle : dstSwizzle);
        };
        auto *source = cpu.m_memory.hostPtr(srcImage);
        auto *dest = cpu.m_memory.hostPtr(dstImage);
        if (!source || !dest || !srcSize || !dstSize || srcPitch < width || dstPitch < width || std::uint64_t(srcImage) + srcSize > 0x100000000ull ||
            std::uint64_t(dstImage) + dstSize > 0x100000000ull || !cpu.m_memory.hostPtr(srcImage + srcSize - 1) ||
            !cpu.m_memory.hostPtr(dstImage + dstSize - 1)) {
            Utils::Log::error("[GX2COPY] invalid image range");
            cpu.m_gpr[3] = 0;
            return;
        }
        // Validate the complete transfer before writing; stage pixels so overlapping images work.
        std::vector<std::uint8_t> pixels(std::size_t(width) * height * bytes);
        for (std::uint32_t y = 0; y < height; y++) {
            for (std::uint32_t x = 0; x < width; x++) {
                const std::uint32_t from = offset(x, y, true), to = offset(x, y, false);
                if (std::uint64_t(from) + bytes > srcSize || std::uint64_t(to) + bytes > dstSize) {
                    Utils::Log::error("[GX2COPY] layout exceeds image allocation at {},{}", x, y);
                    cpu.m_gpr[3] = 0;
                    return;
                }
                std::memcpy(pixels.data() + (std::size_t(y) * width + x) * bytes, source + from, bytes);
            }
        }
        for (std::uint32_t y = 0; y < height; y++)
            for (std::uint32_t x = 0; x < width; x++)
                std::memcpy(dest + offset(x, y, false), pixels.data() + (std::size_t(y) * width + x) * bytes, bytes);
        cpu.m_gpr[3] = 0;
    }

    void gx2_CalcTVSize(Core::Interpreter &cpu)
    {
        const std::uint32_t size = 1280u * 720u * 4u * 2u;
        if (cpu.m_gpr[6])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[6], size);
        if (cpu.m_gpr[7])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[7], GX2_BUFFER_ALIGN);
        if (gx2SizeTrace() && size >= gx2SizeTraceMin() && gx2SizeTraceTake())
            Utils::Log::error("[GX2SIZE] TV mode={} fmt=0x{:X} buffering={} -> size=0x{:X} align=0x{:X} LR={}", cpu.m_gpr[3], cpu.m_gpr[4],
                              cpu.m_gpr[5], size, GX2_BUFFER_ALIGN, Core::Diag::symbolize(cpu, cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode));
        cpu.m_gpr[3] = 0;
    }

    void gx2_CalcDRCSize(Core::Interpreter &cpu)
    {
        const std::uint32_t size = 854u * 480u * 4u * 2u;
        if (cpu.m_gpr[6])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[6], size);
        if (cpu.m_gpr[7])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[7], GX2_BUFFER_ALIGN);
        if (gx2SizeTrace() && size >= gx2SizeTraceMin() && gx2SizeTraceTake())
            Utils::Log::error("[GX2SIZE] DRC mode={} fmt=0x{:X} buffering={} -> size=0x{:X} align=0x{:X} LR={}", cpu.m_gpr[3], cpu.m_gpr[4],
                              cpu.m_gpr[5], size, GX2_BUFFER_ALIGN, Core::Diag::symbolize(cpu, cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode));
        cpu.m_gpr[3] = 0;
    }

    void gx2_CalcSurfaceSizeAndAlignment(Core::Interpreter &cpu)
    {
        const std::uint32_t s = cpu.m_gpr[3];
        if (s) {
            const std::uint32_t rawWidth = cpu.m_memory.read<std::uint32_t>(s + SURF_WIDTH);
            const std::uint32_t rawHeight = cpu.m_memory.read<std::uint32_t>(s + SURF_HEIGHT);
            const std::uint32_t dim = cpu.m_memory.read<std::uint32_t>(s + 0x00);
            const std::uint32_t mipLevels = cpu.m_memory.read<std::uint32_t>(s + 0x10);
            const std::uint32_t aa = cpu.m_memory.read<std::uint32_t>(s + 0x18);
            const std::uint32_t use = cpu.m_memory.read<std::uint32_t>(s + 0x1C);
            std::uint32_t tileMode = cpu.m_memory.read<std::uint32_t>(s + 0x30);
            std::uint32_t width = rawWidth;
            std::uint32_t height = rawHeight;
            std::uint32_t depth = cpu.m_memory.read<std::uint32_t>(s + SURF_DEPTH);
            const std::uint32_t format = cpu.m_memory.read<std::uint32_t>(s + SURF_FORMAT);
            if (depth == 0)
                depth = 1;
            if (isBlockCompressed(format)) { // element = 4x4 texel block
                width = (std::max(width, 1u) + 3) / 4;
                height = (std::max(height, 1u) + 3) / 4;
            }
            // Legacy fallback uses linear alignment; supported layouts below replace it.
            // LINEAR_SPECIAL is the tightly packed CPU upload layout, with byte alignment.
            std::uint32_t surfaceAlign = tileMode == 16 ? 1 : 0x100;
            std::uint32_t pitch = tileMode == 16 ? std::max(width, 1u) : alignUp(width ? width : 1, 64);
            std::uint32_t imageSize = alignUp(pitch * (height ? height : 1) * depth * bytesPerElement(format), surfaceAlign);
            // Base 2D layouts remain independent of the requested mip count.
            // Other dimensions, scan usage and AA still use the legacy calculation.
            if ((dim == 1 || dim == 5) && !aa && !(use & (4u | 8u))) {
                if (const auto layout = Core::Gfx::baseSurfaceLayout(width, height, dim == 1 ? 1 : depth, bytesPerElement(format), tileMode, rawWidth,
                                                                     rawHeight)) {
                    tileMode = layout->tileMode;
                    pitch = layout->pitch;
                    imageSize = layout->imageSize;
                    surfaceAlign = layout->alignment;
                    cpu.m_memory.write<std::uint32_t>(s + 0x30, tileMode);
                    const auto swizzle = cpu.m_memory.read<std::uint32_t>(s + 0x34);
                    cpu.m_memory.write<std::uint32_t>(s + 0x34, (swizzle & 0xFF00FFFF) | (tileMode == 4 ? 0xD0000 : 0));
                    if (!mipLevels)
                        cpu.m_memory.write<std::uint32_t>(s + 0x10, 1);
                }
            }
            // Depth planes need padded tile rows too. An unpadded height made
            // the final tile exceed imageSize, rejecting otherwise valid depth
            // views and repeating their full CPU decode on every draw.
            if ((dim == 1 || dim == 5) && !aa && (use & 4u) && !(use & 8u)) {
                if (const auto layout = Core::Gfx::latteSurfaceLayout(rawWidth, rawHeight, dim == 1 ? 1 : depth, format, dim, tileMode, 0, use)) {
                    tileMode = layout->tileMode;
                    pitch = layout->pitch;
                    imageSize = layout->imageSize;
                    surfaceAlign = layout->alignment;
                    cpu.m_memory.write<unsigned>(s + 0x30, tileMode);
                }
            }
            cpu.m_memory.write<std::uint32_t>(s + SURF_PITCH, pitch);
            cpu.m_memory.write<std::uint32_t>(s + SURF_IMAGE_SIZE, imageSize);
            cpu.m_memory.write<std::uint32_t>(s + SURF_MIPMAP_SIZE, 0);
            cpu.m_memory.write<std::uint32_t>(s + SURF_ALIGNMENT, surfaceAlign);
            if (!aa && (dim == 1 || dim == 3 || dim == 5) && !(use & 8u)) {
                // Keep established base layouts for 2D/array resources. Cube
                // layouts and every smaller mip use the shared Latte calculator.
                const auto levels = std::min(std::max(1u, mipLevels), unsigned(std::bit_width(std::max(rawWidth, rawHeight))));
                bool valid = levels != 0;
                auto actualTile = tileMode;
                if (dim == 3) {
                    const auto base = Core::Gfx::latteSurfaceLayout(rawWidth, rawHeight, depth, format, dim, tileMode, 0, use);
                    if (!base)
                        valid = false;
                    else {
                        pitch = base->pitch;
                        imageSize = base->imageSize;
                        surfaceAlign = base->alignment;
                        actualTile = base->tileMode;
                    }
                }
                std::array<std::uint32_t, 13> offsets{};
                std::uint64_t mipBytes = 0;
                unsigned transition = actualTile == 4 ? 13 : 0;
                for (unsigned level = 1; valid && level < levels; ++level) {
                    const auto mip = Core::Gfx::latteSurfaceLayout(rawWidth, rawHeight, depth, format, dim, actualTile, level, use);
                    if (!mip) {
                        valid = false;
                        break;
                    }
                    if (actualTile == 4 && mip->tileMode < 4 && transition == 13)
                        transition = level;
                    if (level == 1) {
                        const auto offset = (std::uint64_t(imageSize) + mip->alignment - 1) / mip->alignment * mip->alignment;
                        if (offset > UINT32_MAX) {
                            valid = false;
                            break;
                        }
                        offsets[0] = unsigned(offset);
                    } else {
                        mipBytes = (mipBytes + mip->alignment - 1) / mip->alignment * mip->alignment;
                        if (mipBytes > UINT32_MAX) {
                            valid = false;
                            break;
                        }
                        offsets[level - 1] = std::uint32_t(mipBytes);
                    }
                    mipBytes += mip->imageSize;
                    valid = mipBytes <= UINT32_MAX;
                }
                if (valid) {
                    cpu.m_memory.write<unsigned>(s + 0x10, levels);
                    cpu.m_memory.write<unsigned>(s + SURF_IMAGE_SIZE, imageSize);
                    cpu.m_memory.write<unsigned>(s + SURF_PITCH, pitch);
                    cpu.m_memory.write<unsigned>(s + SURF_ALIGNMENT, surfaceAlign);
                    cpu.m_memory.write<unsigned>(s + SURF_MIPMAP_SIZE, unsigned(mipBytes));
                    cpu.m_memory.write<unsigned>(s + 0x30, actualTile);
                    cpu.m_memory.write<unsigned>(s + 0x34, (cpu.m_memory.read<unsigned>(s + 0x34) & 0xFF00FFFFu) | (transition << 16));
                    for (unsigned index = 0; index < offsets.size(); ++index)
                        cpu.m_memory.write<unsigned>(s + 0x40 + index * 4, offsets[index]);
                }
            }
            if (gx2SizeTrace() && imageSize >= gx2SizeTraceMin() && gx2SizeTraceTake())
                Utils::Log::error("[GX2SIZE] Surface @0x{:08X} dim={} raw={}x{} depth={} mip={} fmt=0x{:X} aa={} use=0x{:X} tile={} -> calc={}x{} "
                                  "pitch={} elem={} image=0x{:X} mip=0x{:X} align=0x{:X} LR={}",
                                  s, dim, rawWidth, rawHeight, depth, mipLevels, format, aa, use, tileMode, width, height, pitch,
                                  bytesPerElement(format), imageSize, cpu.m_memory.read<unsigned>(s + SURF_MIPMAP_SIZE), surfaceAlign,
                                  Core::Diag::symbolize(cpu, cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode));
        }
        cpu.m_gpr[3] = 0;
    }

    void gx2_CalcAuxInfoNone(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[4])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[4], 0);
        if (cpu.m_gpr[5])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[5], GX2_BUFFER_ALIGN);
        cpu.m_gpr[3] = 0;
    }

    // GX2GetCurrentDisplayList(u32 *outBuffer, u32 *outSize) -> TRUE while a list is open.
    void gx2_GetCurrentDisplayList(Core::Interpreter &cpu)
    {
        const std::uint32_t outBuf = cpu.m_gpr[3], outSize = cpu.m_gpr[4];
        const auto &lists = openLists(cpu);
        if (lists.empty()) {
            cpu.m_gpr[3] = 0;
            return;
        }
        if (outBuf)
            cpu.m_memory.write<std::uint32_t>(outBuf, lists.back().buffer);
        if (outSize)
            cpu.m_memory.write<std::uint32_t>(outSize, lists.back().capacity);
        cpu.m_gpr[3] = 1;
    }

    // GX2BeginDisplayListEx(void *buffer, u32 size, BOOL profiling)
    void gx2_BeginDisplayListEx(Core::Interpreter &cpu)
    {
        g_dlStats.begins++;
        auto &lists = openLists(cpu);
        if (!lists.empty())
            g_dlStats.nested++;
        lists.push_back({cpu.m_gpr[3], cpu.m_gpr[4], {}});
        cpu.m_gpr[3] = 0;
    }

    // GX2EndDisplayList(void *buffer) -> bytes used. Must be non-zero: titles treat a zero-sized
    // list as empty and skip calling it.
    void gx2_EndDisplayList(Core::Interpreter &cpu)
    {
        auto &lists = openLists(cpu);
        if (lists.empty()) {
            cpu.m_gpr[3] = 0;
            return;
        }
        auto open = std::move(lists.back());
        lists.pop_back();
        const std::uint32_t buf = cpu.m_gpr[3] ? cpu.m_gpr[3] : open.buffer;
        // Recorded lists are guest-owned GPU state, not disposable cache entries.
        // Keep live lists callable when more buffers are recorded; recording into
        // the same address replaces its previous contents. Session reset clears them.
        const auto list = std::make_shared<const CapturedList>(std::move(open.commands));
        g_displayLists[buf] = list;
        g_dlStats.ends++;
        g_dlStats.recorded += list->size();
        if (list->empty())
            g_dlStats.emptyLists++;
        if ((g_dlStats.ends % 2000) == 0)
            dlReport();
        cpu.m_gpr[3] = kDlBytesPerCmd * (static_cast<std::uint32_t>(list->size()) + 1);
    }

    // GX2CallDisplayList(void *buffer, u32 size) / GX2DirectCallDisplayList: splice the recorded
    // commands into whatever sink is active (the frame, or an enclosing list).
    void gx2_CallDisplayList(Core::Interpreter &cpu)
    {
        g_dlStats.calls++;
        if (const auto it = g_displayLists.find(cpu.m_gpr[3]); it != g_displayLists.end()) {
            // Splicing only appends to the stream or a distinct open recording.
            // It cannot modify this completed list, so avoid a second payload copy.
            const auto &cmds = *it->second;
            g_dlStats.hits++;
            g_dlStats.spliced += cmds.size();
            for (const auto &c: cmds) {
                if (isDrawCmd(c.type))
                    g_dlStats.splicedDraws++;
                pushCmd(cpu, c);
            }
        } else {
            g_dlStats.misses++;
            // Native/prebuilt or copied lists do not necessarily have an HLE
            // recording at this address. Keep their missing commands inspectable.
            static const std::uint32_t traceFrom = [] {
                const char *value = std::getenv("WEMU_GX2_DL_MISS_TRACE");
                return value ? static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10)) : 0xFFFFFFFFu;
            }();
            static std::unordered_map<std::uint32_t, unsigned> sites;
            static unsigned reports{};
            const auto lr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
            if (g_swapCount >= traceFrom && reports < 64 && sites[lr]++ < 16) {
                ++reports;
                std::string words;
                try {
                    for (unsigned offset = 0; offset < std::min(cpu.m_gpr[4], 64u); offset += 4)
                        words += std::format(" {:08X}", cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[3] + offset));
                } catch (const Core::MemoryException &) {
                    words += " unreadable";
                }
                Utils::Log::error("[GX2DL_MISS] frame={} buffer={:08X} bytes={} lr={:08X} words:{}", g_swapCount, cpu.m_gpr[3], cpu.m_gpr[4], lr,
                                  words);
            }
        }
        cpu.m_gpr[3] = 0;
    }

    void gx2_RetSize(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0x1000u; }

    // GX2CalcFetchShaderSizeEx(attribCount, type, tessMode)
    //
    // This buffer holds the generated fetch-shader program. It scales with the number of
    // attributes; returning a blanket 4 KB per shader makes titles that generate thousands of
    // small fetch shaders burn several extra MB from their resource heaps before the menu.
    void gx2_CalcFetchShaderSizeEx(Core::Interpreter &cpu)
    {
        const std::uint32_t attribs = cpu.m_gpr[3];
        constexpr std::uint32_t kHeaderBytes = 0x100;
        constexpr std::uint32_t kBytesPerAttrib = 0x20;
        constexpr std::uint32_t kFetchShaderAlign = 0x100;
        cpu.m_gpr[3] = alignUp(kHeaderBytes + attribs * kBytesPerAttrib, kFetchShaderAlign);
    }

    void gx2_InitFetchShaderEx(Core::Interpreter &cpu)
    {
        const auto shader = cpu.m_gpr[3], program = cpu.m_gpr[4];
        const auto count = std::min(cpu.m_gpr[5], 32u), streams = cpu.m_gpr[6];
        if (!shader || !program || (count && !streams))
            return;
        auto &layout = g_fetchLayouts[program];
        layout.clear();
        for (std::uint32_t i = 0; i < count * 8; i++)
            layout.push_back(cpu.m_memory.read<std::uint32_t>(streams + i * 4));
        for (std::uint32_t off = 0; off < 0x20; off += 4)
            cpu.m_memory.write<std::uint32_t>(shader + off, 0);
        cpu.m_memory.write<std::uint32_t>(shader, cpu.m_gpr[7]);
        cpu.m_memory.write<std::uint32_t>(shader + 8, alignUp(32 + count * 16, 0x100));
        cpu.m_memory.write<std::uint32_t>(shader + 0xC, program);
        cpu.m_memory.write<std::uint32_t>(shader + 0x10, count);
        cpu.m_gpr[3] = 0;
    }
    void gx2_TempGpuVersion(Core::Interpreter &cpu) { cpu.m_gpr[3] = 2u; }

    unsigned integerArgument(Core::Interpreter &cpu, unsigned index)
    {
        return index < 8 ? cpu.m_gpr[index + 3] : cpu.m_memory.read<unsigned>(cpu.m_gpr[1] + 8 + (index - 8) * 4);
    }
    unsigned depthStencilControlWord(Core::Interpreter &cpu, unsigned first)
    {
        const auto arg = [&](unsigned i) { return integerArgument(cpu, first + i); };
        auto word = unsigned(arg(3) != 0) | (unsigned(arg(0) != 0) << 1) | (unsigned(arg(1) != 0) << 2) | ((arg(2) & 7) << 4) |
                    (unsigned(arg(4) != 0) << 7);
        constexpr unsigned arguments[]{5, 8, 6, 7, 9, 12, 10, 11};
        constexpr unsigned shifts[]{8, 11, 14, 17, 20, 23, 26, 29};
        for (unsigned i = 0; i < 8; ++i)
            word |= (arg(arguments[i]) & 7) << shifts[i];
        return word;
    }

    unsigned polygonControlWord(Core::Interpreter &cpu, unsigned first)
    {
        const auto arg = [&](unsigned index) { return integerArgument(cpu, first + index); };
        return unsigned(arg(1) != 0) | (unsigned(arg(2) != 0) << 1) | (unsigned(arg(0) != 0) << 2) | ((arg(3) & 3) << 3) | ((arg(4) & 7) << 5) |
               ((arg(5) & 7) << 8) | (unsigned(arg(6) != 0) << 11) | (unsigned(arg(7) != 0) << 12) | (unsigned(arg(8) != 0) << 13);
    }
    void capturePolygonControl(Core::Interpreter &cpu, unsigned word)
    {
        Core::Gfx::Gx2Command cmd{Core::Gfx::Gx2Cmd::SetPolygonControl};
        cmd.gpr[0] = word;
        cmd.callerLr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
        pushCmd(cpu, cmd);
        cpu.m_gpr[3] = 0;
    }
    void samplerField(Core::Interpreter &cpu, unsigned word, unsigned shift, unsigned bits, std::uint32_t value)
    {
        const auto address = cpu.m_gpr[3] + word * 4;
        const std::uint32_t mask = ((1u << bits) - 1) << shift;
        const auto old = cpu.m_memory.read<std::uint32_t>(address);
        cpu.m_memory.write<std::uint32_t>(address, (old & ~mask) | ((value << shift) & mask));
    }

    void gx2_InitSampler(Core::Interpreter &cpu)
    {
        const auto mode = cpu.m_gpr[4] & 7, filter = cpu.m_gpr[5] & 7;
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3], mode | (mode << 3) | (mode << 6) | (filter << 9) | (filter << 12));
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3] + 4, 1023u << 10);
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3] + 8, 1u << 31);
    }

    void gx2_InitSamplerClamping(Core::Interpreter &cpu)
    {
        for (unsigned i = 0; i < 3; i++)
            samplerField(cpu, 0, i * 3, 3, cpu.m_gpr[4 + i]);
    }
    void gx2_InitSamplerXYFilter(Core::Interpreter &cpu)
    {
        samplerField(cpu, 0, 9, 3, cpu.m_gpr[4]);
        samplerField(cpu, 0, 12, 3, cpu.m_gpr[5]);
        samplerField(cpu, 0, 19, 3, cpu.m_gpr[6]);
    }
    void gx2_InitSamplerZMFilter(Core::Interpreter &cpu)
    {
        samplerField(cpu, 0, 15, 2, cpu.m_gpr[4]);
        samplerField(cpu, 0, 17, 2, cpu.m_gpr[5]);
    }
    void gx2_InitSamplerLOD(Core::Interpreter &cpu)
    {
        auto fixed = [](double value, int low, int high) {
            return static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(std::isfinite(value) ? std::clamp(value * 64.0, double(low), double(high)) : 0));
        };
        samplerField(cpu, 1, 0, 10, fixed(cpu.m_fpr[1], 0, 1023));
        samplerField(cpu, 1, 10, 10, fixed(cpu.m_fpr[2], 0, 1023));
        samplerField(cpu, 1, 20, 12, fixed(cpu.m_fpr[3], -2048, 2047));
    }

    // No-op GX2 functions (the title fills these structs itself; only the GPU reads the registers).
    const char *const kNoops[] = {
            "GX2DrawDone",
            "GX2ExpandAAColorBuffer",
            "GX2ExpandDepthBuffer",
            "GX2Flush",
            "GX2InitColorBufferRegs",
            "GX2InitDepthBufferHiZEnable",
            "GX2InitDepthBufferRegs",
            "GX2InitSampler",
            "GX2InitSamplerBorderType",
            "GX2InitSamplerClamping",
            "GX2InitSamplerDepthCompare",
            "GX2InitSamplerLOD",
            "GX2InitSamplerXYFilter",
            "GX2InitSamplerZMFilter",
            "GX2InitTextureRegs",
            "GX2Invalidate",
            "GX2QueryBegin",
            "GX2QueryBeginConditionalRender",
            "GX2QueryEnd",
            "GX2QueryEndConditionalRender",
            "GX2SampleBottomGPUCycle",
            "GX2SampleTopGPUCycle",
            "GX2SetAlphaTest",
            "GX2SetAlphaTestReg",
            "GX2SetAlphaToMask",
            "GX2SetBlendConstantColorReg",
            "GX2SetBlendControlReg",
            "GX2SetClearDepthStencil",
            "GX2SetColorControlReg",
            "GX2SetCullOnlyControl",
            "GX2SetDefaultState",
            "GX2SetDepthOnlyControl",
            "GX2SetDepthStencilControlReg",
            "GX2SetDRCBuffer",
            "GX2SetDRCEnable",
            "GX2SetDRCGamma",
            "GX2SetDRCScale",
            "GX2SetGeometrySampler",
            "GX2SetGeometrySamplerBorderColor",
            "GX2SetGeometryShaderInputRingBuffer",
            "GX2SetGeometryShaderOutputRingBuffer",
            "GX2SetGeometryTexture",
            "GX2SetGeometryUniformBlock",
            "GX2SetLineWidth",
            "GX2SetMaxTessellationLevel",
            "GX2SetMinTessellationLevel",
            "GX2SetPixelSamplerBorderColor",
            "GX2SetPointLimits",
            "GX2SetPointSize",
            "GX2SetPolygonControl",
            "GX2SetPolygonControlReg",
            "GX2SetPolygonOffset",
            "GX2SetPolygonOffsetReg",
            "GX2SetPrimitiveRestartIndex",
            "GX2SetRasterizerClipControl",
            "GX2SetShaderModeEx",
            "GX2SetStencilMask",
            "GX2SetStencilMaskReg",
            "GX2SetSurfaceSwizzle",
            "GX2SetTargetChannelMasksReg",
            "GX2SetTessellation",
            "GX2SetTVBuffer",
            "GX2SetTVEnable",
            "GX2SetTVGamma",
            "GX2SetTVScale",
            "GX2SetVertexSampler",
            "GX2SetVertexSamplerBorderColor",
            "GX2SetVertexTexture",
            "GX2GetContextStateDisplayList",
    };

    const char *const kRetSize[] = {
            "GX2GetGeometryShaderGPRs", "GX2GetGeometryShaderStackEntries", "GX2GetPixelShaderGPRs", "GX2GetPixelShaderStackEntries",
            "GX2GetVertexShaderGPRs",   "GX2GetVertexShaderStackEntries",   "GX2GPUTimeToCPUTime",
    };

} // namespace

namespace Core::Gfx {
    Gx2CommandStream &gx2Stream()
    {
        static Gx2CommandStream stream;
        return stream;
    }
} // namespace Core::Gfx

void ReleaseGx2HostResources() noexcept
{
    // Drop worker/image/readback owners while Vulkan, renderer and guest memory
    // are still alive, rather than during process-static destruction.
    auto &host = hostReplayState();
    host.bufferCalls.clear();
    host.worker.reset(); // finish the frame in flight before its replayer goes away
    host.bufferSnapshots.reset();
    host.replayer.reset();
    host.frame.clear();
    host.nativeInitialized = false;
    g_openLists.clear();
    g_displayLists.clear();
    g_fetchLayouts.clear();
    Core::Gfx::gx2Stream().clear();
}

void CopyGx2DisplayListState(std::uint32_t destination, std::uint32_t source, std::uint32_t bytes)
{
    if (!bytes || destination == source)
        return;
    const auto found = g_displayLists.find(source);
    const auto commands = found == g_displayLists.end() ? nullptr : found->second;
    g_displayLists.erase(destination);
    if (!commands || bytes < std::uint64_t(kDlBytesPerCmd) * (commands->size() + 1))
        return;
    g_displayLists[destination] = commands;
    static const bool trace = std::getenv("WEMU_GX2_DL_COPY_TRACE") != nullptr;
    static unsigned reports{};
    if (trace && reports++ < 64)
        Utils::Log::error("[GX2DL_COPY] frame={} source={:08X} destination={:08X} bytes={} commands={}", g_swapCount, source, destination, bytes,
                          commands->size());
}

void Core::Gx2::onBufferAllocatorReturn(Interpreter &cpu)
{
    auto &calls = hostReplayState().bufferCalls;
    const auto it = calls.find(cpu.m_scheduler.currentHandle());
    if (it == calls.end() || it->second.empty())
        throw std::runtime_error("GX2R allocator returned without a pending buffer operation");
    const auto call = it->second.back();
    it->second.pop_back();
    if (it->second.empty())
        calls.erase(it);
    const auto memory = cpu.m_gpr[3];
    if (call.freeing) {
        cpu.m_memory.write<std::uint32_t>(call.buffer + 12, 0);
        cpu.m_gpr[3] = 0;
    } else {
        if (memory) {
            const auto flags = (cpu.m_memory.read<std::uint32_t>(call.buffer) & ~(1u << 30)) | (1u << 29);
            cpu.m_memory.write<std::uint32_t>(call.buffer, flags);
            cpu.m_memory.write<std::uint32_t>(call.buffer + 12, memory);
        }
        cpu.m_gpr[3] = memory ? 1 : 0;
    }
    cpu.m_gpr[1] = call.stack;
    cpu.m_lr = call.lr;
    cpu.m_pc = cpu.m_nextPc = call.lr;
}

void RegisterGx2Functions()
{
    Core::syscallHandler.registerSyscall("GX2RGetBufferAlignment", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = 256; });
    Core::syscallHandler.registerSyscall("GX2RGetBufferAllocationSize", [](Core::Interpreter &cpu) {
        const auto buffer = cpu.m_gpr[3];
        const auto bytes = std::uint64_t{cpu.m_memory.read<std::uint32_t>(buffer + 4)} * cpu.m_memory.read<std::uint32_t>(buffer + 8);
        cpu.m_gpr[3] = bytes <= 0xFFFFFFC0u ? static_cast<std::uint32_t>((bytes + 63) & ~std::uint64_t{63}) : 0;
    });
    Core::syscallHandler.registerSyscall("GX2RBufferExists", [](Core::Interpreter &cpu) {
        cpu.m_gpr[3] = cpu.m_gpr[3] && cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[3] + 12) ? 1 : 0;
    });
    Core::syscallHandler.registerSyscall("GX2RCreateBuffer", [](Core::Interpreter &cpu) {
        const auto buffer = cpu.m_gpr[3];
        const auto bytes = std::uint64_t{cpu.m_memory.read<std::uint32_t>(buffer + 4)} * cpu.m_memory.read<std::uint32_t>(buffer + 8);
        if (!bytes || bytes > 0xFFFFFFC0u || cpu.m_memory.read<std::uint32_t>(buffer + 12)) {
            cpu.m_gpr[3] = 0;
            return;
        }
        cpu.m_gpr[3] = static_cast<std::uint32_t>((bytes + 63) & ~std::uint64_t{63});
        cpu.m_gpr[4] = 256;
        if (callBufferAllocator(cpu, "MEMAllocFromDefaultHeapEx", buffer, false))
            return;
        Core::syscallHandler.get("MEMAllocFromDefaultHeapEx")(cpu);
        const auto memory = cpu.m_gpr[3];
        if (memory) {
            auto flags = cpu.m_memory.read<std::uint32_t>(buffer);
            flags = (flags & ~(1u << 30)) | (1u << 29);
            cpu.m_memory.write<std::uint32_t>(buffer, flags);
            cpu.m_memory.write<std::uint32_t>(buffer + 12, memory);
        }
        cpu.m_gpr[3] = memory ? 1 : 0;
    });
    Core::syscallHandler.registerSyscall("GX2RDestroyBufferEx", [](Core::Interpreter &cpu) {
        const auto buffer = cpu.m_gpr[3], options = cpu.m_gpr[4];
        if (buffer && (cpu.m_memory.read<std::uint32_t>(buffer) & (1u << 29)) && !(options & (1u << 19))) {
            const auto memory = cpu.m_memory.read<std::uint32_t>(buffer + 12);
            if (memory) {
                cpu.m_gpr[3] = memory;
                if (callBufferAllocator(cpu, "MEMFreeToDefaultHeap", buffer, true))
                    return;
                Core::syscallHandler.get("MEMFreeToDefaultHeap")(cpu);
                cpu.m_memory.write<std::uint32_t>(buffer + 12, 0);
            }
        }
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2RCreateBufferUserMemory", [](Core::Interpreter &cpu) {
        const auto buffer = cpu.m_gpr[3], memory = cpu.m_gpr[4], size = cpu.m_gpr[5];
        const auto bytes = std::uint64_t{cpu.m_memory.read<std::uint32_t>(buffer + 4)} * cpu.m_memory.read<std::uint32_t>(buffer + 8);
        if (!memory || !bytes || bytes > size || !cpu.m_memory.hostPtr(memory, size)) {
            cpu.m_gpr[3] = 0;
            return;
        }
        auto flags = cpu.m_memory.read<std::uint32_t>(buffer);
        flags &= ~((1u << 29) | (1u << 30)); // caller owns the storage; initially unlocked
        cpu.m_memory.write<std::uint32_t>(buffer, flags);
        cpu.m_memory.write<std::uint32_t>(buffer + 12, memory);
        cpu.m_gpr[3] = 1;
    });
    Core::syscallHandler.registerSyscall("GX2RLockBufferEx", [](Core::Interpreter &cpu) {
        const auto buffer = cpu.m_gpr[3];
        auto flags = cpu.m_memory.read<std::uint32_t>(buffer);
        flags |= (1u << 30) | (cpu.m_gpr[4] & (1u << 22));
        cpu.m_memory.write<std::uint32_t>(buffer, flags);
        cpu.m_gpr[3] = cpu.m_memory.read<std::uint32_t>(buffer + 12);
    });
    Core::syscallHandler.registerSyscall("GX2RUnlockBufferEx", [](Core::Interpreter &cpu) {
        const auto buffer = cpu.m_gpr[3];
        cpu.m_memory.write<std::uint32_t>(buffer, cpu.m_memory.read<std::uint32_t>(buffer) & ~((1u << 30) | (1u << 22)));
        cpu.m_gpr[3] = 0;
    });
    // CPU and captured GPU commands share the same coherent guest storage.
    Core::syscallHandler.registerSyscall("GX2RInvalidateBuffer", gx2_noop);
    Core::syscallHandler.registerSyscall("GX2RSetAttributeBuffer", [](Core::Interpreter &cpu) {
        const auto buffer = cpu.m_gpr[3], index = cpu.m_gpr[4], stride = cpu.m_gpr[5], offset = cpu.m_gpr[6];
        const auto bytes = std::uint64_t{cpu.m_memory.read<unsigned>(buffer + 4)} * cpu.m_memory.read<unsigned>(buffer + 8);
        const auto address = cpu.m_memory.read<unsigned>(buffer + 12);
        if (bytes > 0xFFFFFFFFu || offset > bytes || !cpu.m_memory.hostPtr(address, bytes)) {
            cpu.m_gpr[3] = 0;
            return;
        }
        cpu.m_gpr[3] = index;
        cpu.m_gpr[4] = static_cast<unsigned>(bytes);
        cpu.m_gpr[5] = stride;
        cpu.m_gpr[6] = address + offset;
        recordCmd(cpu, Core::Gfx::Gx2Cmd::SetAttribBuffer);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2RDrawIndexed", [](Core::Interpreter &cpu) {
        const auto mode = cpu.m_gpr[3], buffer = cpu.m_gpr[4], type = cpu.m_gpr[5], count = cpu.m_gpr[6];
        const auto indexOffset = cpu.m_gpr[7], vertexOffset = cpu.m_gpr[8], instances = cpu.m_gpr[9];
        const auto elementSize = cpu.m_memory.read<std::uint32_t>(buffer + 4);
        const auto data = cpu.m_memory.read<std::uint32_t>(buffer + 12);
        const auto offset = std::uint64_t(indexOffset) * elementSize + data;
        if (offset > UINT32_MAX)
            return;
        cpu.m_gpr[3] = mode;
        cpu.m_gpr[4] = count;
        cpu.m_gpr[5] = type;
        cpu.m_gpr[6] = static_cast<std::uint32_t>(offset);
        cpu.m_gpr[7] = vertexOffset;
        cpu.m_gpr[8] = instances;
        recordCmd(cpu, Core::Gfx::Gx2Cmd::DrawIndexedEx);
        cpu.m_gpr[3] = 0;
    });
    using C = Core::Gfx::Gx2Cmd;
    for (const char *name: kNoops)
        Core::syscallHandler.registerSyscall(name, gx2_noop);
    for (const char *name: kRetSize)
        Core::syscallHandler.registerSyscall(name, gx2_RetSize);
    for (const auto *name: {"GX2CalcGeometryShaderInputRingBufferSize", "GX2CalcGeometryShaderOutputRingBufferSize"})
        Core::syscallHandler.registerSyscall(name, [](Core::Interpreter &cpu) { cpu.m_gpr[3] *= 0x4000u; });

    Core::syscallHandler.registerSyscall("GX2Init", gx2_Init);
    for (const auto *name: {"GX2GetPixelShaderGPRs", "GX2GetVertexShaderGPRs", "GX2GetGeometryShaderGPRs"})
        Core::syscallHandler.registerSyscall(name, gx2_ShaderResources<false>);
    for (const auto *name: {"GX2GetPixelShaderStackEntries", "GX2GetVertexShaderStackEntries", "GX2GetGeometryShaderStackEntries"})
        Core::syscallHandler.registerSyscall(name, gx2_ShaderResources<true>);
    Core::syscallHandler.registerSyscall("GX2CalcTVSize", gx2_CalcTVSize);
    Core::syscallHandler.registerSyscall("GX2CalcDRCSize", gx2_CalcDRCSize);
    Core::syscallHandler.registerSyscall("GX2CalcSurfaceSizeAndAlignment", gx2_CalcSurfaceSizeAndAlignment);
    Core::syscallHandler.registerSyscall("GX2CopySurface", gx2_CopySurface);
    Core::syscallHandler.registerSyscall("GX2CalcFetchShaderSizeEx", gx2_CalcFetchShaderSizeEx);
    Core::syscallHandler.registerSyscall("GX2InitFetchShaderEx", gx2_InitFetchShaderEx);
    Core::syscallHandler.registerSyscall("GX2CalcColorBufferAuxInfo", gx2_CalcAuxInfoNone);
    Core::syscallHandler.registerSyscall("GX2CalcDepthBufferHiZInfo", [](Core::Interpreter &cpu) {
        const auto buffer = cpu.m_gpr[3];
        const auto pitch = cpu.m_memory.read<unsigned>(buffer + SURF_PITCH);
        const auto height = cpu.m_memory.read<unsigned>(buffer + SURF_HEIGHT);
        const auto slices = cpu.m_memory.read<unsigned>(buffer + 0xC);
        const auto layout = Core::Gfx::latteHtileLayout(pitch, height, slices);
        if (cpu.m_gpr[4])
            cpu.m_memory.write<unsigned>(cpu.m_gpr[4], layout ? layout->imageSize : 0);
        if (cpu.m_gpr[5])
            cpu.m_memory.write<unsigned>(cpu.m_gpr[5], layout ? layout->alignment : GX2_BUFFER_ALIGN);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2GetCurrentDisplayList", gx2_GetCurrentDisplayList);
    Core::syscallHandler.registerSyscall("GX2BeginDisplayListEx", gx2_BeginDisplayListEx);
    Core::syscallHandler.registerSyscall("GX2EndDisplayList", gx2_EndDisplayList);
    Core::syscallHandler.registerSyscall("GX2CallDisplayList", gx2_CallDisplayList);
    Core::syscallHandler.registerSyscall("GX2DirectCallDisplayList", gx2_CallDisplayList);
    Core::syscallHandler.registerSyscall("GX2TempGetGPUVersion", gx2_TempGpuVersion);

    // Captured render commands -> command stream.
    Core::syscallHandler.registerSyscall("GX2SetContextState", gx2_rec<C::SetContextState>);
    // Setup makes the new context current, so following state belongs to it.
    Core::syscallHandler.registerSyscall("GX2SetupContextStateEx", gx2_rec<C::SetContextState>);
    Core::syscallHandler.registerSyscall("GX2SetColorBuffer", gx2_rec<C::SetColorBuffer>);
    Core::syscallHandler.registerSyscall("GX2SetDepthBuffer", gx2_rec<C::SetDepthBuffer>);
    Core::syscallHandler.registerSyscall("GX2ClearColor", gx2_rec<C::ClearColor>);
    Core::syscallHandler.registerSyscall("GX2ClearDepthStencilEx", gx2_rec<C::ClearDepthStencilEx>);
    Core::syscallHandler.registerSyscall("GX2ClearBuffersEx", gx2_rec<C::ClearBuffersEx>);
    Core::syscallHandler.registerSyscall("GX2SetViewport", gx2_rec<C::SetViewport>);
    Core::syscallHandler.registerSyscall("GX2SetScissor", gx2_rec<C::SetScissor>);
    Core::syscallHandler.registerSyscall("GX2SetFetchShader", gx2_rec<C::SetFetchShader>);
    Core::syscallHandler.registerSyscall("GX2SetVertexShader", gx2_rec<C::SetVertexShader>);
    Core::syscallHandler.registerSyscall("GX2SetPixelShader", gx2_rec<C::SetPixelShader>);
    Core::syscallHandler.registerSyscall("GX2SetGeometryShader", gx2_rec<C::SetGeometryShader>);
    Core::syscallHandler.registerSyscall("GX2SetAttribBuffer", gx2_rec<C::SetAttribBuffer>);
    Core::syscallHandler.registerSyscall("GX2SetVertexUniformBlock", gx2_rec<C::SetVertexUniformBlock>);
    Core::syscallHandler.registerSyscall("GX2SetStreamOutEnable", gx2_rec<C::SetStreamOutEnable>);
    Core::syscallHandler.registerSyscall("GX2SetStreamOutBuffer", gx2_rec<C::SetStreamOutBuffer>);
    Core::syscallHandler.registerSyscall("GX2SetStreamOutContext", gx2_rec<C::SetStreamOutContext>);
    Core::syscallHandler.registerSyscall("GX2SaveStreamOutContext", gx2_rec<C::SaveStreamOutContext>);
    Core::syscallHandler.registerSyscall("GX2SetPixelUniformBlock", gx2_rec<C::SetPixelUniformBlock>);
    Core::syscallHandler.registerSyscall("GX2SetVertexUniformReg", gx2_rec<C::SetVertexUniformReg>);
    Core::syscallHandler.registerSyscall("GX2SetPixelUniformReg", gx2_rec<C::SetPixelUniformReg>);
    Core::syscallHandler.registerSyscall("GX2SetPixelTexture", gx2_rec<C::SetPixelTexture>);
    Core::syscallHandler.registerSyscall("GX2SetVertexTexture", gx2_rec<C::SetVertexTexture>);
    Core::syscallHandler.registerSyscall("GX2SetPixelSampler", gx2_rec<C::SetPixelSampler>);
    Core::syscallHandler.registerSyscall("GX2SetVertexSampler", gx2_rec<C::SetVertexSampler>);
    Core::syscallHandler.registerSyscall("GX2SetPixelSamplerBorderColor", gx2_rec<C::SetPixelSamplerBorderColor>);
    Core::syscallHandler.registerSyscall("GX2SetVertexSamplerBorderColor", gx2_rec<C::SetVertexSamplerBorderColor>);
    Core::syscallHandler.registerSyscall("GX2InitSampler", gx2_InitSampler);
    Core::syscallHandler.registerSyscall("GX2InitSamplerClamping", gx2_InitSamplerClamping);
    Core::syscallHandler.registerSyscall("GX2InitSamplerXYFilter", gx2_InitSamplerXYFilter);
    Core::syscallHandler.registerSyscall("GX2InitSamplerZMFilter", gx2_InitSamplerZMFilter);
    Core::syscallHandler.registerSyscall("GX2InitSamplerLOD", gx2_InitSamplerLOD);
    Core::syscallHandler.registerSyscall("GX2InitSamplerBorderType", [](Core::Interpreter &cpu) { samplerField(cpu, 0, 22, 2, cpu.m_gpr[4]); });
    Core::syscallHandler.registerSyscall("GX2InitSamplerDepthCompare", [](Core::Interpreter &cpu) { samplerField(cpu, 0, 26, 3, cpu.m_gpr[4]); });
    Core::syscallHandler.registerSyscall("GX2SetBlendConstantColor", gx2_rec<C::SetBlendConstantColor>);
    Core::syscallHandler.registerSyscall("GX2InitBlendConstantColorReg", [](Core::Interpreter &cpu) {
        for (unsigned i = 0; i < 4; ++i)
            cpu.m_memory.write<unsigned>(cpu.m_gpr[3] + i * 4, std::bit_cast<unsigned>(static_cast<float>(cpu.m_fpr[i + 1])));
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetBlendConstantColorReg", [](Core::Interpreter &cpu) {
        Core::Gfx::Gx2Command command{Core::Gfx::Gx2Cmd::SetBlendConstantColor};
        for (unsigned i = 0; i < 4; ++i)
            command.fpr[i] = std::bit_cast<float>(cpu.m_memory.read<unsigned>(cpu.m_gpr[3] + i * 4));
        pushCmd(cpu, command);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetBlendControl", gx2_rec<C::SetBlendControl>);
    Core::syscallHandler.registerSyscall("GX2InitBlendControlReg", [](Core::Interpreter &cpu) {
        const auto address = cpu.m_gpr[3];
        const auto arg = [&](unsigned i) { return integerArgument(cpu, i); };
        const auto word = (arg(2) & 31) | ((arg(4) & 7) << 5) | ((arg(3) & 31) << 8) | ((arg(6) & 31) << 16) | ((arg(8) & 7) << 21) |
                          ((arg(7) & 31) << 24) | (unsigned(arg(5) != 0) << 29);
        cpu.m_memory.write<unsigned>(address, arg(1));
        cpu.m_memory.write<unsigned>(address + 4, word);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetBlendControlReg", [](Core::Interpreter &cpu) {
        const auto address = cpu.m_gpr[3], word = cpu.m_memory.read<unsigned>(address + 4);
        Core::Gfx::Gx2Command command{Core::Gfx::Gx2Cmd::SetBlendControl};
        command.gpr = {cpu.m_memory.read<unsigned>(address),
                       word & 31,
                       (word >> 8) & 31,
                       (word >> 5) & 7,
                       (word >> 29) & 1,
                       (word >> 16) & 31,
                       (word >> 24) & 31,
                       (word >> 21) & 7};
        command.callerLr = cpu.m_lr + Core::Memory::ApplicationCode;
        pushCmd(cpu, command);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetColorControl", gx2_rec<C::SetColorControl>);
    Core::syscallHandler.registerSyscall("GX2InitColorControlReg", [](Core::Interpreter &cpu) {
        const auto word =
                ((cpu.m_gpr[4] & 255) << 16) | ((cpu.m_gpr[5] & 255) << 8) | 0x80u | (unsigned(cpu.m_gpr[6] != 0) << 1) | (cpu.m_gpr[7] ? 0u : 0x10u);
        cpu.m_memory.write<unsigned>(cpu.m_gpr[3], word);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetColorControlReg", [](Core::Interpreter &cpu) {
        const auto word = cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[3]);
        Core::Gfx::Gx2Command command{Core::Gfx::Gx2Cmd::SetColorControl};
        command.gpr = {(word >> 16) & 0xFF, (word >> 8) & 0xFF, (word >> 1) & 1, ((word >> 4) & 7) != 1};
        command.callerLr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
        pushCmd(cpu, command);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetCullOnlyControl", [](Core::Interpreter &cpu) {
        capturePolygonControl(cpu, unsigned(cpu.m_gpr[4] != 0) | (unsigned(cpu.m_gpr[5] != 0) << 1) | (unsigned(cpu.m_gpr[3] != 0) << 2));
    });
    Core::syscallHandler.registerSyscall("GX2SetPolygonControl",
                                         [](Core::Interpreter &cpu) { capturePolygonControl(cpu, polygonControlWord(cpu, 0)); });
    Core::syscallHandler.registerSyscall("GX2InitPolygonControlReg", [](Core::Interpreter &cpu) {
        const auto word = polygonControlWord(cpu, 1);
        cpu.m_memory.write<unsigned>(cpu.m_gpr[3], word);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2GetPolygonControlReg", [](Core::Interpreter &cpu) {
        const auto word = cpu.m_memory.read<unsigned>(cpu.m_gpr[3]);
        constexpr unsigned shifts[]{2, 0, 1, 3, 5, 8, 11, 12, 13};
        constexpr unsigned masks[]{1, 1, 1, 3, 7, 7, 1, 1, 1};
        for (unsigned index = 0; index < 9; ++index)
            if (const auto output = integerArgument(cpu, index + 1))
                cpu.m_memory.write<unsigned>(output, (word >> shifts[index]) & masks[index]);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetPolygonControlReg",
                                         [](Core::Interpreter &cpu) { capturePolygonControl(cpu, cpu.m_memory.read<unsigned>(cpu.m_gpr[3])); });
    Core::syscallHandler.registerSyscall("GX2SetRasterizerClipControl", [](Core::Interpreter &cpu) {
        Core::Gfx::Gx2Command cmd{Core::Gfx::Gx2Cmd::SetRasterizerClipControl};
        cmd.gpr = {unsigned(cpu.m_gpr[3] != 0), unsigned(cpu.m_gpr[4] != 0), 0};
        pushCmd(cpu, cmd);
        cpu.m_gpr[3] = 0;
    });
    for (const auto *name: {"GX2SetRasterizerClipControlEx", "GX2SetRasterizerClipControlHalfZ"})
        Core::syscallHandler.registerSyscall(name, gx2_rec<C::SetRasterizerClipControl>);
    Core::syscallHandler.registerSyscall("GX2SetPolygonOffset", gx2_rec<C::SetPolygonOffset>);
    Core::syscallHandler.registerSyscall("GX2InitPolygonOffsetReg", [](Core::Interpreter &cpu) {
        const auto pointer = cpu.m_gpr[3];
        for (unsigned i = 0; i < 5; ++i) {
            const float value = float(cpu.m_fpr[i + 1]) * ((i == 1 || i == 3) ? 16.f : 1.f);
            cpu.m_memory.write<unsigned>(pointer + (i < 4 ? (i ^ 1u) : i) * 4, std::bit_cast<unsigned>(value));
        }
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2GetPolygonOffsetReg", [](Core::Interpreter &cpu) {
        const auto pointer = cpu.m_gpr[3];
        std::array<float, 5> values{};
        for (unsigned i = 0; i < values.size(); ++i)
            values[i] = std::bit_cast<float>(cpu.m_memory.read<unsigned>(pointer + (i < 4 ? (i ^ 1u) : i) * 4)) / ((i == 1 || i == 3) ? 16.f : 1.f);
        for (unsigned i = 0; i < values.size(); ++i)
            if (const auto output = cpu.m_gpr[i + 4])
                cpu.m_memory.write<unsigned>(output, std::bit_cast<unsigned>(values[i]));
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetPolygonOffsetReg", [](Core::Interpreter &cpu) {
        Core::Gfx::Gx2Command cmd{C::SetPolygonOffset};
        for (unsigned i = 0; i < 5; ++i)
            cmd.fpr[i] =
                    std::bit_cast<float>(cpu.m_memory.read<unsigned>(cpu.m_gpr[3] + (i < 4 ? (i ^ 1u) : i) * 4)) / ((i == 1 || i == 3) ? 16.f : 1.f);
        pushCmd(cpu, cmd);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetDepthStencilControl", [](Core::Interpreter &cpu) {
        const auto arg = [&](unsigned i) { return integerArgument(cpu, i); };
        Core::Gfx::Gx2Command command{Core::Gfx::Gx2Cmd::SetDepthStencilControl};
        for (unsigned i = 0; i < command.gpr.size(); ++i)
            command.gpr[i] = arg(i);
        command.payload = {depthStencilControlWord(cpu, 0)};
        command.callerLr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
        pushCmd(cpu, command);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2InitDepthStencilControlReg", [](Core::Interpreter &cpu) {
        cpu.m_memory.write<unsigned>(cpu.m_gpr[3], depthStencilControlWord(cpu, 1));
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetDepthOnlyControl", [](Core::Interpreter &cpu) {
        const auto saved = cpu.m_gpr[6];
        cpu.m_gpr[6] = 0; // This entry point disables stencil testing.
        recordCmd(cpu, Core::Gfx::Gx2Cmd::SetDepthStencilControl);
        cpu.m_gpr[6] = saved;
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetDepthStencilControlReg", [](Core::Interpreter &cpu) {
        const auto word = cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[3]);
        Core::Gfx::Gx2Command command{Core::Gfx::Gx2Cmd::SetDepthStencilControl};
        command.gpr = {(word >> 1) & 1, (word >> 2) & 1, (word >> 4) & 7,  word & 1,
                       (word >> 7) & 1, (word >> 8) & 7, (word >> 14) & 7, (word >> 17) & 7};
        command.payload = {word};
        command.callerLr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
        pushCmd(cpu, command); // display-list recordings must keep state next to their draws
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetTargetChannelMasks", gx2_rec<C::SetTargetChannelMasks>);
    Core::syscallHandler.registerSyscall("GX2InitStencilMaskReg", [](Core::Interpreter &cpu) {
        const auto address = cpu.m_gpr[3];
        for (unsigned face = 0; face < 2; ++face) {
            const auto r = 4 + face * 3;
            const auto word = (cpu.m_gpr[r + 2] & 255) | ((cpu.m_gpr[r] & 255) << 8) | ((cpu.m_gpr[r + 1] & 255) << 16);
            cpu.m_memory.write<unsigned>(address + face * 4, word);
        }
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetStencilMask", [](Core::Interpreter &cpu) {
        Core::Gfx::Gx2Command command{Core::Gfx::Gx2Cmd::SetStencilMask};
        for (unsigned face = 0; face < 2; ++face) {
            const auto r = 3 + face * 3;
            command.payload.push_back((cpu.m_gpr[r + 2] & 255) | ((cpu.m_gpr[r] & 255) << 8) | ((cpu.m_gpr[r + 1] & 255) << 16));
        }
        command.callerLr = cpu.m_lr + Core::Memory::ApplicationCode;
        pushCmd(cpu, command);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetStencilMaskReg", [](Core::Interpreter &cpu) {
        Core::Gfx::Gx2Command command{Core::Gfx::Gx2Cmd::SetStencilMask};
        command.payload = {cpu.m_memory.read<unsigned>(cpu.m_gpr[3]), cpu.m_memory.read<unsigned>(cpu.m_gpr[3] + 4)};
        command.callerLr = cpu.m_lr + Core::Memory::ApplicationCode;
        pushCmd(cpu, command);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2SetTargetChannelMasksReg", [](Core::Interpreter &cpu) {
        const auto word = cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[3]);
        Core::Gfx::Gx2Command command{Core::Gfx::Gx2Cmd::SetTargetChannelMasks};
        for (unsigned slot = 0; slot < command.gpr.size(); ++slot)
            command.gpr[slot] = (word >> (slot * 4)) & 15;
        command.callerLr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
        pushCmd(cpu, command);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2DrawEx", gx2_rec<C::DrawEx>);
    Core::syscallHandler.registerSyscall("GX2DrawIndexedEx", gx2_rec<C::DrawIndexedEx>);
    Core::syscallHandler.registerSyscall("GX2CopyColorBufferToScanBuffer", gx2_rec<C::CopyColorBufferToScanBuffer>);
    Core::syscallHandler.registerSyscall("GX2SwapScanBuffers", gx2_swap); // replays + presents the frame
    Core::syscallHandler.registerSyscall("GX2GetSwapStatus", gx2_GetSwapStatus);
    Core::syscallHandler.registerSyscall("GX2SetSwapInterval", [](Core::Interpreter &cpu) {
        cpu.m_scheduler.setSwapInterval(cpu.m_gpr[3]);
        cpu.m_gpr[3] = 0;
    });
    Core::syscallHandler.registerSyscall("GX2GetSwapInterval", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_scheduler.swapInterval(); });
    Core::syscallHandler.registerSyscall("GX2WaitForVsync", gx2_WaitForVsync);
    Core::syscallHandler.registerSyscall("GX2WaitForFlip", gx2_WaitForFlip);
}
