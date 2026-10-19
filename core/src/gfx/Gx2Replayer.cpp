#include "gfx/Gx2Replayer.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>

#include "cpu/memory/Memory.hpp"
#include "gfx/CubeSampler.hpp"
#include "gfx/LatteSurfaceLayout.hpp"
#include "gfx/LatteVsInterp.hpp"
#include "gfx/Renderer.hpp" // full definition: the GPU path calls Renderer::gpu* methods
#include "gfx/SurfaceLayout.hpp"
#include "gfx/TextureFormat.hpp"
#include "gfx/TriangleCoverage.hpp"
#include "gfx/UniformBufferFetch.hpp"
#include "gfx/VolumeSampler.hpp"
#include "gfx/VulkanRasterBackend.hpp"
#include "utils/FrameProfiler.hpp"
#include "utils/Logger.hpp"

namespace Core::Gfx {

    namespace {

        // GX2Surface field offsets (big-endian u32 each).
        constexpr std::uint32_t SURF_WIDTH = 0x04;
        constexpr std::uint32_t SURF_HEIGHT = 0x08;
        constexpr std::uint32_t SURF_FORMAT = 0x14;
        constexpr std::uint32_t SURF_IMAGE_PTR = 0x24;
        constexpr std::uint32_t SURF_TILE_MODE = 0x30;
        constexpr std::uint32_t SURF_SWIZZLE = 0x34;
        constexpr std::uint32_t SURF_PITCH = 0x3C;

        float asFloat(std::uint32_t bits)
        {
            float f;
            std::memcpy(&f, &bits, 4);
            return f;
        }


        // Decode one BC1 (DXT1) 8-byte block; out = 16 RGBA texels (4x4).
        void decodeBc1Block(const std::uint8_t *b, std::uint8_t out[16][4])
        {
            const std::uint16_t c0 = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(b[2] | (b[3] << 8));
            std::uint8_t pal[4][4];
            auto expand = [](std::uint16_t c, std::uint8_t *px) {
                px[0] = static_cast<std::uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
                px[1] = static_cast<std::uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
                px[2] = static_cast<std::uint8_t>((c & 0x1F) * 255 / 31);
                px[3] = 255;
            };
            expand(c0, pal[0]);
            expand(c1, pal[1]);
            if (c0 > c1) {
                for (int i = 0; i < 3; i++) {
                    pal[2][i] = static_cast<std::uint8_t>((2 * pal[0][i] + pal[1][i]) / 3);
                    pal[3][i] = static_cast<std::uint8_t>((pal[0][i] + 2 * pal[1][i]) / 3);
                }
                pal[2][3] = pal[3][3] = 255;
            } else {
                for (int i = 0; i < 3; i++) {
                    pal[2][i] = static_cast<std::uint8_t>((pal[0][i] + pal[1][i]) / 2);
                    pal[3][i] = 0;
                }
                pal[2][3] = 255;
                pal[3][3] = 0;
            }
            std::uint32_t bits = static_cast<std::uint32_t>(b[4]) | (b[5] << 8) | (b[6] << 16) | (static_cast<std::uint32_t>(b[7]) << 24);
            for (int i = 0; i < 16; i++, bits >>= 2)
                std::memcpy(out[i], pal[bits & 3], 4);
        }

        std::uint8_t decodeBc4Value(const std::uint8_t *b, std::uint32_t pixel)
        {
            std::uint8_t pal[8];
            pal[0] = b[0];
            pal[1] = b[1];
            if (pal[0] > pal[1]) {
                for (int i = 1; i <= 6; i++)
                    pal[i + 1] = static_cast<std::uint8_t>(((7 - i) * pal[0] + i * pal[1]) / 7);
            } else {
                for (int i = 1; i <= 4; i++)
                    pal[i + 1] = static_cast<std::uint8_t>(((5 - i) * pal[0] + i * pal[1]) / 5);
                pal[6] = 0;
                pal[7] = 255;
            }
            std::uint64_t bits = 0;
            for (int i = 0; i < 6; i++)
                bits |= static_cast<std::uint64_t>(b[2 + i]) << (8 * i);
            return pal[(bits >> (pixel * 3)) & 7];
        }

        std::array<std::uint8_t, 4> mapTextureChannels(const std::array<std::uint8_t, 4> &texel, std::uint32_t map)
        {
            std::array<std::uint8_t, 4> result{};
            for (unsigned c = 0; c < 4; c++) {
                const auto selector = (map >> (24 - c * 8)) & 7;
                result[c] = selector < 4 ? texel[selector] : selector == 5 ? 255 : 0;
            }
            return result;
        }

        std::uint8_t byteFromUnit(const double v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); }

        float interpolateValue(float a, float b, float c, float w0, float w1, float w2)
        {
            // Rounded barycentric weights need not sum to exactly one. Constant
            // attributes and depth planes must stay constant, especially at LESS
            // and EQUAL depth tests against a previously rendered plane.
            return a == b && a == c ? a : w0 * a + w1 * b + w2 * c;
        }

        std::uint8_t clampByte(const int v) { return static_cast<std::uint8_t>(std::clamp(v, 0, 255)); }

        bool shouldDumpAfterDraw(const std::uint32_t draw)
        {
            const char *spec = std::getenv("WEMU_GX2_DUMP_AFTER_DRAWS");
            if (!spec || !*spec)
                return false;
            const char *p = spec;
            while (*p) {
                char *end = nullptr;
                const auto first = static_cast<std::uint32_t>(std::strtoul(p, &end, 10));
                if (end == p)
                    break;
                auto last = first;
                if (*end == '-') {
                    p = end + 1;
                    last = static_cast<std::uint32_t>(std::strtoul(p, &end, 10));
                }
                if (first <= draw && draw <= last)
                    return true;
                p = end;
                while (*p && *p != ',')
                    ++p;
                if (*p == ',')
                    ++p;
            }
            return false;
        }

        std::uint32_t shaderHash(const std::vector<std::uint32_t> &program)
        {
            std::uint32_t h = 2166136261u; // FNV-1a over shader bytecode
            for (const auto w: program)
                h = (h ^ w) * 16777619u;
            return h;
        }

        constexpr std::uint32_t r600CfInst(std::uint32_t w1) { return (w1 >> 23) & 0x7F; }
        constexpr std::uint32_t r600CfAluInst(std::uint32_t w1) { return (w1 >> 26) & 0xF; }
        constexpr bool r600IsAluClause(std::uint32_t w1) { return r600CfAluInst(w1) >= 0x8; }
        constexpr bool r600CfEop(std::uint32_t w1) { return (w1 >> 21) & 1; }

        const char *cfName(std::uint32_t inst)
        {
            switch (inst) {
                case 0x00:
                    return "NOP";
                case 0x01:
                    return "TEX";
                case 0x02:
                    return "VTX";
                case 0x03:
                    return "VTX_TC";
                case 0x0A:
                    return "JUMP";
                case 0x0D:
                    return "ELSE";
                case 0x0E:
                    return "POP";
                case 0x13:
                    return "CALL_FS";
                case 0x14:
                    return "RETURN";
                case 0x27:
                    return "EXP";
                case 0x28:
                    return "EXP_DONE";
                default:
                    return "?";
            }
        }

        const char *alu2Name(std::uint32_t op)
        {
            switch (op) {
                case 0x00:
                    return "ADD";
                case 0x01:
                    return "MUL";
                case 0x02:
                    return "MUL_IEEE";
                case 0x03:
                    return "MAX";
                case 0x04:
                    return "MIN";
                case 0x10:
                    return "FRACT";
                case 0x14:
                    return "FLOOR";
                case 0x19:
                    return "MOV";
                case 0x1A:
                    return "NOP";
                case 0x50:
                    return "DOT4";
                case 0x51:
                    return "DOT4_IEEE";
                case 0x66:
                    return "RECIP_IEEE";
                case 0x6B:
                    return "FLT_TO_INT";
                case 0x6C:
                    return "INT_TO_FLT";
                case 0x6D:
                    return "UINT_TO_FLT";
                case 0x70:
                    return "ASHR";
                case 0x71:
                    return "LSHR";
                case 0x72:
                    return "LSHL";
                case 0x79:
                    return "FLT_TO_UINT";
                default:
                    return "?";
            }
        }

        const char *alu3Name(std::uint32_t op)
        {
            switch (op) {
                case 0x10:
                    return "MULADD";
                case 0x14:
                    return "MULADD_IEEE";
                case 0x18:
                    return "CNDE";
                case 0x19:
                    return "CNDGT";
                case 0x1A:
                    return "CNDGE";
                case 0x1C:
                    return "CNDE_INT";
                case 0x1D:
                    return "CNDGT_INT";
                case 0x1E:
                    return "CNDGE_INT";
                default:
                    return "?";
            }
        }

        bool shouldSummarizePixelShader(std::uint32_t hash)
        {
            const char *on = std::getenv("WEMU_PS_BYTECODE_SUMMARY");
            if (!on || on[0] != '1')
                return false;
            const char *filter = std::getenv("WEMU_PS_BYTECODE_HASH");
            if (!filter || !*filter)
                return true;
            return static_cast<std::uint32_t>(std::strtoul(filter, nullptr, 16)) == hash;
        }

        void summarizeAluSlot(const std::vector<std::uint32_t> &program, std::uint32_t slot)
        {
            const std::size_t base = static_cast<std::size_t>(slot) * 2;
            if (base + 1 >= program.size())
                return;
            const std::uint32_t w0 = program[base];
            const std::uint32_t w1 = program[base + 1];
            const std::uint32_t src0Sel = w0 & 0x1FF;
            const std::uint32_t src0Chan = (w0 >> 10) & 3;
            const std::uint32_t src1Sel = (w0 >> 13) & 0x1FF;
            const std::uint32_t src1Chan = (w0 >> 23) & 3;
            const std::uint32_t dst = (w1 >> 21) & 0x7F;
            const std::uint32_t dstChan = (w1 >> 29) & 3;
            const std::uint32_t op3 = (w1 >> 13) & 0x1F;
            if (op3 >= 0x08) {
                const std::uint32_t src2Sel = w1 & 0x1FF;
                const std::uint32_t src2Chan = (w1 >> 10) & 3;
                Utils::Log::error("[PSSUM]     alu@{} OP3 {}(0x{:02X}) R{}.{} = S{}.{} * S{}.{} + S{}.{} last={}", slot, alu3Name(op3), op3, dst,
                                  dstChan, src0Sel, src0Chan, src1Sel, src1Chan, src2Sel, src2Chan, (w0 >> 31) & 1);
            } else {
                const std::uint32_t op = (w1 >> 7) & 0x7FF;
                Utils::Log::error("[PSSUM]     alu@{} OP2 {}(0x{:02X}) R{}.{} = S{}.{} op S{}.{} wm={} last={}", slot, alu2Name(op), op, dst, dstChan,
                                  src0Sel, src0Chan, src1Sel, src1Chan, (w1 >> 4) & 1, (w0 >> 31) & 1);
            }
        }

        void summarizePixelShaderBytecode(const std::vector<std::uint32_t> &program, std::uint32_t guestShaderAddr)
        {
            if (program.size() < 2)
                return;
            const std::uint32_t hash = shaderHash(program);
            if (!shouldSummarizePixelShader(hash))
                return;

            Utils::Log::error("[PSSUM] shader=0x{:08X} hash={:08X} words={} bytecode summary", guestShaderAddr, hash, program.size());
            for (std::uint32_t cf = 0; cf * 2 + 1 < program.size() && cf < 24; cf++) {
                const std::uint32_t w0 = program[std::size_t(cf) * 2];
                const std::uint32_t w1 = program[std::size_t(cf) * 2 + 1];
                if (r600IsAluClause(w1)) {
                    const std::uint32_t addr = w0 & 0x3FFFFF;
                    const std::uint32_t count = ((w1 >> 18) & 0x7F) + 1;
                    Utils::Log::error("[PSSUM]   CF{} ALU kind={} addr={} count={} kc0=b{} a{} m{} kc1=b{} a{} m{}", cf, r600CfAluInst(w1), addr,
                                      count, (w0 >> 22) & 0xF, (w1 >> 2) & 0xFF, (w0 >> 30) & 3, (w0 >> 26) & 0xF, (w1 >> 10) & 0xFF, w1 & 3);
                    for (std::uint32_t slot = addr; slot < addr + std::min<std::uint32_t>(count, 12); slot++)
                        summarizeAluSlot(program, slot);
                    if (count > 12)
                        Utils::Log::error("[PSSUM]     ... {} more ALU slots", count - 12);
                    continue;
                }

                const std::uint32_t inst = r600CfInst(w1);
                const std::uint32_t addr = w0 & 0x3FFFFF;
                Utils::Log::error("[PSSUM]   CF{} {}(0x{:02X}) addr={} w0=0x{:08X} w1=0x{:08X} eop={}", cf, cfName(inst), inst, addr, w0, w1,
                                  r600CfEop(w1));
                if (inst == 0x01 || inst == 0x02 || inst == 0x03) {
                    for (std::uint32_t slot = addr; slot < addr + 4 && slot * 2 + 1 < program.size(); slot++)
                        Utils::Log::error("[PSSUM]     fetch@{} raw={} {}", slot, std::format("{:08X}", program[std::size_t(slot) * 2]),
                                          std::format("{:08X}", program[std::size_t(slot) * 2 + 1]));
                } else if (inst == 0x27 || inst == 0x28) {
                    Utils::Log::error("[PSSUM]     export type={} base={} gpr=R{} sel=({}, {}, {}, {})", (w0 >> 13) & 3, w0 & 0x1FFF,
                                      (w0 >> 15) & 0x7F, w1 & 7, (w1 >> 3) & 7, (w1 >> 6) & 7, (w1 >> 9) & 7);
                }
                if (r600CfEop(w1))
                    break;
            }
        }

        enum class SimplePixelShaderKind : std::uint8_t {
            None,
            TextureRegs, // out = texture * p1 + p0
            TextureParamRegs, // out = texture * (PARAM0 * p1 + p0)
        };

        SimplePixelShaderKind simplePixelShaderKind(const std::vector<std::uint32_t> &program)
        {
            // Patterns observed in MK8's nw::lyt UI panes:
            //   CF0 TEX
            //   CF1 ALU: texture * c1 + c0
            //   CF2 EXP_DONE R1.xyzw
            //
            // and:
            //   CF0 TEX
            //   CF1 ALU: PARAM0 * c1 + c0, then texture * factor
            //   CF2 EXP_DONE R1.xyzw
            // Keep this exact enough that WEMU_PS_TEXMOD cannot accidentally shade unrelated PS
            // bytecode when used for diagnostics.
            if (program.size() < 80)
                return SimplePixelShaderKind::None;
            const auto src0Sel = [](std::uint32_t w0) { return w0 & 0x1FF; };
            const auto src0Chan = [](std::uint32_t w0) { return (w0 >> 10) & 3; };
            const auto src1Sel = [](std::uint32_t w0) { return (w0 >> 13) & 0x1FF; };
            const auto src1Chan = [](std::uint32_t w0) { return (w0 >> 23) & 3; };
            const auto dstGpr = [](std::uint32_t w1) { return (w1 >> 21) & 0x7F; };
            const auto dstChan = [](std::uint32_t w1) { return (w1 >> 29) & 3; };
            const auto op2 = [](std::uint32_t w1) { return (w1 >> 7) & 0x7FF; };
            const auto op3 = [](std::uint32_t w1) { return (w1 >> 13) & 0x1F; };
            const auto isLast = [](std::uint32_t w0) { return ((w0 >> 31) & 1) != 0; };

            if (r600CfInst(program[1]) != 0x01 || (program[0] & 0x3FFFFF) != 48)
                return SimplePixelShaderKind::None;
            if (!r600IsAluClause(program[3]) || (program[2] & 0x3FFFFF) != 32)
                return SimplePixelShaderKind::None;
            if (r600CfInst(program[5]) != 0x28 || ((program[4] >> 15) & 0x7F) != 1 || (program[5] & 0xFFF) != 0x688)
                return SimplePixelShaderKind::None;

            const std::uint32_t aluCount = ((program[3] >> 18) & 0x7F) + 1;
            if (aluCount == 4) {
                for (std::uint32_t i = 0; i < 4; i++) {
                    const std::uint32_t w0 = program[(std::size_t{32} + i) * 2];
                    const std::uint32_t w1 = program[(std::size_t{32} + i) * 2 + 1];
                    if (op3(w1) != 0x10 || dstGpr(w1) != 1 || dstChan(w1) != i || src0Sel(w0) != 0 || src0Chan(w0) != i || src1Sel(w0) != 257 ||
                        src1Chan(w0) != i || (w1 & 0x1FF) != 256 || ((w1 >> 10) & 3) != i)
                        return SimplePixelShaderKind::None;
                }
                if (!isLast(program[std::size_t{33} * 2]) || !isLast(program[std::size_t{35} * 2]))
                    return SimplePixelShaderKind::None;
                return SimplePixelShaderKind::TextureRegs;
            }

            if (aluCount != 8)
                return SimplePixelShaderKind::None;

            const std::uint32_t factorSrc[4][3] = {
                    {1, 1, 257}, // slot 32: PARAM0.y * c1.y + c0.y -> R127.z
                    {1, 0, 257}, // slot 33: PARAM0.x * c1.x + c0.x -> R123.w
                    {1, 3, 257}, // slot 34: PARAM0.w * c1.w + c0.w -> R123.x
                    {1, 2, 257}, // slot 35: PARAM0.z * c1.z + c0.z -> R123.y
            };
            for (std::uint32_t i = 0; i < 4; i++) {
                const std::uint32_t w0 = program[(std::size_t{32} + i) * 2];
                const std::uint32_t w1 = program[(std::size_t{32} + i) * 2 + 1];
                if (op3(w1) != 0x10 || src0Sel(w0) != factorSrc[i][0] || src0Chan(w0) != factorSrc[i][1] || src1Sel(w0) != factorSrc[i][2] ||
                    src1Chan(w0) != factorSrc[i][1] || (w1 & 0x1FF) != 256 || ((w1 >> 10) & 3) != factorSrc[i][1])
                    return SimplePixelShaderKind::None;
            }
            if (!isLast(program[std::size_t{33} * 2]) || isLast(program[std::size_t{34} * 2]) || isLast(program[std::size_t{35} * 2]))
                return SimplePixelShaderKind::None;

            for (std::uint32_t i = 0; i < 4; i++) {
                const std::uint32_t slot = 36 + i;
                const std::uint32_t w0 = program[std::size_t(slot) * 2];
                const std::uint32_t w1 = program[std::size_t(slot) * 2 + 1];
                if (op3(w1) >= 0x08 || op2(w1) != 0x01 || dstGpr(w1) != 1 || dstChan(w1) != i || src0Sel(w0) != 0 || src0Chan(w0) != i)
                    return SimplePixelShaderKind::None;
            }
            if (!isLast(program[std::size_t{36} * 2]) || isLast(program[std::size_t{37} * 2]) || isLast(program[std::size_t{38} * 2]) ||
                !isLast(program[std::size_t{39} * 2]))
                return SimplePixelShaderKind::None;
            return SimplePixelShaderKind::TextureParamRegs;
        }

    } // namespace

    void Gx2Replayer::buildClearFrame(unsigned coveredWidth, unsigned coveredHeight)
    {
        m_fb.resize(static_cast<std::size_t>(kWidth) * kHeight * 4);
        const auto toByte = [](float f) { return static_cast<std::uint8_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
        const std::uint8_t r = toByte(m_clear[0]);
        const std::uint8_t g = toByte(m_clear[1]);
        const std::uint8_t b = toByte(m_clear[2]);
        // Scanout overwrites the covered rectangle immediately. Clear only its
        // margins, including when a new source is smaller than the last frame.
        for (unsigned y = 0; y < kHeight; ++y)
            for (unsigned x = y < coveredHeight ? coveredWidth : 0; x < kWidth; ++x) {
                const auto i = (std::size_t(y) * kWidth + x) * 4;
                m_fb[i + 0] = r;
                m_fb[i + 1] = g;
                m_fb[i + 2] = b;
                m_fb[i + 3] = 0xFF; // X
            }
    }

    std::size_t Gx2Replayer::SurfaceKeyHash::operator()(const SurfaceKey &k) const noexcept
    {
        std::uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](std::uint32_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(k.imagePtr);
        mix(k.width);
        mix(k.height);
        mix(k.pitch);
        mix(k.format);
        mix(k.tileMode);
        mix(k.swizzle);
        return static_cast<std::size_t>(h);
    }

    bool Gx2Replayer::useViewBacking()
    {
        static const bool enabled = []() {
            const char *e = std::getenv("WEMU_GX2_VIEW_BACKING");
            return !e || e[0] != '0';
        }();
        return enabled;
    }

    Gx2Replayer::DepthBacking *Gx2Replayer::depthBacking(const Surface &s, bool resolve) const
    {
        DepthFormat format;
        if (s.format == 0x80E)
            format = DepthFormat::Float32;
        else if (s.format == 5)
            format = DepthFormat::UNorm16;
        else if (s.format == 0x11)
            format = DepthFormat::UNorm24;
        else
            return nullptr;
        if (!m_mem || !s.imagePtr || !s.width || !s.height || s.width > 4096 || s.height > 4096 || s.pitch < s.width ||
            std::uint64_t(s.pitch) * s.height > 16ull * 1024 * 1024 || s.aa || s.dimension != 1 || s.depth > 1 || s.firstMip || s.firstSlice)
            return nullptr;
        auto key = makeSurfaceKey(s);
        key.format = s.format; // Depth storage retains numeric type, not just element layout.
        auto found = m_depthBackings.find(key);
        if (found == m_depthBackings.end()) {
            const auto *base = m_mem->hostPtr(s.imagePtr);
            const auto end = std::uint64_t(s.imagePtr) + s.imageSize;
            const auto *last = end <= 0x100000000ull && s.imageSize ? m_mem->hostPtr(std::uint32_t(end - 1)) : nullptr;
            if (!s.imageSize || !base || !last || std::uintptr_t(last) - std::uintptr_t(base) != s.imageSize - 1)
                return nullptr;
            DepthBacking backing;
            backing.format = format;
            backing.pixels.resize(std::size_t(s.pitch) * s.height);
            const auto elementBytes = depthElementBytes(format);
            for (unsigned y = 0; y < s.height; ++y)
                for (unsigned x = 0; x < s.width; ++x) {
                    const auto offset = tiledElementOffset(x, y, s.pitch, elementBytes * 8, s.tileMode, s.swizzle);
                    if (std::uint64_t(offset) + elementBytes > s.imageSize)
                        return nullptr;
                    std::uint32_t word{};
                    for (unsigned b = 0; b < elementBytes; ++b)
                        word |= std::uint32_t(base[offset + b]) << (b * 8);
                    backing.pixels[std::size_t(y) * s.pitch + x] = unpackDepth(word, format);
                }
            found = m_depthBackings.emplace(key, std::move(backing)).first;
        }
        auto &backing = found->second;
        if (backing.clear) {
            if (backing.uniform != backing.clear) {
                for (unsigned y = 0; y < s.height; ++y)
                    std::fill_n(backing.pixels.data() + std::size_t(y) * s.pitch, s.width, *backing.clear);
                backing.changed();
                backing.uniform = backing.clear;
            }
            backing.clear.reset();
        }
        if (resolve && backing.pending) {
            const auto &bytes = backing.pending->resolve();
            if (bytes.size() != backing.pixels.size() * sizeof(float))
                throw std::runtime_error("Depth backend returned an incomplete surface");
            std::memcpy(backing.pixels.data(), bytes.data(), bytes.size());
            backing.pending.reset();
            backing.changed();
        }
        return &backing;
    }

    void Gx2Replayer::clearDepthSurface(const Surface &original, float depth, unsigned flags)
    {
        const auto s = attachmentSurface(original);
        if (!(flags & 1))
            return;
        if (auto *backing = depthBacking(s, false)) {
            backing->pending.reset();
            backing->clear = quantizeDepth(depth, backing->format);
        }
    }

    Gx2Replayer::SurfaceKey Gx2Replayer::makeSurfaceKey(const Surface &s)
    {
        return {s.imagePtr, s.width, s.height, s.pitch, s.format, s.tileMode, s.swizzle};
    }

    std::shared_ptr<const void> Gx2Replayer::backingOwner(const SurfaceKey &key) const
    {
        const auto version = m_backingVersions[key];
        auto &owner = m_backingOwners[key];
        if (!owner.token || owner.version != version)
            owner = {version, std::make_shared<const int>()};
        return owner.token;
    }

    std::vector<std::uint8_t> &Gx2Replayer::viewBacking(const Surface &s)
    {
        resolveViewBacking(s);
        // Callers write through the returned reference.
        touchBacking(makeSurfaceKey(s));
        auto &backing = m_viewBackings[makeSurfaceKey(s)];
        const std::size_t bytes = static_cast<std::size_t>(s.pitch) * s.height * colorElementBytes(colorFormat(s.format));
        if (backing.size() != bytes)
            backing.assign(bytes, 0);
        return backing;
    }

    const std::vector<std::uint8_t> *Gx2Replayer::findViewBacking(const Surface &s) const
    {
        resolveViewBacking(s);
        const auto it = m_viewBackings.find(makeSurfaceKey(s));
        return it == m_viewBackings.end() ? nullptr : &it->second;
    }

    void Gx2Replayer::resolveViewBacking(const Surface &s) const
    {
        if (m_pendingReadbacks.empty())
            return;
        const auto key = makeSurfaceKey(s);
        const auto pending = m_pendingReadbacks.find(key);
        if (pending == m_pendingReadbacks.end())
            return;
        const auto &pixels = pending->second->resolve();
        auto &backing = m_viewBackings.at(key);
        if (pixels.size() != backing.size())
            throw std::runtime_error("Deferred raster target size changed");
        std::copy(pixels.begin(), pixels.end(), backing.begin());
        touchBacking(key);
        m_pendingReadbacks.erase(pending);
    }

    void Gx2Replayer::resolvePendingBackings(std::span<const Surface> within) const
    {
        // Software workers can select array slices dynamically. Resolve on the
        // replay thread before dispatch so sampling never mutates shared maps.
        // Slices of `within` live inside its base or mip storage; other targets stay on the GPU.
        const auto inside = [&](const SurfaceKey &key, std::size_t bytes) {
            if (within.empty())
                return true;
            const auto overlaps = [&](std::uint64_t base, std::uint64_t size) {
                return size && key.imagePtr < base + size && base < std::uint64_t(key.imagePtr) + bytes;
            };
            return std::any_of(within.begin(), within.end(), [&](const Surface &surface) {
                return overlaps(surface.imagePtr, surface.imageSize) || overlaps(surface.mipImagePtr, surface.mipImageSize);
            });
        };
        for (auto it = m_pendingReadbacks.begin(); it != m_pendingReadbacks.end();) {
            auto &backing = m_viewBackings.at(it->first);
            if (!inside(it->first, backing.size())) {
                ++it;
                continue;
            }
            const auto &pixels = it->second->resolve();
            if (pixels.size() != backing.size())
                throw std::runtime_error("Deferred raster target size changed");
            std::copy(pixels.begin(), pixels.end(), backing.begin());
            touchBacking(it->first);
            it = m_pendingReadbacks.erase(it);
        }
    }

    std::array<std::uint8_t, 4> Gx2Replayer::sampleViewBacking(const Surface &s, const std::vector<std::uint8_t> &backing, float u, float v) const
    {
        if (!s.width || !s.height || !s.pitch || backing.empty())
            return {128, 128, 128, 255};
        u -= std::floor(u); // wrap
        v -= std::floor(v);
        const auto tx = std::min(s.width - 1, static_cast<std::uint32_t>(u * static_cast<float>(s.width)));
        const auto ty = std::min(s.height - 1, static_cast<std::uint32_t>(v * static_cast<float>(s.height)));
        if (colorFormat(s.format) != ColorFormat::RGBA8) {
            const auto format = colorFormat(s.format);
            const auto bytes = colorElementBytes(format);
            const auto offset = (std::size_t(ty) * s.pitch + tx) * bytes;
            if (offset + bytes > backing.size())
                return {128, 128, 128, 255};
            const auto value = readFloatColor(backing.data() + offset, format);
            return {byteFromUnit(value[0]), byteFromUnit(value[1]), byteFromUnit(value[2]), byteFromUnit(value[3])};
        }
        const std::size_t off = (static_cast<std::size_t>(ty) * s.pitch + tx) * 4;
        if (off + 3 >= backing.size())
            return {128, 128, 128, 255};
        return {backing[off + 0], backing[off + 1], backing[off + 2], backing[off + 3]};
    }

    std::optional<Gx2Replayer::Surface> Gx2Replayer::arraySlice(const Surface &original, unsigned relative) const
    {
        if (original.dimension != 5 || !original.depth || !original.sliceCount || original.firstMip || original.aa ||
            original.firstSlice >= original.depth || original.sliceCount > original.depth - original.firstSlice || relative >= original.sliceCount ||
            !original.imageSize || original.imageSize % original.depth || !original.imagePtr || !m_mem)
            return {};
        const auto fmt = original.format & 0x3F;
        const bool compressed = fmt >= 0x31 && fmt <= 0x35;
        if (!compressed && fmt != 1 && fmt != 5 && fmt != 7 && fmt != 8 && fmt != 0xE && fmt != 0x11 && fmt != 0x16 && fmt != 0x1A &&
            fmt != 0x22 && original.format != 0x820 && original.format != 0x823)
            return {};
        const unsigned bytes = compressed                         ? (fmt == 0x31 || fmt == 0x34 ? 8 : 16)
                               : original.format == 0x823 || fmt == 0x22 ? 16
                               : original.format == 0x820         ? 8
                               : fmt == 1                         ? 1
                               : fmt == 5 || fmt == 7 || fmt == 8 ? 2
                                                                  : 4;
        const unsigned width = compressed ? (original.width + 3) / 4 : original.width;
        unsigned height = compressed ? (original.height + 3) / 4 : original.height;
        unsigned pitchAlign = 1, heightAlign = 1;
        if (original.tileMode == 2) {
            pitchAlign = std::max(8u, 32u / bytes);
            heightAlign = 8;
        } else if (original.tileMode == 4) {
            pitchAlign = std::max(32u, 128u / bytes);
            heightAlign = 16;
        } else if (original.tileMode > 1 && original.tileMode != 16) {
            return {};
        }
        height = (height + heightAlign - 1) & ~(heightAlign - 1);
        const std::uint64_t stride = original.imageSize / original.depth;
        if (!width || !height || original.pitch < width || original.pitch % pitchAlign || std::uint64_t(original.pitch) * height * bytes > stride ||
            (original.tileMode == 4 && stride % 2048))
            return {};
        const auto slice = original.firstSlice + relative;
        const std::uint64_t address = original.imagePtr + stride * slice;
        if (std::uint64_t(original.imagePtr) + original.imageSize > 0x100000000ull || !m_mem->hostPtr(static_cast<std::uint32_t>(address)) ||
            !m_mem->hostPtr(static_cast<std::uint32_t>(address + stride - 1)))
            return {};
        Surface selected = original;
        selected.dimension = 1;
        selected.depth = 1;
        selected.firstSlice = 0;
        selected.sliceCount = 1;
        selected.imagePtr = static_cast<std::uint32_t>(address);
        selected.imageSize = static_cast<std::uint32_t>(stride);
        if (original.tileMode == 4) {
            // Thin 2D macro tiles rotate the bank/pipe selection between slices.
            const auto swizzle = (((original.swizzle >> 8) & 7) + slice * 2) & 7;
            selected.swizzle = (original.swizzle & ~0x700u) | (swizzle << 8);
        }
        return selected;
    }

    std::vector<std::shared_ptr<RasterReadback>> Gx2Replayer::renderedArrayLayers(const Surface &surface)
    {
        // Every layer must have been rendered (or cleared) as its own surface; otherwise the guest
        // memory snapshot paths keep the array.
        const bool depthLayers = surface.format == 0x80E || surface.format == 5 || surface.format == 0x11;
        std::vector<std::shared_ptr<RasterReadback>> layers;
        bool rendered = false;
        for (unsigned layer = 0; layer < surface.sliceCount; ++layer) {
            const auto view = arraySlice(surface, layer);
            if (!view) {
                return {};
            }
            auto key = makeSurfaceKey(*view);
            if (depthLayers) {
                key.format = view->format;
                auto *backing = m_depthBackings.contains(key) ? depthBacking(*view, false) : nullptr;
                if (!backing)
                    return {};
                if (backing->pending) {
                    layers.push_back(backing->pending);
                    rendered = true;
                    continue;
                }
                const auto size = backing->pixels.size() * sizeof(float);
                std::vector<std::uint8_t> bytes(size);
                std::memcpy(bytes.data(), backing->pixels.data(), size);
                layers.push_back(std::make_shared<RasterReadback>(size, [bytes = std::move(bytes)] { return bytes; }));
                continue;
            }
            if (const auto pending = m_pendingReadbacks.find(key); pending != m_pendingReadbacks.end()) {
                layers.push_back(pending->second);
                rendered = true;
            } else if (const auto backing = m_viewBackings.find(key); backing != m_viewBackings.end())
                layers.push_back(std::make_shared<RasterReadback>(backing->second.size(), [bytes = backing->second] { return bytes; }));
            else {
                // A layer never rendered keeps its guest data (MK8 renders 17 of a 32-slice
                // R8 volume array and samples all of them): decode it like an ordinary texture.
                const auto decoded = view->format == surface.format ? decodedTexture(*view) : nullptr;
                const auto rowBytes = std::size_t(surface.width) * 4;
                if (!decoded || decoded->rgba.size() != rowBytes * surface.height || surface.pitch < surface.width)
                    return {};
                std::vector<std::uint8_t> bytes(std::size_t(surface.pitch) * surface.height * 4);
                for (unsigned y = 0; y < surface.height; ++y)
                    std::memcpy(bytes.data() + std::size_t(y) * surface.pitch * 4, decoded->rgba.data() + y * rowBytes, rowBytes);
                const auto size = bytes.size();
                layers.push_back(std::make_shared<RasterReadback>(size, [bytes = std::move(bytes)] { return bytes; }));
            }
        }
        // Without a GPU layer, layerSnapshot() (CPU-rendered layers, possibly with guest ones) or
        // arraySnapshot() (never rendered) share one upload per version instead.
        return rendered ? layers : std::vector<std::shared_ptr<RasterReadback>>{};
    }

    std::vector<std::shared_ptr<RasterReadback>> Gx2Replayer::renderedSubresources(const Surface &surface, unsigned levels) const
    {
        // Same views as mipSnapshot(), so rendered targets match by surface key. MK8 renders its
        // HDR glare chain and environment cube faces on the GPU every frame; decoding them on the
        // CPU cost a readback and ~50 ms per frame.
        if (surface.format == 5 || surface.format == 0x11 || surface.format == 0x80E || surface.aa || !surface.sliceCount ||
            surface.sliceCount > 256 || (surface.dimension != 1 && surface.dimension != 3 && surface.dimension != 5) ||
            (surface.dimension == 1 && surface.sliceCount != 1) || !levels)
            return {};
        std::vector<std::shared_ptr<RasterReadback>> result;
        result.reserve(std::size_t(levels) * surface.sliceCount);
        for (unsigned level = 0; level < levels; ++level) {
            const auto selected = mipSurface(surface, level + surface.firstMip);
            if (!selected)
                return {};
            auto view = *selected;
            view.firstMip = 0;
            if (view.dimension == 3)
                view.dimension = 5;
            if (view.width != std::max(1u, surface.width >> level) || view.height != std::max(1u, surface.height >> level))
                return {};
            for (unsigned layer = 0; layer < surface.sliceCount; ++layer) {
                const auto slice = view.dimension == 5 ? arraySlice(view, layer) : std::optional{view};
                if (!slice)
                    return {};
                const auto pending = m_pendingReadbacks.find(makeSurfaceKey(*slice));
                if (pending == m_pendingReadbacks.end())
                    return {};
                result.push_back(pending->second);
            }
        }
        return result;
    }

    std::shared_ptr<const std::vector<std::uint8_t>> Gx2Replayer::layerSnapshot(const Surface &surface)
    {
        // Rendered RGBA8/HDR layers that software draws left on the CPU (MK8 writes its 1280x720 HDR
        // array with multi-target draws). Decoding them through the sampler cost ~140 ms per frame.
        const auto elementBytes = colorElementBytes(colorFormat(surface.format));
        if (surface.dimension != 5 || !surface.sliceCount || surface.sliceCount > 256 || !surface.width || !surface.height ||
            surface.format == 5 || surface.format == 0x11 || surface.format == 0x80E)
            return {};
        std::vector<const std::vector<std::uint8_t> *> backings;
        std::vector<unsigned> pitches;
        std::vector<std::shared_ptr<DecodedTexture>> guestLayers; // keeps decoded layers alive while packing
        std::uint64_t generation = 0;
        bool rendered = false;
        for (unsigned layer = 0; layer < surface.sliceCount; ++layer) {
            const auto view = arraySlice(surface, layer);
            if (!view || view->width != surface.width || view->height != surface.height)
                return {};
            const auto key = makeSurfaceKey(*view);
            if (m_pendingReadbacks.contains(key))
                return {};
            if (const auto backing = m_viewBackings.find(key); backing != m_viewBackings.end()) {
                if (backing->second.size() < std::size_t(view->pitch) * view->height * elementBytes)
                    return {};
                backings.push_back(&backing->second);
                pitches.push_back(view->pitch);
                rendered = true;
            } else {
                // Layers never rendered keep their guest data; RGBA8 arrays can decode it like an
                // ordinary texture (MK8 renders 17 of a 32-slice R8 volume array).
                auto decoded = colorFormat(surface.format) == ColorFormat::RGBA8 ? decodedTexture(*view) : nullptr;
                if (!decoded || decoded->rgba.size() != std::size_t(surface.width) * surface.height * 4)
                    return {};
                backings.push_back(&decoded->rgba);
                pitches.push_back(surface.width);
                // A changed guest layer is decoded into a new entry.
                generation = (generation ^ reinterpret_cast<std::uintptr_t>(decoded.get())) * 1099511628211ull;
                guestLayers.push_back(std::move(decoded));
            }
            // Every draw or clear into a layer bumps its write count (see m_cpuWrittenSurfaces).
            if (const auto written = m_cpuWrittenSurfaces.find(view->imagePtr); written != m_cpuWrittenSurfaces.end())
                generation = (generation ^ written->second.writes ^ view->imagePtr) * 1099511628211ull;
        }
        if (!rendered)
            return {}; // arraySnapshot() keeps arrays that were never rendered
        for (const auto &entry: m_layerSnapshots)
            if (entry.surface == surface && entry.generation == generation)
                return entry.bytes;
        std::shared_ptr<const std::vector<std::uint8_t>> bytes;
        // Evicted snapshots no longer pin their decoded guest layers, whose addresses could be reused.
        if (guestLayers.empty())
            for (const auto &entry: m_retainedLayerSnapshots)
                if (entry.surface == surface && entry.generation == generation && (bytes = entry.bytes.lock()))
                    break;
        if (!bytes) {
            const auto rowBytes = std::size_t(surface.width) * elementBytes;
            auto packed = std::make_shared<std::vector<std::uint8_t>>(rowBytes * surface.height * surface.sliceCount);
            for (unsigned layer = 0; layer < surface.sliceCount; ++layer)
                for (unsigned y = 0; y < surface.height; ++y)
                    std::memcpy(packed->data() + (std::size_t(layer) * surface.height + y) * rowBytes,
                                backings[layer]->data() + std::size_t(y) * pitches[layer] * elementBytes, rowBytes);
            bytes = std::move(packed);
            if (m_retainedLayerSnapshots.size() >= 256)
                std::erase_if(m_retainedLayerSnapshots, [](const auto &entry) { return entry.bytes.expired(); });
            if (m_retainedLayerSnapshots.size() >= 256)
                m_retainedLayerSnapshots.clear();
            m_retainedLayerSnapshots.push_back({surface, generation, bytes});
        }
        std::erase_if(m_layerSnapshots, [&](const auto &entry) { return entry.surface == surface; });
        if (m_layerSnapshots.size() >= 8)
            m_layerSnapshots.erase(m_layerSnapshots.begin());
        m_layerSnapshots.push_back({surface, generation, bytes, std::move(guestLayers)});
        return bytes;
    }

    Gx2Replayer::Surface Gx2Replayer::attachmentSurface(const Surface &surface) const
    {
        auto view = surface;
        if (view.firstMip) {
            const auto selected = mipSurface(view, view.firstMip);
            if (!selected)
                return {};
            view = *selected;
        }
        // Linear 3D attachments select independent Z slices, just like array
        // layers. Thick tiling needs a separate address calculation.
        if (view.dimension == 2 && (view.tileMode <= 1 || view.tileMode == 16) && view.sliceCount == 1) {
            view.dimension = 5;
            return arraySlice(view, 0).value_or(Surface{});
        }
        // Cube attachments select one 2D face using the same slice layout.
        if (view.dimension == 3 && view.width == view.height && view.depth && view.depth % 6 == 0)
            view.dimension = 5;
        if (view.dimension == 5 && view.sliceCount == 1)
            if (const auto selected = arraySlice(view, 0))
                return *selected;
        return view;
    }

    std::optional<Gx2Replayer::Surface> Gx2Replayer::mipSurface(const Surface &surface, unsigned level) const
    {
        if (!level)
            return surface;
        if (!m_mem || level >= surface.mipCount || level > surface.mipOffsets.size() || !surface.mipImagePtr || !surface.mipImageSize || surface.aa)
            return {};
        const auto layout =
                latteSurfaceLayout(surface.width, surface.height, surface.depth, surface.format, surface.dimension, surface.tileMode, level);
        const std::uint64_t offset = level == 1 ? 0 : surface.mipOffsets[level - 1];
        if (!layout || offset + layout->imageSize > surface.mipImageSize ||
            std::uint64_t(surface.mipImagePtr) + offset + layout->imageSize > 0x100000000ull)
            return {};
        auto view = surface;
        view.imagePtr = surface.mipImagePtr + unsigned(offset);
        view.imageSize = layout->imageSize;
        view.width = std::max(1u, surface.width >> level);
        view.height = std::max(1u, surface.height >> level);
        view.pitch = layout->pitch;
        view.tileMode = layout->tileMode;
        view.depth = layout->depth;
        view.firstMip = 0;
        view.mipCount = 1;
        if (surface.dimension == 3)
            view.dimension = 5; // Cube mips can pad storage to more than six slices.
        return view;
    }

    std::shared_ptr<Gx2Replayer::MipSnapshot> Gx2Replayer::mipSnapshot(const Surface &surface, bool includeMips)
    {
        const auto format = surface.format & 0x3Fu;
        const bool knownUnorm =
                !(surface.format & 0x300u) && (format == 1 || format == 7 || format == 8 || format == 0x1A || (format >= 0x31 && format <= 0x35));
        if (!knownUnorm && surface.format != 0x234 && surface.format != 0x235 && surface.format != 0x80E && surface.format != 0x816 &&
            surface.format != 0x820 && surface.format != 0x823)
            return {}; // Do not turn the legacy decoder's unknown-format placeholder into a native texture.
        if (!m_mem || surface.aa || (surface.dimension != 1 && surface.dimension != 3 && surface.dimension != 5) || !surface.sliceCount ||
            surface.sliceCount > 256 || (surface.dimension == 1 && surface.sliceCount != 1) || surface.firstMip >= std::max(1u, surface.mipCount) ||
            (surface.dimension == 3 && (surface.width != surface.height || surface.firstSlice % 6 || surface.sliceCount % 6)))
            return {};
        const auto count = includeMips ? std::min(surface.viewMipCount ? surface.viewMipCount : std::max(1u, surface.mipCount),
                                                  std::max(1u, surface.mipCount) - surface.firstMip)
                                       : 1u;
        constexpr std::uint64_t budget = 64ull * 1024 * 1024;
        std::vector<Surface> levels, slices;
        std::uint64_t generation = 0, decodedBytes = 0;
        const auto overlaps = [](unsigned base, std::uint64_t size, unsigned other, std::uint64_t otherSize) {
            return std::uint64_t(base) < std::uint64_t(other) + otherSize && std::uint64_t(other) < std::uint64_t(base) + size;
        };
        const auto colorBytes = std::uint64_t(m_color.pitch) * m_color.height * colorElementBytes(colorFormat(m_color.format));
        // A snapshot already validated this frame stays valid while no slice was written since and
        // the bound target does not alias it. Checking its stored ranges avoids rebuilding every
        // level and slice view (AddrLib layouts) on each of the hundreds of lookups per frame.
        const auto validatedThisFrame = [&](const std::shared_ptr<MipSnapshot> &entry) {
            if (!entry || entry->surface != surface || entry->levelCount != count || entry->validatedFrame != m_presentCount ||
                entry->sliceRanges.empty())
                return false;
            std::uint64_t current = 0;
            for (const auto &[pointer, size]: entry->sliceRanges) {
                if (const auto written = m_cpuWrittenSurfaces.find(pointer); written != m_cpuWrittenSurfaces.end())
                    current = (current ^ written->second.writes ^ pointer) * 1099511628211ull;
                if (overlaps(pointer, size, m_color.imagePtr, colorBytes))
                    return false;
            }
            return current == entry->generation;
        };
        for (const auto &entry: m_mipSnapshots)
            if (validatedThisFrame(entry))
                return entry;
        for (const auto &weak: m_retainedMipSnapshots)
            if (auto entry = weak.lock(); validatedThisFrame(entry))
                return entry;
        bool feedback = false;
        for (unsigned level = 0; level < count; ++level) {
            const auto selected = mipSurface(surface, level + surface.firstMip);
            if (!selected)
                return {};
            auto view = *selected;
            view.firstMip = 0;
            if (view.dimension == 3)
                view.dimension = 5;
            levels.push_back(view);
            decodedBytes += std::uint64_t(view.width) * view.height * surface.sliceCount * 16;
            if (decodedBytes > budget)
                return {};
            for (unsigned layer = 0; layer < surface.sliceCount; ++layer) {
                const auto slice = view.dimension == 5 ? arraySlice(view, layer) : std::optional{view};
                // 0x820 is only decoded from its rendered RGBA8 backings (MK8's environment probe);
                // guest-memory half floats have no CPU decoder.
                if (!slice || (surface.format == 0x820 && !findViewBacking(*slice)))
                    return {};
                slices.push_back(*slice);
                if (const auto written = m_cpuWrittenSurfaces.find(slice->imagePtr); written != m_cpuWrittenSurfaces.end())
                    generation = (generation ^ written->second.writes ^ slice->imagePtr) * 1099511628211ull;
                feedback |= overlaps(slice->imagePtr, slice->imageSize, m_color.imagePtr, colorBytes);
            }
        }
        const auto rawSize = std::uint64_t(surface.imageSize) + (count > 1 || surface.firstMip ? surface.mipImageSize : 0);
        if (rawSize + decodedBytes > budget)
            return {};
        std::shared_ptr<MipSnapshot> previous;
        for (const auto &entry: m_mipSnapshots)
            if (entry->surface == surface && entry->levelCount == count && entry->generation == generation) {
                previous = entry;
                if (!feedback && entry->validatedFrame == m_presentCount)
                    return entry;
                break;
            }
        if (!previous && !feedback)
            for (const auto &weak: m_retainedMipSnapshots)
                if (auto entry = weak.lock(); entry && entry->surface == surface && entry->levelCount == count && entry->generation == generation) {
                    previous = entry;
                    if (entry->validatedFrame == m_presentCount)
                        return entry;
                    break;
                }
        std::vector<std::uint8_t> source;
        const auto append = [&](unsigned pointer, unsigned size) {
            const auto *bytes = m_mem->hostPtr(pointer, size);
            if (!bytes || !size)
                return false;
            source.insert(source.end(), bytes, bytes + size);
            return true;
        };
        if (!append(surface.imagePtr, surface.imageSize) || ((count > 1 || surface.firstMip) && !append(surface.mipImagePtr, surface.mipImageSize)))
            return {};
        if (previous && !feedback && previous->source == source) {
            previous->validatedFrame = m_presentCount;
            return previous;
        }
        auto result = std::make_shared<MipSnapshot>();
        result->surface = surface;
        result->levelCount = count;
        result->validatedFrame = m_presentCount;
        result->generation = generation;
        for (const auto &slice: slices)
            result->sliceRanges.emplace_back(slice.imagePtr, slice.imageSize);
        result->source = std::move(source);
        result->levels = std::move(levels);
        for (unsigned level = 0; level < count; ++level) {
            const auto &view = result->levels[level];
            auto &values = result->texels.emplace_back();
            values.reserve(std::size_t(view.width) * view.height * surface.sliceCount * 4);
            for (unsigned layer = 0; layer < surface.sliceCount; ++layer) {
                const auto &slice = slices[level * surface.sliceCount + layer];
                for (unsigned y = 0; y < view.height; ++y)
                    for (unsigned x = 0; x < view.width; ++x) {
                        const auto color =
                                sampleTexture(slice, TextureSampler{}, (float(x) + .5f) / float(view.width), (float(y) + .5f) / float(view.height));
                        values.insert(values.end(), color.begin(), color.end());
                    }
            }
        }
        for (unsigned level = 1; level < count; ++level)
            result->mips.push_back({result->levels[level].width, result->levels[level].height, result->texels[level]});
        if (!feedback) {
            std::erase_if(m_mipSnapshots, [&](const auto &entry) { return entry->surface == surface && entry->levelCount == count; });
            auto bytes = decodedBytes + rawSize;
            for (const auto &entry: m_mipSnapshots) {
                bytes += entry->source.size();
                for (const auto &mip: entry->texels)
                    bytes += mip.size() * sizeof(float);
            }
            while (!m_mipSnapshots.empty() && (bytes > budget || m_mipSnapshots.size() >= 16)) {
                bytes -= m_mipSnapshots.front()->source.size();
                for (const auto &mip: m_mipSnapshots.front()->texels)
                    bytes -= mip.size() * sizeof(float);
                m_mipSnapshots.erase(m_mipSnapshots.begin());
            }
            m_mipSnapshots.push_back(result);
            if (m_retainedMipSnapshots.size() >= 256) {
                std::erase_if(m_retainedMipSnapshots, [](const auto &entry) { return entry.expired(); });
                if (m_retainedMipSnapshots.size() >= 256)
                    m_retainedMipSnapshots.clear();
            }
            m_retainedMipSnapshots.push_back(result);
        }
        return result;
    }

    bool Gx2Replayer::sampleMipSnapshot(const MipSnapshot &snapshot, const TextureSampler &sampler, const std::array<float, 4> &coords,
                                        float cubeIndex, bool explicitLod, std::array<float, 4> &rgba)
    {
        const auto &surface = snapshot.surface;
        const bool cube = surface.dimension == 3;
        if (!snapshot.levelCount || !std::isfinite(coords[0]) || !std::isfinite(coords[1]) || (explicitLod && !std::isfinite(coords[3])) ||
            ((cube || surface.dimension == 5) && !std::isfinite(coords[2])) ||
            (cube && (!std::isfinite(cubeIndex) || coords[2] < 0 || coords[2] >= 6))) {
            rgba = sampler.border();
            return true;
        }
        const auto mipFilter = explicitLod ? (sampler.regs[0] >> 17) & 3 : 0;
        if (mipFilter > 2 || ((sampler.regs[0] >> 9) & 7) > 1 || (explicitLod && ((sampler.regs[0] >> 12) & 7) > 1))
            return false;
        float lod = 0;
        if (mipFilter) {
            const float available = float(snapshot.levelCount - 1);
            const float minimum = std::min(available, float(sampler.regs[1] & 1023) / 64.f);
            const float maximum = std::clamp(float((sampler.regs[1] >> 10) & 1023) / 64.f, minimum, available);
            lod = std::clamp(coords[3], minimum, maximum);
        }
        const auto filter = (sampler.regs[0] >> (lod > 0 ? 12 : 9)) & 7;
        const unsigned low = mipFilter == 1 ? unsigned(std::floor(lod + .5f)) : unsigned(std::floor(lod));
        const unsigned high = mipFilter == 2 ? std::min(low + 1, snapshot.levelCount - 1) : low;
        const auto sampleLevel = [&](unsigned level) {
            const auto &view = snapshot.levels[level];
            const auto texel = [&](unsigned layer, unsigned x, unsigned y) {
                const auto offset = ((std::size_t(layer) * view.height + y) * view.width + x) * 4;
                std::array<float, 4> value{};
                std::copy_n(snapshot.texels[level].data() + offset, 4, value.begin());
                return value;
            };
            if (cube) {
                const auto index = unsigned(std::clamp(std::floor(double(cubeIndex) + .5), 0.0, double(surface.sliceCount) / 6.0 - 1.0));
                return sampleCube(view.width, coords[0], coords[1], unsigned(coords[2]), filter == 1,
                                  [&](unsigned face, unsigned x, unsigned y) { return texel(index * 6 + face, x, y); });
            }
            const auto layer =
                    surface.dimension == 5 ? unsigned(std::clamp(std::floor(double(coords[2]) + .5), 0.0, double(surface.sliceCount - 1))) : 0;
            auto state = sampler;
            state.regs[0] = (state.regs[0] & ~(7u << 9)) | (filter << 9);
            return state.sample(view.width, view.height, coords[0], coords[1], [&](unsigned x, unsigned y) { return texel(layer, x, y); });
        };
        rgba = sampleLevel(low);
        if (high != low) {
            const auto next = sampleLevel(high);
            for (unsigned channel = 0; channel < 4; ++channel)
                rgba[channel] = std::lerp(rgba[channel], next[channel], lod - float(low));
        }
        return true;
    }

    std::shared_ptr<Gx2Replayer::ArraySnapshot> Gx2Replayer::arraySnapshot(const Surface &surface)
    {
        // Canonical depth and HDR values need typed array uploads. Keep them
        // on reference sampling until native uploads can consume their per-layer
        // versions without reading stale bytes or clamping brightness to UNORM8.
        if (surface.format == 5 || surface.format == 0x11 || surface.format == 0x80E || colorFormat(surface.format) != ColorFormat::RGBA8)
            return {};
        constexpr std::uint64_t budget = 64ull * 1024 * 1024;
        const std::uint64_t decodedBytes = std::uint64_t(surface.width) * surface.height * surface.sliceCount * 4;
        if (!surface.width || !surface.height || surface.width > 4096 || surface.height > 4096 || !surface.sliceCount || surface.sliceCount > 256 ||
            decodedBytes + surface.imageSize > budget || !useViewBacking())
            return {};
        const auto sourceEnd = std::uint64_t(surface.imagePtr) + surface.imageSize;
        for (const auto &[key, backing]: m_viewBackings)
            if (std::uint64_t(key.imagePtr) < sourceEnd && std::uint64_t(surface.imagePtr) < std::uint64_t(key.imagePtr) + backing.size())
                return {};
        std::vector<Surface> slices;
        for (unsigned layer = 0; layer < surface.sliceCount; ++layer) {
            const auto selected = arraySlice(surface, layer);
            // Rendered aliases must use versioned snapshots, not a raw-memory cache.
            if (!selected || findViewBacking(*selected) || m_cpuWritten.contains(selected->imagePtr))
                return {};
            slices.push_back(*selected);
        }
        const auto *begin = m_mem->hostPtr(surface.imagePtr);
        const auto *end = m_mem->hostPtr(surface.imagePtr + surface.imageSize - 1);
        if (!begin || !end || std::uintptr_t(end) - std::uintptr_t(begin) != surface.imageSize - 1)
            return {};
        // Guest memory does not change while one frame replays, and the checks above exclude
        // surfaces this replay renders into, so each snapshot is compared at most once per present.
        for (const auto &entry: m_arraySnapshots)
            if (entry->surface == surface && entry->source.size() == surface.imageSize &&
                (entry->validatedFrame == m_presentCount || std::memcmp(entry->source.data(), begin, surface.imageSize) == 0)) {
                entry->validatedFrame = m_presentCount;
                return entry;
            }
        auto result = std::make_shared<ArraySnapshot>();
        result->surface = surface;
        result->validatedFrame = m_presentCount;
        result->source.assign(begin, begin + surface.imageSize);
        result->rgba.resize(decodedBytes);
        for (unsigned layer = 0; layer < slices.size(); ++layer)
            for (unsigned y = 0; y < surface.height; ++y)
                for (unsigned x = 0; x < surface.width; ++x) {
                    const auto color = sample(slices[layer], (float(x) + 0.5f) / float(surface.width), (float(y) + 0.5f) / float(surface.height));
                    const auto offset = ((std::size_t(layer) * surface.height + y) * surface.width + x) * 4;
                    std::copy(color.begin(), color.end(), result->rgba.data() + offset);
                }
        std::erase_if(m_arraySnapshots, [&](const auto &entry) { return entry->surface == surface; });
        auto bytes = decodedBytes + surface.imageSize;
        for (const auto &entry: m_arraySnapshots)
            bytes += entry->source.size() + entry->rgba.size();
        while (!m_arraySnapshots.empty() && (bytes > budget || m_arraySnapshots.size() >= 8)) {
            bytes -= m_arraySnapshots.front()->source.size() + m_arraySnapshots.front()->rgba.size();
            m_arraySnapshots.erase(m_arraySnapshots.begin());
        }
        m_arraySnapshots.push_back(result);
        return result;
    }

    std::shared_ptr<Gx2Replayer::DecodedTexture> Gx2Replayer::decodedTexture(const Surface &surface, bool blocksOnly, bool floatTexels)
    {
        constexpr std::uint64_t budget = 256ull * 1024 * 1024;
        constexpr std::uint64_t entryBudget = 128ull * 1024 * 1024;
        const bool signedBc = surface.format == 0x234 || surface.format == 0x235;
        const bool blockFormat = surface.format == 0x31 || surface.format == 0x431 || surface.format == 0x34 || signedBc;
        if ((blocksOnly && !blockFormat) || (floatTexels && (blocksOnly || colorFormat(surface.format) == ColorFormat::RGBA8)))
            return {};
        const bool floating = floatTexels || signedBc;
        const std::uint64_t pixels = std::uint64_t(surface.width) * surface.height;
        if (!blocksOnly && pixels > entryBudget / (floating ? 16 : 4))
            return {};
        const unsigned blockBytes = surface.format == 0x235 ? 16 : 8;
        const std::uint64_t bytes = blocksOnly ? ((std::uint64_t(surface.width) + 3) / 4) * ((std::uint64_t(surface.height) + 3) / 4) * blockBytes
                                               : pixels * (floating ? 16 : 4);
        if (!m_mem || !surface.imagePtr || !surface.imageSize || !surface.width || !surface.height || bytes > entryBudget ||
            surface.imageSize > entryBudget - bytes || std::uint64_t(surface.imagePtr) + surface.imageSize > 0x100000000ull ||
            findViewBacking(surface) || m_cpuWritten.contains(surface.imagePtr) ||
            (floatTexels && m_pendingReadbacks.contains(makeSurfaceKey(surface))))
            return {};
        const auto *base = m_mem->hostPtr(surface.imagePtr);
        const auto *last = m_mem->hostPtr(surface.imagePtr + surface.imageSize - 1);
        if (!base || !last || std::uintptr_t(last) - std::uintptr_t(base) != surface.imageSize - 1)
            return {};
        const DecodedKey key{makeSurfaceKey(surface), floating && !blocksOnly ? surface.compMap : 0u, surface.imageSize, blocksOnly, floatTexels};
        auto &entry = m_decodedTextures[key];
        if (!entry)
            if (const auto retained = m_retainedDecodedTextures.find(key); retained != m_retainedDecodedTextures.end()) {
                entry = retained->second.lock();
                if (entry)
                    m_decodedBytes += entry->bytes();
            }
        // Exact byte comparison can use vectorized libc instead of a serial
        // dependency-chain hash. Validate once per frame; guest-only source
        // bytes cannot change during command replay. Include the snapshot in
        // the same cache budget as the decoded pixels.
        if (!entry || entry->validatedFrame != m_presentCount) {
            if (entry && entry->source.size() == surface.imageSize && std::memcmp(entry->source.data(), base, surface.imageSize) == 0) {
                entry->validatedFrame = m_presentCount;
            } else {
                auto decoded = std::make_shared<DecodedTexture>();
                decoded->source.assign(base, base + surface.imageSize);
                decoded->validatedFrame = m_presentCount;
                if (blocksOnly)
                    decoded->blocks.resize(bytes);
                else if (floating)
                    decoded->floating.resize(bytes / sizeof(float));
                else
                    decoded->rgba.resize(bytes);
                const auto format = surface.format & 0x3F;
                if (floatTexels) {
                    for (unsigned y = 0; y < surface.height; ++y)
                        for (unsigned x = 0; x < surface.width; ++x) {
                            const auto texel = sampleTexture(surface, TextureSampler{}, (float(x) + .5f) / float(surface.width),
                                                             (float(y) + .5f) / float(surface.height));
                            std::copy(texel.begin(), texel.end(), decoded->floating.data() + (std::size_t(y) * surface.width + x) * 4);
                        }
                } else if (blocksOnly) {
                    const auto width = (surface.width + 3) / 4, height = (surface.height + 3) / 4;
                    for (unsigned y = 0; y < height; ++y)
                        for (unsigned x = 0; x < width; ++x) {
                            const auto offset = tiledElementOffset(x, y, surface.pitch, blockBytes * 8, surface.tileMode, surface.swizzle);
                            if (std::uint64_t(offset) + blockBytes > surface.imageSize) {
                                if (!entry)
                                    m_decodedTextures.erase(key);
                                return {}; // Never upload a truncated compressed block.
                            }
                            std::copy_n(base + offset, blockBytes, decoded->blocks.data() + (std::size_t(y) * width + x) * blockBytes);
                        }
                } else if (format >= 0x31 && format <= 0x35) {
                    // A BC block supplies sixteen texels. Resolve its tiled address
                    // once and reuse BC1 color palettes instead of re-entering the
                    // general sampler and decoding the entire BC1 block per texel.
                    const unsigned blockBytes = format == 0x31 || format == 0x34 ? 8 : 16;
                    for (unsigned y = 0; y < surface.height; y += 4)
                        for (unsigned x = 0; x < surface.width; x += 4) {
                            const auto blockOffset =
                                    tiledElementOffset(x / 4, y / 4, surface.pitch, blockBytes * 8, surface.tileMode, surface.swizzle);
                            const auto *block = std::uint64_t(blockOffset) + blockBytes <= surface.imageSize ? base + blockOffset : nullptr;
                            std::uint8_t rgba[16][4]{};
                            if (block && !signedBc && format <= 0x33)
                                decodeBc1Block(block + (format == 0x31 ? 0 : 8), rgba);
                            for (unsigned by = 0; by < 4 && y + by < surface.height; ++by)
                                for (unsigned bx = 0; bx < 4 && x + bx < surface.width; ++bx) {
                                    const auto output = (std::size_t(y + by) * surface.width + x + bx) * 4;
                                    const auto pixel = by * 4 + bx;
                                    if (signedBc) {
                                        std::array<float, 4> mapped{};
                                        if (block) {
                                            const std::array<float, 4> raw{decodeBc4Snorm(block, pixel),
                                                                           format == 0x35 ? decodeBc4Snorm(block + 8, pixel) : 0.f, 0, 1};
                                            for (unsigned channel = 0; channel < 4; ++channel) {
                                                const auto selector = (surface.compMap >> (24 - channel * 8)) & 7;
                                                mapped[channel] = selector < 4 ? raw[selector] : selector == 5 ? 1.f : 0.f;
                                            }
                                        }
                                        std::copy(mapped.begin(), mapped.end(), decoded->floating.data() + output);
                                    } else if (block) {
                                        if (format == 0x34) {
                                            const auto value = decodeBc4Value(block, pixel);
                                            std::fill_n(rgba[pixel], 4, value);
                                        } else if (format == 0x35) {
                                            const auto r = decodeBc4Value(block, pixel), g = decodeBc4Value(block + 8, pixel);
                                            const std::array<std::uint8_t, 4> raw{r, g, 0, std::max(r, g)};
                                            std::copy(raw.begin(), raw.end(), rgba[pixel]);
                                        } else if (format == 0x32) {
                                            const auto alpha = (block[pixel / 2] >> ((pixel % 2) * 4)) & 15;
                                            rgba[pixel][3] = std::uint8_t(alpha * 17);
                                        } else if (format == 0x33)
                                            rgba[pixel][3] = decodeBc4Value(block, pixel);
                                        std::copy_n(rgba[pixel], 4, decoded->rgba.data() + output);
                                    } else {
                                        // Preserve the reference behavior for malformed/truncated
                                        // descriptors; the fast path only reads complete blocks.
                                        const auto texel = sample(surface, (float(x + bx) + .5f) / float(surface.width),
                                                                  (float(y + by) + .5f) / float(surface.height));
                                        std::copy(texel.begin(), texel.end(), decoded->rgba.data() + output);
                                    }
                                }
                        }
                } else {
                    for (unsigned y = 0; y < surface.height; ++y)
                        for (unsigned x = 0; x < surface.width; ++x) {
                            const auto texel = sample(surface, (float(x) + .5f) / float(surface.width), (float(y) + .5f) / float(surface.height));
                            const auto offset = (std::size_t(y) * surface.width + x) * 4;
                            std::copy(texel.begin(), texel.end(), decoded->rgba.data() + offset);
                        }
                }
                if (entry)
                    m_decodedBytes -= entry->bytes();
                m_decodedBytes += decoded->bytes();
                entry = std::move(decoded);
            }
        }
        if (m_retainedDecodedTextures.size() >= 4096) {
            std::erase_if(m_retainedDecodedTextures, [](const auto &retained) { return retained.second.expired(); });
            if (m_retainedDecodedTextures.size() >= 4096)
                m_retainedDecodedTextures.clear();
        }
        m_retainedDecodedTextures[key] = entry;
        entry->used = ++m_decodedClock;
        auto result = entry;
        while (m_decodedBytes > budget && m_decodedTextures.size() > 1) {
            auto oldest = m_decodedTextures.end();
            for (auto it = m_decodedTextures.begin(); it != m_decodedTextures.end(); ++it)
                if (it->second && it->second != result && (oldest == m_decodedTextures.end() || it->second->used < oldest->second->used))
                    oldest = it;
            if (oldest == m_decodedTextures.end())
                break;
            m_decodedBytes -= oldest->second->bytes();
            m_decodedTextures.erase(oldest);
        }
        return result;
    }

    // Component-mapped RGBA32F texels of a 3D texture's base level, slice-major. Empty when
    // the format or tiling is not decoded (thick tiling, compressed volumes).
    std::vector<float> Gx2Replayer::volumeTexels(const Surface &surface) const
    {
        const auto format = surface.format & 0x3F;
        const bool linear = surface.tileMode <= 1 || surface.tileMode == 16;
        if (!m_mem || !surface.imagePtr || !surface.width || !surface.height || !surface.depth || surface.depth > 256 || surface.firstMip ||
            surface.pitch < surface.width || !surface.imageSize || surface.imageSize % surface.depth ||
            std::uint64_t(surface.width) * surface.height * surface.depth > 4ull * 1024 * 1024 ||
            (!linear && surface.tileMode != 2 && surface.tileMode != 4) || (format != 0x16 && format != 0x1A && format != 0x06))
            return {};
        const auto *base = m_mem->hostPtr(surface.imagePtr);
        const auto *last = std::uint64_t(surface.imagePtr) + surface.imageSize <= 0x100000000ull
                                   ? m_mem->hostPtr(surface.imagePtr + surface.imageSize - 1)
                                   : nullptr;
        if (!base || !last || std::uintptr_t(last) - std::uintptr_t(base) != surface.imageSize - 1)
            return {};
        const auto smallFloat = [](std::uint32_t bits, unsigned mantissaBits) {
            const auto exponent = bits >> mantissaBits, mantissa = bits & ((1u << mantissaBits) - 1);
            const float fraction = float(mantissa) / float(1u << mantissaBits);
            if (exponent == 0)
                return std::ldexp(fraction, -14);
            if (exponent == 31)
                return mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            return std::ldexp(1.0f + fraction, int(exponent) - 15);
        };
        const std::uint32_t stride = surface.imageSize / surface.depth;
        std::vector<float> texels(std::size_t(surface.width) * surface.height * surface.depth * 4);
        for (unsigned z = 0; z < surface.depth; ++z) {
            auto swizzle = surface.swizzle;
            if (surface.tileMode == 4)
                swizzle = (swizzle & ~0x700u) | (((((swizzle >> 8) & 7) + z * 2) & 7) << 8);
            auto slice = surface;
            slice.imagePtr += z * stride;
            slice.imageSize = stride;
            slice.dimension = 1;
            slice.depth = 1;
            slice.firstSlice = 0;
            slice.sliceCount = 1;
            slice.swizzle = swizzle;
            const auto *rendered = findViewBacking(slice);
            for (unsigned y = 0; y < surface.height; ++y)
                for (unsigned x = 0; x < surface.width; ++x) {
                    const std::uint64_t offset =
                            std::uint64_t(z) * stride + (linear ? (std::uint64_t(y) * surface.pitch + x) * 4
                                                                : tiledElementOffset(x, y, surface.pitch, 32, surface.tileMode, swizzle));
                    if (offset + 4 > std::uint64_t(z + 1) * stride)
                        return {};
                    const std::uint32_t word =
                            base[offset] | (base[offset + 1] << 8) | (base[offset + 2] << 16) | (std::uint32_t(base[offset + 3]) << 24);
                    std::array<float, 4> raw{0, 0, 0, 1};
                    if (rendered) {
                        const auto local = (std::size_t(y) * surface.pitch + x) * 4;
                        if (local + 4 > rendered->size())
                            return {};
                        if (surface.format == 0x816)
                            raw = readFloatColor(rendered->data() + local, ColorFormat::R11G11B10Float);
                        else
                            for (unsigned c = 0; c < 4; ++c)
                                raw[c] = float((*rendered)[local + c]) / 255.f;
                    } else if (format == 0x16) { // R11G11B10_FLOAT
                        raw = {smallFloat(word & 0x7FF, 6), smallFloat((word >> 11) & 0x7FF, 6), smallFloat(word >> 22, 5), 1.0f};
                    } else if (format == 0x06) { // R32_FLOAT
                        raw[0] = asFloat(word);
                    } else {
                        for (unsigned c = 0; c < 4; ++c)
                            raw[c] = float((word >> (c * 8)) & 255) / 255.0f;
                    }
                    auto *out = texels.data() + ((std::size_t(z) * surface.height + y) * surface.width + x) * 4;
                    for (unsigned c = 0; c < 4; ++c) {
                        const auto selector = (surface.compMap >> (24 - c * 8)) & 7;
                        out[c] = selector < 4 ? raw[selector] : selector == 5 ? 1.0f : 0.0f;
                    }
                }
        }
        return texels;
    }

    bool Gx2Replayer::supportsGather(const Surface &surface, const TextureSampler &sampler)
    {
        const auto state = sampler.regs[0];
        return surface.dimension == 1 && surface.mipCount == 1 && !surface.firstMip && !surface.aa && !surface.firstSlice &&
               surface.sliceCount == 1 && surface.imagePtr && surface.width && surface.height && (state & 7) <= 2 && ((state >> 3) & 7) <= 2 &&
               ((state >> 9) & 7) <= 1 && ((state >> 12) & 7) <= 1 && !((state >> 19) & 7);
    }

    std::array<float, 4> Gx2Replayer::sampleTexture(const Surface &original, const TextureSampler &sampler, float u, float v, float layer,
                                                    const std::vector<std::uint8_t> *feedback, bool gather,
                                                    const std::vector<float> *depthFeedback) const
    {
        if (original.dimension == 2) {
            const auto values = volumeTexels(original);
            if (values.empty())
                return sampler.border();
            return sampleVolume(sampler, original.width, original.height, original.depth, u, v, layer, [&](unsigned x, unsigned y, unsigned z) {
                const auto offset = ((std::size_t(z) * original.height + y) * original.width + x) * 4;
                return std::array<float, 4>{values[offset], values[offset + 1], values[offset + 2], values[offset + 3]};
            });
        }
        if (original.dimension == 5) {
            if (!std::isfinite(layer) || !original.sliceCount)
                return sampler.border();
            const auto relative = static_cast<std::uint32_t>(std::clamp(std::floor(double(layer) + 0.5), 0.0, double(original.sliceCount - 1)));
            const auto slice = arraySlice(original, relative);
            if (!slice)
                return sampler.border();
            const auto &selected = *slice;
            // Only the base slice can reuse this draw's base-layer feedback snapshot.
            const auto *selectedFeedback = selected.imagePtr == original.imagePtr && selected.swizzle == original.swizzle ? feedback : nullptr;
            return sampleTexture(selected, sampler, u, v, 0, selectedFeedback);
        }
        const Surface &s = original;
        auto depthKey = makeSurfaceKey(s);
        depthKey.format = s.format;
        const std::vector<float> *depthPixels = depthFeedback;
        if (!depthPixels && m_depthBackings.contains(depthKey))
            if (const auto *backing = depthBacking(s))
                depthPixels = &backing->pixels;
        const auto format = colorFormat(s.format);
        const auto elementBytes = colorElementBytes(format);
        const auto *floatBacking = format != ColorFormat::RGBA8 ? (feedback ? feedback : findViewBacking(s)) : nullptr;
        auto fetch = [&](unsigned x, unsigned y) {
            if ((s.format == 0x234 || s.format == 0x235) && m_mem && s.imagePtr && !feedback) {
                const unsigned blockBytes = s.format == 0x234 ? 8 : 16;
                const auto offset = tiledElementOffset(x / 4, y / 4, s.pitch, blockBytes * 8, s.tileMode, s.swizzle);
                if (std::uint64_t(offset) + blockBytes > s.imageSize || std::uint64_t(s.imagePtr) + offset + blockBytes > 0x100000000ull)
                    return sampler.border();
                const auto *block = m_mem->hostPtr(s.imagePtr + offset, blockBytes);
                if (!block)
                    return sampler.border();
                const unsigned pixel = (y % 4) * 4 + x % 4;
                const std::array<float, 4> raw{decodeBc4Snorm(block, pixel), s.format == 0x235 ? decodeBc4Snorm(block + 8, pixel) : 0.f, 0, 1};
                std::array<float, 4> mapped{};
                for (unsigned c = 0; c < 4; ++c) {
                    const auto selector = (s.compMap >> (24 - c * 8)) & 7;
                    mapped[c] = selector < 4 ? raw[selector] : selector == 5 ? 1.f : 0.f;
                }
                return mapped;
            }
            if (format != ColorFormat::RGBA8 && s.width && s.height && m_mem) {
                std::array<float, 4> raw{};
                const auto offset = floatBacking ? (std::size_t(y) * s.pitch + x) * elementBytes
                                                 : tiledElementOffset(x, y, s.pitch, elementBytes * 8, s.tileMode, s.swizzle);
                if (floatBacking && offset + elementBytes <= floatBacking->size())
                    raw = readFloatColor(floatBacking->data() + offset, format);
                else if (!floatBacking && offset + elementBytes <= s.imageSize &&
                         std::uint64_t(s.imagePtr) + offset + elementBytes <= 0x100000000ull) {
                    const auto *p = m_mem->hostPtr(s.imagePtr + offset);
                    const auto *last = m_mem->hostPtr(s.imagePtr + offset + elementBytes - 1);
                    if (p && last && std::uintptr_t(last) - std::uintptr_t(p) == elementBytes - 1)
                        raw = readFloatColor(p, format);
                }
                std::array<float, 4> mapped;
                for (unsigned c = 0; c < 4; ++c) {
                    const auto selector = (s.compMap >> (24 - c * 8)) & 7;
                    mapped[c] = selector < 4 ? raw[selector] : selector == 5 ? 1.f : 0.f;
                }
                return mapped;
            }
            if (depthPixels && x < s.width && y < s.height) {
                const std::array raw{(*depthPixels)[std::size_t(y) * s.pitch + x], 0.f, 0.f, 1.f};
                std::array<float, 4> mapped{};
                for (unsigned c = 0; c < 4; ++c) {
                    const auto selector = (s.compMap >> (24 - c * 8)) & 7;
                    mapped[c] = selector < 4 ? raw[selector] : selector == 5 ? 1.f : 0.f;
                }
                return mapped;
            }
            if (s.format == 0x80E && !feedback && !findViewBacking(s) && m_mem && s.imagePtr) {
                const auto offset = tiledElementOffset(x, y, s.pitch, 32, s.tileMode, s.swizzle);
                if (std::uint64_t(offset) + 4 <= s.imageSize && std::uint64_t(s.imagePtr) + offset + 4 <= 0x100000000ull) {
                    const auto *p = m_mem->hostPtr(s.imagePtr + offset);
                    const auto *end = m_mem->hostPtr(s.imagePtr + offset + 3);
                    if (p && end && std::uintptr_t(end) - std::uintptr_t(p) == 3)
                        return decodeR32Float(p, s.compMap);
                }
                return sampler.border();
            }
            std::array<std::uint8_t, 4> raw{};
            if (s.imagePtr && s.width && s.height) {
                if (feedback) {
                    const auto offset = (std::size_t(y) * s.pitch + x) * 4;
                    raw = {128, 128, 128, 255};
                    if (s.pitch && offset + 3 < feedback->size())
                        std::copy_n(feedback->data() + offset, 4, raw.begin());
                } else {
                    const float tu = (float(x) + 0.5f) / float(s.width), tv = (float(y) + 0.5f) / float(s.height);
                    raw = sample(s, tu, tv);
                }
            }
            const auto mapped = mapTextureChannels(raw, s.compMap);
            std::array<float, 4> rgba{};
            for (unsigned c = 0; c < 4; c++)
                rgba[c] = float(mapped[c]) / 255.0f;
            return rgba;
        };
        // Unbound resources still honor constant component selectors.
        if (!s.width || !s.height)
            return fetch(0, 0);
        return gather ? sampler.gather(s.width, s.height, u, v, fetch) : sampler.sample(s.width, s.height, u, v, fetch);
    }

    std::array<std::uint8_t, 4> Gx2Replayer::blendTexel(const std::array<std::uint8_t, 4> &src, const std::uint8_t *dst,
                                                        const BlendState &blend) const
    {
        const auto factor = [&](const std::uint32_t mode, const int c) -> int {
            switch (mode) {
                case 0:
                    return 0; // ZERO
                case 1:
                    return 255; // ONE
                case 2:
                    return src[c]; // SRC_COLOR
                case 3:
                    return 255 - src[c]; // INV_SRC_COLOR
                case 4:
                    return src[3]; // SRC_ALPHA
                case 5:
                    return 255 - src[3]; // INV_SRC_ALPHA
                case 6:
                    return dst[3]; // DST_ALPHA
                case 7:
                    return 255 - dst[3]; // INV_DST_ALPHA
                case 8:
                    return dst[c]; // DST_COLOR
                case 9:
                    return 255 - dst[c]; // INV_DST_COLOR
                case 10:
                    return c == 3 ? 255 : std::min<int>(src[3], 255 - dst[3]); // SRC_ALPHA_SAT
                case 11:
                    return src[3]; // BOTH_SRC_ALPHA
                case 12:
                    return 255 - src[3]; // BOTH_INV_SRC_ALPHA
                case 13:
                    return m_blendConstant[c]; // BLEND_FACTOR
                case 14:
                    return 255 - m_blendConstant[c]; // INV_BLEND_FACTOR
                case 15:
                    return 0; // SRC1_COLOR: unsupported dual-source blend
                case 16:
                    return 255; // INV_SRC1_COLOR: unsupported dual-source blend
                case 17:
                    return 0; // SRC1_ALPHA: unsupported dual-source blend
                case 18:
                    return 255; // INV_SRC1_ALPHA: unsupported dual-source blend
                case 19:
                    return m_blendConstant[3]; // CONSTANT_ALPHA
                case 20:
                    return 255 - m_blendConstant[3]; // INV_CONSTANT_ALPHA
                default:
                    return 255;
            }
        };
        const auto apply = [&](const int s, const int d, const int sf, const int df, const std::uint32_t op) -> std::uint8_t {
            switch (op) {
                case 1:
                    return clampByte((s * sf - d * df) / 255); // SUB
                case 2:
                    return static_cast<std::uint8_t>(std::min(s, d)); // MIN
                case 3:
                    return static_cast<std::uint8_t>(std::max(s, d)); // MAX
                case 4:
                    return clampByte((d * df - s * sf) / 255); // REV_SUB
                case 0:
                default:
                    return clampByte((s * sf + d * df) / 255); // ADD
            }
        };

        std::array<std::uint8_t, 4> out{};
        for (int c = 0; c < 3; c++)
            out[c] = apply(src[c], dst[c], factor(blend.colorSrcBlend, c), factor(blend.colorDstBlend, c), blend.colorCombine);

        const std::uint32_t alphaSrcBlend = blend.useAlphaBlend ? blend.alphaSrcBlend : blend.colorSrcBlend;
        const std::uint32_t alphaDstBlend = blend.useAlphaBlend ? blend.alphaDstBlend : blend.colorDstBlend;
        const std::uint32_t alphaCombine = blend.useAlphaBlend ? blend.alphaCombine : blend.colorCombine;
        out[3] = apply(src[3], dst[3], factor(alphaSrcBlend, 3), factor(alphaDstBlend, 3), alphaCombine);
        return out;
    }

    Gx2Replayer::Surface Gx2Replayer::parseSurface(const std::uint32_t addr)
    {
        Surface s;
        if (!m_mem || !addr)
            return s;
        try {
            s.width = m_mem->read<std::uint32_t>(addr + SURF_WIDTH);
            s.dimension = m_mem->read<std::uint32_t>(addr);
            s.depth = m_mem->read<std::uint32_t>(addr + 0xC);
            s.mipCount = m_mem->read<std::uint32_t>(addr + 0x10);
            s.mipImageSize = m_mem->read<unsigned>(addr + 0x28);
            s.mipImagePtr = m_mem->read<unsigned>(addr + 0x2C);
            for (unsigned level = 0; level < s.mipOffsets.size(); ++level)
                s.mipOffsets[level] = m_mem->read<unsigned>(addr + 0x40 + level * 4);
            s.aa = m_mem->read<std::uint32_t>(addr + 0x18);
            s.imageSize = m_mem->read<std::uint32_t>(addr + 0x20);
            s.height = m_mem->read<std::uint32_t>(addr + SURF_HEIGHT);
            s.format = m_mem->read<std::uint32_t>(addr + SURF_FORMAT);
            s.imagePtr = m_mem->read<std::uint32_t>(addr + SURF_IMAGE_PTR);
            s.tileMode = m_mem->read<std::uint32_t>(addr + SURF_TILE_MODE);
            s.swizzle = m_mem->read<std::uint32_t>(addr + SURF_SWIZZLE);
            s.pitch = m_mem->read<std::uint32_t>(addr + SURF_PITCH);
        } catch (const Core::MemoryException &) {
            return {};
        }
        if (s.width == 0 || s.height == 0 || s.width > 4096 || s.height > 4096 || s.pitch == 0 || s.pitch > 8192)
            return {};
        return s;
    }

    // Titles often build GX2ColorBuffer/Texture structs on their stack, so the struct pointer is
    // stale by replay time — prefer the call-time snapshot recorded in the command payload.
    Gx2Replayer::Surface Gx2Replayer::parseSurface(const Gx2Command &cmd)
    {
        if (cmd.payload.size() >= 16) {
            Surface s;
            s.width = cmd.payload[SURF_WIDTH / 4];
            s.dimension = cmd.payload[0];
            s.depth = cmd.payload[0xC / 4];
            s.mipCount = cmd.payload[0x10 / 4];
            s.mipImageSize = cmd.payload[0x28 / 4];
            s.mipImagePtr = cmd.payload[0x2C / 4];
            if (cmd.payload.size() >= 0x74 / 4)
                std::copy_n(cmd.payload.begin() + 0x40 / 4, s.mipOffsets.size(), s.mipOffsets.begin());
            s.aa = cmd.payload[0x18 / 4];
            s.imageSize = cmd.payload[0x20 / 4];
            s.height = cmd.payload[SURF_HEIGHT / 4];
            s.format = cmd.payload[SURF_FORMAT / 4];
            s.imagePtr = cmd.payload[SURF_IMAGE_PTR / 4];
            s.tileMode = cmd.payload[SURF_TILE_MODE / 4];
            s.swizzle = cmd.payload[SURF_SWIZZLE / 4];
            s.pitch = cmd.payload[SURF_PITCH / 4];
            const bool buffer = cmd.type != Gx2Cmd::SetPixelTexture && cmd.type != Gx2Cmd::SetVertexTexture;
            const bool legacyPair = cmd.type == Gx2Cmd::ClearBuffersEx && cmd.payload.size() < 64;
            if (buffer && !legacyPair && cmd.payload.size() >= 32) {
                s.firstMip = cmd.payload[0x74 / 4];
                s.firstSlice = cmd.payload[0x78 / 4];
                s.sliceCount = std::max(1u, cmd.payload[0x7C / 4]);
            }
            if (s.width == 0 || s.height == 0 || s.width > 4096 || s.height > 4096 || s.pitch == 0 || s.pitch > 8192)
                return {};
            return s;
        }
        return parseSurface(cmd.gpr[0]);
    }

    void Gx2Replayer::clearSurface(const Surface &original, const float rgba[4])
    {
        const auto s = attachmentSurface(original);
        if (!s.imagePtr)
            return;
        m_cpuWritten.insert(s.imagePtr);
        auto &w = m_cpuWrittenSurfaces[s.imagePtr];
        w.surface = s;
        w.frame = m_presentCount + 1;
        w.writes++;
        std::uint8_t *base = nullptr;
        if (useViewBacking()) {
            base = viewBacking(s).data();
        } else {
            if (!m_mem)
                return;
            base = m_mem->hostPtr(s.imagePtr);
            if (!base || !m_mem->hostPtr(s.imagePtr + s.pitch * s.height * colorElementBytes(colorFormat(s.format)) - 1))
                return;
        }
        // Every element receives the same fully written bytes: encode one, then replicate rows.
        const auto format = colorFormat(s.format);
        const auto elementBytes = colorElementBytes(format);
        std::array<std::uint8_t, 16> element{};
        if (format != ColorFormat::RGBA8)
            writeFloatColor(element.data(), {rgba[0], rgba[1], rgba[2], rgba[3]}, 15, format);
        else {
            const auto toByte = [](float f) { return static_cast<std::uint8_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
            element = {toByte(rgba[0]), toByte(rgba[1]), toByte(rgba[2]), toByte(rgba[3])};
        }
        const std::size_t rowBytes = std::size_t(s.width) * elementBytes, pitchBytes = std::size_t(s.pitch) * elementBytes;
        if (!s.height || !rowBytes)
            return;
        for (std::uint32_t x = 0; x < s.width; x++)
            std::memcpy(base + std::size_t(x) * elementBytes, element.data(), elementBytes);
        for (std::uint32_t y = 1; y < s.height; y++)
            std::memcpy(base + y * pitchBytes, base, rowBytes);
    }

    void Gx2Replayer::repairProjection()
    {
        static const bool enabled = []() {
            const char *e = std::getenv("WEMU_PROJ_FIX");
            return e && e[0] == '1';
        }();
        if (!enabled)
            return;
        // Scale/translate the synthesised X/Y rows. Default maps a unit quad [0,1] across the full
        // clip range given the observed w (~869); tune via env while dialling in the menu layout.
        static const float scale = []() {
            const char *e = std::getenv("WEMU_PROJ_SCALE");
            return e ? static_cast<float>(std::atof(e)) : 1738.0f;
        }();
        // Find the projection: a register row acting as the z-row (z-component ~ -1, w-component a
        // large positive distance ~ 850..900) whose two preceding rows are entirely zero (the
        // missing X/Y scale rows). Registers are vec4s at m_vsRegs[k*4].
        for (std::uint32_t k = 2; k < 63; k++) {
            const float *z = &m_vsRegs[std::size_t(k) * 4];
            if (!(z[2] < -0.9f && z[2] > -1.1f && z[3] > 800.0f && z[3] < 950.0f))
                continue;
            float *rowX = &m_vsRegs[std::size_t(k - 2) * 4];
            float *rowY = &m_vsRegs[std::size_t(k - 1) * 4];
            const bool xZero = rowX[0] == 0 && rowX[1] == 0 && rowX[2] == 0 && rowX[3] == 0;
            const bool yZero = rowY[0] == 0 && rowY[1] == 0 && rowY[2] == 0 && rowY[3] == 0;
            if (!xZero || !yZero)
                continue;
            const float w = z[3]; // ~869: pos.w for a z=0 UI element, so scale relative to it
            rowX[0] = scale;
            rowX[3] = -scale * 0.5f * w / 869.0f; // centre the [0,1] range
            rowY[1] = -scale; // clip-space y points down
            rowY[3] = scale * 0.5f * w / 869.0f;
        }
    }

    void Gx2Replayer::decodeTextureToRgba(const Surface &s, std::vector<std::uint8_t> &out, std::uint32_t &tw, std::uint32_t &th) const
    {
        tw = s.width ? std::min(s.width, 2048u) : 1;
        th = s.height ? std::min(s.height, 2048u) : 1;
        out.resize(static_cast<std::size_t>(tw) * th * 4);
        for (std::uint32_t y = 0; y < th; y++) {
            for (std::uint32_t x = 0; x < tw; x++) {
                const auto t =
                        sample(s, (static_cast<float>(x) + 0.5f) / static_cast<float>(tw), (static_cast<float>(y) + 0.5f) / static_cast<float>(th));
                const std::size_t o = (static_cast<std::size_t>(y) * tw + x) * 4;
                out[o + 0] = t[0];
                out[o + 1] = t[1];
                out[o + 2] = t[2];
                out[o + 3] = t[3];
            }
        }
    }

    std::array<std::uint8_t, 4> Gx2Replayer::sample(const Surface &s, float u, float v) const
    {
        if (!m_mem || !s.imagePtr)
            return {128, 128, 128, 255};
        if (useViewBacking()) {
            if (const auto *backing = findViewBacking(s))
                return sampleViewBacking(s, *backing, u, v);
        }
        u -= std::floor(u); // wrap
        v -= std::floor(v);
        const auto tx = std::min(s.width - 1, static_cast<std::uint32_t>(u * static_cast<float>(s.width)));
        const auto ty = std::min(s.height - 1, static_cast<std::uint32_t>(v * static_cast<float>(s.height)));
        // Surfaces our own rasterizer produced are linear regardless of what the header claims. In
        // per-view backing mode, only an exact view match is considered CPU-written; another view of
        // the same image pointer must not force linear sampling of shared guest memory.
        const std::uint32_t tile = (!useViewBacking() && m_cpuWritten.contains(s.imagePtr)) ? 0 : s.tileMode;
        const std::uint32_t fmt = s.format & 0x3F;
        if (fmt == 0x31 || fmt == 0x32 || fmt == 0x33 || fmt == 0x34 || fmt == 0x35) {
            // BC1/2/3/4/5: element = 4x4 block. This is still nearest-neighbour and ignores the
            // real pixel shader, but the block decode itself is correct enough for UI glyph/mask
            // textures that previously appeared as coloured garbage or fully transparent.
            const std::uint32_t blockSize = (fmt == 0x31 || fmt == 0x34) ? 8 : 16;
            // GX2Surface.pitch already counts compression blocks for BC formats.
            const std::uint32_t off = tiledElementOffset(tx / 4, ty / 4, s.pitch, blockSize * 8, tile, s.swizzle);
            const std::uint8_t *block = m_mem->hostPtr(s.imagePtr + off);
            if (!block || !m_mem->hostPtr(s.imagePtr + off + blockSize - 1))
                return {128, 128, 128, 255};
            if (fmt == 0x34) { // BC4: one channel
                const std::uint8_t g = decodeBc4Value(block, (ty & 3) * 4 + (tx & 3));
                return {g, g, g, g};
            }
            if (fmt == 0x35) { // BC5: two BC4 channels
                const std::uint32_t pixel = (ty & 3) * 4 + (tx & 3);
                const std::uint8_t r = decodeBc4Value(block, pixel);
                const std::uint8_t g = decodeBc4Value(block + 8, pixel);
                return {r, g, 0, std::max(r, g)};
            }
            std::uint8_t texels[16][4];
            decodeBc1Block(block + ((fmt == 0x32 || fmt == 0x33) ? 8 : 0), texels);
            auto &t = texels[(ty & 3) * 4 + (tx & 3)];
            if (fmt == 0x33) // BC3: alpha block precedes
                t[3] = decodeBc4Value(block, (ty & 3) * 4 + (tx & 3));
            else if (fmt == 0x32) { // BC2/DXT3: explicit 4-bit alpha
                const std::uint32_t pixel = (ty & 3) * 4 + (tx & 3);
                const std::uint8_t a4 = (block[pixel / 2] >> ((pixel & 1) * 4)) & 0xF;
                t[3] = static_cast<std::uint8_t>((a4 << 4) | a4);
            }
            return {t[0], t[1], t[2], t[3]};
        }
        if (fmt == 0x01) { // R8_UNORM
            const std::uint32_t off = tiledElementOffset(tx, ty, s.pitch, 8, tile, s.swizzle);
            const std::uint8_t *px = m_mem->hostPtr(s.imagePtr + off);
            if (!px)
                return {128, 128, 128, 255};
            return {px[0], px[0], px[0], 255};
        }
        if (fmt == 0x07) { // R8_G8_UNORM
            const std::uint32_t off = tiledElementOffset(tx, ty, s.pitch, 16, tile, s.swizzle);
            const std::uint8_t *px = m_mem->hostPtr(s.imagePtr + off);
            if (!px || !m_mem->hostPtr(s.imagePtr + off + 1))
                return {128, 128, 128, 255};
            return {px[0], px[1], 0, 255};
        }
        if (fmt == 0x08) { // R5_G6_B5_UNORM
            const auto off = tiledElementOffset(tx, ty, s.pitch, 16, tile, s.swizzle);
            const std::uint64_t address = std::uint64_t(s.imagePtr) + off;
            if (std::uint64_t(off) + 2 > s.imageSize || address + 2 > 0x100000000ull)
                return {128, 128, 128, 255};
            const auto *px = m_mem->hostPtr(static_cast<std::uint32_t>(address), 2);
            return px ? decodeRgb565(px) : std::array<std::uint8_t, 4>{128, 128, 128, 255};
        }
        if (fmt == 0x05) { // R16_UNORM, little-endian GPU element
            const std::uint32_t off = tiledElementOffset(tx, ty, s.pitch, 16, tile, s.swizzle);
            const std::uint8_t *px = m_mem->hostPtr(s.imagePtr + off);
            if (!px || !m_mem->hostPtr(s.imagePtr + off + 1))
                return {128, 128, 128, 255};
            return {px[1], 0, 0, 255};
        }
        // Everything else: treat as RGBA8 (exact for our own intermediates and format 0x1A).
        const std::uint32_t off = tiledElementOffset(tx, ty, s.pitch, 32, tile, s.swizzle);
        const std::uint8_t *px = m_mem->hostPtr(s.imagePtr + off);
        if (!px || !m_mem->hostPtr(s.imagePtr + off + 3))
            return {128, 128, 128, 255};
        return {px[0], px[1], px[2], px[3]};
    }

    Gx2Replayer::AttributeFetch Gx2Replayer::prepareAttributeFetch() const
    {
        AttributeFetch plan;
        if (!m_mem || m_fetchLayout.empty())
            return plan;
        plan.valid = true;
        for (std::size_t i = 0; i + 7 < m_fetchLayout.size(); i += 8) {
            const auto location = m_fetchLayout[i], buffer = m_fetchLayout[i + 1];
            const auto format = m_fetchLayout[i + 3];
            auto endian = m_fetchLayout[i + 7];
            const auto semantic = std::find(m_vertexSemantics.begin(), m_vertexSemantics.end(), location);
            if (semantic == m_vertexSemantics.end())
                continue;
            auto &e = plan.elements.emplace_back();
            e.out = std::uint32_t(std::distance(m_vertexSemantics.begin(), semantic));
            e.mask = m_fetchLayout[i + 6];
            if (buffer >= m_attribs.size()) {
                e.fails = true;
                continue;
            }
            const auto &binding = m_attribs[buffer];
            // GX2AttribFormat: the low byte selects the component layout; 0x100 = integer,
            // 0x200 = signed, 0x800 = float (or integer scaled to float).
            const auto type = format & 0xFF;
            unsigned components = 0, bytes = 0;
            switch (type) {
                case 0x00:
                    components = 1, bytes = 1;
                    break; // 8
                case 0x04:
                    components = 2, bytes = 1;
                    break; // 8_8
                case 0x0A:
                    components = 4, bytes = 1;
                    break; // 8_8_8_8
                case 0x02:
                case 0x03:
                    components = 1, bytes = 2;
                    break; // 16 / 16 float
                case 0x07:
                case 0x08:
                    components = 2, bytes = 2;
                    break; // 16_16 / float
                case 0x0E:
                case 0x0F:
                    components = 4, bytes = 2;
                    break; // 16_16_16_16 / float
                case 0x05:
                case 0x06:
                    components = 1, bytes = 4;
                    break; // 32 / 32 float
                case 0x0C:
                case 0x0D:
                    components = 2, bytes = 4;
                    break; // 32_32 / float
                case 0x10:
                case 0x11:
                    components = 3, bytes = 4;
                    break; // 32_32_32 / float
                case 0x12:
                case 0x13:
                    components = 4, bytes = 4;
                    break; // 32_32_32_32 / float
                case 0x0B:
                    components = 1, bytes = 4, e.packed1010102 = true;
                    break; // 2_10_10_10
                default:
                    e.fails = true;
                    continue;
            }
            e.components = std::uint8_t(components);
            e.bytes = std::uint8_t(bytes);
            e.floating = type == 0x03 || type == 0x06 || type == 0x08 || type == 0x0D || type == 0x0F || type == 0x11 || type == 0x13;
            e.integer = format & 0x100;
            e.isSigned = format & 0x200;
            e.scaled = (format & 0x800) && !e.floating;
            if (endian == 3)
                endian = bytes == 4 ? 2 : bytes == 2 ? 1 : 0;
            const unsigned extent = components * bytes;
            for (unsigned byteIndex = 0; byteIndex < extent; ++byteIndex) {
                const auto swapped = endian == 2 ? byteIndex ^ 3u : endian == 1 ? byteIndex ^ 1u : byteIndex;
                e.fails |= swapped >= extent;
                e.order[byteIndex] = std::uint8_t(swapped);
            }
            // Per-instance elements always read the first element.
            e.stride = m_fetchLayout[i + 4] == 1 ? 0 : binding.stride;
            e.offset = m_fetchLayout[i + 2];
            e.size = binding.size;
            e.address = binding.addr;
            if (e.fails || !binding.addr)
                e.fails = true;
            else if (e.offset < binding.size) {
                // Bound a fetch by where its element starts, not by its byte extent: MK8 fetches
                // 32_32_32_32 positions from 12-byte strides, so the last vertex's fourth float lies
                // past the declared size and is read from the following memory.
                e.count = e.stride ? (binding.size - 1 - e.offset) / e.stride + 1 : ~std::uint64_t{};
                const std::uint64_t last = std::uint64_t(binding.addr) + e.offset + (e.stride ? (e.count - 1) * e.stride : 0) + extent;
                if (last <= 0x100000000ull)
                    e.host = m_mem->hostPtr(binding.addr + e.offset, last - binding.addr - e.offset);
            }
        }
        return plan;
    }

    bool Gx2Replayer::fetchAttributes(const AttributeFetch &fetch, std::uint32_t vertex, std::span<std::array<float, 4>> out) const
    {
        if (!fetch.valid)
            return false;
        std::ranges::fill(out, std::array<float, 4>{0, 0, 0, 1});
        for (const auto &e: fetch.elements) {
            if (e.out >= out.size())
                continue;
            if (e.fails)
                return false;
            const unsigned extent = unsigned(e.components) * e.bytes;
            const std::uint8_t *p;
            if (e.host) {
                if (e.stride && vertex >= e.count)
                    return false;
                p = e.host + std::uint64_t(vertex) * e.stride;
            } else {
                const std::uint64_t start = std::uint64_t(vertex) * e.stride + e.offset;
                if (start >= e.size || std::uint64_t(e.address) + start + extent > 0x100000000ull)
                    return false;
                const auto addr = e.address + static_cast<std::uint32_t>(start);
                p = m_mem->hostPtr(addr);
                if (!p || !m_mem->hostPtr(addr + extent - 1))
                    return false;
            }
            std::array<float, 4> value{0, 0, 0, 1};
            if (e.floating && e.bytes == 4) {
                for (unsigned c = 0; c < e.components; c++) {
                    const auto *order = &e.order[c * 4];
                    value[c] = asFloat(std::uint32_t(p[order[0]]) | std::uint32_t(p[order[1]]) << 8 | std::uint32_t(p[order[2]]) << 16 |
                                       std::uint32_t(p[order[3]]) << 24);
                }
            } else {
                const auto convert = [&](std::uint32_t bits, unsigned width) -> float {
                    const auto signBit = 1u << (width - 1);
                    const auto signedBits = std::int32_t(e.isSigned && (bits & signBit) && width < 32 ? bits | ~((signBit << 1) - 1) : bits);
                    if (e.integer && !e.scaled)
                        return asFloat(e.isSigned ? std::uint32_t(signedBits) : bits); // raw integer bits for integer shader inputs
                    if (e.scaled || e.integer)
                        return e.isSigned ? float(signedBits) : float(bits);
                    if (e.isSigned)
                        return std::max(-1.0f, float(signedBits) / float(signBit - 1));
                    return width == 32 ? float(double(bits) / 4294967295.0) : float(bits) / float((signBit << 1) - 1);
                };
                const auto halfToFloat = [](std::uint32_t h) {
                    const std::uint32_t sign = (h & 0x8000u) << 16, exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
                    if (exponent == 0) {
                        const float value = std::ldexp(float(mantissa), -24);
                        return sign ? -value : value;
                    }
                    if (exponent == 31)
                        return asFloat(sign | 0x7F800000u | (mantissa << 13));
                    return asFloat(sign | ((exponent + 112) << 23) | (mantissa << 13));
                };
                for (unsigned c = 0; c < e.components; c++) {
                    std::uint32_t bits = 0;
                    for (unsigned b = 0; b < e.bytes; b++)
                        bits |= std::uint32_t(p[e.order[c * e.bytes + b]]) << (b * 8);
                    if (e.packed1010102) {
                        for (unsigned k = 0; k < 3; ++k) {
                            const auto field = (bits >> (k * 10)) & 0x3FF;
                            value[k] = convert(field, 10);
                        }
                        value[3] = (e.integer || e.scaled) ? float(bits >> 30) : float(bits >> 30) / 3.0f;
                    } else if (e.floating)
                        value[c] = halfToFloat(bits);
                    else
                        value[c] = convert(bits, e.bytes * 8u);
                }
            }
            auto &a = out[e.out];
            for (unsigned c = 0; c < 4; c++) {
                const auto select = (e.mask >> (24 - c * 8)) & 7;
                if (select < 4)
                    a[c] = value[select];
                else if (select == 4 || select == 5)
                    a[c] = select == 5 ? 1.0f : 0.0f;
            }
        }
        return true;
    }

    std::size_t Gx2Replayer::DirectVertexEntry::bytes() const
    {
        std::size_t total = (fetchLayout.size() + sourceIndices.size()) * sizeof(std::uint32_t);
        for (const auto &[address, data]: sources)
            total += data.size();
        if (vertices)
            total += vertices->attributes.size() * sizeof(vertices->attributes[0]) + vertices->indices.size() * sizeof(std::uint32_t);
        return total;
    }

    bool Gx2Replayer::directVertexSources(const AttributeFetch &fetch, unsigned outputs, std::uint32_t low, std::uint32_t high,
                                          std::vector<std::pair<std::uint32_t, std::uint32_t>> &ranges)
    {
        ranges.clear();
        if (!fetch.valid)
            return true;
        for (const auto &e: fetch.elements) {
            // Skipped and failing elements read nothing; the per-vertex path is not tracked.
            if (e.out >= outputs || e.fails)
                continue;
            if (!e.host)
                return false;
            const std::uint64_t extent = std::uint64_t(e.components) * e.bytes;
            if (!e.stride) {
                if (e.count)
                    ranges.emplace_back(e.address + e.offset, std::uint32_t(extent));
                continue;
            }
            if (low >= e.count)
                continue;
            const std::uint64_t last = std::min<std::uint64_t>(high, e.count - 1);
            ranges.emplace_back(e.address + e.offset + low * e.stride, std::uint32_t((last - low) * e.stride + extent));
        }
        std::ranges::sort(ranges);
        std::size_t merged = 0;
        for (const auto &range: ranges) {
            if (merged && std::uint64_t(range.first) <= std::uint64_t(ranges[merged - 1].first) + ranges[merged - 1].second) {
                auto &previous = ranges[merged - 1];
                previous.second = std::uint32_t(std::max<std::uint64_t>(previous.second, std::uint64_t(range.first) + range.second - previous.first));
            } else
                ranges[merged++] = range;
        }
        ranges.resize(merged);
        return true;
    }

    void Gx2Replayer::drawImage(const Gx2Command &cmd)
    {
        const unsigned width = cmd.gpr[0], height = cmd.gpr[1];
        if (!width || !height || width > 4096 || height > 4096 || cmd.payload.size() != 32 + std::uint64_t(width) * height || !m_color.imagePtr ||
            !m_color.width || !m_color.height || m_color.width > m_color.pitch)
            return;
        const auto transform = [&](unsigned offset, float x, float y) {
            std::array<float, 4> result{};
            for (unsigned row = 0; row < 4; ++row)
                result[row] = asFloat(cmd.payload[offset + row * 4]) * x + asFloat(cmd.payload[offset + row * 4 + 1]) * y +
                              asFloat(cmd.payload[offset + row * 4 + 3]);
            return result;
        };
        std::array<RasterDraw::Vertex, 4> corners{};
        const std::array<std::array<float, 2>, 4> coords{{{-1, 1}, {1, 1}, {-1, -1}, {1, -1}}};
        for (unsigned i = 0; i < 4; ++i) {
            const auto clip = transform(0, coords[i][0], coords[i][1]);
            // This library path currently supports quads fully in front of the
            // near plane. Reject invalid views rather than dividing through W=0.
            if (!std::ranges::all_of(clip, [](float v) { return std::isfinite(v); }) || clip[3] <= 0)
                return;
            const auto uv = transform(16, (coords[i][0] + 1) * .5f, (1 - coords[i][1]) * .5f);
            auto &v = corners[i];
            v.x = m_vp[0] + (clip[0] / clip[3] + 1) * m_vp[2] * .5f;
            v.y = m_vp[1] + (1 - clip[1] / clip[3]) * m_vp[3] * .5f;
            v.reciprocalW = 1 / clip[3];
            v.inputs[0] = {uv[0], uv[1], 0, 1};
        }
        const std::array<RasterDraw::Vertex, 6> vertices{corners[0], corners[1], corners[2], corners[1], corners[3], corners[2]};
        const auto *rgba = reinterpret_cast<const std::uint8_t *>(cmd.payload.data() + 32);
        TextureSampler sampler;
        sampler.regs[0] = 2u | (2u << 3) | (1u << 9) | (1u << 12); // clamp to edge, bilinear
        const auto texel = [&](unsigned x, unsigned y) {
            const auto *pixel = rgba + (std::size_t(y) * width + x) * 4;
            return std::array<float, 4>{float(pixel[0]) / 255.f, float(pixel[1]) / 255.f, float(pixel[2]) / 255.f, float(pixel[3]) / 255.f};
        };
        const auto key = makeSurfaceKey(m_color);
        static unsigned diagnostics{};
        if (std::getenv("WEMU_MVP_TRACE") && diagnostics++ < 6)
            Utils::Log::error("[MVP_DRAW] target={:08X} {}x{} fmt={:X} vp=({},{},{},{}) scissor=({},{},{},{}) quad=({},{})-({},{})", m_color.imagePtr,
                              m_color.width, m_color.height, m_color.format, m_vp[0], m_vp[1], m_vp[2], m_vp[3], m_scissor[0], m_scissor[1],
                              m_scissor[2], m_scissor[3], corners[0].x, corners[0].y, corners[3].x, corners[3].y);
        if (useViewBacking() && m_rasterBackend) {
            static const auto shader = Latte::lowerFragmentShader(*Latte::decodeProgram(
                    {2, Latte::CF_TEX << 23, 0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0x688u, 0x10, 0x30000000u | (0x688u << 9), 1u << 23, 0}));
            std::shared_ptr<RasterReadback> rendered;
            if (m_rasterBackend->supportsRenderedTargets()) {
                const auto pending = m_pendingReadbacks.find(key);
                if (pending != m_pendingReadbacks.end())
                    rendered = pending->second;
            }
            auto &target = rendered ? m_viewBackings.at(key) : viewBacking(m_color);
            touchBacking(key);
            RasterDraw::Texture texture{shader.textures.at(0), width, height, sampler, texel};
            texture.unorm8 = {rgba, std::size_t(width) * height * 4};
            texture.unorm8Pitch = width;
            RasterDraw draw{shader,
                            vertices,
                            {},
                            {&texture, 1},
                            target,
                            m_color.width,
                            m_color.height,
                            m_color.pitch,
                            {m_scissor[0], m_scissor[1], m_scissor[2], m_scissor[3]},
                            15,
                            {}};
            draw.colorFormat = colorFormat(m_color.format);
            draw.renderedTarget = rendered;
            draw.supersedesTarget = true; // the result replaces m_pendingReadbacks[key] below
            if (auto result = m_rasterBackend->renderDeferred(draw)) {
                if (result->size() != target.size())
                    throw std::runtime_error("Image draw returned an incomplete target");
                if (result->readback)
                    m_pendingReadbacks[key] = std::move(result->readback);
                else {
                    m_pendingReadbacks.erase(key);
                    std::copy(result->rgba.begin(), result->rgba.end(), target.begin());
                }
                return;
            }
        }
        auto *target = useViewBacking() ? viewBacking(m_color).data() : m_mem ? m_mem->hostPtr(m_color.imagePtr) : nullptr;
        if (!target)
            return;
        const unsigned bytes = colorElementBytes(colorFormat(m_color.format));
        if (!useViewBacking() && !m_mem->hostPtr(m_color.imagePtr, std::size_t(m_color.pitch) * m_color.height * bytes))
            return;
        const int left = std::max(0, m_scissor[0]), top = std::max(0, m_scissor[1]);
        const int right = int(std::min(std::int64_t(m_color.width), std::int64_t(m_scissor[0]) + m_scissor[2]));
        const int bottom = int(std::min(std::int64_t(m_color.height), std::int64_t(m_scissor[1]) + m_scissor[3]));
        for (unsigned index = 0; index < vertices.size(); index += 3) {
            const auto &a = vertices[index], &b = vertices[index + 1], &c = vertices[index + 2];
            const double area = double(b.x - a.x) * (c.y - a.y) - double(b.y - a.y) * (c.x - a.x);
            if (!std::isfinite(area) || area == 0)
                continue;
            TriangleCoverage coverage({a.x, a.y}, {b.x, b.y}, {c.x, c.y});
            for (int y = top; y < bottom; ++y)
                for (int x = left; x < right; ++x) {
                    if (!coverage.contains(x + .5, y + .5))
                        continue;
                    const double w1 = ((x + .5 - a.x) * (c.y - a.y) - (y + .5 - a.y) * (c.x - a.x)) / area;
                    const double w2 = ((b.x - a.x) * (y + .5 - a.y) - (b.y - a.y) * (x + .5 - a.x)) / area;
                    const double w0 = 1 - w1 - w2;
                    const double reciprocalW = w0 * a.reciprocalW + w1 * b.reciprocalW + w2 * c.reciprocalW;
                    if (reciprocalW <= 0)
                        continue;
                    std::array<float, 2> uv{};
                    for (unsigned channel = 0; channel < 2; ++channel)
                        uv[channel] = float((w0 * a.inputs[0][channel] * a.reciprocalW + w1 * b.inputs[0][channel] * b.reciprocalW +
                                             w2 * c.inputs[0][channel] * c.reciprocalW) /
                                            reciprocalW);
                    const auto value = sampler.sample(width, height, uv[0], uv[1], texel);
                    auto *pixel = target + (std::size_t(y) * m_color.pitch + unsigned(x)) * bytes;
                    if (colorFormat(m_color.format) == ColorFormat::RGBA8)
                        for (unsigned channel = 0; channel < 4; ++channel)
                            pixel[channel] = byteFromUnit(value[channel]);
                    else
                        writeFloatColor(pixel, value, 15, colorFormat(m_color.format));
                }
        }
    }

    void Gx2Replayer::drawPrimitives(const Gx2Command &cmd, const bool indexed, int selectedColor)
    {
        if (!m_rasterizerEnabled)
            return;
        static const bool nativeAuditEnabled = [] {
            const auto *value = std::getenv("WEMU_NATIVE_SHADER_AUDIT");
            return value && value[0] == '1';
        }();
        static const bool profileEnabled = [] {
            const auto *value = std::getenv("WEMU_RASTER_PROFILE");
            return value && value[0] == '1';
        }();
        // WEMU_RASTER_PROFILE_FROM=N limits both per-draw diagnostics to presents >= N.
        static const std::uint32_t diagnosticsFrom = [] {
            const auto *value = std::getenv("WEMU_RASTER_PROFILE_FROM");
            return value ? std::uint32_t(std::strtoul(value, nullptr, 10)) : 0u;
        }();
        const bool nativeAudit = nativeAuditEnabled && m_presentCount >= diagnosticsFrom;
        const bool profile = profileEnabled && m_presentCount >= diagnosticsFrom;
        const auto profileStart = (profile || nativeAudit) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const auto pixelsBefore = m_stats.pixels;
        m_stats.draws++;
        // Triangle culling precedes fragment work and has no memory effects.
        // Avoid fetching/skinning hidden meshes or touching their attachments.
        if ((m_polygonControl & 3) == 3 && (cmd.gpr[0] == 4 || cmd.gpr[0] == 0x13))
            return;
        // The software pixel path samples one resource. Use the shader's resource ID,
        // not the last unit-zero binding (post-process passes commonly bind other units).
        unsigned textureUnit = 0;
        unsigned textureParam = 0, textureSelectX = 0, textureSelectY = 1;
        for (std::size_t cf = 0; cf + 1 < m_psProgram.size(); cf += 2) {
            const auto w0 = m_psProgram[cf], w1 = m_psProgram[cf + 1];
            if (((w1 >> 26) & 15) >= 8)
                continue;
            if (((w1 >> 23) & 127) == 1 && std::uint64_t(w0) * 2 + 2 < m_psProgram.size()) {
                const auto t0 = m_psProgram[std::size_t(w0) * 2], t2 = m_psProgram[std::size_t(w0) * 2 + 2];
                textureUnit = (t0 >> 8) & 255;
                const auto input = (t0 >> 16) & 127;
                if (input < m_pixelInputSemantics.size()) {
                    const auto semantic = m_pixelInputSemantics[input];
                    const auto output = std::find(m_vertexOutputSemantics.begin(), m_vertexOutputSemantics.end(), semantic);
                    if (output != m_vertexOutputSemantics.end()) {
                        textureParam = std::distance(m_vertexOutputSemantics.begin(), output);
                        textureSelectX = (t2 >> 20) & 7;
                        textureSelectY = (t2 >> 23) & 7;
                    }
                }
                break;
            }
            if (w1 & (1u << 21))
                break;
        }
        m_texture = textureUnit < m_pixelTextures.size() ? m_pixelTextures[textureUnit] : Surface{};
        // Export destination is independent of translation support. In particular,
        // a burst shader with only slot zero writable still targets slot zero when
        // its other exports or instructions require reference execution.
        const auto pixelDeclaration = m_psProgram.empty() ? nullptr : LatteVsInterp::compile(m_psProgram);
        const auto colorExports = pixelDeclaration && pixelDeclaration->valid ? pixelDeclaration->colorExportMask : 0u;
        unsigned writableColors{};
        for (unsigned slot = 0; slot < m_colorBuffers.size(); ++slot)
            if ((colorExports & (1u << slot)) && m_colorWriteEnable && m_channelMask[slot] && m_colorBuffers[slot].imagePtr)
                writableColors |= 1u << slot;
        // Independent color attachments can use separate native passes while the
        // backend exposes one attachment. A read-only depth test gives every pass the
        // same coverage; depth writes, stencil and framebuffer feedback need a
        // simultaneous MRT path, so retain fallback for those.
        if (selectedColor < 0 && std::popcount(writableColors) > 1 && (!m_depthTest || !m_depthWrite) && !m_stencilTest && !m_multiWriteEnable &&
            !m_gpuRenderer) {
            bool independent = true;
            const auto aliases = [](const Surface &a, const Surface &b) {
                return a.imagePtr && b.imagePtr && std::uint64_t(a.imagePtr) < std::uint64_t(b.imagePtr) + b.imageSize &&
                       std::uint64_t(b.imagePtr) < std::uint64_t(a.imagePtr) + a.imageSize;
            };
            for (unsigned slot = 0; slot < 8 && independent; ++slot)
                if (writableColors & (1u << slot)) {
                    independent = bool(*m_fragmentShaderCache.get(m_psProgram, {}, true, slot));
                    for (unsigned other = 0; other < slot; ++other)
                        if ((writableColors & (1u << other)) && aliases(m_colorBuffers[slot], m_colorBuffers[other]))
                            independent = false;
                    if (m_depthTest && aliases(m_colorBuffers[slot], m_depth))
                        independent = false;
                    for (const auto &texture: m_pixelTextures) {
                        independent &= !aliases(m_colorBuffers[slot], texture);
                        auto mips = texture;
                        mips.imagePtr = texture.mipImagePtr;
                        mips.imageSize = texture.mipImageSize;
                        independent &= !aliases(m_colorBuffers[slot], mips);
                    }
                    for (const auto &texture: m_vertexTextures) {
                        independent &= !aliases(m_colorBuffers[slot], texture);
                        auto mips = texture;
                        mips.imagePtr = texture.mipImagePtr;
                        mips.imageSize = texture.mipImageSize;
                        independent &= !aliases(m_colorBuffers[slot], mips);
                    }
                }
            if (independent) {
                const auto savedColor = m_color;
                const auto savedTarget = m_colorTarget;
                --m_stats.draws;
                for (unsigned slot = 0; slot < 8; ++slot)
                    if (writableColors & (1u << slot))
                        drawPrimitives(cmd, indexed, int(slot));
                m_color = savedColor;
                m_colorTarget = savedTarget;
                return;
            }
        }
        if (selectedColor < 0 && writableColors != 1 && std::has_single_bit(writableColors) && !m_multiWriteEnable)
            selectedColor = int(std::countr_zero(writableColors));
        const auto colorExport = selectedColor < 0 ? 0u : unsigned(selectedColor);
        if (selectedColor >= 0) {
            m_color = m_colorBuffers[colorExport];
            m_colorTarget = colorExport;
            writableColors = 1; // The selected guest attachment maps to native location zero.
        }
        // Zero writable colors can use the same analysis to prove a depth-only
        // replacement. A specialized original shader still needs mask 1 at submission.
        const bool onlyColorZero = writableColors <= 1 && !m_multiWriteEnable;
        const auto colorShader = m_psProgram.empty() ? nullptr : m_fragmentShaderCache.get(m_psProgram, {}, onlyColorZero, colorExport);
        if (selectedColor < 0 && m_colorBuffers[0].imagePtr && (writableColors == 1 || (colorShader && *colorShader && colorShader->exportsColor))) {
            m_color = m_colorBuffers[0];
            m_colorTarget = 0;
            if (m_gpuGraph && m_gpuRenderer)
                m_gpuRenderer->gpuBindTarget(m_color.imagePtr, m_color.width, m_color.height);
        }
        if (!m_mem || !m_color.imagePtr) {
            m_stats.skipNoTarget++;
            return;
        }
        std::uint8_t *target = nullptr;
        std::shared_ptr<RasterReadback> renderedTarget;
        static const bool residentTargetsEnabled = [] {
            const auto *value = std::getenv("WEMU_NATIVE_RESIDENT_TARGETS");
            return value && std::string_view(value) == "1";
        }();
        if (useViewBacking()) {
            if (residentTargetsEnabled && m_rasterBackend && m_rasterBackend->supportsRenderedTargets()) {
                const auto pending = m_pendingReadbacks.find(makeSurfaceKey(m_color));
                if (pending != m_pendingReadbacks.end())
                    renderedTarget = pending->second;
            }
            target = renderedTarget ? m_viewBackings.at(makeSurfaceKey(m_color)).data() : viewBacking(m_color).data();
        } else {
            target = m_mem->hostPtr(m_color.imagePtr);
            if (!target || !m_mem->hostPtr(m_color.imagePtr + m_color.pitch * m_color.height * colorElementBytes(colorFormat(m_color.format)) - 1)) {
                m_stats.skipBadPtr++;
                return;
            }
        }
        m_lastDrawColor = m_color;
        auto *drawDepth = m_depthTest ? depthBacking(m_depth, false) : nullptr;
        // Software raster and copied-back native results write drawDepth->pixels during this call.
        struct DepthWrites {
                DepthBacking *backing;
                ~DepthWrites()
                {
                    if (backing)
                        backing->changed();
                }
        } depthWrites{drawDepth && m_depthWrite ? drawDepth : nullptr};

        const auto targetFormat = colorFormat(m_color.format);
        const auto targetElementBytes = colorElementBytes(targetFormat);
        // 128-bit integer targets keep their exported bits in guest memory: MK8 encodes the BC3
        // blocks of its reflection cube map into R32G32B32A32_UINT views of that texture, which
        // the RGBA8 backing cannot hold. The compressed view then decodes them from memory.
        const bool rawTarget = (m_color.format & 0x3F) == 0x22 && (m_color.format & 0x100) && m_mem && m_color.imagePtr;
        // Cube faces and array layers are separate slices of the bound view.
        const auto rawSurface = rawTarget ? attachmentSurface(m_color) : Surface{};
        const std::uint32_t mode = cmd.gpr[0];
        const std::uint32_t count = std::min<std::uint32_t>(cmd.gpr[1], 0x10000);
        const Attrib &ab = m_attribs[0];
        if (m_fetchLayout.empty() && (!ab.addr || ab.stride < 8)) {
            m_stats.skipNoAttrib++;
            return;
        }

        // Gather indices.
        std::vector<std::uint32_t> idx;
        idx.reserve(count);
        if (indexed) {
            const std::uint32_t type = cmd.gpr[2] & 0xF;
            if (type != 0 && type != 1 && type != 4 && type != 9)
                return;
            const std::uint32_t indexBytes = type == 9 || type == 1 ? 4 : 2;
            // Whole buffers in one mapped range decode without per-index lookups.
            const std::uint8_t *indices = std::uint64_t(cmd.gpr[3]) + std::uint64_t(count) * indexBytes <= 0x100000000ull
                                                  ? m_mem->hostPtr(cmd.gpr[3], std::size_t(count) * indexBytes)
                                                  : nullptr;
            for (std::uint32_t i = 0; indices && i < count; i++) {
                const auto *p = indices + std::size_t(i) * indexBytes;
                const auto value = type == 1 ? std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24
                                   : type == 9 ? std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]
                                   : type == 0 ? std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8
                                               : std::uint32_t(p[0]) << 8 | p[1];
                idx.push_back(value + cmd.gpr[4]); // base vertex
            }
            for (std::uint32_t i = 0; !indices && i < count; i++) {
                const std::uint64_t address = std::uint64_t(cmd.gpr[3]) + std::uint64_t(i) * indexBytes;
                if (address + indexBytes > 0x100000000ull)
                    return;
                const std::uint8_t *p = m_mem->hostPtr(static_cast<std::uint32_t>(address));
                if (!p || !m_mem->hostPtr(static_cast<std::uint32_t>(address + indexBytes - 1)))
                    return;
                if (type == 1)
                    idx.push_back(std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24);
                else if (type == 9)
                    idx.push_back(std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]);
                else if (type == 0)
                    idx.push_back(std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8);
                else
                    idx.push_back(std::uint32_t(p[0]) << 8 | p[1]);
                idx.back() += cmd.gpr[4]; // base vertex
            }
        } else {
            for (std::uint32_t i = 0; i < count; i++)
                idx.push_back(cmd.gpr[2] + i); // DrawEx: gpr[2] = first vertex offset
        }

        const auto attributeFetch = prepareAttributeFetch();
        // Fetch one vertex: position from floats[0..1], uv heuristics by stride.
        const std::uint32_t nFloats = ab.stride / 4;
        auto fetch = [&](std::uint32_t vi, float &x, float &y, float &u, float &v) -> bool {
            std::array<std::array<float, 4>, 4> attributes{};
            if (fetchAttributes(attributeFetch, vi, attributes)) {
                x = attributes[0][0];
                y = attributes[0][1];
                u = attributes[1][0];
                v = attributes[1][1];
                return true;
            }
            if (!m_fetchLayout.empty())
                return false;
            const std::uint8_t *p = m_mem->hostPtr(ab.addr + vi * ab.stride);
            if (!p || !m_mem->hostPtr(ab.addr + vi * ab.stride + ab.stride - 1))
                return false;
            auto f = [&](std::uint32_t i) {
                return asFloat(static_cast<std::uint32_t>(p[std::size_t(i) * 4]) << 24 | p[std::size_t(i) * 4 + 1] << 16 |
                               p[std::size_t(i) * 4 + 2] << 8 | p[std::size_t(i) * 4 + 3]);
            };
            x = f(0);
            y = f(1);
            if (nFloats >= 8) { // pos4 + aux2 + uv2 (MK8 composite verts)
                u = f(6);
                v = f(7);
            } else if (nFloats >= 4) {
                u = f(2);
                v = f(3);
            } else {
                u = x;
                v = y;
            }
            return true;
        };

        bool nativeVertexExecution = false, forceVertexReference = false;
        // Repeated indices reuse the same shader result within a draw. Bound the
        // cache independently of guest index values. Texture-reading shaders stay
        // uncached because software rasterization can change an aliased resource
        // between primitives. Each new draw starts with fresh uniforms/attributes.
        struct CachedVertex {
                std::uint32_t index{};
                bool valid{};
                LatteVsInterp::Output output;
        };
        std::vector<CachedVertex> vertexCache;
        const auto vertexDeclaration = LatteVsInterp::compile(m_vsProgram);
        bool vertexShaderReadsTextures = false;
        {
            bool &readsTextures = vertexShaderReadsTextures;
            for (std::size_t cf = 0; cf + 1 < m_vsProgram.size(); cf += 2) {
                const auto word = m_vsProgram[cf + 1];
                if (!Latte::isAluClause(word) &&
                    (Latte::cfInst(word) == Latte::CF_TEX || Latte::cfInst(word) == Latte::CF_VTX || Latte::cfInst(word) == Latte::CF_VTX_TC)) {
                    const auto slots = (((word >> 10) & 7) | ((word >> 16) & 8)) + 1;
                    for (unsigned slot = 0; slot < slots; ++slot) {
                        const auto offset = std::uint64_t(m_vsProgram[cf]) * 2 + std::uint64_t(slot) * 4;
                        if (offset + 3 >= m_vsProgram.size() ||
                            !Latte::decodeUniformBufferFetch(m_vsProgram[offset], m_vsProgram[offset + 1], m_vsProgram[offset + 2]))
                            readsTextures = true;
                    }
                }
                if (!Latte::isAluClause(word) && (word & (1u << 21)))
                    break;
            }
        }
        // Allocated on the first CPU shading: 128 outputs are ~77 KB, wasted on GPU vertex draws.
        const bool vertexCacheWanted = indexed && count >= 6 && m_vertexCacheEnabled && vertexDeclaration->valid && !vertexShaderReadsTextures;
        // Outputs of the pre-shading pass (filled below, once the draw's vertex path is known),
        // indexed by vertex index - preshadedBase. preshadedReady marks computed entries.
        std::vector<LatteVsInterp::Output> preshaded;
        std::vector<std::uint8_t> preshadedReady;
        std::uint32_t preshadedBase = 0;
        // Execute the real vertex shader for one vertex; returns false if unavailable so the
        // caller falls back to the [0,1]/matrix heuristics. PARAM exports are optionally returned
        // because the pixel shader consumes them as interpolated inputs.
        auto runShader = [&](std::uint32_t vi, float &sx, float &sy, float &u, float &v, std::array<std::array<float, 4>, 32> *params = nullptr,
                             std::array<bool, 32> *paramValid = nullptr, float *z = nullptr, float *reciprocalW = nullptr,
                             std::optional<std::array<float, 4>> *clipPosition = nullptr) -> bool {
            if (clipPosition)
                clipPosition->reset();
            // Real vertex-shader execution. ON by default: combined with the UI-composite fallback
            // it produces the placed, clean title screen (logo + background, no stray squares). Set
            // WEMU_VS_EXEC=0 to fall back to the [0,1] heuristic (the old fullscreen-layer look).
            static const bool enabled = []() {
                const char *e = std::getenv("WEMU_VS_EXEC");
                return !e || e[0] != '0';
            }();
            if (!enabled || m_vsProgram.empty())
                return false;
            const LatteVsInterp::Output *preshadedOutput =
                    vi - preshadedBase < preshaded.size() && preshadedReady[vi - preshadedBase] ? &preshaded[vi - preshadedBase] : nullptr;
            std::array<std::array<float, 4>, 32> attribs{};
            if (preshadedOutput) {
                // Attributes were consumed by the pre-shading pass.
            } else if (!m_fetchLayout.empty()) {
                if (!fetchAttributes(attributeFetch, vi, std::span(attribs).first(m_vertexInputCount)))
                    return false;
            } else {
                const std::uint8_t *p = m_mem->hostPtr(ab.addr + vi * ab.stride);
                if (!p || !m_mem->hostPtr(ab.addr + vi * ab.stride + ab.stride - 1))
                    return false;
                auto f = [&](std::uint32_t i) {
                    return asFloat(static_cast<std::uint32_t>(p[std::size_t(i) * 4]) << 24 | p[std::size_t(i) * 4 + 1] << 16 |
                                   p[std::size_t(i) * 4 + 2] << 8 | p[std::size_t(i) * 4 + 3]);
                };
                attribs[0] = {f(0), f(1), nFloats >= 3 ? f(2) : 0.0f, nFloats >= 4 ? f(3) : 1.0f};
                if (nFloats >= 8)
                    attribs[1] = {f(4), f(5), f(6), f(7)};
                else
                    attribs[1] = {f(0), f(1), 0.0f, 1.0f};
            }
            if (nativeVertexExecution && !forceVertexReference) {
                if (params)
                    std::copy_n(attribs.begin(), std::min<std::size_t>(m_vertexInputCount, 16), params->begin());
                sx = sy = 0.f;
                u = v = 0.f;
                if (z)
                    *z = 0.f;
                if (reciprocalW)
                    *reciprocalW = 1.f;
                return true;
            }
            const auto kcache = [&](std::uint32_t bank, std::uint32_t vec4Idx, std::uint32_t chan) -> float {
                // CF KCACHE banks select the corresponding GX2 uniform-block
                // location directly. Keep the explicit diagnostic offset opt-in.
                static const std::uint32_t bias = []() {
                    const char *e = std::getenv("WEMU_KCACHE_BIAS");
                    return e ? static_cast<std::uint32_t>(std::atoi(e)) : 0u;
                }();
                bank += bias;
                if (bank >= m_vsBlocks.size())
                    return 0.0f;
                const auto &blk = m_vsBlocks[bank];
                const std::uint64_t idx = std::uint64_t(vec4Idx) * 4 + chan;
                return idx < blk.size() ? asFloat(blk[idx]) : 0.0f;
            };
            // WEMU_VS_DEBUG_DRAW=N traces the shader for draw N's first vertex only, and reports
            // whether this draw got fresh uniform registers — that distinguishes "export reads
            // stale/uninitialised constants" from "the export never runs".
            static const int dbgDraw = []() {
                const char *e = std::getenv("WEMU_VS_DEBUG_DRAW");
                return e ? std::atoi(e) : -1;
            }();
            // WEMU_VS_SUMMARY=1: one line per draw (no ALU dump) — which shader ran, what it
            // exported, and the two candidate transforms. Lets all 36 draws be compared in one run
            // instead of guessing which ones are nw::lyt panes.
            static const bool summary = []() {
                const char *e = std::getenv("WEMU_VS_SUMMARY");
                return e && e[0] == '1';
            }();
            // WEMU_VS_DEBUG_TEX=0xADDR traces the shader for the first vertex of any draw binding
            // that texture — draw numbers drift between runs, but the logo texture address is
            // stable, so this reliably catches the exact draw we care about.
            static const std::uint32_t dbgTex = []() -> std::uint32_t {
                const char *e = std::getenv("WEMU_VS_DEBUG_TEX");
                return e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 16)) : 0;
            }();
            static const bool debugTraceOnly = []() {
                const char *e = std::getenv("WEMU_VS_DEBUG_TRACE_ONLY");
                return e && e[0] == '1';
            }();
            const bool dbg = ((dbgDraw >= 0 && static_cast<std::uint32_t>(dbgDraw) == m_stats.draws) || (dbgTex && m_texture.imagePtr == dbgTex)) &&
                             !idx.empty() && vi == idx.front() && (!debugTraceOnly || m_trace);
            const bool sum = summary && !idx.empty() && vi == idx.front() && (!debugTraceOnly || m_trace);
            if (dbg) {
                std::fprintf(stderr, "[VSDBG] draw#%u regsFresh=%d prog=%zudw stride=%u attrib0=(%g,%g,%g,%g)\n", m_stats.draws, m_regsFresh ? 1 : 0,
                             m_vsProgram.size(), ab.stride, attribs[0][0], attribs[0][1], attribs[0][2], attribs[0][3]);
                for (int c = 0; c < 20; c++)
                    std::fprintf(stderr, "[VSDBG]   c%-2d %g %g %g %g\n", c, m_vsRegs[std::size_t(c) * 4], m_vsRegs[std::size_t(c) * 4 + 1],
                                 m_vsRegs[std::size_t(c) * 4 + 2], m_vsRegs[std::size_t(c) * 4 + 3]);
                LatteVsInterp::setDebugOnce(true);
            }
            const auto vertexSample = [&](std::uint32_t resource, std::uint32_t sampler, const std::array<float, 4> &coords,
                                          std::array<float, 4> &rgba) {
                if (resource >= m_vertexTextures.size() || sampler >= m_vertexSamplers.size())
                    return false;
                const auto &surface = m_vertexTextures[resource];
                rgba = sampleTexture(surface, m_vertexSamplers[sampler], coords[0], coords[1], coords[2]);
                return true;
            };
            if (vertexCacheWanted && vertexCache.empty())
                vertexCache.resize(128);
            auto *cached = vertexCache.empty() ? nullptr : &vertexCache[vi % vertexCache.size()];
            LatteVsInterp::Output o;
            if (preshadedOutput) {
                o = *preshadedOutput;
            } else if (cached && cached->valid && cached->index == vi) {
                o = cached->output;
                ++m_vertexShaderCacheHits;
            } else {
                o = LatteVsInterp::runVertex(*vertexDeclaration, std::span(attribs).first(m_vertexInputCount), m_vsRegs, 64, kcache, vertexSample);
                ++m_vertexShaderExecutions;
                if (cached) {
                    cached->index = vi;
                    cached->valid = o.valid;
                    cached->output = o;
                }
            }
            if (params)
                *params = o.params;
            if (paramValid)
                *paramValid = o.paramValid;
            if (dbg) {
                LatteVsInterp::setDebugOnce(false);
                std::fprintf(stderr, "[VSDBG] draw#%u -> valid=%d pos=(%g,%g,%g,%g)\n", m_stats.draws, o.valid ? 1 : 0, o.pos[0], o.pos[1], o.pos[2],
                             o.pos[3]);
            }
            if (sum && m_trace) {
                if (const auto *directory = std::getenv("WEMU_FB_DUMP_DIR")) {
                    const auto path = std::format("{}/vs_inputs_draw{:03}.txt", directory, m_stats.draws);
                    if (FILE *file = std::fopen(path.c_str(), "w")) {
                        std::array<std::array<float, 4>, 32> first{};
                        const bool fetched = fetchAttributes(0, std::span(first).first(m_vertexInputCount));
                        std::fprintf(file, "%u %u %u\n", vi, m_vertexInputCount, unsigned(fetched));
                        for (const auto &attribute: first)
                            for (const auto value: attribute)
                                std::fprintf(file, "%08X ", std::bit_cast<std::uint32_t>(value));
                        for (const auto value: m_vsRegs)
                            std::fprintf(file, "%08X ", std::bit_cast<std::uint32_t>(value));
                        for (const auto &block: m_vsBlocks) {
                            std::fprintf(file, "\n%zu\n", block.size());
                            for (const auto word: block)
                                std::fprintf(file, "%08X ", word);
                        }
                        std::fclose(file);
                    }
                }
            }
            if (sum) {
                const std::uint32_t h = shaderHash(m_vsProgram);
                std::fprintf(stderr,
                             "[VSSUM] draw#%02u tgt=0x%08X(%ux%u) prog=%zudw hash=%08X stride=%u tex=0x%08X %ux%u | pos=(%g,%g,%g,%g)"
                             " valid=%d p0=(%g,%g,%g,%g)%c p1=(%g,%g,%g,%g)%c | c4=(%g,%g,%g,%g) c5=(%g,%g,%g,%g) c7=(%g,%g,%g,%g) c8=(%g,%g,%g,%g)"
                             " c9=(%g,%g,%g,%g) c15=(%g,%g,%g,%g) c16=(%g,%g,%g,%g)\n",
                             m_stats.draws, m_color.imagePtr, m_color.width, m_color.height, m_vsProgram.size(), h, ab.stride, m_texture.imagePtr,
                             m_texture.width, m_texture.height, o.pos[0], o.pos[1], o.pos[2], o.pos[3], o.valid ? 1 : 0, o.params[0][0],
                             o.params[0][1], o.params[0][2], o.params[0][3], o.paramValid[0] ? 'v' : '-', o.params[1][0], o.params[1][1],
                             o.params[1][2], o.params[1][3], o.paramValid[1] ? 'v' : '-', m_vsRegs[16], m_vsRegs[17], m_vsRegs[18], m_vsRegs[19],
                             m_vsRegs[20], m_vsRegs[21], m_vsRegs[22], m_vsRegs[23], m_vsRegs[28], m_vsRegs[29], m_vsRegs[30], m_vsRegs[31],
                             m_vsRegs[32], m_vsRegs[33], m_vsRegs[34], m_vsRegs[35], m_vsRegs[36], m_vsRegs[37], m_vsRegs[38], m_vsRegs[39],
                             m_vsRegs[60], m_vsRegs[61], m_vsRegs[62], m_vsRegs[63], m_vsRegs[64], m_vsRegs[65], m_vsRegs[66], m_vsRegs[67]);
            }
            if (!o.valid)
                return false;
            // Experimental generic fallback for 2D UI shaders whose model transform is valid but
            // whose projection X/Y rows are all zero in the captured uniform file. In that state the
            // shader exports (0,0,z,w), so the usual perspective divide collapses the pane to the
            // screen centre. Some MK8 nw::lyt draws still carry a plausible local rect in c15 and
            // affine model rows in c4/c5; mapping that intermediate model-space position around the
            // viewport centre recovers the pane placement without patching game code. Default-off
            // until more titles/shaders confirm the convention.
            static const bool degenModelFallback = []() {
                const char *e = std::getenv("WEMU_VS_DEGEN_MODEL_FALLBACK");
                return e && e[0] == '1';
            }();
            const bool collapsed = o.pos[0] == 0.0f && o.pos[1] == 0.0f;
            if (degenModelFallback && collapsed && ab.stride == 8) {
                const bool projectionXyMissing = m_vsRegs[32] == 0.0f && m_vsRegs[33] == 0.0f && m_vsRegs[34] == 0.0f && m_vsRegs[35] == 0.0f &&
                                                 m_vsRegs[36] == 0.0f && m_vsRegs[37] == 0.0f && m_vsRegs[42] < -0.9f && m_vsRegs[42] > -1.1f &&
                                                 m_vsRegs[43] > 100.0f;
                const float *rowX = &m_vsRegs[16]; // c4
                const float *rowY = &m_vsRegs[20]; // c5
                const float *rect = &m_vsRegs[60]; // c15: local scale/offset in observed UI shaders
                const bool plausibleRect = std::isfinite(rect[0]) && std::isfinite(rect[1]) && std::isfinite(rect[2]) && std::isfinite(rect[3]) &&
                                           std::fabs(rect[0]) > 2.0f && std::fabs(rect[1]) > 2.0f && std::fabs(rect[0]) < 4096.0f &&
                                           std::fabs(rect[1]) < 4096.0f;
                if (projectionXyMissing && plausibleRect) {
                    const float lx = attribs[0][0] * rect[0] + rect[2];
                    const float ly = attribs[0][1] * rect[1] + rect[3];
                    const float lz = attribs[0][2];
                    const float mx = rowX[0] * lx + rowX[1] * ly + rowX[2] * lz + rowX[3];
                    const float my = rowY[0] * lx + rowY[1] * ly + rowY[2] * lz + rowY[3];
                    const float cx = static_cast<float>(kWidth) * 0.5f;
                    const float cy = static_cast<float>(kHeight) * 0.5f;
                    const float tsx = cx + mx;
                    const float tsy = cy - my;
                    if (std::isfinite(tsx) && std::isfinite(tsy) && tsx > -static_cast<float>(kWidth) * 2.0f &&
                        tsx < static_cast<float>(kWidth) * 3.0f && tsy > -static_cast<float>(kHeight) * 2.0f &&
                        tsy < static_cast<float>(kHeight) * 3.0f) {
                        sx = tsx;
                        sy = tsy;
                        u = attribs[0][0];
                        v = attribs[0][1];
                        return true;
                    }
                }
            }
            // UI-composite fallback. The stride-8 unit-quad passes share MK8's nw::lyt / agl
            // "MultiFilter" framework (verified by reading the .rodata uniform names: the matrix at
            // 0x520C1D84 belongs to "MultiFilter - Copy/Blur2/Blur4", ui/cmn/ui_{copy,blur2,blur4}
            // .baglmf), whose transform is uploaded with zero X/Y rows in this boot, so the shader
            // collapses every such quad to a point. Two kinds of pass collapse here:
            //   * large content — the logo (1280x420) and the full-screen background: centring the
            //     unit quad at its native texture size reproduces its real on-screen placement well.
            //   * small blur/copy intermediates — the bloom filter buffers. We don't reproduce the
            //     multi-pass gaussian blur, so centring these just stamps a hard square over the UI.
            // So we centre only the large placeable content and drop the small filter intermediates
            // (a degenerate zero-area quad the rasteriser skips) rather than mis-placing them. The
            // whole branch disappears the moment a draw yields a real position.
            if (mode != 1 && collapsed && ab.stride == 8) {
                const float cx = static_cast<float>(kWidth) * 0.5f, cy = static_cast<float>(kHeight) * 0.5f;
                // Threshold: content whose texture is at least half the screen wide is real UI
                // (logo/background); anything smaller is a filter intermediate we drop.
                if (m_texture.imagePtr && m_texture.width >= kWidth / 2) {
                    sx = cx + (attribs[0][0] - 0.5f) * static_cast<float>(m_texture.width);
                    sy = cy + (attribs[0][1] - 0.5f) * static_cast<float>(m_texture.height);
                    u = attribs[0][0];
                    v = attribs[0][1];
                } else {
                    sx = cx; // collapse to a point -> zero area -> dropped by the rasteriser
                    sy = cy;
                    u = 0.0f;
                    v = 0.0f;
                }
                return true;
            }
            // Other degenerate-projection draws (the fullscreen post-process passes): let the
            // caller fall back to its heuristic rather than smear them across the frame.
            if (mode != 1 && collapsed && m_vsRegs[32] == 0.0f && m_vsRegs[33] == 0.0f && m_vsRegs[34] == 0.0f && m_vsRegs[35] == 0.0f &&
                m_vsRegs[36] == 0.0f && m_vsRegs[37] == 0.0f)
                return false;
            const float w = std::fabs(o.pos[3]) > 1e-6f ? o.pos[3] : 1.0f;
            if (clipPosition)
                *clipPosition = o.pos;
            sx = m_vp[0] + (o.pos[0] / w * 0.5f + 0.5f) * m_vp[2];
            sy = m_vp[1] + (0.5f - o.pos[1] / w * 0.5f) * m_vp[3]; // NDC y-up -> raster y-down
            if (z)
                *z = (m_vpFar + m_vpNear) * .5f + (o.pos[2] / w) * (m_vpFar - m_vpNear) * .5f;
            if (reciprocalW)
                *reciprocalW = 1.f / w;
            const auto &uv = o.params[textureParam];
            u = textureSelectX < 4 ? uv[textureSelectX] : textureSelectX == 5 ? 1.0f : 0.0f;
            v = textureSelectY < 4 ? uv[textureSelectY] : textureSelectY == 5 ? 1.0f : 0.0f;
            return true;
        };

        // Fallback position mapping for draws whose vertex shader cannot yet be interpreted. Many
        // simple UI/post-process draws upload a row-major clip matrix in c0..c3 immediately before
        // drawing. If those rows are fresh and plausible, use them; otherwise retain the older
        // [0,1] screen-space heuristic.
        auto mapUniformMatrix = [&](const float x, const float y, float &sx, float &sy) -> bool {
            static const bool enabled = []() {
                const char *e = std::getenv("WEMU_VS_MATRIX_FALLBACK");
                return !e || e[0] != '0';
            }();
            if (!enabled || !m_regsFresh)
                return false;
            const float *M = m_vsRegs;
            const bool hasX = std::fabs(M[0]) > 1e-6f || std::fabs(M[1]) > 1e-6f || std::fabs(M[3]) > 1e-6f;
            const bool hasY = std::fabs(M[4]) > 1e-6f || std::fabs(M[5]) > 1e-6f || std::fabs(M[7]) > 1e-6f;
            if (!hasX || !hasY)
                return false;
            const float cx = M[0] * x + M[1] * y + M[3];
            const float cy = M[4] * x + M[5] * y + M[7];
            const float cw = M[12] * x + M[13] * y + M[15];
            if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cw) || std::fabs(cw) < 1e-6f)
                return false;
            const float nx = cx / cw;
            const float ny = cy / cw;
            if (!std::isfinite(nx) || !std::isfinite(ny) || std::fabs(nx) > 100.0f || std::fabs(ny) > 100.0f)
                return false;
            sx = m_vp[0] + (nx * 0.5f + 0.5f) * m_vp[2];
            sy = m_vp[1] + (0.5f - ny * 0.5f) * m_vp[3];
            return true;
        };
        const auto mapVertex = [&](float x, float y, float &sx, float &sy) {
            if (mapUniformMatrix(x, y, sx, sy))
                return;
            sx = m_vp[0] + x * m_vp[2];
            sy = m_vp[1] + y * m_vp[3];
        };

        struct RasterVertex {
                float x{0.0f};
                float y{0.0f};
                float u{0.0f};
                float v{0.0f};
                std::array<std::array<float, 4>, 32> params{
                        {{1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}};
                std::array<bool, 32> paramValid{};
                float z{}, reciprocalW{1.f};
                unsigned vertexIndex{};
                std::optional<std::array<float, 4>> clipPosition;
        };

        unsigned interpolatedParamCount = textureParam + 1;
        for (unsigned i = 0; i < m_pixelInputCount; ++i) {
            const auto semantic = m_pixelInputSemantics[i];
            const auto it = std::find(m_vertexOutputSemantics.begin(), m_vertexOutputSemantics.end(), semantic);
            if (semantic != 255 && it != m_vertexOutputSemantics.end())
                interpolatedParamCount = std::max(interpolatedParamCount, unsigned(std::distance(m_vertexOutputSemantics.begin(), it)) + 1);
        }
        auto interpolateParams = [&](const RasterVertex(&rv)[3], const float w0, const float w1, const float w2) {
            const bool affine = rv[0].reciprocalW == rv[1].reciprocalW && rv[1].reciprocalW == rv[2].reciprocalW;
            const float total = affine ? 1.f : w0 * rv[0].reciprocalW + w1 * rv[1].reciprocalW + w2 * rv[2].reciprocalW;
            const float p0 = affine ? w0 : w0 * rv[0].reciprocalW / total;
            const float p1 = affine ? w1 : w1 * rv[1].reciprocalW / total;
            const float p2 = affine ? w2 : w2 * rv[2].reciprocalW / total;
            std::array<std::array<float, 4>, 32> out{};
            for (unsigned p = 0; p < interpolatedParamCount; p++)
                for (int c = 0; c < 4; c++)
                    out[p][c] = interpolateValue(rv[0].params[p][c], rv[1].params[p][c], rv[2].params[p][c], p0, p1, p2);
            return out;
        };

        if (m_trace) {
            float tx0 = 0, ty0 = 0, tu0 = 0, tv0 = 0, tx1 = 0, ty1 = 0, tu1 = 0, tv1 = 0;
            if (!idx.empty())
                fetch(idx[0], tx0, ty0, tu0, tv0);
            if (idx.size() > 1)
                fetch(idx[1], tx1, ty1, tu1, tv1);
            const auto mid = sample(m_texture, 0.5f, 0.5f);
            float s0x = 0, s0y = 0;
            if (!idx.empty()) {
                float su = 0, sv = 0;
                if (runShader(idx[0], s0x, s0y, su, sv))
                    Utils::Log::error("[GX2TRACE]   vs#{} out v0 screen=({:.1f},{:.1f}) uv=({:.3f},{:.3f}) prog={}dw", m_stats.draws, s0x, s0y, su,
                                      sv, m_vsProgram.size());
                else
                    mapVertex(tx0, ty0, s0x, s0y);
                // Snapshot the uniform register file per draw for offline interpreter debugging.
                const char *dir = std::getenv("WEMU_FB_DUMP_DIR");
                char path[512];
                std::snprintf(path, sizeof(path), "%s/regs_draw%02u.txt", dir ? dir : ".", m_stats.draws);
                if (FILE *fp = std::fopen(path, "w")) {
                    for (int i = 0; i < 256; i += 4)
                        std::fprintf(fp, "c%-3d %g %g %g %g\n", i / 4, m_vsRegs[i], m_vsRegs[i + 1], m_vsRegs[i + 2], m_vsRegs[i + 3]);
                    std::fclose(fp);
                }
                // Uniform block snapshots (kcache source in uniform-block mode).
                std::snprintf(path, sizeof(path), "%s/block_draw%02u.txt", dir ? dir : ".", m_stats.draws);
                if (FILE *fp = std::fopen(path, "w")) {
                    for (std::size_t b = 0; b < m_vsBlocks.size(); b++) {
                        if (m_vsBlocks[b].empty())
                            continue;
                        std::fprintf(fp, "# block %zu (%zu words)\n", b, m_vsBlocks[b].size());
                        for (std::size_t i = 0; i + 3 < m_vsBlocks[b].size() && i < std::size_t{160} * 4; i += 4)
                            std::fprintf(fp, "b%zu.%-3zu %g %g %g %g\n", b, i / 4, asFloat(m_vsBlocks[b][i]), asFloat(m_vsBlocks[b][i + 1]),
                                         asFloat(m_vsBlocks[b][i + 2]), asFloat(m_vsBlocks[b][i + 3]));
                    }
                    std::fclose(fp);
                }
            }
            Utils::Log::error("[GX2TRACE] draw#{} mode={} cnt={} stride={} tex=0x{:08X}(f{:02X},t{},{}x{}) tgt=0x{:08X}({}x{}) "
                              "v0=({:.2f},{:.2f})uv({:.2f},{:.2f})->({:.0f},{:.0f}) midTexel=({},{},{},{})",
                              m_stats.draws, mode, count, ab.stride, m_texture.imagePtr, m_texture.format & 0x3F, m_texture.tileMode, m_texture.width,
                              m_texture.height, m_color.imagePtr, m_color.width, m_color.height, tx0, ty0, tu0, tv0, s0x, s0y, mid[0], mid[1], mid[2],
                              mid[3]);
            if (!m_psProgram.empty()) {
                std::size_t psBlockWords = 0;
                for (const auto &blk: m_psBlocks)
                    psBlockWords += blk.size();
                Utils::Log::error("[PSSUM] draw#{} psProg={}dw hash={:08X} psRegsFresh={} p0=({:.4g},{:.4g},{:.4g},{:.4g})"
                                  " p1=({:.4g},{:.4g},{:.4g},{:.4g}) psBlockWords={}",
                                  m_stats.draws, m_psProgram.size(), shaderHash(m_psProgram), m_psRegsFresh ? 1 : 0, m_psRegs[0], m_psRegs[1],
                                  m_psRegs[2], m_psRegs[3], m_psRegs[4], m_psRegs[5], m_psRegs[6], m_psRegs[7], psBlockWords);
            }
            // Dump the raw per-vertex positions: this reveals whether menu panes carry real screen
            // rectangles in their vertex data (positioned UI) or are all unit quads (position must
            // come from a uniform we don't yet read).
            if (ab.stride == 8) {
                std::string verts;
                for (std::uint32_t i = 0; i < std::min<std::size_t>(idx.size(), 6); i++) {
                    float vx = 0, vy = 0, vu = 0, vv = 0;
                    if (fetch(idx[i], vx, vy, vu, vv))
                        verts += std::format(" ({:.3g},{:.3g})", vx, vy);
                }
                Utils::Log::error("[GX2TRACE]   draw#{} verts:{}", m_stats.draws, verts);
            }
        }

        static const bool residentTexturesEnabled = [] {
            const auto *value = std::getenv("WEMU_NATIVE_RESIDENT_TEXTURES");
            return value && std::string_view(value) == "1";
        }();
        const bool residentTextures = residentTexturesEnabled && m_rasterBackend && m_rasterBackend->supportsRenderedTextures();
        const auto textureBacking = [&](const Surface &surface) -> const std::vector<std::uint8_t> * {
            if (!residentTextures)
                return findViewBacking(surface);
            const auto backing = m_viewBackings.find(makeSurfaceKey(surface));
            return backing == m_viewBackings.end() ? nullptr : &backing->second;
        };
        // Fast path for the dominant case: linear (or CPU-written) RGBA8 texture — sample without
        // per-pixel hostPtr lookups. Falls back to sample() for tiled/BC surfaces.
        const auto *texBacking = useViewBacking() ? textureBacking(m_texture) : nullptr;
        const std::uint32_t texTile = (!useViewBacking() && m_cpuWritten.contains(m_texture.imagePtr)) ? 0 : m_texture.tileMode;
        const std::uint32_t texFmt = m_texture.format & 0x3F;
        const std::uint8_t *texBase = nullptr;
        std::vector<std::uint8_t> textureSnapshot;
        if (texBacking) {
            texBase = texBacking->data();
        } else if (m_texture.imagePtr && texTile <= 1 && texFmt != 0x01 && texFmt != 0x07 && texFmt != 0x31 && texFmt != 0x32 && texFmt != 0x33 &&
                   texFmt != 0x34 && texFmt != 0x35) {
            texBase = m_mem->hostPtr(m_texture.imagePtr);
            if (texBase && !m_mem->hostPtr(m_texture.imagePtr + (m_texture.pitch * (m_texture.height - 1) + m_texture.width) * 4 - 1))
                texBase = nullptr;
        }
        auto sampleTexel = [&](float uu, float vv) -> std::array<std::uint8_t, 4> {
            if (colorFormat(m_texture.format) != ColorFormat::RGBA8) {
                const auto value = sampleTexture(m_texture, TextureSampler{}, uu, vv);
                return {byteFromUnit(value[0]), byteFromUnit(value[1]), byteFromUnit(value[2]), byteFromUnit(value[3])};
            }
            if (texBase) {
                uu -= std::floor(uu);
                vv -= std::floor(vv);
                const auto tx = std::min(m_texture.width - 1, static_cast<std::uint32_t>(uu * static_cast<float>(m_texture.width)));
                const auto ty = std::min(m_texture.height - 1, static_cast<std::uint32_t>(vv * static_cast<float>(m_texture.height)));
                const std::size_t off = (static_cast<std::size_t>(ty) * m_texture.pitch + tx) * 4;
                if (texBacking && off + 3 >= texBacking->size())
                    return {128, 128, 128, 255};
                const std::uint8_t *p = texBase + off;
                return mapTextureChannels({p[0], p[1], p[2], p[3]}, m_texture.compMap);
            }
            return mapTextureChannels(sample(m_texture, uu, vv), m_texture.compMap);
        };
        static const bool psTexModEnabled = []() {
            const char *e = std::getenv("WEMU_PS_TEXMOD");
            return e && e[0] == '1';
        }();
        const SimplePixelShaderKind psKind = psTexModEnabled ? simplePixelShaderKind(m_psProgram) : SimplePixelShaderKind::None;
        static const bool pixelExecution = [] {
            const char *e = std::getenv("WEMU_PS_EXEC");
            return !e || e[0] != '0';
        }();
        const auto pixelProgram = pixelExecution ? pixelDeclaration : nullptr;
        // A proven unconditional color-only shader has no observable fragment
        // effects when color writes are disabled. Keep geometry/depth execution,
        // but avoid texture reads and shader work whose results cannot be stored.
        const bool skipColorShader = pixelProgram && writableColors == 0 && !m_multiWriteEnable &&
                                     (!m_colorWriteEnable || !m_channelMask[m_colorTarget & 7]) &&
                                     m_fragmentShaderCache.hasUnconditionalColorOnlyEffects(m_psProgram);
        const bool pixelAlwaysRejected = pixelProgram && !skipColorShader && LatteVsInterp::rejectsEveryPixel(*pixelProgram);
        // Rejected coverage has no shader side effects in the translated subset.
        // Depth exports must execute before comparison; unsupported shaders retain
        // the late test until their effects are understood.
        const auto depthShader = drawDepth && pixelProgram && m_depthCompare != 7 ? colorShader : nullptr;
        const bool earlyDepth = drawDepth && m_depthCompare != 7 &&
                                (skipColorShader || (depthShader && *depthShader && !depthShader->writesDepth) ||
                                 (pixelProgram && m_fragmentShaderCache.hasUnconditionalColorOnlyEffects(m_psProgram)));
        std::optional<std::array<float, 2>> depthBounds;
        const auto rejectConstantDepth = [&](float incoming) {
            if (!earlyDepth)
                return false;
            if (!depthBounds) {
                std::array bounds{std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
                for (unsigned y = 0; y < m_depth.height; ++y)
                    for (unsigned x = 0; x < m_depth.width; ++x) {
                        const auto stored = drawDepth->pixels[std::size_t(y) * m_depth.pitch + x];
                        // Float targets can contain NaNs. Disable this optimization
                        // when there is no ordered bound; per-pixel testing remains exact.
                        if (std::isnan(stored)) {
                            bounds = {-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
                            break;
                        }
                        bounds[0] = std::min(bounds[0], stored);
                        bounds[1] = std::max(bounds[1], stored);
                    }
                depthBounds = bounds;
            }
            incoming = clampDepth(incoming);
            const auto [minimum, maximum] = *depthBounds;
            switch (m_depthCompare) {
                case 0:
                    return true;
                case 1:
                    return incoming >= maximum;
                case 2:
                    return incoming < minimum || incoming > maximum;
                case 3:
                    return incoming > maximum;
                case 4:
                    return incoming <= minimum;
                case 5:
                    return minimum == maximum && incoming == minimum;
                case 6:
                    return incoming < minimum;
                default:
                    return false;
            }
        };
        const auto writeDepth = [&](float &stored, float incoming) {
            stored = quantizeDepth(incoming, drawDepth->format);
            if (depthBounds) {
                // Never narrow within a draw. Overwritten extrema can only make
                // rejection more conservative; every new write expands the bound.
                (*depthBounds)[0] = std::min((*depthBounds)[0], stored);
                (*depthBounds)[1] = std::max((*depthBounds)[1], stored);
            }
        };
        std::array<int, 32> pixelInputParams;
        pixelInputParams.fill(-1);
        for (unsigned i = 0; i < m_pixelInputCount; i++) {
            const auto semantic = m_pixelInputSemantics[i];
            const auto it = std::find(m_vertexOutputSemantics.begin(), m_vertexOutputSemantics.end(), semantic);
            if (semantic != 255 && it != m_vertexOutputSemantics.end())
                pixelInputParams[i] = static_cast<int>(std::distance(m_vertexOutputSemantics.begin(), it));
        }
        // CPU feedback snapshots are created only if software rasterization runs.
        // Native inputs are consumed before committing output, or use GPU versions.
        std::array<std::vector<std::uint8_t>, 16> pixelFeedback;
        std::array<std::vector<float>, 16> pixelDepthFeedback;
        std::array<const std::vector<std::uint8_t> *, 16> pixelBackings{};
        std::array<bool, 16> sampledResources{}, explicitResources{};
        std::array<std::shared_ptr<MipSnapshot>, 16> pixelMips{};
        if (pixelProgram)
            for (std::size_t cf = 0; cf + 1 < m_psProgram.size(); cf += 2) {
                const auto w0 = m_psProgram[cf], w1 = m_psProgram[cf + 1];
                if (Latte::isAluClause(w1))
                    continue;
                if (Latte::cfInst(w1) == Latte::CF_TEX)
                    for (unsigned slot = 0; slot < (((w1 >> 10) & 7) | ((w1 >> 16) & 8)) + 1; ++slot) {
                        const auto offset = std::uint64_t(w0) * 2 + std::uint64_t(slot) * 4;
                        if (offset + 3 >= m_psProgram.size())
                            break;
                        const auto word = m_psProgram[offset], op = word & 31, resource = (word >> 8) & 255;
                        if (resource < sampledResources.size() && op != 0x0E) {
                            sampledResources[resource] = true;
                            explicitResources[resource] = explicitResources[resource] || op == 0x11;
                        }
                    }
                if (w1 & (1u << 21))
                    break;
            }
        for (unsigned i = 0; pixelProgram && i < m_pixelTextures.size(); i++) {
            if (useViewBacking() && m_pixelTextures[i].dimension != 5)
                pixelBackings[i] = textureBacking(m_pixelTextures[i]);
        }
        bool softwareInputsReady = false;
        std::array<std::vector<float>, 18> pixelVolumes;
        const auto prepareSoftwareInputs = [&] {
            if (softwareInputsReady)
                return;
            if (skipColorShader) {
                resolveViewBacking(m_color);
                if (drawDepth && drawDepth->pending)
                    depthBacking(m_depth);
                softwareInputsReady = true;
                return;
            }
            // Workers must see immutable CPU inputs, but unrelated render targets
            // can remain on the GPU. Array and volume slices are selected dynamically:
            // resolve every target stored inside a bound array, not every target.
            std::vector<Surface> layered;
            if (m_texture.dimension == 5)
                layered.push_back(m_texture);
            for (const auto &surface: m_pixelTextures)
                if (surface.dimension == 5 || surface.dimension == 2)
                    layered.push_back(surface);
            if (!layered.empty())
                resolvePendingBackings(layered);
            resolveViewBacking(m_color);
            resolveViewBacking(m_texture);
            if (pixelProgram)
                for (const auto &surface: m_pixelTextures)
                    resolveViewBacking(surface);
            if (drawDepth && drawDepth->pending)
                depthBacking(m_depth);
            if (drawDepth && m_depthWrite)
                for (unsigned i = 0; i < m_pixelTextures.size(); ++i)
                    if (makeSurfaceKey(m_pixelTextures[i]) == makeSurfaceKey(m_depth))
                        pixelDepthFeedback[i] = drawDepth->pixels;
            if (texBacking && makeSurfaceKey(m_color) == makeSurfaceKey(m_texture)) {
                textureSnapshot = *texBacking;
                texBase = textureSnapshot.data();
            }
            for (unsigned i = 0; pixelProgram && i < m_pixelTextures.size(); ++i) {
                if (m_pixelTextures[i].dimension == 2)
                    pixelVolumes[i] = volumeTexels(m_pixelTextures[i]);
                if (makeSurfaceKey(m_pixelTextures[i]) == makeSurfaceKey(m_color)) {
                    if (const auto *backing = findViewBacking(m_pixelTextures[i])) {
                        pixelFeedback[i] = *backing;
                        if (!pixelFeedback[i].empty())
                            pixelBackings[i] = &pixelFeedback[i];
                    }
                }
                if (sampledResources[i] && (m_pixelTextures[i].dimension == 3 || explicitResources[i]))
                    pixelMips[i] = mipSnapshot(m_pixelTextures[i], explicitResources[i]);
            }
            softwareInputsReady = true;
        };
        const LatteVsInterp::TextureSample pixelSample = [&](unsigned resource, unsigned sampler, const std::array<float, 4> &coords,
                                                             std::array<float, 4> &rgba) {
            if (resource >= m_pixelTextures.size() || sampler >= m_pixelSamplers.size())
                return false;
            const auto &surface = m_pixelTextures[resource];
            if (surface.dimension == 2) {
                const auto &values = pixelVolumes[resource];
                if (values.empty())
                    return false;
                rgba = sampleVolume(m_pixelSamplers[sampler], surface.width, surface.height, surface.depth, coords[0], coords[1], coords[2],
                                    [&](unsigned x, unsigned y, unsigned z) {
                                        const auto offset = ((std::size_t(z) * surface.height + y) * surface.width + x) * 4;
                                        return std::array<float, 4>{values[offset], values[offset + 1], values[offset + 2], values[offset + 3]};
                                    });
                return true;
            }
            rgba = sampleTexture(surface, m_pixelSamplers[sampler], coords[0], coords[1], coords[2], pixelBackings[resource], false,
                                 pixelDepthFeedback[resource].empty() ? nullptr : &pixelDepthFeedback[resource]);
            return true;
        };
        const LatteVsInterp::KcacheFetch pixelConstants = [&](unsigned bank, unsigned index, unsigned channel) {
            if (bank >= m_psBlocks.size())
                return 0.0f;
            const auto &block = m_psBlocks[bank];
            const std::uint64_t offset = std::uint64_t(index) * 4 + channel;
            return offset < block.size() ? asFloat(block[offset]) : 0.0f;
        };
        const LatteVsInterp::TextureGather pixelGather = [&](unsigned resource, unsigned sampler, const std::array<float, 4> &coords,
                                                             std::array<float, 4> &rgba) {
            if (resource >= m_pixelTextures.size() || sampler >= m_pixelSamplers.size() ||
                !supportsGather(m_pixelTextures[resource], m_pixelSamplers[sampler]))
                return false;
            rgba = sampleTexture(m_pixelTextures[resource], m_pixelSamplers[sampler], coords[0], coords[1], 0, pixelBackings[resource], true,
                                 pixelDepthFeedback[resource].empty() ? nullptr : &pixelDepthFeedback[resource]);
            return true;
        };
        const LatteVsInterp::TextureFetch pixelFetch = [&](const LatteVsInterp::TextureRequest &request, std::array<float, 4> &rgba) {
            if (request.resource >= m_pixelTextures.size() || request.sampler >= m_pixelSamplers.size())
                return false;
            const auto &surface = m_pixelTextures[request.resource];
            auto coords = request.coords;
            if (request.offset != std::array<float, 3>{}) {
                // Offsets apply in texels of the sampled level (MK8's BC3 encoder reads 4x4 blocks).
                if (surface.dimension == 3 || surface.dimension == 2 || !surface.width || !surface.height)
                    return false;
                const auto level = request.explicitLod && std::isfinite(coords[3])
                                           ? unsigned(std::clamp(std::floor(coords[3] + .5f), 0.f, float(std::max(1u, surface.mipCount) - 1)))
                                           : 0u;
                coords[0] += request.offset[0] / float(std::max(1u, surface.width >> level));
                coords[1] += request.offset[1] / float(std::max(1u, surface.height >> level));
                if (surface.dimension == 5)
                    coords[2] += request.offset[2];
            }
            if (surface.dimension != 3 && !request.explicitLod)
                return pixelSample(request.resource, request.sampler, coords, rgba);
            const auto &snapshot = pixelMips[request.resource];
            return snapshot && sampleMipSnapshot(*snapshot, m_pixelSamplers[request.sampler], coords, request.cubeIndex, request.explicitLod, rgba);
        };
        bool pixelFallbackReported = false;
        auto shadeTexel = [&](float u, float v, const std::array<std::array<float, 4>, 32> &params, LatteVsInterp::Output *output) {
            if (skipColorShader) {
                output->colorValid = true;
                return std::array<std::uint8_t, 4>{};
            }
            if (pixelProgram && !pixelAlwaysRejected) {
                std::array<std::array<float, 4>, 32> inputs{};
                for (unsigned i = 0; i < m_pixelInputCount; i++)
                    if (pixelInputParams[i] >= 0)
                        inputs[i] = params[pixelInputParams[i]];
                const auto pixel = LatteVsInterp::runPixel(*pixelProgram, std::span(inputs).first(m_pixelInputCount), m_psRegs, 64, pixelSample,
                                                           pixelConstants, pixelGather, pixelFetch, colorExport);
                *output = pixel;
                if (pixel.discarded)
                    return std::array<std::uint8_t, 4>{};
                if (pixel.colorValid)
                    return std::array<std::uint8_t, 4>{byteFromUnit(pixel.color[0]), byteFromUnit(pixel.color[1]), byteFromUnit(pixel.color[2]),
                                                       byteFromUnit(pixel.color[3])};
                if (pixel.depthValid)
                    return std::array<std::uint8_t, 4>{}; // A depth-only export does not write colour.
                if (m_trace && !pixelFallbackReported) {
                    Utils::Log::error("[GX2TRACE] draw#{} unsupported pixel execution; using legacy texture approximation", m_stats.draws);
                    pixelFallbackReported = true;
                }
            }
            // Supported shaders already sampled their own resources.
            const auto texel = sampleTexel(u, v);
            // Default-off bridge toward real pixel-shader execution. The traced MK8 UI shader
            // 8067831F matches:
            //   out = texture * c1 + c0
            // and 6CC2C1DE matches:
            //   out = texture * (PARAM0 * c1 + c0)
            // where c0/c1 are GX2 pixel uniform registers p0/p1. This keeps the approximation
            // local to the confirmed bytecode pattern instead of hard-coding a game address/hash.
            if (psKind == SimplePixelShaderKind::None)
                return texel;
            std::array<std::uint8_t, 4> out{};
            for (int c = 0; c < 4; c++) {
                // Pixel shader GPR1 is the interpolated PARAM0 stream for this bytecode pattern.
                // If the vertex shader did not export it, use neutral white modulation.
                const float t = static_cast<float>(texel[c]) / 255.0f;
                const float v = (psKind == SimplePixelShaderKind::TextureParamRegs) ? (t * (params[0][c] * m_psRegs[4 + c] + m_psRegs[c]))
                                                                                    : (t * m_psRegs[4 + c] + m_psRegs[c]);
                out[c] = byteFromUnit(v);
            }
            return out;
        };

        const bool targetBlendEnabled = ((m_targetBlendEnable >> (m_colorTarget & 7)) & 1) != 0;
        const BlendState &blend = m_blend[m_colorTarget & 7];
        const std::uint32_t channelMask = m_colorWriteEnable ? (m_channelMask[m_colorTarget & 7] & 0xF) : 0;
        // A depth-only pass can retain an unrelated, smaller color binding.
        // Its disabled color attachment must not clip depth coverage.
        const bool colorOutputEnabled = channelMask && !(colorShader && *colorShader && !colorShader->exportsColor);
        if (colorOutputEnabled) {
            m_cpuWritten.insert(m_color.imagePtr);
            auto &written = m_cpuWrittenSurfaces[m_color.imagePtr];
            written.surface = m_color;
            written.frame = m_presentCount + 1;
            ++written.writes;
        }
        // Textures sampled by this draw still see the previous backing version; later ones must not.
        struct BackingWrite {
                const Gx2Replayer *replayer;
                std::optional<SurfaceKey> key;
                ~BackingWrite()
                {
                    if (key)
                        replayer->touchBacking(*key);
                }
        } backingWrite{this, colorOutputEnabled ? std::optional{makeSurfaceKey(m_color)} : std::nullopt};
        const auto rasterWidth = drawDepth && !colorOutputEnabled ? m_depth.width : m_color.width;
        const auto rasterHeight = drawDepth && !colorOutputEnabled ? m_depth.height : m_color.height;
        const int sx0 = std::max(m_scissor[0], 0);
        const int sy0 = std::max(m_scissor[1], 0);
        const int sx1 = std::min<std::int32_t>(m_scissor[0] + m_scissor[2], static_cast<std::int32_t>(rasterWidth));
        const int sy1 = std::min<std::int32_t>(m_scissor[1] + m_scissor[3], static_cast<std::int32_t>(rasterHeight));

        const auto writeRawElement = [&](unsigned x, unsigned y, const LatteVsInterp::Output &pixel) {
            if (!pixel.colorValid || !rawSurface.imagePtr || x >= rawSurface.width || y >= rawSurface.height)
                return;
            const auto offset = tiledElementOffset(x, y, rawSurface.pitch, 128, rawSurface.tileMode, rawSurface.swizzle);
            if (rawSurface.imageSize && std::uint64_t(offset) + 16 > rawSurface.imageSize)
                return;
            auto *element = m_mem->hostPtr(rawSurface.imagePtr + offset, 16);
            if (!element)
                return;
            for (unsigned c = 0; c < 4; ++c)
                if (channelMask & (1u << c)) {
                    const auto bits = std::bit_cast<std::uint32_t>(pixel.color[c]);
                    for (unsigned b = 0; b < 4; ++b)
                        element[c * 4 + b] = std::uint8_t(bits >> (b * 8)); // GPU byte order
                }
        };
        const auto writeColor = [&](std::uint8_t *dst, const std::array<std::uint8_t, 4> &texel, const LatteVsInterp::Output &pixel) {
            const auto mask = pixel.depthValid && !pixel.colorValid ? 0u : channelMask;
            if (targetFormat != ColorFormat::RGBA8) {
                std::array<float, 4> color = pixel.color;
                if (!pixel.colorValid)
                    for (unsigned c = 0; c < 4; ++c)
                        color[c] = float(texel[c]) / 255.f;
                if (targetBlendEnabled)
                    color = blendFloatColor(color, readFloatColor(dst, targetFormat), blend.colorSrcBlend, blend.colorDstBlend, blend.colorCombine,
                                            blend.useAlphaBlend ? blend.alphaSrcBlend : blend.colorSrcBlend,
                                            blend.useAlphaBlend ? blend.alphaDstBlend : blend.colorDstBlend,
                                            blend.useAlphaBlend ? blend.alphaCombine : blend.colorCombine, m_blendConstant);
                writeFloatColor(dst, color, mask, targetFormat);
            } else {
                const auto out = targetBlendEnabled ? blendTexel(texel, dst, blend) : texel;
                for (unsigned c = 0; c < 4; ++c)
                    if (mask & (1u << c))
                        dst[c] = out[c];
            }
        };

        // Array slices can alias a target without matching its base-view key.
        // Keep those draws and raw guest-memory targets serial until all aliases
        // participate in the same pre-draw snapshot scheme.
        const bool parallelSafe = useViewBacking() && !m_trace && !drawDepth && m_color.pitch >= m_color.width && m_texture.dimension != 5 &&
                                  std::none_of(m_pixelTextures.begin(), m_pixelTextures.end(),
                                               [](const auto &surface) { return surface.imagePtr && surface.dimension == 5; });
        const auto workerCount = [&] {
            if (m_rasterWorkerCount)
                return m_rasterWorkerCount;
            static const unsigned configured = [] {
                const auto *value = std::getenv("WEMU_RASTER_WORKERS");
                if (value) {
                    char *end = nullptr;
                    const auto count = std::strtoul(value, &end, 10);
                    if (end != value && *end == '\0' && count >= 1 && count <= 16)
                        return unsigned(count);
                }
                return std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
            }();
            return configured;
        }();
        static const std::uint64_t verifyEnvironment = [] {
            const auto *value = std::getenv("WEMU_RASTER_VERIFY_EVERY");
            if (!value || *value < '0' || *value > '9')
                return std::uint64_t(0);
            char *end = nullptr;
            const auto interval = std::strtoull(value, &end, 10);
            return end != value && *end == '\0' ? std::uint64_t(interval) : std::uint64_t(0);
        }();
        const auto verifyEvery = m_rasterVerifyEvery.value_or(verifyEnvironment);
        static const bool nativeDepthEnabled = [] {
            const auto *value = std::getenv("WEMU_NATIVE_DEPTH");
            return !value || value[0] != '0';
        }();

        // Preserve pre-draw snapshots. Vertex texture feedback into a surface
        // modified by this draw remains on the reference path.
        // GX2 bindings persist across draws, so only resources this vertex program samples can
        // alias the target. Programs that cannot be lowered keep the conservative all-slot check.
        const auto vertexProgram = m_rasterBackend && !m_vsProgram.empty() ? m_vertexShaderCache.get(m_vsProgram) : nullptr;
        const auto vertexSamples = [&](const Surface &surface) {
            const auto resource = static_cast<unsigned>(&surface - m_vertexTextures.data());
            return !vertexProgram || !*vertexProgram ||
                   std::any_of(vertexProgram->textures.begin(), vertexProgram->textures.end(),
                               [&](const auto &binding) { return binding.resource == resource; });
        };
        // Array slices can alias a target without matching its base-view key: compare storage.
        const auto vertexFeedback = [&](const Surface &surface) {
            if (!surface.imagePtr || !vertexSamples(surface))
                return false;
            if (surface.dimension != 5 || !surface.imageSize)
                return surface.dimension == 5 || makeSurfaceKey(surface) == makeSurfaceKey(m_color) ||
                       (drawDepth && m_depthWrite && makeSurfaceKey(surface) == makeSurfaceKey(m_depth));
            const auto overlaps = [&](const Surface &target, std::uint64_t bytes) {
                const auto inside = [&](std::uint64_t base, std::uint64_t size) {
                    return size && base < std::uint64_t(target.imagePtr) + bytes && std::uint64_t(target.imagePtr) < base + size;
                };
                return target.imagePtr && (inside(surface.imagePtr, surface.imageSize) || inside(surface.mipImagePtr, surface.mipImageSize));
            };
            return overlaps(m_color, std::uint64_t(m_color.pitch) * m_color.height * colorElementBytes(colorFormat(m_color.format))) ||
                   (drawDepth && m_depthWrite && overlaps(m_depth, std::uint64_t(m_depth.pitch) * m_depth.height * 4));
        };
        const bool frontBias = (m_polygonControl & (1u << 11)) && (m_polygonOffset[0] != 0 || m_polygonOffset[1] != 0);
        const bool backBias = (m_polygonControl & (1u << 12)) && (m_polygonOffset[2] != 0 || m_polygonOffset[3] != 0);
        const bool splitBias =
                !(m_polygonControl & 3) &&
                (frontBias != backBias || (frontBias && (m_polygonOffset[0] != m_polygonOffset[2] || m_polygonOffset[1] != m_polygonOffset[3])));
        const bool useBackBias = (m_polygonControl & 1) != 0;
        const bool nativeBias = useBackBias ? backBias : frontBias;
        const bool collectBackend = m_rasterBackend && !m_gpuRenderer && !rawTarget && !m_stencilTest && !m_multiWriteEnable &&
                                    (!drawDepth || !splitBias) && (!m_depthTest || !m_depth.imagePtr || drawDepth) &&
                                    (!drawDepth || (nativeDepthEnabled && m_rasterBackend->supportsDepth())) && pixelProgram && useViewBacking() &&
                                    m_pixelInputCount <= 16 && m_color.pitch >= m_color.width && !m_color.aa && m_color.dimension == 1 &&
                                    !m_color.firstMip && !m_color.firstSlice && (mode != 1 || m_rasterBackend->supportsPoints()) &&
                                    (targetFormat == ColorFormat::RGBA8 || m_rasterBackend->supportsFloatTargets()) &&
                                    std::none_of(m_vertexTextures.begin(), m_vertexTextures.end(), vertexFeedback);
        if (nativeAudit && m_rasterBackend && !collectBackend)
            Utils::Log::error("[NATIVE_GATE] split_bias={} stencil={} multi={} inputs={} dim={} mip={} slice={} pitch={} vertex_feedback={}",
                              splitBias, m_stencilTest, m_multiWriteEnable, m_pixelInputCount, m_color.dimension, m_color.firstMip,
                              m_color.firstSlice, m_color.pitch, std::any_of(m_vertexTextures.begin(), m_vertexTextures.end(), vertexFeedback));
        std::shared_ptr<const Latte::VertexShader> nativeVertexShader;
        static const bool gpuVertexEnabled = [] {
            const auto *v = std::getenv("WEMU_NATIVE_VERTEX");
            const auto *execution = std::getenv("WEMU_VS_EXEC");
            return (!v || v[0] != '0') && (!execution || execution[0] != '0');
        }();
        static const bool gpuTriangleVertexEnabled = [] {
            const auto *v = std::getenv("WEMU_NATIVE_TRIANGLE_VERTEX");
            return !v || v[0] != '0';
        }();
        if (collectBackend && (mode == 1 || mode == 4 || mode == 0x13) && gpuVertexEnabled && m_rasterBackend->supportsVertexStage() &&
            !m_fetchLayout.empty()) {
            const auto candidate = m_vertexShaderCache.get(m_vsProgram);
            // The current Vulkan vertex payload contains sixteen attributes.
            // Wider fetch inputs must use the complete reference register file.
            const bool inputsSupported = m_vertexInputCount <= 16;
            const bool buffersSupported = std::all_of(candidate->uniformBuffers.begin(), candidate->uniformBuffers.end(), [&](const auto &buffer) {
                return buffer.bank < m_vsBlocks.size() && m_vsBlocks[buffer.bank].size() <= std::size_t(buffer.count) * 4;
            });
            const bool resourcesSupported = std::all_of(candidate->textures.begin(), candidate->textures.end(), [&](const auto &binding) {
                if (binding.resource >= m_vertexTextures.size() || binding.sampler >= m_vertexSamplers.size())
                    return false;
                const auto &surface = m_vertexTextures[binding.resource];
                return surface.imagePtr && surface.dimension == 1 && !surface.aa && !surface.firstMip && !surface.firstSlice &&
                       surface.sliceCount == 1;
            });
            // Interpreting vertex programs on the replay thread dominated MK8's
            // character select and race frames, so triangles run them on the GPU
            // too. Software fallback re-executes the reference program per corner.
            if (*candidate && inputsSupported && resourcesSupported && buffersSupported &&
                ((mode == 1 && count >= 64) || !candidate->textures.empty() || !candidate->uniformBuffers.empty() ||
                 (mode != 1 && gpuTriangleVertexEnabled))) {
                nativeVertexShader = candidate;
                nativeVertexExecution = true;
            }
            if (nativeAudit)
                Utils::Log::error("[NATIVEVS] frame={} draw={} lowered={} native={} inputs={} buffers={} resources={} mode={} count={} reason={}",
                                  m_presentCount, m_stats.draws, bool(*candidate), nativeVertexExecution, m_vertexInputCount, buffersSupported,
                                  resourcesSupported, mode, count, candidate->error);
        }
        const auto clipTriangle = [&](RasterVertex(&rv)[3], const auto &emit) {
            if (!std::all_of(std::begin(rv), std::end(rv), [](const auto &v) { return v.clipPosition.has_value(); })) {
                emit(rv);
                return;
            }
            // Clip before the perspective divide: a triangle crossing the eye
            // plane otherwise wraps across the screen and can paint its back half
            // over unrelated UI. X/Y homogeneous clipping also bounds raster math.
            // Retain the existing depth-clamp policy independently of X/Y clipping.
            const auto distance = [](const RasterVertex &v, unsigned plane) {
                const auto &p = *v.clipPosition;
                return double(p[3]) + (plane & 1 ? -double(p[plane / 2]) : double(p[plane / 2]));
            };
            unsigned outside = 0, sharedOutside = 15;
            for (const auto &vertex: rv) {
                const auto &p = *vertex.clipPosition;
                if (!std::all_of(p.begin(), p.end(), [](float value) { return std::isfinite(value); }))
                    return;
                unsigned code = 0;
                for (unsigned plane = 0; plane < 4; ++plane)
                    if (distance(vertex, plane) < 0)
                        code |= 1u << plane;
                outside |= code;
                sharedOutside &= code;
            }
            if (sharedOutside)
                return;
            const auto project = [&](RasterVertex &vertex) {
                const auto &p = *vertex.clipPosition;
                if (p[3] <= 0)
                    return false;
                vertex.reciprocalW = 1.f / p[3];
                vertex.x = m_vp[0] + (p[0] / p[3] * .5f + .5f) * m_vp[2];
                vertex.y = m_vp[1] + (.5f - p[1] / p[3] * .5f) * m_vp[3];
                vertex.z = (m_vpFar + m_vpNear) * .5f + (p[2] / p[3]) * (m_vpFar - m_vpNear) * .5f;
                return true;
            };
            if (!outside) {
                for (auto &vertex: rv)
                    if (!project(vertex))
                        return;
                emit(rv);
                return;
            }
            // A triangle clipped against four planes has at most seven vertices.
            std::array<RasterVertex, 8> polygon{}, clipped{};
            std::copy_n(rv, 3, polygon.begin());
            unsigned size = 3;
            for (unsigned plane = 0; plane < 4 && size; ++plane) {
                unsigned written = 0;
                auto previous = polygon[size - 1];
                auto previousDistance = distance(previous, plane);
                for (unsigned i = 0; i < size; ++i) {
                    const auto &current = polygon[i];
                    const auto currentDistance = distance(current, plane);
                    if ((previousDistance < 0) != (currentDistance < 0)) {
                        const auto t = float(previousDistance / (previousDistance - currentDistance));
                        auto intersection = previous;
                        for (unsigned c = 0; c < 4; ++c) {
                            (*intersection.clipPosition)[c] = std::lerp((*previous.clipPosition)[c], (*current.clipPosition)[c], t);
                            for (unsigned p = 0; p < interpolatedParamCount; ++p)
                                intersection.params[p][c] = std::lerp(previous.params[p][c], current.params[p][c], t);
                        }
                        intersection.u = std::lerp(previous.u, current.u, t);
                        intersection.v = std::lerp(previous.v, current.v, t);
                        clipped[written++] = intersection;
                    }
                    if (currentDistance >= 0)
                        clipped[written++] = current;
                    previous = current;
                    previousDistance = currentDistance;
                }
                size = written;
                polygon.swap(clipped);
            }
            for (unsigned i = 0; i < size; ++i)
                if (!project(polygon[i]))
                    return;
            for (unsigned i = 1; i + 1 < size; ++i) {
                const RasterVertex triangle[]{polygon[0], polygon[i], polygon[i + 1]};
                emit(triangle);
            }
        };
        // Prepared triangles reference assembled corners, or copies kept in clippedCorners for
        // triangles produced by clipping (a deque keeps those references stable).
        struct PreparedTriangle {
                std::array<const RasterVertex *, 3> corners;
                const RasterVertex &operator[](std::size_t i) const { return *corners[i]; }
        };
        std::vector<PreparedTriangle> preparedTriangles;
        std::deque<RasterVertex> clippedCorners;
        if (collectBackend && mode != 1) {
            const auto triangles = mode == 0x13 ? idx.size() / 4 * 2 : idx.size() / 3;
            preparedTriangles.reserve(triangles);
        }
        const auto rasterProjected = [&](const RasterVertex(&rv)[3]) {
            const float area = (rv[1].x - rv[0].x) * (rv[2].y - rv[0].y) - (rv[2].x - rv[0].x) * (rv[1].y - rv[0].y);
            if (std::fabs(area) < 0.5f)
                return;
            const bool front = (area > 0) == bool(m_polygonControl & 4);
            if (m_polygonControl & (front ? 1u : 2u))
                return;
            const auto biasIndex = front ? 0u : 2u;
            const bool biasEnabled = front ? frontBias : backBias;
            const float triangleBias =
                    drawDepth && biasEnabled
                            ? polygonDepthBias({rv[0].x, rv[1].x, rv[2].x}, {rv[0].y, rv[1].y, rv[2].y}, {rv[0].z, rv[1].z, rv[2].z},
                                               drawDepth->format, m_polygonOffset[biasIndex], m_polygonOffset[biasIndex + 1], m_polygonOffset[4])
                            : 0.f;
            m_stats.tris++;
            // GPU path: hand the screen-space triangle to the GPU rasteriser instead of filling it
            // here. All the geometry work above (shader execution, placement, UV) is reused as-is.
            if (m_gpuRenderer) {
                for (int i = 0; i < 3; i++) {
                    m_gpuTri.push_back(rv[i].x);
                    m_gpuTri.push_back(rv[i].y);
                    m_gpuTri.push_back(rv[i].u);
                    m_gpuTri.push_back(rv[i].v);
                }
                return;
            }
            const int minX = std::max(sx0, static_cast<int>(std::floor(std::min({rv[0].x, rv[1].x, rv[2].x}))));
            const int maxX = std::min(sx1 - 1, static_cast<int>(std::ceil(std::max({rv[0].x, rv[1].x, rv[2].x}))));
            const int minY = std::max(sy0, static_cast<int>(std::floor(std::min({rv[0].y, rv[1].y, rv[2].y}))));
            const int maxY = std::min(sy1 - 1, static_cast<int>(std::ceil(std::max({rv[0].y, rv[1].y, rv[2].y}))));
            if (minX > maxX || minY > maxY)
                return;
            // Empty geometry never reads or writes pixels. Preserve pending GPU
            // versions until a covered software draw actually needs their data.
            prepareSoftwareInputs();
            // Constant planes interpolate exactly, so their comparison against
            // conservative stored-depth bounds can reject the whole triangle.
            if (rv[0].z == rv[1].z && rv[1].z == rv[2].z && rejectConstantDepth(rv[0].z + triangleBias)) {
                ++m_earlyDepthRejectedTriangles;
                return;
            }
            const TriangleCoverage coverage({rv[0].x, rv[0].y}, {rv[1].x, rv[1].y}, {rv[2].x, rv[2].y});
            // Incremental barycentrics: w0/w1 are affine in x and y — one add per pixel instead
            // of the full edge-function evaluation (and no per-pixel divides).
            // Accumulate in double so long scanlines do not drift across texel
            // centers. Guest shader inputs are still converted to float below.
            const double preciseArea =
                    (double(rv[1].x) - rv[0].x) * (double(rv[2].y) - rv[0].y) - (double(rv[2].x) - rv[0].x) * (double(rv[1].y) - rv[0].y);
            const double invArea = 1.0 / preciseArea;
            const double w0dx = (double(rv[1].y) - rv[2].y) * invArea, w0dy = (double(rv[2].x) - rv[1].x) * invArea;
            const double w1dx = (double(rv[2].y) - rv[0].y) * invArea, w1dy = (double(rv[0].x) - rv[2].x) * invArea;
            const double rx = double(minX) + 0.5, ry = double(minY) + 0.5;
            double w0row = ((rv[1].x - rx) * (rv[2].y - ry) - (rv[2].x - rx) * (rv[1].y - ry)) * invArea;
            double w1row = ((rv[2].x - rx) * (rv[0].y - ry) - (rv[0].x - rx) * (rv[2].y - ry)) * invArea;
            struct Row {
                    double w0, w1;
                    std::uint64_t pixels{}, alphaSum{};
            };
            std::vector<Row> rows;
            rows.reserve(maxY - minY + 1);
            // Preserve serial interpolation rounding across worker partitions.
            for (int yy = minY; yy <= maxY; ++yy, w0row += w0dy, w1row += w1dy)
                rows.push_back({w0row, w1row});
            const auto rasterRows = [&](unsigned first, unsigned end) {
                for (unsigned r = first; r < end; ++r) {
                    const int yy = minY + int(r);
                    auto &result = rows[r];
                    std::uint8_t *row = colorOutputEnabled ? target + static_cast<std::size_t>(yy) * m_color.pitch * targetElementBytes : nullptr;
                    double weight0 = result.w0, weight1 = result.w1;
                    for (int xx = minX; xx <= maxX; xx++, weight0 += w0dx, weight1 += w1dx) {
                        const float w0 = float(weight0), w1 = float(weight1), w2 = float(1.0 - weight0 - weight1);
                        if (!coverage.contains(double(xx) + 0.5, double(yy) + 0.5))
                            continue;
                        if (drawDepth) {
                            if (unsigned(xx) >= m_depth.width || unsigned(yy) >= m_depth.height)
                                continue;
                            if (earlyDepth && !depthCompare(clampDepth(interpolateValue(rv[0].z, rv[1].z, rv[2].z, w0, w1, w2) + triangleBias),
                                                            drawDepth->pixels[std::size_t(yy) * m_depth.pitch + xx], m_depthCompare)) {
                                ++m_earlyDepthRejectedFragments;
                                continue;
                            }
                        }
                        LatteVsInterp::Output pixel;
                        const auto texel = shadeTexel(w0 * rv[0].u + w1 * rv[1].u + w2 * rv[2].u, w0 * rv[0].v + w1 * rv[1].v + w2 * rv[2].v,
                                                      interpolateParams(rv, w0, w1, w2), &pixel);
                        if (pixel.discarded)
                            continue;
                        if (drawDepth) {
                            if (unsigned(xx) >= m_depth.width || unsigned(yy) >= m_depth.height)
                                continue;
                            auto &stored = drawDepth->pixels[std::size_t(yy) * m_depth.pitch + xx];
                            const float incoming = clampDepth(
                                    pixel.depthValid ? pixel.depth : interpolateValue(rv[0].z, rv[1].z, rv[2].z, w0, w1, w2) + triangleBias);
                            if (!depthCompare(incoming, stored, m_depthCompare))
                                continue;
                            if (m_depthWrite)
                                writeDepth(stored, incoming);
                        }
                        result.pixels++;
                        result.alphaSum += texel[3];
                        if (colorOutputEnabled) {
                            writeColor(row + static_cast<std::size_t>(xx) * targetElementBytes, texel, pixel);
                            if (rawTarget)
                                writeRawElement(unsigned(xx), unsigned(yy), pixel);
                        }
                    }
                }
            };
            const auto areaPixels = std::uint64_t(maxX - minX + 1) * rows.size();
            if (parallelSafe && workerCount > 1 && rows.size() > 1 && areaPixels >= m_parallelMinPixels) {
                if (!m_rasterWorkers)
                    m_rasterWorkers = std::make_unique<RasterWorkers>(workerCount);
                const bool verify = verifyEvery && (m_parallelTriangles % verifyEvery == 0);
                ++m_parallelTriangles;
                std::vector<std::uint8_t> before;
                const auto targetBytes = std::size_t(m_color.pitch) * m_color.height * targetElementBytes;
                if (verify)
                    before.assign(target, target + targetBytes);
                m_rasterWorkers->run(unsigned(rows.size()), rasterRows);
                if (verify) {
                    // Reuse the same shader inputs and immutable feedback snapshots.
                    // Restore all target bytes so blending starts from identical data.
                    const std::vector<std::uint8_t> parallelOutput(target, target + targetBytes);
                    const auto parallelRows = rows;
                    std::copy(before.begin(), before.end(), target);
                    for (auto &row: rows)
                        row.pixels = row.alphaSum = 0;
                    rasterRows(0, unsigned(rows.size()));
                    const auto mismatch = std::mismatch(parallelOutput.begin(), parallelOutput.end(), target);
                    const bool countersMatch = std::equal(rows.begin(), rows.end(), parallelRows.begin(), [](const Row &a, const Row &b) {
                        return a.pixels == b.pixels && a.alphaSum == b.alphaSum;
                    });
                    if (mismatch.first != parallelOutput.end() || !countersMatch)
                        throw std::runtime_error(std::format("Raster verification mismatch: frame={} draw={} triangle={} target=0x{:08X} byte={}",
                                                             m_presentCount, m_stats.draws, m_stats.tris, m_color.imagePtr,
                                                             std::distance(parallelOutput.begin(), mismatch.first)));
                    ++m_verifiedRasterTriangles;
                    Utils::Log::error("[RASTER_VERIFY] matched={} eligible={} frame={} ps={:016X}", m_verifiedRasterTriangles, m_parallelTriangles,
                                      m_presentCount, shaderHash(m_psProgram));
                }
            } else {
                rasterRows(0, unsigned(rows.size()));
            }
            for (const auto &row: rows) {
                m_stats.pixels += row.pixels;
                m_stats.alphaSum += row.alphaSum;
            }
        };

        const auto rasterPrepared = [&](const RasterVertex(&prepared)[3]) {
            RasterVertex rv[3]{prepared[0], prepared[1], prepared[2]};
            if (nativeVertexExecution) {
                forceVertexReference = true;
                bool valid = true;
                for (auto &v: rv)
                    valid &= runShader(v.vertexIndex, v.x, v.y, v.u, v.v, &v.params, &v.paramValid, &v.z, &v.reciprocalW, &v.clipPosition);
                forceVertexReference = false;
                if (!valid)
                    return;
            }
            if (nativeVertexExecution)
                clipTriangle(rv, rasterProjected);
            else
                rasterProjected(rv);
        };
        std::vector<RasterVertex> preparedPoints;
        if (collectBackend && mode == 1)
            preparedPoints.reserve(idx.size());
        const auto rasterPoint = [&](const RasterVertex &prepared) {
            RasterVertex rv = prepared;
            if (nativeVertexExecution) {
                forceVertexReference = true;
                const bool valid = runShader(rv.vertexIndex, rv.x, rv.y, rv.u, rv.v, &rv.params, &rv.paramValid, &rv.z, &rv.reciprocalW);
                forceVertexReference = false;
                if (!valid)
                    return;
            }
            ++m_stats.points;
            if (!std::isfinite(rv.x) || !std::isfinite(rv.y) || rv.x < float(sx0) || rv.y < float(sy0) || rv.x >= float(sx1) || rv.y >= float(sy1))
                return;
            const auto x = unsigned(std::floor(rv.x)), y = unsigned(std::floor(rv.y));
            prepareSoftwareInputs();
            if (drawDepth) {
                if (x >= m_depth.width || y >= m_depth.height)
                    return;
                if (earlyDepth && !depthCompare(clampDepth(rv.z), drawDepth->pixels[std::size_t(y) * m_depth.pitch + x], m_depthCompare)) {
                    ++m_earlyDepthRejectedFragments;
                    return;
                }
            }
            LatteVsInterp::Output pixel;
            const auto texel = shadeTexel(rv.u, rv.v, rv.params, &pixel);
            if (pixel.discarded)
                return;
            if (drawDepth) {
                if (x >= m_depth.width || y >= m_depth.height)
                    return;
                auto &stored = drawDepth->pixels[std::size_t(y) * m_depth.pitch + x];
                const auto incoming = clampDepth(pixel.depthValid ? pixel.depth : rv.z);
                if (!depthCompare(incoming, stored, m_depthCompare))
                    return;
                if (m_depthWrite)
                    writeDepth(stored, incoming);
            }
            if (colorOutputEnabled) {
                writeColor(target + (std::size_t(y) * m_color.pitch + x) * targetElementBytes, texel, pixel);
                if (rawTarget)
                    writeRawElement(x, y, pixel);
            }
            ++m_stats.pixels;
            m_stats.alphaSum += texel[3];
        };

        // A corner depends only on its vertex index within a draw, so shared corners are assembled
        // once: cornerState is 0 (pending), 1 (ready) or 2 (unavailable, drops its triangles).
        // GPU vertex programs only need the fetched attributes: build each distinct vertex once,
        // directly in the backend layout. Reference corners (~600 bytes each) are assembled only
        // if the draw falls back to software. Verification compares against those corners.
        static const bool verifyEnvironmentSet = std::getenv("WEMU_NATIVE_VERIFY_EVERY") != nullptr;
        const bool directVertices = collectBackend && nativeVertexExecution && mode != 1 &&
                                    !(m_nativeVerifyEvery ? *m_nativeVerifyEvery != 0 : verifyEnvironmentSet);
        std::vector<RasterVertex> corners;
        std::vector<std::uint8_t> cornerState;
        std::uint32_t cornerBase = 0;
        if (indexed && idx.size() >= 6 && !directVertices) {
            const auto [low, high] = std::minmax_element(idx.begin(), idx.end());
            if (std::uint64_t(*high) - *low + 1 <= idx.size() * 2 + 64) {
                cornerBase = *low;
                corners.resize(std::size_t(*high) - *low + 1);
                cornerState.assign(corners.size(), 0);
            }
        }
        const auto assembleCorner = [&](std::uint32_t vi, RasterVertex &corner) {
            corner.vertexIndex = vi;
            if (runShader(vi, corner.x, corner.y, corner.u, corner.v, &corner.params, &corner.paramValid, &corner.z, &corner.reciprocalW,
                          &corner.clipPosition)) {
                if (!nativeVertexExecution && !corner.paramValid[0])
                    corner.params[0] = {1.0f, 1.0f, 1.0f, 1.0f};
                return true;
            }
            float x = 0, y = 0;
            if (!fetch(vi, x, y, corner.u, corner.v))
                return false;
            mapVertex(x, y, corner.x, corner.y);
            return true;
        };
        const auto emit = [&](const RasterVertex(&triangle)[3]) {
            if (collectBackend) {
                PreparedTriangle prepared;
                for (unsigned k = 0; k < 3; ++k)
                    prepared.corners[k] = &clippedCorners.emplace_back(triangle[k]);
                preparedTriangles.push_back(prepared);
            } else {
                rasterPrepared(triangle);
            }
        };
        // Corners that need no clipping are projected the same way by every triangle using them,
        // so the projected copy is kept per index too (projectedState like cornerState).
        std::vector<RasterVertex> projectedCorners;
        std::vector<std::uint8_t> projectedState;
        if (collectBackend && !corners.empty() && !nativeVertexExecution) {
            projectedCorners.resize(corners.size());
            projectedState.assign(corners.size(), 0);
        }
        const auto projectedCorner = [&](std::uint32_t slot) -> const RasterVertex * {
            if (!projectedState[slot]) {
                auto &vertex = projectedCorners[slot] = corners[slot];
                const auto &p = *vertex.clipPosition;
                projectedState[slot] = 2;
                if (p[3] > 0) {
                    vertex.reciprocalW = 1.f / p[3];
                    vertex.x = m_vp[0] + (p[0] / p[3] * .5f + .5f) * m_vp[2];
                    vertex.y = m_vp[1] + (.5f - p[1] / p[3] * .5f) * m_vp[3];
                    vertex.z = (m_vpFar + m_vpNear) * .5f + (p[2] / p[3]) * (m_vpFar - m_vpNear) * .5f;
                    projectedState[slot] = 1;
                }
            }
            return projectedState[slot] == 1 ? &projectedCorners[slot] : nullptr;
        };
        // Outcode of a cached corner against clipTriangle's four planes; 16 marks a non-finite
        // clip position, for which clipTriangle drops the triangle.
        const auto clipCode = [](const RasterVertex &vertex) {
            const auto &p = *vertex.clipPosition;
            if (!std::all_of(p.begin(), p.end(), [](float value) { return std::isfinite(value); }))
                return 16u;
            unsigned code = 0;
            for (unsigned plane = 0; plane < 4; ++plane)
                if (double(p[3]) + (plane & 1 ? -double(p[plane / 2]) : double(p[plane / 2])) < 0)
                    code |= 1u << plane;
            return code;
        };
        auto rasterTri = [&](const std::uint32_t i0, const std::uint32_t i1, const std::uint32_t i2) {
            const std::uint32_t vis[3] = {idx[i0], idx[i1], idx[i2]};
            if (!corners.empty()) {
                std::uint32_t slots[3];
                for (int i = 0; i < 3; i++) {
                    slots[i] = vis[i] - cornerBase;
                    if (!cornerState[slots[i]])
                        cornerState[slots[i]] = assembleCorner(vis[i], corners[slots[i]]) ? 1 : 2;
                    if (cornerState[slots[i]] != 1)
                        return;
                }
                if (collectBackend) {
                    // Fast paths of clipTriangle that keep the corners as they are or only project them.
                    PreparedTriangle prepared{{&corners[slots[0]], &corners[slots[1]], &corners[slots[2]]}};
                    const bool clipped = std::all_of(std::begin(slots), std::end(slots), [&](auto slot) { return corners[slot].clipPosition.has_value(); });
                    if (nativeVertexExecution || !clipped) {
                        preparedTriangles.push_back(prepared);
                        return;
                    }
                    unsigned outside = 0, sharedOutside = 15;
                    for (const auto slot: slots) {
                        const auto code = clipCode(corners[slot]);
                        if (code & 16)
                            return;
                        outside |= code;
                        sharedOutside &= code;
                    }
                    if (sharedOutside)
                        return;
                    if (!outside) {
                        for (unsigned k = 0; k < 3; ++k)
                            if (!(prepared.corners[k] = projectedCorner(slots[k])))
                                return;
                        preparedTriangles.push_back(prepared);
                        return;
                    }
                }
                RasterVertex rv[3]{corners[slots[0]], corners[slots[1]], corners[slots[2]]};
                if (nativeVertexExecution)
                    emit(rv);
                else
                    clipTriangle(rv, emit);
                return;
            }
            RasterVertex rv[3];
            for (int i = 0; i < 3; i++)
                if (!assembleCorner(vis[i], rv[i]))
                    return;
            if (nativeVertexExecution)
                emit(rv);
            else
                clipTriangle(rv, emit);
        };
        // Pre-shading: CPU vertex shading dominates replay time, and vertices are independent, so
        // shade every distinct index of the draw once, in parallel, before assembling primitives.
        // Only pure shaders qualify (no texture reads, no diagnostics that trace a single run);
        // results are identical to running them lazily in runShader.
        static const bool preshadeEnabled = [] {
            const auto set = [](const char *name) { return std::getenv(name) != nullptr; };
            const auto *value = std::getenv("WEMU_VS_PRESHADE");
            return (!value || value[0] != '0') && !set("WEMU_VS_DEBUG") && !set("WEMU_VS_DEBUG_DRAW") && !set("WEMU_VS_DEBUG_TEX") &&
                   !set("WEMU_VS_SUMMARY") && !set("WEMU_VS_DEGEN_MODEL_FALLBACK") && !set("WEMU_KCACHE_BIAS");
        }();
        if (preshadeEnabled && !nativeVertexExecution && !m_trace && vertexDeclaration->valid && !vertexShaderReadsTextures && !m_vsProgram.empty() &&
            !m_fetchLayout.empty() && idx.size() >= 64) {
            const auto [low, high] = std::minmax_element(idx.begin(), idx.end());
            const std::uint64_t span = std::uint64_t(*high) - *low + 1;
            if (span <= idx.size() * 4 + 256) {
                preshadedBase = *low;
                preshaded.resize(span);
                preshadedReady.assign(span, 0);
                std::vector<std::uint32_t> unique;
                unique.reserve(idx.size());
                for (const auto vi: idx)
                    if (!preshadedReady[vi - preshadedBase]) {
                        preshadedReady[vi - preshadedBase] = 2; // queued
                        unique.push_back(vi);
                    }
                const auto shade = [&](unsigned first, unsigned end) {
                    const LatteVsInterp::KcacheFetch kcache = [&](std::uint32_t bank, std::uint32_t vec4Idx, std::uint32_t chan) -> float {
                        if (bank >= m_vsBlocks.size())
                            return 0.0f;
                        const auto &blk = m_vsBlocks[bank];
                        const std::uint64_t offset = std::uint64_t(vec4Idx) * 4 + chan;
                        return offset < blk.size() ? asFloat(blk[offset]) : 0.0f;
                    };
                    std::array<std::array<float, 4>, 32> attribs{};
                    for (unsigned k = first; k < end; ++k) {
                        const auto slot = unique[k] - preshadedBase;
                        if (!fetchAttributes(attributeFetch, unique[k], std::span(attribs).first(m_vertexInputCount))) {
                            preshadedReady[slot] = 0; // runShader reports the failure as before
                            continue;
                        }
                        preshaded[slot] =
                                LatteVsInterp::runVertex(*vertexDeclaration, std::span(attribs).first(m_vertexInputCount), m_vsRegs, 64, kcache);
                        preshadedReady[slot] = 1;
                    }
                };
                // About 2.5 us per vertex: from 64 vertices the pool beats its dispatch cost.
                if (unique.size() >= 64) {
                    if (!m_vertexWorkers)
                        m_vertexWorkers = std::make_unique<RasterWorkers>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u));
                    m_vertexWorkers->run(unsigned(unique.size()), shade);
                } else
                    shade(0, unsigned(unique.size()));
                m_vertexShaderExecutions += unique.size();
            }
        }
        if (mode == 1) {
            for (const auto vi: idx) {
                RasterVertex rv;
                rv.vertexIndex = vi;
                if (!runShader(vi, rv.x, rv.y, rv.u, rv.v, &rv.params, &rv.paramValid, &rv.z, &rv.reciprocalW)) {
                    float x{}, y{};
                    if (!fetch(vi, x, y, rv.u, rv.v))
                        continue;
                    mapVertex(x, y, rv.x, rv.y);
                }
                if (collectBackend)
                    preparedPoints.push_back(rv);
                else
                    rasterPoint(rv);
            }
        }
        const auto assembleTriangles = [&] {
            if (mode == 0x13) { // QUADS
                for (std::uint32_t i = 0; i + 3 < idx.size(); i += 4) {
                    rasterTri(i, i + 1, i + 2);
                    rasterTri(i, i + 2, i + 3);
                }
            } else { // treat everything else as a triangle list
                for (std::uint32_t i = 0; i + 2 < idx.size(); i += 3)
                    rasterTri(i, i + 1, i + 2);
            }
        };
        // Only the program's declared inputs are stored per vertex (compact attributes); the
        // backend supplies the reference defaults for the other attribute slots.
        std::vector<std::array<float, 4>> directAttributes;
        std::vector<std::uint32_t> directIndices;
        std::shared_ptr<const DirectVertices> directArrays;
        bool directReused = false;
        const auto attributeStride = unsigned(std::min<std::size_t>(m_vertexInputCount, RasterDraw::Vertex{}.inputs.size()));
        // Static geometry is converted once: a draw whose indices, fetch state and guest vertex
        // bytes match a previous conversion reuses its arrays (and the backend its upload).
        std::uint64_t directKey = 0;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> directSources;
        bool cacheDirect = false;
        if (directVertices && idx.size() >= 64) {
            const auto [low, high] = std::minmax_element(idx.begin(), idx.end());
            cacheDirect = directVertexSources(attributeFetch, attributeStride, *low, *high, directSources);
            if (cacheDirect) {
                const auto mix = [&](std::uint64_t value) { directKey = (directKey ^ value) * 0x100000001B3ull + (directKey >> 29); };
                for (unsigned i = 0; i < 6; ++i)
                    mix(cmd.gpr[i]);
                mix(idx.size());
                mix(*low);
                mix(*high);
                mix(mode);
                mix(attributeStride);
                for (const auto &binding: m_attribs)
                    mix((std::uint64_t(binding.addr) << 32) ^ binding.stride ^ (std::uint64_t(binding.size) << 16));
                for (const auto word: m_fetchLayout)
                    mix(word);
                const auto found = m_directVertexCache.find(directKey);
                if (found != m_directVertexCache.end()) {
                    auto &entry = found->second;
                    const bool inputsMatch = entry.mode == mode && entry.attributeStride == attributeStride && entry.attribs == m_attribs &&
                                             entry.semantics == m_vertexSemantics && entry.fetchLayout == m_fetchLayout &&
                                             std::ranges::equal(entry.sourceIndices, idx) && entry.sources.size() == directSources.size();
                    bool bytesMatch = inputsMatch && entry.validatedFrame == m_presentCount;
                    for (std::size_t i = 0; inputsMatch && !bytesMatch && i < directSources.size(); ++i) {
                        const auto &[address, bytes] = entry.sources[i];
                        const auto *current = m_mem->hostPtr(address, bytes.size());
                        if (address != directSources[i].first || bytes.size() != directSources[i].second || !current ||
                            std::memcmp(current, bytes.data(), bytes.size()) != 0)
                            break;
                        bytesMatch = i + 1 == directSources.size();
                    }
                    bytesMatch |= inputsMatch && directSources.empty();
                    if (bytesMatch) {
                        entry.validatedFrame = m_presentCount;
                        entry.used = ++m_directVertexClock;
                        directArrays = entry.vertices;
                        directReused = true;
                        ++m_directVertexReuses;
                    }
                }
                // Convert into the cache only from a draw's second sighting, and not for draws
                // whose data kept changing: dynamic buffers would only churn it.
                if (!directArrays) {
                    if (m_directVertexSeen.size() >= 65536)
                        m_directVertexSeen.clear();
                    auto &changes = m_directVertexSeen[directKey];
                    cacheDirect = changes > 0 && changes < 4;
                    if (found != m_directVertexCache.end() || !changes)
                        changes = std::uint8_t(std::min(changes + 1, 255));
                }
            }
        }
        if (directVertices && !directArrays) {
            const auto stride = std::max(1u, attributeStride);
            const auto [low, high] = idx.empty() ? std::pair{idx.end(), idx.end()} : std::minmax_element(idx.begin(), idx.end());
            const std::uint64_t span = idx.empty() ? 0 : std::uint64_t(*high) - *low + 1;
            const bool dense = span <= idx.size() * 2 + 64;
            std::vector<std::uint32_t> denseSlots(dense ? span : 0, ~0u);
            std::unordered_map<std::uint32_t, std::uint32_t> sparseSlots;
            directAttributes.reserve(std::min<std::size_t>(idx.size(), dense ? span : idx.size()) * stride);
            directIndices.reserve(idx.size() / 2 * 3);
            // ~0u: not fetched yet; ~1u: unavailable, which drops its triangles like a failed corner.
            const auto slotOf = [&](std::uint32_t vi) {
                auto &slot = dense ? denseSlots[vi - *low] : sparseSlots.try_emplace(vi, ~0u).first->second;
                if (slot == ~0u) {
                    const auto base = directAttributes.size();
                    directAttributes.resize(base + stride);
                    // A program without declared inputs still gets slot 0's default.
                    if (!attributeStride)
                        directAttributes[base] = {1, 1, 1, 1};
                    slot = fetchAttributes(attributeFetch, vi, std::span(directAttributes).subspan(base, attributeStride))
                                   ? std::uint32_t(base / stride)
                                   : ~1u;
                    if (slot == ~1u)
                        directAttributes.resize(base);
                }
                return slot;
            };
            const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
                const std::uint32_t slots[]{slotOf(idx[a]), slotOf(idx[b]), slotOf(idx[c])};
                if (slots[0] != ~1u && slots[1] != ~1u && slots[2] != ~1u)
                    directIndices.insert(directIndices.end(), std::begin(slots), std::end(slots));
            };
            if (mode == 0x13)
                for (std::uint32_t i = 0; i + 3 < idx.size(); i += 4) {
                    triangle(i, i + 1, i + 2);
                    triangle(i, i + 2, i + 3);
                }
            else
                for (std::uint32_t i = 0; i + 2 < idx.size(); i += 3)
                    triangle(i, i + 1, i + 2);
            if (cacheDirect) {
                constexpr std::uint64_t budget = 192ull * 1024 * 1024;
                DirectVertexEntry entry;
                entry.fetchLayout = m_fetchLayout;
                entry.sourceIndices = idx;
                entry.semantics = m_vertexSemantics;
                entry.attribs = m_attribs;
                entry.attributeStride = attributeStride;
                entry.mode = mode;
                for (const auto &[address, size]: directSources) {
                    const auto *bytes = m_mem->hostPtr(address, size);
                    entry.sources.emplace_back(address, std::vector<std::uint8_t>(bytes, bytes + size));
                }
                entry.vertices = directArrays =
                        std::make_shared<const DirectVertices>(DirectVertices{std::move(directAttributes), std::move(directIndices)});
                entry.validatedFrame = m_presentCount;
                entry.used = ++m_directVertexClock;
                if (const auto old = m_directVertexCache.find(directKey); old != m_directVertexCache.end()) {
                    m_directVertexBytes -= old->second.bytes();
                    m_directVertexCache.erase(old);
                }
                m_directVertexBytes += entry.bytes();
                if (m_directVertexBytes > budget) {
                    // Evict least recently used entries in one pass, down to three quarters.
                    std::vector<std::pair<std::uint64_t, std::uint64_t>> order;
                    for (const auto &[key, cached]: m_directVertexCache)
                        order.emplace_back(cached.used, key);
                    std::ranges::sort(order);
                    for (const auto &[used, key]: order) {
                        if (m_directVertexBytes <= budget / 4 * 3)
                            break;
                        const auto it = m_directVertexCache.find(key);
                        m_directVertexBytes -= it->second.bytes();
                        m_directVertexCache.erase(it);
                    }
                }
                m_directVertexCache.emplace(directKey, std::move(entry));
            }
        } else if (mode != 1 && !directVertices)
            assembleTriangles();
        const std::span<const std::array<float, 4>> directAttributeView =
                directArrays ? std::span(directArrays->attributes) : std::span(directAttributes);
        const std::span<const std::uint32_t> directIndexView = directArrays ? std::span(directArrays->indices) : std::span(directIndices);
        std::int64_t nativePrepareUs{}, nativeExecuteUs{}, nativeVerifyUs{}, fallbackUs{};
        bool nativeCommitted = false;
        if (collectBackend && (!preparedTriangles.empty() || !preparedPoints.empty() || !directIndexView.empty())) {
            const auto prepareStart = (profile || nativeAudit) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            std::vector<Latte::TextureType> resourceTypes(m_pixelTextures.size(), Latte::TextureType::TwoD);
            for (unsigned i = 0; i < m_pixelTextures.size(); ++i)
                if (m_pixelTextures[i].dimension == 5)
                    resourceTypes[i] = Latte::TextureType::TwoDArray;
                else if (m_pixelTextures[i].dimension == 2)
                    resourceTypes[i] = Latte::TextureType::ThreeD;
                else if (m_pixelTextures[i].dimension == 3)
                    resourceTypes[i] = Latte::TextureType::Cube;
            const auto translatedShader = skipColorShader ? m_fragmentShaderCache.get({0, (Latte::CF_EXP_DONE << 23) | (1u << 21) | 0xB24u})
                                                          : m_fragmentShaderCache.get(m_psProgram, resourceTypes, onlyColorZero, colorExport);
            const auto &shader = *translatedShader;
            bool supported = bool(shader);
            std::string unsupportedReason = supported ? std::string{} : "fragment shader not lowered: " + shader.error;
            std::vector<RasterDraw::Texture> textures;
            std::vector<std::vector<std::uint8_t>> ownedTextures;
            std::vector<std::vector<float>> ownedFloatTextures;
            std::vector<std::shared_ptr<ArraySnapshot>> arrays;
            std::vector<std::shared_ptr<DecodedTexture>> decodedTextures;
            std::vector<std::shared_ptr<MipSnapshot>> mipTextures;
            ownedTextures.reserve(shader.textures.size());
            ownedFloatTextures.reserve(shader.textures.size());
            auto allBindings = shader.textures;
            if (nativeVertexShader)
                for (auto binding: nativeVertexShader->textures) {
                    binding.binding += 17;
                    allBindings.push_back(binding);
                }
            ownedTextures.reserve(allBindings.size());
            ownedFloatTextures.reserve(allBindings.size());
            static const bool deferredRender = [] {
                const auto *value = std::getenv("WEMU_NATIVE_DEFER_READBACK");
                return value && std::string_view(value) == "1";
            }();
            for (const auto &binding: allBindings) {
                const bool vertexBinding = binding.binding > 17;
                const auto &boundSurfaces = vertexBinding ? m_vertexTextures : m_pixelTextures;
                const auto &boundSamplers = vertexBinding ? m_vertexSamplers : m_pixelSamplers;
                if (binding.resource >= boundSurfaces.size() || binding.sampler >= boundSamplers.size()) {
                    supported = false;
                    unsupportedReason = "replayer#1";
                    break;
                }
                const auto surface = boundSurfaces[binding.resource];
                if (!vertexBinding && surface.dimension == 5 && (!binding.explicitLod || surface.mipCount <= 1)) {
                    // Arrays whose layers were rendered as separate surfaces (MK8's cascaded shadow
                    // maps, HDR and volume slices): pass each layer's current version, so resident
                    // layers are copied on the GPU instead of read back or sampled in software.
                    // Explicit LODs of a single-level view always select that level.
                    if (auto layers = renderedArrayLayers(surface); !layers.empty()) {
                        const bool depthLayers = surface.format == 0x80E || surface.format == 5 || surface.format == 0x11;
                        textures.push_back({binding, surface.width, surface.height, boundSamplers[binding.sampler], [&, surface](unsigned x, unsigned y) {
                                                return sampleTexture(surface, TextureSampler{}, (float(x) + 0.5f) / float(surface.width),
                                                                     (float(y) + 0.5f) / float(surface.height), 0, nullptr);
                                            }});
                        textures.back().renderedLayers = std::move(layers);
                        textures.back().layers = surface.sliceCount;
                        textures.back().unorm8Pitch = surface.pitch;
                        textures.back().unorm8Map = surface.compMap;
                        textures.back().r32Map = surface.compMap;
                        textures.back().renderedDepth = depthLayers;
                        textures.back().renderedFormat = depthLayers ? ColorFormat::RGBA8 : colorFormat(surface.format);
                        textures.back().sourceMipCount = 1;
                        continue;
                    }
                    if (const auto layers = layerSnapshot(surface)) {
                        textures.push_back({binding, surface.width, surface.height, boundSamplers[binding.sampler], [&, surface](unsigned x, unsigned y) {
                                                return sampleTexture(surface, TextureSampler{}, (float(x) + 0.5f) / float(surface.width),
                                                                     (float(y) + 0.5f) / float(surface.height), 0, nullptr);
                                            }});
                        auto &texture = textures.back();
                        if (surface.format == 0x816) {
                            texture.r11g11b10Bytes = *layers;
                            texture.r11g11b10Pitch = surface.width;
                        } else if (colorFormat(surface.format) == ColorFormat::RGBA32Float) {
                            texture.rgba32Bytes = *layers;
                            texture.rgba32Pitch = surface.width;
                        } else {
                            texture.unorm8 = *layers;
                            texture.unorm8Pitch = surface.width;
                        }
                        texture.unorm8Map = surface.compMap;
                        texture.layers = surface.sliceCount;
                        texture.sourceMipCount = 1;
                        // Immutable until a layer is written: the backend shares one upload per version.
                        texture.snapshotOwner = layers;
                        continue;
                    }
                }
                if (!vertexBinding && (surface.dimension == 3 || binding.explicitLod) && m_rasterBackend->supportsRenderedMipLevels() &&
                    deferredRender) {
                    // Levels and cube faces still on the GPU are assembled there, not snapshotted.
                    const auto levels = binding.explicitLod ? std::min(surface.viewMipCount ? surface.viewMipCount : std::max(1u, surface.mipCount),
                                                                       std::max(1u, surface.mipCount) - surface.firstMip)
                                                            : 1u;
                    // A single-level 2D view keeps the ordinary rendered-texture path below.
                    if (auto subresources = surface.dimension == 1 && levels == 1 ? decltype(renderedSubresources(surface, levels)){}
                                                                                   : renderedSubresources(surface, levels);
                        !subresources.empty()) {
                        textures.push_back({binding, surface.width, surface.height, boundSamplers[binding.sampler], [&, surface](unsigned x, unsigned y) {
                                                return sampleTexture(surface, TextureSampler{}, (float(x) + 0.5f) / float(surface.width),
                                                                     (float(y) + 0.5f) / float(surface.height), 0, nullptr);
                                            }});
                        auto &texture = textures.back();
                        texture.renderedLayers = std::move(subresources);
                        texture.renderedLevels = levels;
                        texture.layers = surface.sliceCount;
                        texture.unorm8Pitch = surface.pitch;
                        texture.unorm8Map = surface.compMap;
                        texture.renderedFormat = colorFormat(surface.format);
                        texture.sourceMipCount = levels;
                        continue;
                    }
                }
                if (!vertexBinding && (surface.dimension == 3 || binding.explicitLod ||
                                       (surface.dimension == 5 &&
                                        (surface.format == 0x234 || surface.format == 0x235 || colorFormat(surface.format) != ColorFormat::RGBA8)))) {
                    auto snapshot = mipSnapshot(surface, binding.explicitLod);
                    if (!snapshot) {
                        supported = false;
                        unsupportedReason = std::format("unsupported cube/mip snapshot ptr={:08X} dim={} mip={}/{}", surface.imagePtr,
                                                        surface.dimension, surface.firstMip, surface.mipCount);
                        break;
                    }
                    const auto &base = snapshot->levels.front();
                    textures.push_back({binding, base.width, base.height, boundSamplers[binding.sampler],
                                        [](unsigned, unsigned) { return std::array<float, 4>{}; }});
                    textures.back().rgba32 = snapshot->texels.front();
                    textures.back().mips = snapshot->mips;
                    textures.back().layers = surface.sliceCount;
                    textures.back().sourceMipCount = snapshot->levelCount;
                    textures.back().snapshotOwner = snapshot;
                    mipTextures.push_back(std::move(snapshot));
                    continue;
                }
                auto depthKey = makeSurfaceKey(surface);
                depthKey.format = surface.format;
                if (m_depthBackings.contains(depthKey)) {
                    auto *depthTexture = depthBacking(surface, !residentTextures);
                    if (!depthTexture) {
                        supported = false;
                        unsupportedReason = "replayer#2";
                        break;
                    }
                    textures.push_back({binding, surface.width, surface.height, boundSamplers[binding.sampler], [&, surface](unsigned x, unsigned y) {
                                            return sampleTexture(surface, TextureSampler{}, (float(x) + .5f) / float(surface.width),
                                                                 (float(y) + .5f) / float(surface.height));
                                        }});
                    if (residentTextures && depthTexture->pending) {
                        textures.back().rendered = depthTexture->pending;
                        textures.back().renderedDepth = true;
                        textures.back().unorm8Pitch = surface.pitch;
                        textures.back().unorm8Map = surface.compMap;
                        textures.back().r32Map = surface.compMap;
                        continue;
                    }
                    if (std::endian::native == std::endian::little) {
                        textures.back().r32Bytes = {reinterpret_cast<const std::uint8_t *>(depthTexture->pixels.data()),
                                                    depthTexture->pixels.size() * sizeof(float)};
                        textures.back().r32Pitch = surface.pitch;
                        if (!depthTexture->version)
                            depthTexture->version = std::make_shared<int>();
                        textures.back().snapshotOwner = depthTexture->version;
                    } else {
                        auto &packed = ownedFloatTextures.emplace_back();
                        for (unsigned y = 0; y < surface.height; ++y)
                            packed.insert(packed.end(), depthTexture->pixels.data() + std::size_t(y) * surface.pitch,
                                          depthTexture->pixels.data() + std::size_t(y) * surface.pitch + surface.width);
                        textures.back().r32 = packed;
                    }
                    textures.back().r32Map = surface.compMap;
                    continue;
                }
                if (shader.usesGather && !supportsGather(surface, boundSamplers[binding.sampler])) {
                    supported = false;
                    unsupportedReason = "replayer#3";
                    break;
                }
                if (surface.dimension == 5) {
                    auto snapshot = arraySnapshot(surface);
                    if (!snapshot) {
                        supported = false;
                        unsupportedReason =
                                std::format("replayer#4 ptr={:08X} {}x{} pitch={} depth={} slices={}+{} fmt={:X} tile={} size={:X}", // TEMP-ARRAYDIAG
                                            surface.imagePtr, surface.width, surface.height, surface.pitch, surface.depth, surface.firstSlice,
                                            surface.sliceCount, surface.format, surface.tileMode, surface.imageSize);
                        for (const auto &[key, backing]: m_depthBackings) // TEMP-ARRAYDIAG
                            if (key.imagePtr >= surface.imagePtr && key.imagePtr < surface.imagePtr + surface.imageSize)
                                unsupportedReason += std::format(" depth@{:08X}:{}x{}:fmt{:X}:pending={}", key.imagePtr, key.width, key.height,
                                                                 key.format, bool(backing.pending));
                        break;
                    }
                    textures.push_back({binding, surface.width, surface.height, boundSamplers[binding.sampler], [&, surface](unsigned x, unsigned y) {
                                            return sampleTexture(surface, TextureSampler{}, (float(x) + 0.5f) / float(surface.width),
                                                                 (float(y) + 0.5f) / float(surface.height), 0, nullptr);
                                        }});
                    textures.back().unorm8 = snapshot->rgba;
                    textures.back().unorm8Pitch = surface.width;
                    textures.back().unorm8Map = surface.compMap;
                    textures.back().layers = surface.sliceCount;
                    // Snapshots are immutable (changed sources get a new one), so backends may
                    // share one upload across draws instead of comparing the texels every draw.
                    textures.back().snapshotOwner = snapshot;
                    arrays.push_back(std::move(snapshot));
                    continue;
                }
                if (surface.dimension == 2) {
                    auto volume = volumeTexels(surface);
                    if (volume.empty()) {
                        supported = false;
                        unsupportedReason = std::format("3D texture ptr={:08X} {}x{}x{} fmt={:X} tile={}", surface.imagePtr, surface.width,
                                                        surface.height, surface.depth, surface.format, surface.tileMode);
                        break;
                    }
                    textures.push_back({binding, surface.width, surface.height, boundSamplers[binding.sampler], [&, surface](unsigned x, unsigned y) {
                                            return sampleTexture(surface, TextureSampler{}, (float(x) + .5f) / float(surface.width),
                                                                 (float(y) + .5f) / float(surface.height));
                                        }});
                    textures.back().rgba32 = ownedFloatTextures.emplace_back(std::move(volume));
                    textures.back().layers = surface.depth;
                    continue;
                }
                if (!surface.imagePtr && !surface.width && !surface.height) {
                    // Unbound slot: sampleTexture() returns zero texels through the
                    // component selectors, so a 1x1 zero texture matches the reference.
                    textures.push_back({binding, 1, 1, boundSamplers[binding.sampler],
                                        [&, surface](unsigned, unsigned) { return sampleTexture(surface, TextureSampler{}, .5f, .5f); }});
                    textures.back().unorm8 = ownedTextures.emplace_back(4, std::uint8_t{0});
                    textures.back().unorm8Pitch = 1;
                    textures.back().unorm8Map = surface.compMap;
                    continue;
                }
                if (!surface.imagePtr && surface.dimension == 1 && !surface.aa && !surface.firstSlice && surface.sliceCount == 1 &&
                    !m_viewBackings.contains(makeSurfaceKey(surface))) {
                    // A sized view of a null image also reads zero texels everywhere (no guest memory
                    // backs it). Keep its size so border addressing filters like the reference.
                    // MK8's GamePad loading screen samples one every frame.
                    const auto bytes = std::size_t(surface.width) * surface.height * 4;
                    if (!m_zeroTexels || m_zeroTexels->size() < bytes)
                        m_zeroTexels = std::make_shared<const std::vector<std::uint8_t>>(bytes, std::uint8_t{0});
                    textures.push_back({binding, surface.width, surface.height, boundSamplers[binding.sampler],
                                        [&, surface](unsigned, unsigned) { return sampleTexture(surface, TextureSampler{}, .5f, .5f); }});
                    textures.back().unorm8 = std::span(*m_zeroTexels).first(bytes);
                    textures.back().unorm8Pitch = surface.width;
                    textures.back().unorm8Map = surface.compMap;
                    textures.back().snapshotOwner = m_zeroTexels;
                    continue;
                }
                if (!surface.imagePtr || !surface.width || !surface.height || surface.dimension != 1 || surface.aa || surface.firstMip ||
                    surface.firstSlice || surface.sliceCount != 1) {
                    supported = false;
                    unsupportedReason = std::format("texture view ptr={:08X} {}x{} dim={} aa={} mip={}/{} slice={}/{} fmt={:X} tile={}",
                                                    surface.imagePtr, surface.width, surface.height, surface.dimension, surface.aa, surface.firstMip,
                                                    surface.mipCount, surface.firstSlice, surface.sliceCount, surface.format, surface.tileMode);
                    break;
                }
                const auto *backing = vertexBinding ? textureBacking(surface) : pixelBackings[binding.resource];
                const auto format = surface.format & 0x3F;
                // Match sample()'s four-byte representation, with typed R32 float
                // conversion below. Other float/integer formats remain incomplete.
                const bool rawBytes = format != 8 && (format < 0x31 || format > 0x35);
                const unsigned texelBytes = format == 1 ? 1 : format == 7 ? 2 : 4;
                textures.push_back(
                        {binding, surface.width, surface.height, boundSamplers[binding.sampler], [&, surface, backing](unsigned x, unsigned y) {
                             if (x >= surface.width || y >= surface.height)
                                 throw std::out_of_range("Raster backend texel coordinate");
                             return sampleTexture(surface, TextureSampler{}, (float(x) + 0.5f) / float(surface.width),
                                                  (float(y) + 0.5f) / float(surface.height), 0, backing);
                         }});
                if (colorFormat(surface.format) != ColorFormat::RGBA8) {
                    textures.back().renderedFormat = colorFormat(surface.format);
                    textures.back().unorm8Map = surface.compMap;
                    textures.back().unorm8Pitch = surface.pitch;
                    const auto pending = m_pendingReadbacks.find(makeSurfaceKey(surface));
                    if (residentTextures && pending != m_pendingReadbacks.end())
                        textures.back().rendered = pending->second;
                    else if (backing) {
                        resolveViewBacking(surface);
                        if (surface.format == 0x816) {
                            textures.back().r11g11b10Bytes = *backing;
                            textures.back().r11g11b10Pitch = surface.pitch;
                        } else {
                            textures.back().rgba32Bytes = *backing;
                            textures.back().rgba32Pitch = surface.pitch;
                        }
                        textures.back().snapshotOwner = backingOwner(makeSurfaceKey(surface));
                    } else if (const auto decoded = decodedTexture(surface, false, true)) {
                        // Guest float textures are decoded once and shared until their bytes change.
                        textures.back().rgba32 = decoded->floating;
                        textures.back().snapshotOwner = decoded;
                    } else {
                        auto &values = ownedFloatTextures.emplace_back();
                        for (unsigned y = 0; y < surface.height; ++y)
                            for (unsigned x = 0; x < surface.width; ++x) {
                                const auto value = sampleTexture(surface, TextureSampler{}, (float(x) + .5f) / float(surface.width),
                                                                 (float(y) + .5f) / float(surface.height));
                                values.insert(values.end(), value.begin(), value.end());
                            }
                        textures.back().rgba32 = values;
                    }
                    continue;
                }
                if (residentTextures) {
                    const auto pending = m_pendingReadbacks.find(makeSurfaceKey(surface));
                    if (pending != m_pendingReadbacks.end()) {
                        textures.back().rendered = pending->second;
                        textures.back().unorm8Pitch = surface.pitch;
                        textures.back().unorm8Map = surface.compMap;
                        continue;
                    }
                }
                if (format == 0x05 && !backing && surface.imageSize && m_mem && (surface.tileMode <= 4 || surface.tileMode == 16) &&
                    surface.pitch >= surface.width && std::uint64_t(surface.imagePtr) + surface.imageSize <= 0x100000000ull) {
                    // R16_UNORM (also how 16-bit depth is viewed as a texture): decode to R32
                    // floats, matching sample()'s (R, 0, 0, 1) reference expansion.
                    const auto *base = m_mem->hostPtr(surface.imagePtr);
                    const auto *last = m_mem->hostPtr(surface.imagePtr + surface.imageSize - 1);
                    if (base && last && std::uintptr_t(last) - std::uintptr_t(base) == surface.imageSize - 1) {
                        auto &values = ownedFloatTextures.emplace_back(std::size_t(surface.width) * surface.height);
                        const bool linear = surface.tileMode <= 1 || surface.tileMode == 16;
                        bool complete = true;
                        for (unsigned y = 0; y < surface.height && complete; ++y)
                            for (unsigned x = 0; x < surface.width; ++x) {
                                const std::uint64_t offset = linear ? (std::uint64_t(y) * surface.pitch + x) * 2
                                                                    : tiledElementOffset(x, y, surface.pitch, 16, surface.tileMode, surface.swizzle);
                                if (offset + 2 > surface.imageSize) {
                                    complete = false;
                                    break;
                                }
                                values[std::size_t(y) * surface.width + x] = float(base[offset] | (base[offset + 1] << 8)) / 65535.0f;
                            }
                        if (complete) {
                            textures.back().r32 = values;
                            textures.back().r32Map = surface.compMap;
                            continue;
                        }
                    }
                }
                const bool linearView = surface.tileMode <= 1 || surface.tileMode == 16;
                if (!backing && (!linearView || format == 8 || (format >= 0x31 && format <= 0x35)) && surface.format != 0x80E) {
                    // Tiled or block-compressed guest textures: reuse the decoded base level.
                    const bool preferBlocks = !vertexBinding && m_rasterBackend->supportsBlockTextures() &&
                                              (surface.format == 0x31 || surface.format == 0x431 || surface.format == 0x34 ||
                                               surface.format == 0x234 || surface.format == 0x235);
                    auto decoded = decodedTexture(surface, preferBlocks);
                    if (!decoded && preferBlocks)
                        decoded = decodedTexture(surface);
                    if (decoded) {
                        if (!decoded->blocks.empty()) {
                            textures.back().blocks = decoded->blocks;
                            textures.back().blockFormat = surface.format == 0x234   ? TextureBlockFormat::BC4Signed
                                                          : surface.format == 0x235 ? TextureBlockFormat::BC5Signed
                                                          : format == 0x34          ? TextureBlockFormat::BC4
                                                                                    : TextureBlockFormat::BC1;
                            textures.back().unorm8Map = surface.compMap;
                        } else if (!decoded->floating.empty())
                            textures.back().rgba32 = decoded->floating;
                        else {
                            textures.back().unorm8 = decoded->rgba;
                            textures.back().unorm8Pitch = surface.width;
                            textures.back().unorm8Map = surface.compMap;
                            textures.back().unorm8Channels = 4;
                        }
                        textures.back().snapshotOwner = decoded;
                        decodedTextures.push_back(std::move(decoded));
                        continue;
                    }
                }
                if (backing && surface.pitch >= surface.width && std::uint64_t(surface.pitch) * surface.height * 4 <= backing->size()) {
                    textures.back().unorm8 = *backing;
                    textures.back().unorm8Pitch = surface.pitch;
                    textures.back().unorm8Map = surface.compMap;
                    textures.back().snapshotOwner = backingOwner(makeSurfaceKey(surface));
                } else if (rawBytes && surface.imageSize && (surface.tileMode <= 4 || surface.tileMode == 16) &&
                           std::uint64_t(surface.width) * surface.height * 4 <= 64ull * 1024 * 1024 &&
                           std::uint64_t(surface.imagePtr) + surface.imageSize <= 0x100000000ull && m_mem) {
                    const auto *base = m_mem->hostPtr(surface.imagePtr);
                    const auto *last = m_mem->hostPtr(surface.imagePtr + surface.imageSize - 1);
                    if (base && last && std::uintptr_t(last) - std::uintptr_t(base) == surface.imageSize - 1) {
                        if (surface.format != 0x80E && (surface.tileMode <= 1 || surface.tileMode == 16) && surface.pitch >= surface.width &&
                            std::uint64_t(surface.pitch) * surface.height * texelBytes <= surface.imageSize) {
                            textures.back().unorm8 = {base, std::size_t(surface.pitch) * surface.height * texelBytes};
                            textures.back().unorm8Pitch = surface.pitch;
                            textures.back().unorm8Map = surface.compMap;
                            textures.back().unorm8Channels = texelBytes;
                            continue;
                        }
                        if (surface.format == 0x80E && std::endian::native == std::endian::little &&
                            (surface.tileMode <= 1 || surface.tileMode == 16) && surface.pitch >= surface.width &&
                            std::uint64_t(surface.pitch) * surface.height * 4 <= surface.imageSize) {
                            textures.back().r32Bytes = {base, std::size_t(surface.pitch) * surface.height * 4};
                            textures.back().r32Pitch = surface.pitch;
                            textures.back().r32Map = surface.compMap;
                            continue;
                        }
                        std::vector<std::uint8_t> decoded(std::size_t(surface.width) * surface.height * texelBytes);
                        bool complete = true;
                        for (unsigned y = 0; y < surface.height && complete; ++y)
                            for (unsigned x = 0; x < surface.width; ++x) {
                                const auto offset = tiledElementOffset(x, y, surface.pitch, texelBytes * 8, surface.tileMode, surface.swizzle);
                                if (std::uint64_t(offset) + texelBytes > surface.imageSize) {
                                    complete = false;
                                    break;
                                }
                                auto *pixel = decoded.data() + (std::size_t(y) * surface.width + x) * texelBytes;
                                std::memcpy(pixel, base + offset, texelBytes);
                            }
                        if (complete) {
                            if (surface.format == 0x80E && std::endian::native == std::endian::little) {
                                ownedTextures.push_back(std::move(decoded));
                                textures.back().r32Bytes = ownedTextures.back();
                                textures.back().r32Pitch = surface.width;
                                textures.back().r32Map = surface.compMap;
                            } else if (surface.format == 0x80E) {
                                auto &values = ownedFloatTextures.emplace_back(decoded.size() / 4);
                                for (std::size_t i = 0; i < decoded.size(); i += 4) {
                                    values[i / 4] = decodeR32Float(decoded.data() + i, 0x00010203)[0];
                                }
                                textures.back().r32 = values;
                                textures.back().r32Map = surface.compMap;
                            } else {
                                ownedTextures.push_back(std::move(decoded));
                                textures.back().unorm8 = ownedTextures.back();
                                textures.back().unorm8Pitch = surface.width;
                                textures.back().unorm8Map = surface.compMap;
                                textures.back().unorm8Channels = texelBytes;
                            }
                        }
                    }
                }
            }
            for (auto &texture: textures) {
                const auto &surfaces = texture.binding.binding > 17 ? m_vertexTextures : m_pixelTextures;
                const auto &surface = surfaces[texture.binding.resource];
                if (!texture.binding.explicitLod)
                    texture.sourceMipCount = surface.imagePtr ? surface.mipCount : 1;
                texture.sourceFirstMip = surface.firstMip;
            }
            std::vector<RasterDraw::Vertex> vertices;
            vertices.reserve(preparedTriangles.size() * 3 + preparedPoints.size());
            // Triangles index their corners: a corner cached in corners/projectedCorners becomes one
            // vertex however many triangles share it; clipped corners stay private.
            std::vector<std::uint32_t> indices;
            indices.reserve(preparedTriangles.size() * 3);
            std::vector<std::uint32_t> cornerVertex(corners.size(), ~0u), projectedVertex(projectedCorners.size(), ~0u);
            const auto vertexSlot = [&](const RasterVertex *corner) -> std::uint32_t * {
                if (!corners.empty() && corner >= corners.data() && corner < corners.data() + corners.size())
                    return &cornerVertex[std::size_t(corner - corners.data())];
                if (!projectedCorners.empty() && corner >= projectedCorners.data() && corner < projectedCorners.data() + projectedCorners.size())
                    return &projectedVertex[std::size_t(corner - projectedCorners.data())];
                return nullptr;
            };
            if (directVertices && !directArrays)
                indices = std::move(directIndices);
            for (const auto &triangle: preparedTriangles) {
                const auto &a = triangle[0], &b = triangle[1], &c = triangle[2];
                const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                if (!nativeVertexShader && std::fabs(area) < 0.5f)
                    continue;
                for (const auto *corner: triangle.corners) {
                    auto *slot = vertexSlot(corner);
                    if (slot && *slot != ~0u) {
                        indices.push_back(*slot);
                        continue;
                    }
                    const auto &rv = *corner;
                    if (!std::isfinite(rv.x) || !std::isfinite(rv.y)) {
                        supported = false;
                        unsupportedReason = "replayer#6";
                    }
                    RasterDraw::Vertex vertex{rv.x, rv.y, {}};
                    vertex.z = rv.z;
                    vertex.reciprocalW = rv.reciprocalW;
                    if (nativeVertexShader)
                        std::copy_n(rv.params.begin(), vertex.inputs.size(), vertex.inputs.begin());
                    else
                        for (unsigned i = 0; i < vertex.inputs.size(); ++i)
                            if (pixelInputParams[i] >= 0)
                                vertex.inputs[i] = rv.params[pixelInputParams[i]];
                    if (slot)
                        *slot = std::uint32_t(vertices.size());
                    indices.push_back(std::uint32_t(vertices.size()));
                    vertices.push_back(vertex);
                }
            }
            for (const auto &rv: preparedPoints) {
                if (!std::isfinite(rv.x) || !std::isfinite(rv.y)) {
                    supported = false;
                    unsupportedReason = "replayer#7";
                    break;
                }
                RasterDraw::Vertex vertex{rv.x, rv.y, {}};
                vertex.z = rv.z;
                vertex.reciprocalW = rv.reciprocalW;
                if (nativeVertexShader)
                    std::copy_n(rv.params.begin(), vertex.inputs.size(), vertex.inputs.begin());
                else
                    for (unsigned i = 0; i < vertex.inputs.size(); ++i)
                        if (pixelInputParams[i] >= 0)
                            vertex.inputs[i] = rv.params[pixelInputParams[i]];
                vertices.push_back(vertex);
            }
            if (std::getenv("WEMU_SCAN_TRACE") && m_depthTest && !vertices.empty()) { // TEMP-SCANTRACE
                float stored = -1.f;
                if (auto *resolvedDepth = depthBacking(m_depth, true)) {
                    const auto px = std::clamp<long>(long(vertices[0].x), 0, long(m_depth.width) - 1);
                    const auto py = std::clamp<long>(long(vertices[0].y), 0, long(m_depth.height) - 1);
                    stored = resolvedDepth->pixels[std::size_t(py) * m_depth.pitch + std::size_t(px)];
                }
                Utils::Log::error("[DEPTHEQ] frame={} draw={} cmp={} write={} z0={:.9f} z1={:.9f} stored={:.9f} depthFmt={} native_vs={}",
                                  m_presentCount, m_stats.draws, m_depthCompare, m_depthWrite, vertices[0].z,
                                  vertices.size() > 1 ? vertices[1].z : 0.f, stored, int(drawDepth ? drawDepth->format : DepthFormat::Float32),
                                  bool(nativeVertexShader));
            }
            bool committed = false;
            if (profile || nativeAudit)
                nativePrepareUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - prepareStart).count();
            if (supported && (!vertices.empty() || !directAttributeView.empty())) {
                const auto targetBytes = std::size_t(m_color.pitch) * m_color.height * targetElementBytes;
                RasterDraw draw{shader,
                                vertices,
                                m_psRegs,
                                textures,
                                {target, targetBytes},
                                m_color.width,
                                m_color.height,
                                m_color.pitch,
                                {m_scissor[0], m_scissor[1], m_scissor[2], m_scissor[3]},
                                shader.exportsColor ? channelMask : 0u,
                                {targetBlendEnabled, blend.colorSrcBlend, blend.colorDstBlend, blend.colorCombine,
                                 blend.useAlphaBlend ? blend.alphaSrcBlend : blend.colorSrcBlend,
                                 blend.useAlphaBlend ? blend.alphaDstBlend : blend.colorDstBlend,
                                 blend.useAlphaBlend ? blend.alphaCombine : blend.colorCombine, m_blendConstant}};
                std::vector<float> pixelUniforms;
                pixelUniforms.reserve(shader.uniforms.size() * 4);
                for (const auto &reference: shader.uniforms)
                    for (unsigned c = 0; c < 4; ++c)
                        pixelUniforms.push_back(pixelConstants(reference.bank, reference.index, c));
                draw.uniforms = pixelUniforms;
                std::vector<float> vertexUniforms;
                if (nativeVertexShader) {
                    static const unsigned bias = [] {
                        const auto *v = std::getenv("WEMU_KCACHE_BIAS");
                        return v ? unsigned(std::atoi(v)) : 0u;
                    }();
                    for (const auto &reference: nativeVertexShader->uniforms)
                        for (unsigned c = 0; c < 4; ++c) {
                            const auto bank = reference.bank + bias;
                            const auto index = std::size_t(reference.index) * 4 + c;
                            vertexUniforms.push_back(bank < m_vsBlocks.size() && index < m_vsBlocks[bank].size() ? asFloat(m_vsBlocks[bank][index])
                                                                                                                 : 0.f);
                        }
                    draw.vertexStage.shader = nativeVertexShader;
                    draw.vertexStage.constants = m_vsRegs;
                    draw.vertexStage.uniforms = vertexUniforms;
                    std::copy_n(pixelInputParams.begin(), draw.vertexStage.inputParams.size(), draw.vertexStage.inputParams.begin());
                    draw.vertexStage.inputCount = m_pixelInputCount;
                    if (directVertices) {
                        draw.vertexStage.attributes = directAttributeView;
                        // Only arrays reused at least once are worth keeping on the GPU.
                        if (directArrays && directReused)
                            draw.vertexStage.attributesOwner = directArrays;
                        draw.vertexStage.attributeStride = std::max(1u, attributeStride);
                    }
                    draw.vertexStage.viewport = {m_vp[0], m_vp[1], m_vp[2], m_vp[3], m_vpNear, m_vpFar};
                }
                draw.enabledColorTargets = writableColors;
                draw.cullFront = m_polygonControl & 1;
                draw.cullBack = m_polygonControl & 2;
                draw.frontFaceClockwise = m_polygonControl & 4;
                if (drawDepth && nativeBias && mode != 1) {
                    const auto index = useBackBias ? 2u : 0u;
                    draw.depthBias = {true, m_polygonOffset[index], m_polygonOffset[index + 1], m_polygonOffset[4]};
                }
                draw.colorFormat = targetFormat;
                draw.topology = mode == 1 ? RasterDraw::Topology::Points : RasterDraw::Topology::Triangles;
                if (mode != 1)
                    draw.indices = directArrays ? directIndexView : std::span<const std::uint32_t>(indices);
                draw.renderedTarget = renderedTarget;
                // The result replaces m_pendingReadbacks for m_color; verification re-reads the old version.
                draw.supersedesTarget = !m_nativeVerifyEvery;
                if (drawDepth)
                    draw.depth = {true,          m_depthWrite,   m_depthCompare, drawDepth->format, drawDepth->pixels,
                                  m_depth.width, m_depth.height, m_depth.pitch,  drawDepth->pending};
                static const bool deferReadback = [] {
                    const auto *value = std::getenv("WEMU_NATIVE_DEFER_READBACK");
                    return value && std::string_view(value) == "1";
                }();
                const auto executeStart = (profile || nativeAudit) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                if (profile || nativeAudit)
                    nativePrepareUs = std::chrono::duration_cast<std::chrono::microseconds>(executeStart - prepareStart).count();
                auto result = deferReadback ? m_rasterBackend->renderDeferred(draw) : m_rasterBackend->render(draw);
                if (profile || nativeAudit)
                    nativeExecuteUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - executeStart).count();
                if (result) {
                    if (result->size() != targetBytes)
                        throw std::runtime_error("Raster backend returned an incomplete target");
                    if (drawDepth && (m_depthWrite || result->depthReadback) &&
                        (result->depthReadback ? result->depthReadback->size() != drawDepth->pixels.size() * sizeof(float)
                                               : result->depth.size() != drawDepth->pixels.size()))
                        throw std::runtime_error("Raster backend returned an incomplete depth surface");
                    static const std::uint64_t nativeVerifyEnvironment = [] {
                        const auto *value = std::getenv("WEMU_NATIVE_VERIFY_EVERY");
                        if (!value || *value < '0' || *value > '9')
                            return std::uint64_t(0);
                        char *end = nullptr;
                        const auto interval = std::strtoull(value, &end, 10);
                        return end != value && *end == '\0' ? std::uint64_t(interval) : std::uint64_t(0);
                    }();
                    const auto interval = m_nativeVerifyEvery.value_or(nativeVerifyEnvironment);
                    // WEMU_NATIVE_VERIFY_FROM=N limits verification to presents >= N.
                    static const std::uint32_t verifyFrom = [] {
                        const auto *value = std::getenv("WEMU_NATIVE_VERIFY_FROM");
                        return value ? std::uint32_t(std::strtoul(value, nullptr, 10)) : 0u;
                    }();
                    static const int verifyFrame = [] {
                        const auto *v = std::getenv("WEMU_NATIVE_VERIFY_FRAME");
                        return v ? std::atoi(v) : -1;
                    }();
                    static const int verifyDraw = [] {
                        const auto *v = std::getenv("WEMU_NATIVE_VERIFY_DRAW");
                        return v ? std::atoi(v) : -1;
                    }();
                    static const auto verifyShader = []() -> std::optional<std::uint32_t> {
                        const auto *value = std::getenv("WEMU_NATIVE_VERIFY_PS_HASH");
                        return value ? std::optional{std::uint32_t(std::strtoul(value, nullptr, 0))} : std::nullopt;
                    }();
                    const bool compare = interval && m_presentCount >= verifyFrom && m_nativeDraws % interval == 0 &&
                                         (verifyFrame < 0 || m_presentCount == unsigned(verifyFrame)) &&
                                         (verifyDraw < 0 || m_stats.draws == unsigned(verifyDraw)) &&
                                         (!verifyShader || shaderHash(m_psProgram) == *verifyShader);
                    ++m_nativeDraws;
                    if (compare) {
                        const auto verifyStart =
                                (profile || nativeAudit) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                        // Verification compares the whole target, including pixels
                        // untouched by clipped geometry. Start from the actual
                        // pre-draw GPU version even when software covers no pixels.
                        prepareSoftwareInputs();
                        result->resolve();
                        result->resolveDepth();
                        // The backend has not changed target; feedback snapshots and
                        // prepared vertices are shared by both executions of this draw.
                        const auto savedStats = m_stats;
                        for (const auto &triangle: preparedTriangles) {
                            const RasterVertex rv[]{triangle[0], triangle[1], triangle[2]};
                            rasterPrepared(rv);
                        }
                        for (const auto &point: preparedPoints)
                            rasterPoint(point);
                        m_stats = savedStats;
                        if (drawDepth && m_depthWrite) {
                            unsigned differences{};
                            float maximum{};
                            for (std::size_t i = 0; i < result->depth.size(); ++i) {
                                const auto error = std::fabs(drawDepth->pixels[i] - result->depth[i]);
                                differences += error != 0.f;
                                maximum = std::max(maximum, error);
                            }
                            Utils::Log::error("[NATIVE_DEPTH_VERIFY] frame={} draw={} different={} max_error={}", m_presentCount, m_stats.draws,
                                              differences, maximum);
                        }
                        if (targetFormat != ColorFormat::RGBA8) {
                            unsigned differences{};
                            float maximum{};
                            for (std::size_t i = 0; i < targetBytes; i += targetElementBytes) {
                                const auto a = readFloatColor(target + i, targetFormat);
                                const auto b = readFloatColor(result->rgba.data() + i, targetFormat);
                                for (unsigned c = 0; c < 4; ++c) {
                                    const float reference = a[c], native = b[c];
                                    const auto error = reference == native || (std::isnan(reference) && std::isnan(native)) ? 0.f
                                                       : std::isfinite(reference) && std::isfinite(native) ? std::abs(reference - native)
                                                                                                           : std::numeric_limits<float>::infinity();
                                    differences += error > (std::isfinite(reference) ? 1e-5f * std::max(1.f, std::abs(reference)) : 0.f);
                                    maximum = std::max(maximum, error);
                                }
                            }
                            if (const auto *directory = std::getenv("WEMU_NATIVE_VERIFY_DIR")) {
                                for (const auto native: {false, true}) {
                                    const auto path = std::format("{}/verify_{}_frame{}_draw{}.ppm", directory, native ? "native" : "reference",
                                                                  m_presentCount, m_stats.draws);
                                    if (FILE *file = std::fopen(path.c_str(), "wb")) {
                                        std::fprintf(file, "P6\n%u %u\n255\n", m_color.width, m_color.height);
                                        const auto *pixels = native ? result->rgba.data() : target;
                                        for (unsigned y = 0; y < m_color.height; ++y)
                                            for (unsigned x = 0; x < m_color.width; ++x) {
                                                const auto value = readFloatColor(pixels + (std::size_t(y) * m_color.pitch + x) * targetElementBytes,
                                                                                  targetFormat);
                                                const std::array<std::uint8_t, 3> rgb{byteFromUnit(value[0]), byteFromUnit(value[1]),
                                                                                      byteFromUnit(value[2])};
                                                std::fwrite(rgb.data(), 1, rgb.size(), file);
                                            }
                                        std::fclose(file);
                                    }
                                }
                            }
                            ++m_comparedNativeDraws;
                            m_differingNativeDraws += differences != 0;
                            Utils::Log::error("[NATIVE_FLOAT_VERIFY] frame={} draw={} different={} max_error={}", m_presentCount, m_stats.draws,
                                              differences, maximum);
                        } else {
                            std::size_t different{}, overOne{}, first = targetBytes, worst = targetBytes;
                            std::array<std::size_t, 4> channelDifferences{};
                            std::array<std::size_t, 4> channelOverOne{};
                            unsigned maxError{};
                            for (std::size_t i = 0; i < targetBytes; ++i) {
                                const auto error = unsigned(std::abs(int(target[i]) - int(result->rgba[i])));
                                if (error) {
                                    ++different;
                                    first = std::min(first, i);
                                    ++channelDifferences[i % 4];
                                }
                                overOne += error > 1;
                                channelOverOne[i % 4] += error > 1;
                                if (error > maxError) {
                                    maxError = error;
                                    worst = i;
                                }
                            }
                            ++m_comparedNativeDraws;
                            m_differingNativeDraws += different != 0;
                            Utils::Log::error("[NATIVE_VERIFY] frame={} draw={} ps={:016X} bytes={} different={} over_one={} max_error={} first={}",
                                              m_presentCount, m_stats.draws, shaderHash(m_psProgram), targetBytes, different, overOne, maxError,
                                              first);
                            if (different && (m_differingNativeDraws == 1 || maxError > m_nativeWorstError)) {
                                m_nativeWorstError = std::max(m_nativeWorstError, maxError);
                                const auto pixel = (worst / 4) * 4;
                                Utils::Log::error("[NATIVE_VERIFY_DETAIL] x={} y={} ref=({},{},{},{}) native=({},{},{},{}) "
                                                  "channel_different=({},{},{},{}) channel_over_one=({},{},{},{}) vertices={} textures={}",
                                                  (pixel / 4) % m_color.pitch, (pixel / 4) / m_color.pitch, target[pixel], target[pixel + 1],
                                                  target[pixel + 2], target[pixel + 3], result->rgba[pixel], result->rgba[pixel + 1],
                                                  result->rgba[pixel + 2], result->rgba[pixel + 3], channelDifferences[0], channelDifferences[1],
                                                  channelDifferences[2], channelDifferences[3], channelOverOne[0], channelOverOne[1],
                                                  channelOverOne[2], channelOverOne[3], vertices.size(), textures.size());
                                for (const auto &texture: textures)
                                    Utils::Log::error("[NATIVE_VERIFY_TEXTURE] resource={} sampler={} size={}x{} map={:08X} "
                                                      "sampler0={:08X} snapshot={} r32={} format={:X}",
                                                      texture.binding.resource, texture.binding.sampler, texture.width, texture.height,
                                                      m_pixelTextures[texture.binding.resource].compMap, texture.sampler.regs[0],
                                                      !texture.unorm8.empty(), !texture.r32.empty(),
                                                      m_pixelTextures[texture.binding.resource].format);
                                Utils::Log::error("[NATIVE_VERIFY_BLEND] enabled={} color=({},{},{}) alpha=({},{},{}) mask={:X}", draw.blend.enabled,
                                                  draw.blend.colorSource, draw.blend.colorDestination, draw.blend.colorOperation,
                                                  draw.blend.alphaSource, draw.blend.alphaDestination, draw.blend.alphaOperation, draw.channelMask);
                                for (unsigned i = 0; i < std::min<std::size_t>(vertices.size(), 6); ++i)
                                    Utils::Log::error("[NATIVE_VERIFY_VERTEX] i={} pos=({:.5f},{:.5f}) param0=({:.5f},{:.5f},{:.5f},{:.5f})", i,
                                                      vertices[i].x, vertices[i].y, vertices[i].inputs[0][0], vertices[i].inputs[0][1],
                                                      vertices[i].inputs[0][2], vertices[i].inputs[0][3]);
                                if (const auto *dir = std::getenv("WEMU_NATIVE_VERIFY_DUMP_DIR")) {
                                    const auto dump = [&](const char *name, const std::uint8_t *rgba) {
                                        const auto path = std::format("{}/native_verify_{}_{}_{}.ppm", dir, m_presentCount, m_stats.draws, name);
                                        if (auto *file = std::fopen(path.c_str(), "wb")) {
                                            std::fprintf(file, "P6\n%u %u\n255\n", m_color.width, m_color.height);
                                            for (unsigned y = 0; y < m_color.height; ++y)
                                                for (unsigned x = 0; x < m_color.width; ++x) {
                                                    const auto offset = (std::size_t(y) * m_color.pitch + x) * 4;
                                                    std::fwrite(rgba + offset, 1, 3, file);
                                                }
                                            std::fclose(file);
                                        }
                                    };
                                    dump("software", target);
                                    dump("native", result->rgba.data());
                                }
                            }
                        }
                        if (profile || nativeAudit)
                            nativeVerifyUs =
                                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - verifyStart).count();
                    }
                    if (result->readback)
                        m_pendingReadbacks[makeSurfaceKey(m_color)] = std::move(result->readback);
                    else {
                        m_pendingReadbacks.erase(makeSurfaceKey(m_color));
                        std::copy(result->rgba.begin(), result->rgba.end(), target);
                    }
                    // Backends may also return a resident copy of a depth surface they only read.
                    if (drawDepth && (m_depthWrite || result->depthReadback)) {
                        drawDepth->pending = std::move(result->depthReadback);
                        if (!drawDepth->pending)
                            std::copy(result->depth.begin(), result->depth.end(), drawDepth->pixels.begin());
                    }
                    if (mode == 1)
                        m_stats.points += vertices.size();
                    else
                        m_stats.tris += (draw.indices.empty() ? vertices.size() : draw.indices.size()) / 3;
                    m_stats.pixels += result->pixels;
                    m_stats.alphaSum += result->alphaSum;
                    committed = true;
                }
            }
            if (!committed) {
                const auto fallbackStart = (profile || nativeAudit) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                if (directVertices)
                    assembleTriangles();
                for (const auto &triangle: preparedTriangles) {
                    const RasterVertex rv[]{triangle[0], triangle[1], triangle[2]};
                    rasterPrepared(rv);
                }
                for (const auto &point: preparedPoints)
                    rasterPoint(point);
                if (profile || nativeAudit)
                    fallbackUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - fallbackStart).count();
            }
            nativeCommitted = committed;
            if (profile || nativeAudit)
                Utils::Log::error(
                        "[NATIVERASTER] frame={} draw={} committed={} supported={} vertices={} reason={}", m_presentCount, m_stats.draws, committed,
                        supported, vertices.size(),
                        committed                    ? std::string{}
                        : !unsupportedReason.empty() ? unsupportedReason
                                                     : [&] {
                                                           const auto *vulkan = dynamic_cast<const VulkanRasterBackend *>(m_rasterBackend.get());
                                                           return vulkan ? vulkan->lastError() : std::string{};
                                                       }());
        }
        // GPU path: flush this draw's collected triangles with its texture (one draw call, one
        // texture upload — cached by address+content, so a static texture uploads once).
        if (m_gpuRenderer && !m_gpuTri.empty()) {
            const std::uint32_t verts = static_cast<std::uint32_t>(m_gpuTri.size() / 4);
            // Graph mode: if the bound texture is itself a colour target rendered this frame, sample
            // that GPU image directly (render-to-texture) instead of decoding a CPU copy of it.
            if (m_gpuGraph && m_gpuRenderer->gpuIsTarget(m_texture.imagePtr)) {
                m_gpuRenderer->gpuDrawTarget(m_texture.imagePtr, m_gpuTri.data(), verts);
            } else {
                // Decode once per texture address and reuse; the GPU-side upload cache then also skips
                // re-uploading since the pixels are byte-identical frame to frame.
                auto it = m_texCache.find(m_texture.imagePtr);
                if (it == m_texCache.end()) {
                    if (m_texCache.size() > 512)
                        m_texCache.clear(); // bound the cache for titles that cycle through many surfaces
                    DecodedTex d;
                    decodeTextureToRgba(m_texture, d.rgba, d.w, d.h);
                    it = m_texCache.emplace(m_texture.imagePtr, std::move(d)).first;
                }
                if (m_gpuGraph)
                    m_gpuRenderer->gpuDrawTexture(m_texture.imagePtr, it->second.rgba.data(), it->second.w, it->second.h, m_gpuTri.data(), verts);
                else
                    m_gpuRenderer->gpuDrawTriangles(m_texture.imagePtr, it->second.rgba.data(), it->second.w, it->second.h, m_gpuTri.data(), verts);
            }
            m_gpuTri.clear();
        }
        if (m_trace && shouldDumpAfterDraw(m_stats.draws)) {
            char kind[32];
            std::snprintf(kind, sizeof(kind), "after_draw%03u", m_stats.draws);
            dumpSurface(m_color, kind, true);
        }
        { // TEMP-SCANTRACE: luminance of the bound target after each draw on selected frames
            static const char *frames = std::getenv("WEMU_DRAW_LUMA_FRAMES");
            if (frames && std::strstr(frames, std::format(",{},", m_presentCount).c_str())) {
                if (const auto *backing = findViewBacking(m_color)) {
                    std::uint64_t sum = 0, n = 0;
                    for (std::size_t i = 0; i + 3 < backing->size(); i += 64) {
                        sum += (*backing)[i] + (*backing)[i + 1] + (*backing)[i + 2];
                        n += 3;
                    }
                    std::string bound;
                    for (unsigned t = 0; t < 4 && t < m_pixelTextures.size(); ++t)
                        bound += std::format(" t{}={:08X}{}[{}x{} f{:X} t{} d{} m{}]", t, m_pixelTextures[t].imagePtr,
                                             m_pendingReadbacks.contains(makeSurfaceKey(m_pixelTextures[t])) ? "p" : "", m_pixelTextures[t].width,
                                             m_pixelTextures[t].height, m_pixelTextures[t].format, m_pixelTextures[t].tileMode,
                                             m_pixelTextures[t].dimension, m_pixelTextures[t].mipCount);
                    Utils::Log::error(
                            "[LUMA] frame={} draw={} image={:08X} mean={:.2f} native={}{} vp=({},{},{},{}) sc=({},{},{},{}) mask={:X} blend={}",
                            m_presentCount, m_stats.draws, m_color.imagePtr, n ? double(sum) / double(n) : 0.0, nativeCommitted, bound, m_vp[0],
                            m_vp[1], m_vp[2], m_vp[3], m_scissor[0], m_scissor[1], m_scissor[2], m_scissor[3], channelMask, targetBlendEnabled);
                    Utils::Log::error("[LUMA]   depth test={} write={} cmp={} image={:08X} {}x{} stencil={} vpz=({},{})", m_depthTest, m_depthWrite,
                                      m_depthCompare, m_depth.imagePtr, m_depth.width, m_depth.height, m_stencilTest, m_vpNear, m_vpFar);
                    for (std::size_t i = 0; i + 7 < m_fetchLayout.size(); i += 8)
                        Utils::Log::error("[LUMA]   fetch loc={} buf={} off={} fmt={:X} type={} mask={:08X} endian={} stride={}", m_fetchLayout[i],
                                          m_fetchLayout[i + 1], m_fetchLayout[i + 2], m_fetchLayout[i + 3], m_fetchLayout[i + 4],
                                          m_fetchLayout[i + 6], m_fetchLayout[i + 7],
                                          m_fetchLayout[i + 1] < m_attribs.size() ? m_attribs[m_fetchLayout[i + 1]].stride : 0);
                    for (std::uint32_t v = 0; v < 2; ++v) {
                        std::array<std::array<float, 4>, 4> a{};
                        if (fetchAttributes(v, a))
                            Utils::Log::error(
                                    "[LUMA]   v{} a0=({:.3f},{:.3f},{:.3f},{:.3f}) a1=({:.3f},{:.3f},{:.3f},{:.3f}) a2=({:.3f},{:.3f},{:.3f},{:.3f})",
                                    v, a[0][0], a[0][1], a[0][2], a[0][3], a[1][0], a[1][1], a[1][2], a[1][3], a[2][0], a[2][1], a[2][2], a[2][3]);
                    }
                    std::string ps;
                    for (std::size_t c = 0; c < 8; ++c)
                        ps += std::format(" c{}=({:.3f},{:.3f},{:.3f},{:.3f})", c, m_psRegs[c * 4], m_psRegs[c * 4 + 1], m_psRegs[c * 4 + 2],
                                          m_psRegs[c * 4 + 3]);
                    Utils::Log::error("[LUMA]   psregs{} psblocks={}", ps, m_psBlocks.size());
                    if (const char *dir = std::getenv("WEMU_FB_DUMP_DIR");
                        dir && m_color.pitch && colorElementBytes(colorFormat(m_color.format)) == 4) {
                        const auto path = std::format("{}/draw_{:05}_{:03}_{:08X}.ppm", dir, m_presentCount, m_stats.draws, m_color.imagePtr);
                        if (FILE *f = std::fopen(path.c_str(), "wb")) {
                            std::fprintf(f, "P6\n%u %u\n255\n", m_color.width, m_color.height);
                            for (std::uint32_t y = 0; y < m_color.height; ++y)
                                for (std::uint32_t x = 0; x < m_color.width; ++x) {
                                    const auto off = (std::size_t(y) * m_color.pitch + x) * 4;
                                    if (off + 3 < backing->size())
                                        std::fwrite(backing->data() + off, 1, 3, f);
                                    else
                                        std::fputc(0, f), std::fputc(0, f), std::fputc(0, f);
                                }
                            std::fclose(f);
                        }
                    }
                }
            }
        }
        m_regsFresh = false; // uniform regs are per-draw: the next draw needs its own set
        m_psRegsFresh = false;
        if (profile || nativeAudit) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - profileStart).count();
            std::uint64_t hash = 14695981039346656037ull;
            for (const auto word: m_psProgram) {
                for (unsigned shift = 0; shift < 32; shift += 8) {
                    hash ^= (word >> shift) & 255;
                    hash *= 1099511628211ull;
                }
            }
            Utils::Log::error("[RASTER] frame={} draw={} us={} pixels={} ps={:016X} words={} target={}x{} image={:08X} native={} stencil={} "
                              "depth={}:{:08X} aa={} fmt={:X}",
                              m_presentCount, m_stats.draws, elapsed, m_stats.pixels - pixelsBefore, hash, m_psProgram.size(), m_color.width,
                              m_color.height, m_color.imagePtr, nativeCommitted, m_stencilTest, m_depthTest, m_depth.imagePtr, m_color.aa,
                              m_color.format);
            Utils::Log::error("[RASTER_STAGES] frame={} draw={} native={} prepare_us={} execute_us={} verify_us={} fallback_us={} other_us={}",
                              m_presentCount, m_stats.draws, nativeCommitted, nativePrepareUs, nativeExecuteUs, nativeVerifyUs, fallbackUs,
                              elapsed - nativePrepareUs - nativeExecuteUs - nativeVerifyUs - fallbackUs);
            if (nativeAudit) {
                const auto auditKey = std::make_pair(m_psProgram, onlyColorZero);
                auto found = m_nativeAuditCache.find(auditKey);
                if (found == m_nativeAuditCache.end()) {
                    if (m_nativeAuditCache.size() >= 128)
                        m_nativeAuditCache.clear();
                    found = m_nativeAuditCache.emplace(auditKey, Latte::lowerFragmentShader(*Latte::decodeProgram(m_psProgram), {}, onlyColorZero))
                                    .first;
                    const auto &shader = found->second;
                    Utils::Log::error("[NATIVEPS] ps={:016X} lowered={} textures={} base_only={} reason={}", hash, bool(shader),
                                      shader.textures.size(), shader.requiresBaseLevelOnly, shader ? "supported shader subset" : shader.error);
                }
                const auto &shader = found->second;
                const auto &blend = m_blend[m_colorTarget & 7];
                Utils::Log::error(
                        "[NATIVEDRAW] frame={} draw={} ps={:016X} lowered={} target_fmt={:X} aa={} slot={} mask={:X} "
                        "blend={} color={},{},{} alpha={},{},{} scissor={},{},{},{}",
                        m_presentCount, m_stats.draws, hash, bool(shader), m_color.format, m_color.aa, m_colorTarget, channelMask, targetBlendEnabled,
                        blend.colorSrcBlend, blend.colorDstBlend, blend.colorCombine, blend.useAlphaBlend ? blend.alphaSrcBlend : blend.colorSrcBlend,
                        blend.useAlphaBlend ? blend.alphaDstBlend : blend.colorDstBlend,
                        blend.useAlphaBlend ? blend.alphaCombine : blend.colorCombine, m_scissor[0], m_scissor[1], m_scissor[2], m_scissor[3]);
                Utils::Log::error("[NATIVEDEPTH] frame={} draw={} image={:08X} size={}x{} pitch={} fmt={:X} test={} write={} compare={} "
                                  "stencil={} supported_view={} near={} far={}",
                                  m_presentCount, m_stats.draws, m_depth.imagePtr, m_depth.width, m_depth.height, m_depth.pitch, m_depth.format,
                                  m_depthTest, m_depthWrite, m_depthCompare, m_stencilTest, drawDepth != nullptr, m_vpNear, m_vpFar);
                for (const auto &binding: shader.textures) {
                    if (binding.resource >= m_pixelTextures.size() || binding.sampler >= m_pixelSamplers.size()) {
                        Utils::Log::error("[NATIVETEX] resource={} sampler={} out_of_range=1", binding.resource, binding.sampler);
                        continue;
                    }
                    const auto &texture = m_pixelTextures[binding.resource];
                    const auto &sampler = m_pixelSamplers[binding.sampler];
                    Utils::Log::error("[NATIVETEX] resource={} sampler={} size={}x{} dim={} fmt={:X} mip={}/{} slice={} aa={} "
                                      "feedback={} tile={} pitch={} image_bytes={} regs={:08X},{:08X},{:08X}",
                                      binding.resource, binding.sampler, texture.width, texture.height, texture.dimension, texture.format,
                                      texture.firstMip, texture.mipCount, texture.firstSlice, texture.aa,
                                      makeSurfaceKey(texture) == makeSurfaceKey(m_color), texture.tileMode, texture.pitch, texture.imageSize,
                                      sampler.regs[0], sampler.regs[1], sampler.regs[2]);
                }
            }
        }
    }

    Gx2Replayer::ContextState Gx2Replayer::captureContextState() const
    {
        ContextState state;
        state.color = m_color;
        state.colorBuffers = m_colorBuffers;
        state.depth = m_depth;
        state.depthTest = m_depthTest;
        state.depthWrite = m_depthWrite;
        state.stencilTest = m_stencilTest;
        state.depthCompare = m_depthCompare;
        state.colorTarget = m_colorTarget;
        state.pixelTextures = m_pixelTextures;
        state.vertexTextures = m_vertexTextures;
        state.pixelSamplers = m_pixelSamplers;
        state.vertexSamplers = m_vertexSamplers;
        state.blend = m_blend;
        state.blendConstant = m_blendConstant;
        state.channelMask = m_channelMask;
        state.targetBlendEnable = m_targetBlendEnable;
        state.colorWriteEnable = m_colorWriteEnable;
        state.multiWriteEnable = m_multiWriteEnable;
        state.polygonControl = m_polygonControl;
        state.polygonOffset = m_polygonOffset;
        state.rasterizerEnabled = m_rasterizerEnabled;
        state.zClipEnabled = m_zClipEnabled;
        state.halfZ = m_halfZ;
        std::copy(std::begin(m_vsRegs), std::end(m_vsRegs), state.vsRegs.begin());
        std::copy(std::begin(m_psRegs), std::end(m_psRegs), state.psRegs.begin());
        state.vsProgram = m_vsProgram;
        state.psProgram = m_psProgram;
        state.fetchLayout = m_fetchLayout;
        state.vsBlocks = m_vsBlocks;
        state.psBlocks = m_psBlocks;
        state.attribs = m_attribs;
        state.vertexSemantics = m_vertexSemantics;
        state.vertexInputCount = m_vertexInputCount;
        state.pixelInputCount = m_pixelInputCount;
        state.vertexOutputSemantics = m_vertexOutputSemantics;
        state.pixelInputSemantics = m_pixelInputSemantics;
        std::copy(std::begin(m_vp), std::end(m_vp), state.viewport.begin());
        state.viewportNear = m_vpNear;
        state.viewportFar = m_vpFar;
        std::copy(std::begin(m_scissor), std::end(m_scissor), state.scissor.begin());
        return state;
    }

    void Gx2Replayer::restoreContextState(const ContextState &state)
    {
        m_color = state.color;
        m_colorBuffers = state.colorBuffers;
        m_depth = state.depth;
        m_depthTest = state.depthTest;
        m_depthWrite = state.depthWrite;
        m_stencilTest = state.stencilTest;
        m_depthCompare = state.depthCompare;
        m_colorTarget = state.colorTarget;
        m_pixelTextures = state.pixelTextures;
        m_vertexTextures = state.vertexTextures;
        m_pixelSamplers = state.pixelSamplers;
        m_vertexSamplers = state.vertexSamplers;
        m_blend = state.blend;
        m_blendConstant = state.blendConstant;
        m_channelMask = state.channelMask;
        m_targetBlendEnable = state.targetBlendEnable;
        m_colorWriteEnable = state.colorWriteEnable;
        m_multiWriteEnable = state.multiWriteEnable;
        m_polygonControl = state.polygonControl;
        m_polygonOffset = state.polygonOffset;
        m_rasterizerEnabled = state.rasterizerEnabled;
        m_zClipEnabled = state.zClipEnabled;
        m_halfZ = state.halfZ;
        std::copy(state.vsRegs.begin(), state.vsRegs.end(), std::begin(m_vsRegs));
        std::copy(state.psRegs.begin(), state.psRegs.end(), std::begin(m_psRegs));
        m_vsProgram = state.vsProgram;
        m_psProgram = state.psProgram;
        m_fetchLayout = state.fetchLayout;
        m_vsBlocks = state.vsBlocks;
        m_psBlocks = state.psBlocks;
        m_attribs = state.attribs;
        m_vertexSemantics = state.vertexSemantics;
        m_vertexInputCount = state.vertexInputCount;
        m_pixelInputCount = state.pixelInputCount;
        m_vertexOutputSemantics = state.vertexOutputSemantics;
        m_pixelInputSemantics = state.pixelInputSemantics;
        std::copy(state.viewport.begin(), state.viewport.end(), std::begin(m_vp));
        m_vpNear = state.viewportNear;
        m_vpFar = state.viewportFar;
        std::copy(state.scissor.begin(), state.scissor.end(), std::begin(m_scissor));
    }

    void Gx2Replayer::setContextState(const std::uint32_t context)
    {
        // State written while a context was active belongs to it; a context seen for the first
        // time starts from the current registers (GX2SetupContextStateEx captures live state).
        if (std::getenv("WEMU_SCAN_TRACE")) // TEMP-SCANTRACE
            Utils::Log::error("[CTX] frame={} after_draw={} {:08X} -> {:08X} known={} depthTest={}", m_presentCount, m_stats.draws, m_activeContext,
                              context, m_contextStates.contains(context), m_depthTest);
        if (m_activeContext)
            m_contextStates[m_activeContext] = captureContextState();
        if (context)
            if (const auto found = m_contextStates.find(context); found != m_contextStates.end())
                restoreContextState(found->second);
        m_activeContext = context;
    }

    void Gx2Replayer::copyToScanBuffer(const Surface &original)
    {
        const auto s = attachmentSurface(original);
        m_stats.scanPtr = s.imagePtr;
        if (!s.imagePtr) {
            buildClearFrame();
            return;
        }
        const auto *backing = useViewBacking() ? findViewBacking(s) : nullptr;
        const std::uint8_t *base = backing ? backing->data() : nullptr;
        if (!base) {
            if (!m_mem) {
                buildClearFrame();
                return;
            }
            base = m_mem->hostPtr(s.imagePtr);
            if (!base || !m_mem->hostPtr(s.imagePtr + s.pitch * s.height * colorElementBytes(colorFormat(s.format)) - 1)) {
                buildClearFrame();
                return;
            }
        }
        const std::uint32_t rows = std::min(kHeight, s.height);
        const std::uint32_t cols = std::min(kWidth, s.width);
        buildClearFrame(cols, rows);
        if (colorFormat(s.format) != ColorFormat::RGBA8) {
            const auto format = colorFormat(s.format);
            for (unsigned y = 0; y < std::min(kHeight, s.height); ++y)
                for (unsigned x = 0; x < std::min(kWidth, s.width); ++x) {
                    const auto value = readFloatColor(base + (std::size_t(y) * s.pitch + x) * colorElementBytes(format), format);
                    for (unsigned c = 0; c < 4; ++c)
                        m_fb[(std::size_t(y) * kWidth + x) * 4 + c] = byteFromUnit(value[c]);
                }
            return;
        }
        for (std::uint32_t y = 0; y < rows; y++) {
            const std::uint8_t *src = base + static_cast<std::size_t>(y) * s.pitch * 4;
            std::uint8_t *dst = m_fb.data() + static_cast<std::size_t>(y) * kWidth * 4;
            std::memcpy(dst, src, static_cast<std::size_t>(cols) * 4);
            if ((y & 15) == 0) // nonzero telemetry: sample every 16th row (it's a health metric)
                for (std::uint32_t x = 0; x < cols * 4; x++)
                    m_stats.scanNonZero += (src[x] != 0);
        }
    }

    void Gx2Replayer::copyToDrcBuffer(const Surface &s)
    {
        // The scan-out engine scales the source to the 854x480 panel (titles often render the
        // GamePad view at another size), so resample instead of cropping like the TV path.
        m_drcFb.assign(static_cast<std::size_t>(kDrcWidth) * kDrcHeight * 4, 0);
        for (std::size_t i = 3; i < m_drcFb.size(); i += 4)
            m_drcFb[i] = 0xFF;
        if (!s.imagePtr || !s.width || !s.height || !s.pitch)
            return;
        const bool floatColor = s.format == 0x823;
        const std::size_t elementBytes = floatColor ? 16 : 4;
        const auto *backing = useViewBacking() ? findViewBacking(s) : nullptr;
        const std::uint8_t *base = backing ? backing->data() : nullptr;
        const std::size_t bytes = static_cast<std::size_t>(s.pitch) * s.height * elementBytes;
        if (backing && backing->size() < bytes)
            return;
        if (!base) {
            if (!m_mem || !(base = m_mem->hostPtr(s.imagePtr)) || !m_mem->hostPtr(s.imagePtr + bytes - 1))
                return;
        }
        for (std::uint32_t y = 0; y < kDrcHeight; y++) {
            const std::uint32_t sy = static_cast<std::uint32_t>(std::uint64_t(y) * s.height / kDrcHeight);
            std::uint8_t *dst = m_drcFb.data() + static_cast<std::size_t>(y) * kDrcWidth * 4;
            for (std::uint32_t x = 0; x < kDrcWidth; x++) {
                const std::uint32_t sx = static_cast<std::uint32_t>(std::uint64_t(x) * s.width / kDrcWidth);
                const std::uint8_t *src = base + (static_cast<std::size_t>(sy) * s.pitch + sx) * elementBytes;
                if (floatColor) {
                    const auto value = readFloatColor(src);
                    for (unsigned c = 0; c < 3; ++c)
                        dst[x * 4 + c] = byteFromUnit(value[c]);
                } else
                    std::memcpy(dst + x * 4, src, 3);
                dst[x * 4 + 3] = 0xFF;
            }
        }
    }

    void Gx2Replayer::dumpSurface(const Surface &s, const char *kind, const bool force)
    {
        if ((!force && !m_dumpSurfaces) || !m_mem || !s.imagePtr)
            return;
        static const std::uint32_t selectedImage = []() {
            const char *value = std::getenv("WEMU_SURFACE_DUMP_IMAGE");
            return value ? static_cast<std::uint32_t>(std::strtoul(value, nullptr, 0)) : 0u;
        }();
        if (!force && selectedImage && s.imagePtr != selectedImage)
            return;
        static std::vector<std::uint32_t> dumped;
        static const unsigned dumpLimit = [] {
            const auto *v = std::getenv("WEMU_SURFACE_DUMP_LIMIT");
            return v ? std::clamp(unsigned(std::strtoul(v, nullptr, 10)), 1u, 256u) : 24u;
        }();
        if (!force && (std::find(dumped.begin(), dumped.end(), s.imagePtr) != dumped.end() || (!selectedImage && dumped.size() > dumpLimit)))
            return;
        if (!force)
            dumped.push_back(s.imagePtr);
        const std::uint8_t *base = m_mem->hostPtr(s.imagePtr);
        if (!base || !m_mem->hostPtr(s.imagePtr + s.pitch * s.height * colorElementBytes(colorFormat(s.format)) - 1))
            return;
        const char *dir = std::getenv("WEMU_FB_DUMP_DIR");
        char path[512];
        std::snprintf(path, sizeof(path), "%s/surf_%s_%08X_%ux%u_f%02X_t%u.ppm", dir ? dir : ".", kind, s.imagePtr, s.width, s.height,
                      s.format & 0x3F, s.tileMode);
        if (FILE *f = std::fopen(path, "wb")) {
            std::fprintf(f, "P6\n%u %u\n255\n", s.width, s.height);
            for (std::uint32_t y = 0; y < s.height; y++)
                for (std::uint32_t x = 0; x < s.width; x++) {
                    // Render through sample() so BC decode etc. matches what draws would see.
                    const auto px = sample(s, (static_cast<float>(x) + 0.5f) / static_cast<float>(s.width),
                                           (static_cast<float>(y) + 0.5f) / static_cast<float>(s.height));
                    std::fwrite(px.data(), 1, 3, f);
                }
            std::fclose(f);
            Utils::Log::error("[GX2] surface dumped: {} pitch={} swizzle={:08X} compMap={:08X}", path, s.pitch, s.swizzle, s.compMap);
        }
    }

    void Gx2Replayer::maybeDumpFrame()
    {
        static const std::uint32_t dumpAt = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_FB_DUMP");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 0xFFFFFFFFu;
        }();
        static const std::uint32_t dumpEvery = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_FB_DUMP_EVERY");
            return env ? std::max(1ul, std::strtoul(env, nullptr, 10)) : 100u;
        }();
        // WEMU_FB_DUMP=0: refresh the dump every 100 presents (survives timeouts — the latest
        // frame is always on disk). Any other N dumps exactly present #N.
        // WEMU_FB_DUMP_FROM/TO bound the periodic dumps to a window of presents.
        static const std::uint32_t dumpFrom = [] {
            const char *env = std::getenv("WEMU_FB_DUMP_FROM");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 0u;
        }();
        static const std::uint32_t dumpTo = [] {
            const char *env = std::getenv("WEMU_FB_DUMP_TO");
            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 0xFFFFFFFFu;
        }();
        const bool periodic =
                dumpAt == 0 && m_presentCount > 0 && (m_presentCount % dumpEvery) == 0 && m_presentCount >= dumpFrom && m_presentCount <= dumpTo;
        if ((!periodic && m_presentCount != dumpAt) || m_fb.empty())
            return;
        const char *dir = std::getenv("WEMU_FB_DUMP_DIR");
        // WEMU_FB_DUMP_NUMBERED=1 keeps every periodic dump instead of overwriting one file.
        static const bool numbered = std::getenv("WEMU_FB_DUMP_NUMBERED") != nullptr;
        const std::string path = std::string(dir ? dir : ".") + (numbered ? std::format("/wemu_frame_{:05}.ppm", m_presentCount) : "/wemu_frame.ppm");
        if (FILE *f = std::fopen(path.c_str(), "wb")) {
            std::fprintf(f, "P6\n%u %u\n255\n", kWidth, kHeight);
            for (std::size_t i = 0; i < m_fb.size(); i += 4)
                std::fwrite(m_fb.data() + i, 1, 3, f);
            std::fclose(f);
            Utils::Log::error("[GX2] framebuffer #{} dumped to {}", m_presentCount, path);
        }
    }

    void Gx2Replayer::replay(const Gx2CommandStream &stream, Core::Memory *mem)
    {
        Profile::Scope replay(Profile::Stage::Replay);
        m_mem = mem;
        m_lastDrawColor = {};
        // GPU path: open a fresh GPU frame; draws append triangles, the present writes back to m_fb.
        if (m_gpuRenderer) {
            if (m_fb.size() != static_cast<std::size_t>(kWidth) * kHeight * 4)
                m_fb.assign(static_cast<std::size_t>(kWidth) * kHeight * 4, 0);
            if (m_gpuGraph)
                m_gpuRenderer->gpuBeginFrame();
            else
                m_gpuRenderer->gpuBegin();
            m_gpuScanAddr = 0;
            m_gpuTri.clear();
        }
        // WEMU_GX2_TRACE=0: auto-trace the first frame that binds a vertex uniform block and has a
        // full draw load (a real menu frame) — explicit frame numbers race the boot's variable pace.
        static const bool autoTrace = []() {
            const char *env = std::getenv("WEMU_GX2_TRACE");
            return env && env[0] == '0' && env[1] == '\0';
        }();
        static bool autoTraced = false;
        if (autoTrace && !autoTraced) {
            // WEMU_GX2_TRACE_MINDRAWS raises the bar so we can trace a genuinely rich menu/scene
            // frame (the steady post-process frame is ~36 draws) rather than the first busy-ish one.
            static const std::uint32_t minDraws = []() -> std::uint32_t {
                const char *e = std::getenv("WEMU_GX2_TRACE_MINDRAWS");
                return e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 10)) : 30u;
            }();
            // Trigger on draw count alone: the UI/menu quads use uniform REGISTERS, not blocks, so
            // requiring a block bind would miss the steady 36-draw UI frame we want to inspect.
            if (stream.drawCount() >= minDraws) {
                m_trace = true;
                m_dumpSurfaces = true;
                autoTraced = true;
            }
        }
        const auto traceSurfaceAlias = [&](const Surface &view, const char *kind) {
            static const bool enabled = []() {
                const char *e = std::getenv("WEMU_GX2_ALIAS_TRACE");
                return e && e[0] == '1';
            }();
            if (!enabled || !view.imagePtr)
                return;
            const auto it = m_cpuWrittenSurfaces.find(view.imagePtr);
            if (it == m_cpuWrittenSurfaces.end())
                return;
            const WrittenSurface &writtenInfo = it->second;
            const Surface &written = writtenInfo.surface;
            if (written.width == view.width && written.height == view.height && written.pitch == view.pitch &&
                (written.format & 0x3F) == (view.format & 0x3F) && written.tileMode == view.tileMode)
                return;
            const std::uint32_t frame = m_presentCount + 1;
            const std::uint64_t kindBit = (kind && kind[0] == 't') ? 1ull : 0ull;
            const std::uint64_t traceKey = (static_cast<std::uint64_t>(frame) << 33) ^ (static_cast<std::uint64_t>(view.imagePtr) << 1) ^ kindBit;
            if (!m_surfaceAliasTraced.insert(traceKey).second)
                return;
            Utils::Log::error("[GX2] alias view frame#{} {} image=0x{:08X}: written@frame#{} writes={} {}x{} pitch={} f{:02X} t{} -> view {}x{} "
                              "pitch={} f{:02X} t{}",
                              frame, kind, view.imagePtr, writtenInfo.frame, writtenInfo.writes, written.width, written.height, written.pitch,
                              written.format & 0x3F, written.tileMode, view.width, view.height, view.pitch, view.format & 0x3F, view.tileMode);
        };
        bool presented = false, drcPresented = false;
        for (const auto &cmd: stream.commands()) {
            switch (cmd.type) {
                case Gx2Cmd::SetDepthBuffer:
                    m_depth = attachmentSurface(parseSurface(cmd));
                    break;
                case Gx2Cmd::SetContextState:
                    setContextState(cmd.gpr[0]);
                    break;
                case Gx2Cmd::SetPolygonControl:
                    m_polygonControl = cmd.gpr[0];
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] polygon control={:08X}", m_polygonControl);
                    break;
                case Gx2Cmd::SetPolygonOffset:
                    for (unsigned i = 0; i < m_polygonOffset.size(); ++i)
                        m_polygonOffset[i] = float(cmd.fpr[i]);
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] polygon offset front={} scale={} back={} scale={} clamp={}", m_polygonOffset[0],
                                          m_polygonOffset[1], m_polygonOffset[2], m_polygonOffset[3], m_polygonOffset[4]);
                    break;
                case Gx2Cmd::SetRasterizerClipControl:
                    m_rasterizerEnabled = cmd.gpr[0] != 0;
                    m_zClipEnabled = cmd.gpr[1] != 0;
                    m_halfZ = cmd.gpr[2] != 0;
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] rasterizer={} zclip={} halfZ={}", m_rasterizerEnabled, m_zClipEnabled, m_halfZ);
                    break;
                case Gx2Cmd::SetDepthStencilControl:
                    if (std::getenv("WEMU_SCAN_TRACE")) // TEMP-SCANTRACE
                        Utils::Log::error("[DEPTHCTL] frame={} after_draw={} test={} write={} cmp={} lr={:08X}", m_presentCount, m_stats.draws,
                                          cmd.gpr[0], cmd.gpr[1], cmd.gpr[2], cmd.callerLr);
                    m_depthTest = cmd.gpr[0] != 0;
                    m_depthWrite = cmd.gpr[1] != 0;
                    m_depthCompare = cmd.gpr[2];
                    m_stencilTest = cmd.gpr[3] != 0;
                    break;
                case Gx2Cmd::ClearDepthStencilEx:
                    if (std::getenv("WEMU_SCAN_TRACE")) // TEMP-SCANTRACE
                        Utils::Log::error("[DEPTHCLR] frame={} after_draw={} surface={:08X} depth={} flags={}", m_presentCount, m_stats.draws,
                                          parseSurface(cmd).imagePtr, cmd.fpr[0], cmd.gpr[2]);
                    clearDepthSurface(parseSurface(cmd), float(cmd.fpr[0]), cmd.gpr[2]);
                    break;
                case Gx2Cmd::ClearColor:
                case Gx2Cmd::ClearBuffersEx: {
                    // ClearColor(colorBuffer, r, g, b, a): colour in f1..f4 (captured to fpr[0..3]).
                    for (int i = 0; i < 4; i++)
                        m_clear[i] = static_cast<float>(cmd.fpr[i]);
                    const Surface cs = attachmentSurface(parseSurface(cmd));
                    if (std::getenv("WEMU_SCAN_TRACE")) // TEMP-SCANTRACE
                        Utils::Log::error("[SCAN] frame={} clear surface={:08X} after_draw={} rgba=({},{},{},{})", m_presentCount, cs.imagePtr,
                                          m_stats.draws, m_clear[0], m_clear[1], m_clear[2], m_clear[3]);
                    if (m_gpuGraph && m_gpuRenderer)
                        m_gpuRenderer->gpuClearTarget(cs.imagePtr, cs.width, cs.height, m_clear);
                    else
                        clearSurface(cs, m_clear);
                    if (cmd.type == Gx2Cmd::ClearBuffersEx) {
                        Gx2Command depthCmd = cmd;
                        depthCmd.gpr[0] = cmd.gpr[1];
                        const std::size_t stride = cmd.payload.size() >= 64 ? 32 : 16;
                        depthCmd.type = Gx2Cmd::SetDepthBuffer;
                        if (cmd.payload.size() >= stride * 2) {
                            const auto depthWords = std::span(cmd.payload).subspan(stride, stride);
                            depthCmd.payload.assign(depthWords.begin(), depthWords.end());
                        } else
                            depthCmd.payload.clear();
                        clearDepthSurface(parseSurface(depthCmd), float(cmd.fpr[4]), cmd.gpr[3]);
                    }
                    break;
                }
                case Gx2Cmd::SetColorBuffer:
                    m_color = attachmentSurface(parseSurface(cmd));
                    m_colorTarget = cmd.gpr[1] & 7;
                    m_colorBuffers[m_colorTarget] = m_color;
                    traceSurfaceAlias(m_color, "color");
                    dumpSurface(m_color, "color");
                    if (m_gpuGraph && m_gpuRenderer && m_color.imagePtr)
                        m_gpuRenderer->gpuBindTarget(m_color.imagePtr, m_color.width, m_color.height);
                    break;
                case Gx2Cmd::SetPixelSampler:
                case Gx2Cmd::SetVertexSampler: {
                    auto &samplers = cmd.type == Gx2Cmd::SetVertexSampler ? m_vertexSamplers : m_pixelSamplers;
                    if (cmd.gpr[1] < samplers.size()) {
                        auto &regs = samplers[cmd.gpr[1]].regs;
                        if (cmd.payload.size() == 3)
                            std::copy_n(cmd.payload.begin(), 3, regs.begin());
                        else if (m_mem && cmd.gpr[0])
                            for (unsigned i = 0; i < 3; i++)
                                regs[i] = m_mem->read<std::uint32_t>(cmd.gpr[0] + 4 * i);
                    }
                    break;
                }
                case Gx2Cmd::SetPixelSamplerBorderColor:
                case Gx2Cmd::SetVertexSamplerBorderColor: {
                    auto &samplers = cmd.type == Gx2Cmd::SetVertexSamplerBorderColor ? m_vertexSamplers : m_pixelSamplers;
                    if (cmd.gpr[0] < samplers.size())
                        for (unsigned i = 0; i < 4; i++)
                            samplers[cmd.gpr[0]].customBorder[i] = static_cast<float>(cmd.fpr[i]);
                    break;
                }
                case Gx2Cmd::SetVertexTexture:
                case Gx2Cmd::SetPixelTexture: {
                    if (m_trace) { // UI panes may bind textures on units > 0 — log them all
                        const Surface t = parseSurface(cmd);
                        Utils::Log::error("[GX2TRACE] setPixTex unit={} img=0x{:08X} {}x{} f{:02X} t{}", cmd.gpr[1], t.imagePtr, t.width, t.height,
                                          t.format & 0x3F, t.tileMode);
                        if (cmd.payload.size() >= 0x9C / 4)
                            Utils::Log::error("[GX2TRACE] texture ptr={:08X} dim={} depth={} mip={}/{} slice={}/{} compMap={:08X} "
                                              "regs={:08X},{:08X},{:08X},{:08X},{:08X}",
                                              cmd.gpr[0], cmd.payload[0], cmd.payload[0xC / 4], cmd.payload[0x74 / 4], cmd.payload[0x78 / 4],
                                              cmd.payload[0x7C / 4], cmd.payload[0x80 / 4], cmd.payload[0x84 / 4], cmd.payload[0x88 / 4],
                                              cmd.payload[0x8C / 4], cmd.payload[0x90 / 4], cmd.payload[0x94 / 4], cmd.payload[0x98 / 4]);
                    }
                    auto &textures = cmd.type == Gx2Cmd::SetVertexTexture ? m_vertexTextures : m_pixelTextures;
                    if (cmd.gpr[1] < textures.size()) {
                        auto &texture = textures[cmd.gpr[1]];
                        texture = parseSurface(cmd);
                        if (cmd.payload.size() > 0x80 / 4) {
                            texture.firstMip = cmd.payload[0x74 / 4];
                            texture.viewMipCount = cmd.payload[0x78 / 4];
                            texture.firstSlice = cmd.payload[0x7C / 4];
                            texture.sliceCount = cmd.payload[0x80 / 4];
                        } else if (m_mem && cmd.gpr[0]) {
                            texture.firstMip = m_mem->read<std::uint32_t>(cmd.gpr[0] + 0x74);
                            texture.viewMipCount = m_mem->read<std::uint32_t>(cmd.gpr[0] + 0x78);
                            texture.firstSlice = m_mem->read<std::uint32_t>(cmd.gpr[0] + 0x7C);
                            texture.sliceCount = m_mem->read<std::uint32_t>(cmd.gpr[0] + 0x80);
                        }
                        if (cmd.payload.size() > 0x84 / 4)
                            texture.compMap = cmd.payload[0x84 / 4];
                        else if (m_mem && cmd.gpr[0])
                            texture.compMap = m_mem->read<std::uint32_t>(cmd.gpr[0] + 0x84);
                        // A one-slice array view samples exactly like a 2D texture (the layer
                        // coordinate clamps to 0), so keep it on the plain 2D paths.
                        if (texture.dimension == 5 && texture.depth == 1 && texture.firstSlice == 0 && texture.sliceCount <= 1) {
                            texture.dimension = 1;
                            texture.sliceCount = 1;
                        }
                        traceSurfaceAlias(texture, "texture");
                        dumpSurface(texture, "tex");
                    }
                    break;
                }
                case Gx2Cmd::SetAttribBuffer:
                    // GX2SetAttribBuffer(index, size, stride, data)
                    if (cmd.gpr[0] < m_attribs.size())
                        m_attribs[cmd.gpr[0]] = {cmd.gpr[3], cmd.gpr[2], cmd.gpr[1]};
                    break;
                case Gx2Cmd::SetFetchShader:
                    m_fetchLayout = cmd.payload;
                    if (m_trace)
                        for (std::size_t i = 0; i + 7 < m_fetchLayout.size(); i += 8)
                            Utils::Log::error("[GX2FETCH] loc={} buf={} off={} fmt={:X} mask={:08X} endian={}", m_fetchLayout[i],
                                              m_fetchLayout[i + 1], m_fetchLayout[i + 2], m_fetchLayout[i + 3], m_fetchLayout[i + 6],
                                              m_fetchLayout[i + 7]);
                    break;
                case Gx2Cmd::SetVertexShader: {
                    // Capture the shader bytecode (GX2VertexShader: size @+0xD0, program @+0xD4)
                    // so draws can run the real vertex transform via LatteVsInterp.
                    const std::uint32_t sh = cmd.gpr[0];
                    m_vsProgram.clear();
                    if (!m_mem || !sh)
                        break;
                    const auto semanticCount = std::min(m_mem->read<std::uint32_t>(sh + 0x40), 32u);
                    m_vertexInputCount = semanticCount ? semanticCount : 4;
                    m_vertexSemantics.fill(0xFF);
                    if (!semanticCount)
                        for (unsigned i = 0; i < 4; ++i)
                            m_vertexSemantics[i] = i;
                    for (std::uint32_t i = 0; i < semanticCount; i++)
                        m_vertexSemantics[i] = m_mem->read<std::uint32_t>(sh + 0x44 + i * 4) & 0xFF;
                    m_vertexOutputSemantics.fill(255);
                    const auto outputWords = std::min(m_mem->read<std::uint32_t>(sh + 0xC), 8u);
                    if (!outputWords)
                        for (unsigned i = 0; i < 4; ++i)
                            m_vertexOutputSemantics[i] = i;
                    for (unsigned word = 0; word < outputWords; ++word) {
                        const auto ids = m_mem->read<std::uint32_t>(sh + 0x10 + word * 4);
                        for (unsigned i = 0; i < 4; ++i)
                            m_vertexOutputSemantics[word * 4 + i] = (ids >> (i * 8)) & 0xFF;
                    }
                    auto it = m_vsCache.find(sh);
                    if (it == m_vsCache.end()) {
                        std::vector<std::uint32_t> prog;
                        std::uint32_t size = 0, ptr = 0;
                        try {
                            size = m_mem->read<std::uint32_t>(sh + 0xD0);
                            ptr = m_mem->read<std::uint32_t>(sh + 0xD4);
                            // Shader bytecode is little-endian in guest memory; read<> assumes
                            // big-endian, so swap back.
                            if (ptr && size >= 8 && size <= 0x10000)
                                for (std::uint32_t i = 0; i < size / 4; i++)
                                    prog.push_back(std::byteswap(m_mem->read<std::uint32_t>(ptr + i * 4)));
                        } catch (const Core::MemoryException &) {
                            prog.clear();
                        }
                        Utils::Log::error("[GX2] VS 0x{:08X}: size={} program=0x{:08X} -> {} dwords captured", sh, size, ptr, prog.size());
                        it = m_vsCache.emplace(sh, std::move(prog)).first;
                    }
                    m_vsProgram = it->second;
                    if (m_dumpSurfaces && !m_vsProgram.empty()) {
                        const char *dir = std::getenv("WEMU_FB_DUMP_DIR");
                        char path[512];
                        std::snprintf(path, sizeof(path), "%s/vs_%08X.hex", dir ? dir : ".", sh);
                        if (FILE *fp = std::fopen(path, "w")) {
                            for (std::size_t i = 0; i < m_vsProgram.size(); i++)
                                std::fprintf(fp, "%08X%c", m_vsProgram[i], (i % 8 == 7) ? '\n' : ' ');
                            std::fclose(fp);
                        }
                    }
                    break;
                }
                case Gx2Cmd::SetPixelShader: {
                    // GX2PixelShader has a shorter register prefix than GX2VertexShader:
                    // WUT documents size @+0xA4 and program @+0xA8.
                    const std::uint32_t sh = cmd.gpr[0];
                    m_psProgram.clear();
                    if (!m_mem || !sh)
                        break;
                    m_pixelInputSemantics.fill(255);
                    const auto inputCount = std::min(m_mem->read<std::uint32_t>(sh + 0x10), 32u);
                    m_pixelInputCount = inputCount ? inputCount : 4;
                    if (!inputCount)
                        for (unsigned i = 0; i < 4; ++i)
                            m_pixelInputSemantics[i] = i;
                    for (unsigned i = 0; i < inputCount; ++i)
                        m_pixelInputSemantics[i] = m_mem->read<std::uint32_t>(sh + 0x14 + i * 4) & 0xFF;
                    auto it = m_psCache.find(sh);
                    if (it == m_psCache.end()) {
                        std::vector<std::uint32_t> prog;
                        std::uint32_t size = 0, ptr = 0;
                        try {
                            size = m_mem->read<std::uint32_t>(sh + 0xA4);
                            ptr = m_mem->read<std::uint32_t>(sh + 0xA8);
                            if (ptr && size >= 8 && size <= 0x10000)
                                for (std::uint32_t i = 0; i < size / 4; i++)
                                    prog.push_back(std::byteswap(m_mem->read<std::uint32_t>(ptr + i * 4)));
                        } catch (const Core::MemoryException &) {
                            prog.clear();
                        }
                        Utils::Log::error("[GX2] PS 0x{:08X}: size={} program=0x{:08X} -> {} dwords captured", sh, size, ptr, prog.size());
                        summarizePixelShaderBytecode(prog, sh);
                        it = m_psCache.emplace(sh, std::move(prog)).first;
                    }
                    m_psProgram = it->second;
                    if (m_dumpSurfaces && !m_psProgram.empty()) {
                        const char *dir = std::getenv("WEMU_FB_DUMP_DIR");
                        char path[512];
                        std::snprintf(path, sizeof(path), "%s/ps_%08X.hex", dir ? dir : ".", sh);
                        if (FILE *fp = std::fopen(path, "w")) {
                            for (std::size_t i = 0; i < m_psProgram.size(); i++)
                                std::fprintf(fp, "%08X%c", m_psProgram[i], (i % 8 == 7) ? '\n' : ' ');
                            std::fclose(fp);
                        }
                    }
                    break;
                }
                case Gx2Cmd::SetVertexUniformBlock:
                    // (location, size, data): keep the call-time payload snapshot for kcache reads.
                    // Uniform-block contents are LITTLE-endian in guest memory (the GPU reads
                    // constants in GPU order, unlike the big-endian uniform *registers*), so the
                    // big-endian read<u32> behind the snapshot leaves every word reversed. Without
                    // this swap a 1.0f identity row reads back as the denormal 4.6e-41 and every
                    // transform collapses to the origin.
                    if (cmd.gpr[0] < m_vsBlocks.size() && !cmd.payload.empty()) {
                        auto blk = cmd.payload;
                        for (auto &w: blk)
                            w = std::byteswap(w);
                        m_vsBlocks[cmd.gpr[0]] = std::move(blk);
                    }
                    break;
                case Gx2Cmd::SetPixelUniformBlock:
                    if (cmd.gpr[0] < m_psBlocks.size() && !cmd.payload.empty()) {
                        auto blk = cmd.payload;
                        for (auto &w: blk)
                            w = std::byteswap(w);
                        const std::size_t words = blk.size();
                        m_psBlocks[cmd.gpr[0]] = std::move(blk);
                        if (m_trace)
                            Utils::Log::error("[GX2TRACE] setPsBlock loc={} bytes={} words={}", cmd.gpr[0], cmd.gpr[1], words);
                    }
                    break;
                case Gx2Cmd::SetVertexUniformReg: {
                    // (offset, count, data): call-time payload into the register file.
                    const std::uint32_t off = cmd.gpr[0], n = cmd.gpr[1];
                    if (off < 256 && !cmd.payload.empty()) {
                        const std::uint32_t lim = std::min<std::uint32_t>({n, 256 - off, static_cast<std::uint32_t>(cmd.payload.size())});
                        for (std::uint32_t i = 0; i < lim; i++)
                            m_vsRegs[off + i] = asFloat(cmd.payload[i]);
                        m_regsFresh = true;
                        repairProjection();
                        if (m_trace) {
                            // Print the call-time PAYLOAD, not a live re-read of cmd.gpr[2]: these
                            // pointers are guest stack addresses that are long dead by replay time,
                            // so re-reading them showed stale junk and made this trace lie about
                            // what was actually written into the register file.
                            std::string data;
                            for (std::uint32_t i = 0; i < lim; i++) {
                                data += std::format(" {:.4g}", asFloat(cmd.payload[i]));
                                if ((i % 4) == 3)
                                    data += " |";
                            }
                            Utils::Log::error("[GX2TRACE] [beforeDraw#{}] setVsRegs off={} cnt={} (c{}..) ptr=0x{:08X} LR=0x{:08X} payload:{}",
                                              m_stats.draws, off, n, off / 4, cmd.gpr[2], cmd.callerLr, data);
                            // For the suspicious all-zero projection upload, show the guest memory
                            // AROUND the pointer: if the real matrix lives adjacent, the captured
                            // base address (not the contents) is what is wrong.
                            if (off == 32 && getenv("WEMU_VSREG_PEEK")) {
                                std::string around;
                                for (int i = -8; i < 24; i++) {
                                    float f = 0;
                                    if (const std::uint8_t *p = m_mem->hostPtr(cmd.gpr[2] + i * 4))
                                        f = asFloat(static_cast<std::uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]);
                                    around += std::format(" {:.4g}", f);
                                    if ((i % 4) == 3)
                                        around += " |";
                                }
                                Utils::Log::error("[GX2TRACE]   peek@{:+d}..{:+d} around 0x{:08X}:{}", -8, 23, cmd.gpr[2], around);
                            }
                        }
                    }
                    break;
                }
                case Gx2Cmd::SetPixelUniformReg: {
                    const std::uint32_t off = cmd.gpr[0], n = cmd.gpr[1];
                    if (off < 256 && !cmd.payload.empty()) {
                        const std::uint32_t lim = std::min<std::uint32_t>({n, 256 - off, static_cast<std::uint32_t>(cmd.payload.size())});
                        for (std::uint32_t i = 0; i < lim; i++)
                            m_psRegs[off + i] = asFloat(cmd.payload[i]);
                        m_psRegsFresh = true;
                        if (m_trace) {
                            std::string data;
                            for (std::uint32_t i = 0; i < lim; i++) {
                                data += std::format(" {:.4g}", asFloat(cmd.payload[i]));
                                if ((i % 4) == 3)
                                    data += " |";
                            }
                            Utils::Log::error("[GX2TRACE] [beforeDraw#{}] setPsRegs off={} cnt={} (p{}..) ptr=0x{:08X} LR=0x{:08X} payload:{}",
                                              m_stats.draws, off, n, off / 4, cmd.gpr[2], cmd.callerLr, data);
                        }
                    }
                    break;
                }
                case Gx2Cmd::SetViewport:
                    for (int i = 0; i < 4; i++)
                        m_vp[i] = static_cast<float>(cmd.fpr[i]);
                    m_vpNear = float(cmd.fpr[4]);
                    m_vpFar = float(cmd.fpr[5]);
                    break;
                case Gx2Cmd::SetScissor:
                    for (int i = 0; i < 4; i++)
                        m_scissor[i] = static_cast<std::int32_t>(cmd.gpr[i]);
                    break;
                case Gx2Cmd::SetBlendControl: {
                    const std::uint32_t target = cmd.gpr[0] & 7;
                    BlendState &b = m_blend[target];
                    b.colorSrcBlend = cmd.gpr[1];
                    b.colorDstBlend = cmd.gpr[2];
                    b.colorCombine = cmd.gpr[3];
                    b.useAlphaBlend = cmd.gpr[4] != 0;
                    b.alphaSrcBlend = cmd.gpr[5];
                    b.alphaDstBlend = cmd.gpr[6];
                    b.alphaCombine = cmd.gpr[7];
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] blend target={} cSrc={} cDst={} cComb={} useA={} aSrc={} aDst={} aComb={}", target,
                                          b.colorSrcBlend, b.colorDstBlend, b.colorCombine, b.useAlphaBlend ? 1 : 0, b.alphaSrcBlend, b.alphaDstBlend,
                                          b.alphaCombine);
                    break;
                }
                case Gx2Cmd::SetBlendConstantColor:
                    for (std::size_t i = 0; i < m_blendConstant.size(); i++)
                        m_blendConstant[i] = byteFromUnit(cmd.fpr[i]);
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] blendConstant {} {} {} {}", m_blendConstant[0], m_blendConstant[1], m_blendConstant[2],
                                          m_blendConstant[3]);
                    break;
                case Gx2Cmd::SetColorControl:
                    m_targetBlendEnable = cmd.gpr[1] & 0xFF;
                    m_colorWriteEnable = cmd.gpr[3] != 0;
                    m_multiWriteEnable = cmd.gpr[2] != 0;
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] colorControl rop={} blendEnable=0x{:02X} multiWrite={} colorWrite={}", cmd.gpr[0],
                                          m_targetBlendEnable, cmd.gpr[2], m_colorWriteEnable ? 1 : 0);
                    break;
                case Gx2Cmd::SetTargetChannelMasks:
                    for (std::size_t i = 0; i < m_channelMask.size(); i++)
                        m_channelMask[i] = cmd.gpr[i] & 0xF;
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] channelMasks {:X} {:X} {:X} {:X} {:X} {:X} {:X} {:X}", m_channelMask[0], m_channelMask[1],
                                          m_channelMask[2], m_channelMask[3], m_channelMask[4], m_channelMask[5], m_channelMask[6], m_channelMask[7]);
                    break;
                case Gx2Cmd::DrawImage:
                    drawImage(cmd);
                    break;
                case Gx2Cmd::DrawEx:
                    drawPrimitives(cmd, false);
                    break;
                case Gx2Cmd::DrawIndexedEx:
                    drawPrimitives(cmd, true);
                    break;
                case Gx2Cmd::CopyColorBufferToScanBuffer:
                    // (colorBuffer, scanTarget): 1 = TV, 4 = DRC. Only the TV feeds our window —
                    // the DRC copy would otherwise wipe the framebuffer with the gamepad surface.
                    // In GPU mode the scan-out is produced at SwapScanBuffers below, not this CPU copy;
                    // graph mode records which colour buffer is the TV source to read back at present.
                    if (std::getenv("WEMU_SCAN_TRACE")) // TEMP-SCANTRACE
                        Utils::Log::error("[SCAN] frame={} copy target={} surface={:08X}", m_presentCount, cmd.gpr[1], parseSurface(cmd).imagePtr);
                    // The DRC copy is captured where it is issued: titles commonly reuse one colour
                    // buffer for both screens, so reading it back at the swap would show the TV image.
                    if ((cmd.gpr[1] & 4) && m_drcCapture && !m_gpuRenderer) {
                        m_lastDrcScan = parseSurface(cmd);
                        copyToDrcBuffer(m_lastDrcScan);
                        drcPresented = true;
                    }
                    if (cmd.gpr[1] & 1) {
                        m_lastScan = attachmentSurface(parseSurface(cmd));
                        if (m_gpuGraph)
                            m_gpuScanAddr = m_lastScan.imagePtr;
                        else if (!m_gpuRenderer) {
                            copyToScanBuffer(m_lastScan);
                            presented = true;
                        }
                    }
                    break;
                case Gx2Cmd::SwapScanBuffers:
                    if (m_dumpSurfaces)
                        dumpSurface(m_color, "final"); // post-draw state of the last bound target
                    {
                        // WEMU_GX2_DUMP_TARGETS=1: on the first sufficiently rich frame, dump the
                        // surface that CopyColorBufferToScanBuffer selected for TV and the final
                        // bound colour target after all software-replayed draws. This is diagnostic
                        // only; it gives a deterministic scan-out comparison for MK8's post-title
                        // frames where the submitted geometry is rich but the presented image is
                        // just horizontal bands.
                        static const bool dumpTargets = []() {
                            const char *env = std::getenv("WEMU_GX2_DUMP_TARGETS");
                            return env && env[0] == '1';
                        }();
                        static const std::uint32_t minDraws = []() -> std::uint32_t {
                            const char *env = std::getenv("WEMU_GX2_DUMP_TARGETS_MIN_DRAWS");
                            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 300u;
                        }();
                        static bool dumpedTargets = false;
                        if (dumpTargets && !dumpedTargets && m_stats.draws >= minDraws) {
                            dumpedTargets = true;
                            dumpSurface(m_lastScan, "scan_cmp", true);
                            dumpSurface(m_color, "color_cmp", true);
                            dumpSurface(m_lastDrawColor, "lastdraw_cmp", true);
                            Utils::Log::error("[GX2] target compare at frame #{} draws={} tris={} scan=0x{:08X} color=0x{:08X} lastDraw=0x{:08X}",
                                              m_presentCount + 1, m_stats.draws, m_stats.tris, m_lastScan.imagePtr, m_color.imagePtr,
                                              m_lastDrawColor.imagePtr);
                        }
                    }
                    if (m_gpuGraph && m_gpuRenderer) {
                        // Execute the recorded render graph and read the TV scan source back into m_fb.
                        // Titles that render straight into the scan buffer never issue a copy, so fall
                        // back to the last bound colour target.
                        const std::uint32_t scan = m_gpuScanAddr ? m_gpuScanAddr : m_color.imagePtr;
                        m_gpuRenderer->gpuPresentTarget(scan, m_fb.data());
                    } else if (m_gpuRenderer) {
                        // Finish the GPU frame: submit the queued draws and read the composited
                        // image back into m_fb, which Renderer::flip_tv then presents.
                        m_gpuRenderer->gpuEnd(m_fb.data());
                    } else if (std::getenv("WEMU_PRESENT_LAST_COLOR") && m_lastDrawColor.imagePtr) {
                        // Experiment: MK8's richer post-title frames draw a final full-screen pass
                        // from the previous TV scan source into another colour target, then may bind
                        // the old scan surface again before SwapScanBuffers. Presenting the most
                        // recent draw target lets us verify whether the visible grey/black/white
                        // bands are just the wrong scan-out choice.
                        copyToScanBuffer(m_lastDrawColor);
                    } else if (!presented) {
                        // Titles stop calling CopyColorBufferToScanBuffer after the boot transition
                        // and render straight into the scan buffer: keep presenting the last one.
                        if (m_lastScan.imagePtr)
                            copyToScanBuffer(m_lastScan);
                        else
                            buildClearFrame();
                    }
                    if (m_drcCapture && !drcPresented && m_lastDrcScan.imagePtr && !m_gpuRenderer)
                        copyToDrcBuffer(m_lastDrcScan);
                    if (std::getenv("WEMU_SCAN_TRACE")) // TEMP-SCANTRACE
                        Utils::Log::error("[SCAN] frame={} presented={} lastScan={:08X} {}x{} fmt={:X} color={:08X} lastDraw={:08X} pending={}",
                                          m_presentCount, presented, m_lastScan.imagePtr, m_lastScan.width, m_lastScan.height, m_lastScan.format,
                                          m_color.imagePtr, m_lastDrawColor.imagePtr, m_pendingReadbacks.contains(makeSurfaceKey(m_lastScan)));
                    m_presentCount++;
                    if ((m_presentCount % 100) == 0)
                        Utils::Log::error("[GX2] replay #{}: draws={} tris={} points={} px={} avgA={} skip(t/a/p)={}/{}/{} scan=0x{:08X} nz={}",
                                          m_presentCount, m_stats.draws, m_stats.tris, m_stats.points, m_stats.pixels,
                                          m_stats.pixels ? m_stats.alphaSum / m_stats.pixels : 0, m_stats.skipNoTarget, m_stats.skipNoAttrib,
                                          m_stats.skipBadPtr, m_stats.scanPtr, m_stats.scanNonZero);
                    // WEMU_FB_DUMP_RICH=1: snapshot the frame whenever it beats the most triangles
                    // seen so far, to a distinctly-named PPM. The steady state is a 55-tri
                    // post-process pattern; any menu/scene geometry produces far more, and those
                    // frames are rare and timing-dependent, so catch them automatically.
                    if (std::getenv("WEMU_FB_DUMP_RICH") && m_stats.tris > m_richestTris && !m_fb.empty()) {
                        m_richestTris = m_stats.tris;
                        const char *dir = std::getenv("WEMU_FB_DUMP_DIR");
                        const std::string path = std::string(dir ? dir : ".") + "/wemu_rich_" + std::to_string(m_stats.tris) + "tris.ppm";
                        if (FILE *f = std::fopen(path.c_str(), "wb")) {
                            std::fprintf(f, "P6\n%u %u\n255\n", kWidth, kHeight);
                            for (std::size_t i = 0; i < m_fb.size(); i += 4)
                                std::fwrite(m_fb.data() + i, 1, 3, f);
                            std::fclose(f);
                            Utils::Log::error("[GX2] RICH frame #{}: {} tris, {} draws -> {}", m_presentCount, m_stats.tris, m_stats.draws, path);
                        }
                    }
                    m_stats = {};
                    maybeDumpFrame();
                    {
                        static const std::uint32_t dumpAt = []() -> std::uint32_t {
                            const char *env = std::getenv("WEMU_FB_DUMP");
                            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 0xFFFFFFFFu;
                        }();
                        m_dumpSurfaces = (m_presentCount + 1 == dumpAt);
                        static const std::uint32_t traceAt = []() -> std::uint32_t {
                            const char *env = std::getenv("WEMU_GX2_TRACE");
                            return env ? static_cast<std::uint32_t>(std::strtoul(env, nullptr, 10)) : 0xFFFFFFFFu;
                        }();
                        m_trace = (m_presentCount + 1 == traceAt);
                    }
                    break;
                default:
                    break; // remaining state commands not needed by the CPU replay
            }
        }
    }

} // namespace Core::Gfx
