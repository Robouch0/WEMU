#include "Gx2.hpp"

#include <algorithm>
#include <cmath>

#include "AsyncCallbacks.hpp"
#include "Coreinit.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "gfx/Gx2CommandStream.hpp"
#include "gfx/Gx2Replayer.hpp"
#include "gfx/Renderer.hpp"
#include "gfx/SurfaceLayout.hpp"
#include "gfx/VulkanRasterBackend.hpp"
#include "utils/Diagnostics.hpp"
#include "utils/Logger.hpp"

// GX2 high-level emulation.
//
// Two responsibilities:
//  (1) Query/sizing functions write real values into guest structs (zeros -> zero-size allocations).
//  (2) Render functions (draw/clear/state/shader/present) are CAPTURED into an ordered command
//      stream (gfx/Gx2CommandStream) for the upcoming GX2->Vulkan replay stage. Everything else is a
//      no-op because the title fills its own GX2 structs and only the GPU consumes the registers.

namespace {

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
    constexpr std::uint32_t kDlBytesPerCmd = 32; // nominal PM4 bytes per captured call
    bool g_dlRecording = false;
    std::uint32_t g_dlBuffer = 0;
    std::vector<Core::Gfx::Gx2Command> g_dlCurrent;
    std::unordered_map<std::uint32_t, std::vector<Core::Gfx::Gx2Command>> g_displayLists;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> g_fetchLayouts;

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

    void pushCmd(const Core::Gfx::Gx2Command &c)
    {
        if (g_dlRecording) {
            if (isDrawCmd(c.type))
                g_dlStats.recDraws++;
            g_dlCurrent.push_back(c);
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
            if (const auto it = g_fetchLayouts.find(program); it != g_fetchLayouts.end())
                c.payload = it->second;
        }
        // Uniform data lives in stack/ring memory the game reuses immediately — copy it now.
        std::uint32_t ptr = 0, words = 0;
        if (type == Core::Gfx::Gx2Cmd::SetVertexUniformReg || type == Core::Gfx::Gx2Cmd::SetPixelUniformReg) {
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
                   type == Core::Gfx::Gx2Cmd::ClearColor || type == Core::Gfx::Gx2Cmd::CopyColorBufferToScanBuffer) {
            // r3 points at a GX2ColorBuffer/DepthBuffer/Texture whose leading GX2Surface fields
            // the replay stage parses. The title often builds these structs on its STACK, so the
            // pointer is stale by frame-end replay — snapshot the struct now, at call time.
            ptr = c.gpr[0];
            words = 16; // covers every field up to pitch (+0x3C)
            if (type == Core::Gfx::Gx2Cmd::SetPixelTexture || type == Core::Gfx::Gx2Cmd::SetVertexTexture)
                words = 0x9C / 4; // retain the texture view, component map, and resource registers
        }
        if (ptr && words) {
            c.payload.reserve(words);
            try {
                for (std::uint32_t i = 0; i < words; i++)
                    c.payload.push_back(cpu.m_memory.read<std::uint32_t>(ptr + i * 4));
            } catch (const Core::MemoryException &) {
                c.payload.clear();
            }
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
        pushCmd(c); // into the open display list when recording, else straight into the frame
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
    constexpr std::uint64_t kVsyncPeriod = 62156250ull / 60;

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
        // Advance the guest clock one frame so time-driven animations (title intro, fades) progress.
        Core::advanceGuestFrameClock(cpu);
        // Uniform BLOCKS are bound before the game fills them (the GPU reads at submission), so
        // their call-time payload snapshot is premature — refresh from memory now, at frame end.
        for (auto &c: Core::Gfx::gx2Stream().mutableCommands()) {
            // Probe: if a reg upload's source memory holds different data at frame end than it
            // did at call time, the title fills the buffer after binding (would need frame-end
            // snapshots for regs too). Logs first few diffs.
            if ((c.type == Core::Gfx::Gx2Cmd::SetVertexUniformReg || c.type == Core::Gfx::Gx2Cmd::SetPixelUniformReg) && !c.payload.empty()) {
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
            c.payload.clear();
            try {
                for (std::uint32_t i = 0; i < words; i++)
                    c.payload.push_back(cpu.m_memory.read<std::uint32_t>(c.gpr[2] + i * 4));
            } catch (const Core::MemoryException &) {
                c.payload.clear();
            }
        }
        static Core::Gfx::Gx2Replayer replayer;
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
        static const bool nativeInitialized = [&] {
            const auto *enabled = std::getenv("WEMU_NATIVE_RASTER");
            if (!gpuMode && enabled && enabled[0] == '1') {
                const char *textureReuse = std::getenv("WEMU_NATIVE_TEXTURE_REUSE");
                const char *lazyReadback = std::getenv("WEMU_NATIVE_LAZY_READBACK");
                replayer.setRasterBackend(std::make_shared<Core::Gfx::VulkanRasterBackend>(!textureReuse || std::string_view(textureReuse) != "0",
                                                                                           lazyReadback && std::string_view(lazyReadback) == "1"));
                Utils::Log::error("[GX2] experimental translated Vulkan raster backend enabled; unsupported draws remain software");
            }
            return true;
        }();
        (void) nativeInitialized;
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
                                                        "SetVertexSamplerBorderColor"};
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
        const std::uint64_t hash = Core::Gfx::gx2Stream().contentHash();
        if (noSkip || !s_haveFrame || hash != s_lastHash) {
            replayer.replay(Core::Gfx::gx2Stream(), &cpu.m_memory);
            s_lastHash = hash;
            s_haveFrame = true;
        }
        if (cpu.m_renderer)
            cpu.m_renderer->flip_tv(replayer.framebuffer().data(), Core::Gfx::Gx2Replayer::kWidth, Core::Gfx::Gx2Replayer::kHeight);
        Core::Gfx::gx2Stream().clear();
        g_swapCount++;
        g_flipCount = g_swapCount;
        g_lastFlip = cpu.m_scheduler.now();
        cpu.m_gpr[3] = 0;
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
            // Base layout is independent of the requested mip count. Mip storage is still unimplemented.
            // Other dimensions, depth/scan usage and AA still use the legacy calculation.
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
            cpu.m_memory.write<std::uint32_t>(s + SURF_PITCH, pitch);
            cpu.m_memory.write<std::uint32_t>(s + SURF_IMAGE_SIZE, imageSize);
            cpu.m_memory.write<std::uint32_t>(s + SURF_MIPMAP_SIZE, 0);
            cpu.m_memory.write<std::uint32_t>(s + SURF_ALIGNMENT, surfaceAlign);
            if (gx2SizeTrace() && imageSize >= gx2SizeTraceMin() && gx2SizeTraceTake())
                Utils::Log::error("[GX2SIZE] Surface @0x{:08X} dim={} raw={}x{} depth={} mip={} fmt=0x{:X} aa={} use=0x{:X} tile={} -> calc={}x{} "
                                  "pitch={} elem={} image=0x{:X} mip=0 align=0x{:X} LR={}",
                                  s, dim, rawWidth, rawHeight, depth, mipLevels, format, aa, use, tileMode, width, height, pitch,
                                  bytesPerElement(format), imageSize, surfaceAlign,
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
        if (outBuf)
            cpu.m_memory.write<std::uint32_t>(outBuf, g_dlRecording ? g_dlBuffer : 0);
        if (outSize)
            cpu.m_memory.write<std::uint32_t>(outSize, g_dlRecording ? kDlBytesPerCmd * static_cast<std::uint32_t>(g_dlCurrent.size()) : 0);
        cpu.m_gpr[3] = g_dlRecording ? 1u : 0u;
    }

    // GX2BeginDisplayListEx(void *buffer, u32 size, BOOL profiling)
    void gx2_BeginDisplayListEx(Core::Interpreter &cpu)
    {
        g_dlStats.begins++;
        if (g_dlRecording)
            g_dlStats.nested++;
        g_dlRecording = true;
        g_dlBuffer = cpu.m_gpr[3];
        g_dlCurrent.clear();
        cpu.m_gpr[3] = 0;
    }

    // GX2EndDisplayList(void *buffer) -> bytes used. Must be non-zero: titles treat a zero-sized
    // list as empty and skip calling it.
    void gx2_EndDisplayList(Core::Interpreter &cpu)
    {
        const std::uint32_t buf = cpu.m_gpr[3] ? cpu.m_gpr[3] : g_dlBuffer;
        g_dlRecording = false;
        // Bound the cache: buffers are normally reused (so this stays small), but never let a
        // title that cycles through fresh buffers grow it without limit.
        if (g_displayLists.size() > 8192)
            g_displayLists.clear();
        auto &list = g_displayLists[buf];
        list = std::move(g_dlCurrent);
        g_dlCurrent.clear();
        g_dlStats.ends++;
        g_dlStats.recorded += list.size();
        if (list.empty())
            g_dlStats.emptyLists++;
        if ((g_dlStats.ends % 2000) == 0)
            dlReport();
        cpu.m_gpr[3] = kDlBytesPerCmd * (static_cast<std::uint32_t>(list.size()) + 1);
    }

    // GX2CallDisplayList(void *buffer, u32 size) / GX2DirectCallDisplayList: splice the recorded
    // commands into whatever sink is active (the frame, or an enclosing list).
    void gx2_CallDisplayList(Core::Interpreter &cpu)
    {
        g_dlStats.calls++;
        if (const auto it = g_displayLists.find(cpu.m_gpr[3]); it != g_displayLists.end()) {
            // Copy first: a nested self-call would otherwise invalidate the iterator mid-splice.
            const std::vector<Core::Gfx::Gx2Command> cmds = it->second;
            g_dlStats.hits++;
            g_dlStats.spliced += cmds.size();
            for (const auto &c: cmds) {
                if (isDrawCmd(c.type))
                    g_dlStats.splicedDraws++;
                pushCmd(c);
            }
        } else {
            g_dlStats.misses++;
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
            "GX2SaveStreamOutContext",
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
            "GX2SetStreamOutBuffer",
            "GX2SetStreamOutContext",
            "GX2SetStreamOutEnable",
            "GX2SetSurfaceSwizzle",
            "GX2SetSwapInterval",
            "GX2SetTargetChannelMasksReg",
            "GX2SetTessellation",
            "GX2SetTVBuffer",
            "GX2SetTVEnable",
            "GX2SetTVGamma",
            "GX2SetTVScale",
            "GX2SetupContextStateEx",
            "GX2SetVertexSampler",
            "GX2SetVertexSamplerBorderColor",
            "GX2SetVertexTexture",
            "GX2GetContextStateDisplayList",
    };

    const char *const kRetSize[] = {
            "GX2CalcGeometryShaderInputRingBufferSize",
            "GX2CalcGeometryShaderOutputRingBufferSize",
            "GX2GetGeometryShaderGPRs",
            "GX2GetGeometryShaderStackEntries",
            "GX2GetPixelShaderGPRs",
            "GX2GetPixelShaderStackEntries",
            "GX2GetVertexShaderGPRs",
            "GX2GetVertexShaderStackEntries",
            "GX2GPUTimeToCPUTime",
    };

} // namespace

namespace Core::Gfx {
    Gx2CommandStream &gx2Stream()
    {
        static Gx2CommandStream stream;
        return stream;
    }
} // namespace Core::Gfx

void RegisterGx2Functions()
{
    using C = Core::Gfx::Gx2Cmd;
    for (const char *name: kNoops)
        Core::syscallHandler.registerSyscall(name, gx2_noop);
    for (const char *name: kRetSize)
        Core::syscallHandler.registerSyscall(name, gx2_RetSize);

    Core::syscallHandler.registerSyscall("GX2Init", gx2_Init);
    Core::syscallHandler.registerSyscall("GX2CalcTVSize", gx2_CalcTVSize);
    Core::syscallHandler.registerSyscall("GX2CalcDRCSize", gx2_CalcDRCSize);
    Core::syscallHandler.registerSyscall("GX2CalcSurfaceSizeAndAlignment", gx2_CalcSurfaceSizeAndAlignment);
    Core::syscallHandler.registerSyscall("GX2CopySurface", gx2_CopySurface);
    Core::syscallHandler.registerSyscall("GX2CalcFetchShaderSizeEx", gx2_CalcFetchShaderSizeEx);
    Core::syscallHandler.registerSyscall("GX2InitFetchShaderEx", gx2_InitFetchShaderEx);
    Core::syscallHandler.registerSyscall("GX2CalcColorBufferAuxInfo", gx2_CalcAuxInfoNone);
    Core::syscallHandler.registerSyscall("GX2CalcDepthBufferHiZInfo", gx2_CalcAuxInfoNone);
    Core::syscallHandler.registerSyscall("GX2GetCurrentDisplayList", gx2_GetCurrentDisplayList);
    Core::syscallHandler.registerSyscall("GX2BeginDisplayListEx", gx2_BeginDisplayListEx);
    Core::syscallHandler.registerSyscall("GX2EndDisplayList", gx2_EndDisplayList);
    Core::syscallHandler.registerSyscall("GX2CallDisplayList", gx2_CallDisplayList);
    Core::syscallHandler.registerSyscall("GX2DirectCallDisplayList", gx2_CallDisplayList);
    Core::syscallHandler.registerSyscall("GX2TempGetGPUVersion", gx2_TempGpuVersion);

    // Captured render commands -> command stream.
    Core::syscallHandler.registerSyscall("GX2SetContextState", gx2_rec<C::SetContextState>);
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
    Core::syscallHandler.registerSyscall("GX2SetBlendControl", gx2_rec<C::SetBlendControl>);
    Core::syscallHandler.registerSyscall("GX2SetColorControl", gx2_rec<C::SetColorControl>);
    Core::syscallHandler.registerSyscall("GX2SetDepthStencilControl", gx2_rec<C::SetDepthStencilControl>);
    Core::syscallHandler.registerSyscall("GX2SetTargetChannelMasks", gx2_rec<C::SetTargetChannelMasks>);
    Core::syscallHandler.registerSyscall("GX2DrawEx", gx2_rec<C::DrawEx>);
    Core::syscallHandler.registerSyscall("GX2DrawIndexedEx", gx2_rec<C::DrawIndexedEx>);
    Core::syscallHandler.registerSyscall("GX2CopyColorBufferToScanBuffer", gx2_rec<C::CopyColorBufferToScanBuffer>);
    Core::syscallHandler.registerSyscall("GX2SwapScanBuffers", gx2_swap); // replays + presents the frame
    Core::syscallHandler.registerSyscall("GX2GetSwapStatus", gx2_GetSwapStatus);
    Core::syscallHandler.registerSyscall("GX2WaitForVsync", gx2_WaitForVsync);
    Core::syscallHandler.registerSyscall("GX2WaitForFlip", gx2_WaitForFlip);
}
