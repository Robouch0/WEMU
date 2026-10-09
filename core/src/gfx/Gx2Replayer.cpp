#include "gfx/Gx2Replayer.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <format>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdexcept>

#include "cpu/memory/Memory.hpp"
#include "gfx/LatteVsInterp.hpp"
#include "gfx/SurfaceLayout.hpp"
#include "gfx/TextureFormat.hpp"
#include "gfx/TriangleCoverage.hpp"
#include "gfx/Renderer.hpp" // full definition: the GPU path calls Renderer::gpu* methods
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

        std::uint8_t byteFromUnit(const double v)
        {
            return static_cast<std::uint8_t>(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5);
        }

        std::uint8_t clampByte(const int v)
        {
            return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
        }

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
                case 0x00: return "NOP";
                case 0x01: return "TEX";
                case 0x02: return "VTX";
                case 0x03: return "VTX_TC";
                case 0x0A: return "JUMP";
                case 0x0D: return "ELSE";
                case 0x0E: return "POP";
                case 0x13: return "CALL_FS";
                case 0x14: return "RETURN";
                case 0x27: return "EXP";
                case 0x28: return "EXP_DONE";
                default: return "?";
            }
        }

        const char *alu2Name(std::uint32_t op)
        {
            switch (op) {
                case 0x00: return "ADD";
                case 0x01: return "MUL";
                case 0x02: return "MUL_IEEE";
                case 0x03: return "MAX";
                case 0x04: return "MIN";
                case 0x10: return "FRACT";
                case 0x14: return "FLOOR";
                case 0x19: return "MOV";
                case 0x1A: return "NOP";
                case 0x50: return "DOT4";
                case 0x51: return "DOT4_IEEE";
                case 0x66: return "RECIP_IEEE";
                case 0x6B: return "FLT_TO_INT";
                case 0x6C: return "INT_TO_FLT";
                case 0x6D: return "UINT_TO_FLT";
                case 0x70: return "ASHR";
                case 0x71: return "LSHR";
                case 0x72: return "LSHL";
                case 0x79: return "FLT_TO_UINT";
                default: return "?";
            }
        }

        const char *alu3Name(std::uint32_t op)
        {
            switch (op) {
                case 0x10: return "MULADD";
                case 0x14: return "MULADD_IEEE";
                case 0x18: return "CNDE";
                case 0x19: return "CNDGT";
                case 0x1A: return "CNDGE";
                case 0x1C: return "CNDE_INT";
                case 0x1D: return "CNDGT_INT";
                case 0x1E: return "CNDGE_INT";
                default: return "?";
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
                Utils::Log::error("[PSSUM]     alu@{} OP3 {}(0x{:02X}) R{}.{} = S{}.{} * S{}.{} + S{}.{} last={}", slot,
                                  alu3Name(op3), op3, dst, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, src2Sel, src2Chan,
                                  (w0 >> 31) & 1);
            } else {
                const std::uint32_t op = (w1 >> 7) & 0x7FF;
                Utils::Log::error("[PSSUM]     alu@{} OP2 {}(0x{:02X}) R{}.{} = S{}.{} op S{}.{} wm={} last={}", slot,
                                  alu2Name(op), op, dst, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, (w1 >> 4) & 1,
                                  (w0 >> 31) & 1);
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
                const std::uint32_t w0 = program[cf * 2];
                const std::uint32_t w1 = program[cf * 2 + 1];
                if (r600IsAluClause(w1)) {
                    const std::uint32_t addr = w0 & 0x3FFFFF;
                    const std::uint32_t count = ((w1 >> 18) & 0x7F) + 1;
                    Utils::Log::error("[PSSUM]   CF{} ALU kind={} addr={} count={} kc0=b{} a{} m{} kc1=b{} a{} m{}", cf,
                                      r600CfAluInst(w1), addr, count, (w0 >> 22) & 0xF, (w1 >> 2) & 0xFF, (w0 >> 30) & 3,
                                      (w0 >> 26) & 0xF, (w1 >> 10) & 0xFF, w1 & 3);
                    for (std::uint32_t slot = addr; slot < addr + std::min<std::uint32_t>(count, 12); slot++)
                        summarizeAluSlot(program, slot);
                    if (count > 12)
                        Utils::Log::error("[PSSUM]     ... {} more ALU slots", count - 12);
                    continue;
                }

                const std::uint32_t inst = r600CfInst(w1);
                const std::uint32_t addr = w0 & 0x3FFFFF;
                Utils::Log::error("[PSSUM]   CF{} {}(0x{:02X}) addr={} w0=0x{:08X} w1=0x{:08X} eop={}", cf, cfName(inst), inst,
                                  addr, w0, w1, r600CfEop(w1));
                if (inst == 0x01 || inst == 0x02 || inst == 0x03) {
                    for (std::uint32_t slot = addr; slot < addr + 4 && slot * 2 + 1 < program.size(); slot++)
                        Utils::Log::error("[PSSUM]     fetch@{} raw={} {}", slot, std::format("{:08X}", program[slot * 2]),
                                          std::format("{:08X}", program[slot * 2 + 1]));
                } else if (inst == 0x27 || inst == 0x28) {
                    Utils::Log::error("[PSSUM]     export type={} base={} gpr=R{} sel=({}, {}, {}, {})", (w0 >> 13) & 3,
                                      w0 & 0x1FFF, (w0 >> 15) & 0x7F, w1 & 7, (w1 >> 3) & 7, (w1 >> 6) & 7, (w1 >> 9) & 7);
                }
                if (r600CfEop(w1))
                    break;
            }
        }

        enum class SimplePixelShaderKind {
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
                    const std::uint32_t w0 = program[(32 + i) * 2];
                    const std::uint32_t w1 = program[(32 + i) * 2 + 1];
                    if (op3(w1) != 0x10 || dstGpr(w1) != 1 || dstChan(w1) != i || src0Sel(w0) != 0 || src0Chan(w0) != i
                        || src1Sel(w0) != 257 || src1Chan(w0) != i || (w1 & 0x1FF) != 256 || ((w1 >> 10) & 3) != i)
                        return SimplePixelShaderKind::None;
                }
                if (!isLast(program[33 * 2]) || !isLast(program[35 * 2]))
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
                const std::uint32_t w0 = program[(32 + i) * 2];
                const std::uint32_t w1 = program[(32 + i) * 2 + 1];
                if (op3(w1) != 0x10 || src0Sel(w0) != factorSrc[i][0] || src0Chan(w0) != factorSrc[i][1]
                    || src1Sel(w0) != factorSrc[i][2] || src1Chan(w0) != factorSrc[i][1] || (w1 & 0x1FF) != 256
                    || ((w1 >> 10) & 3) != factorSrc[i][1])
                    return SimplePixelShaderKind::None;
            }
            if (!isLast(program[33 * 2]) || isLast(program[34 * 2]) || isLast(program[35 * 2]))
                return SimplePixelShaderKind::None;

            for (std::uint32_t i = 0; i < 4; i++) {
                const std::uint32_t slot = 36 + i;
                const std::uint32_t w0 = program[slot * 2];
                const std::uint32_t w1 = program[slot * 2 + 1];
                if (op3(w1) >= 0x08 || op2(w1) != 0x01 || dstGpr(w1) != 1 || dstChan(w1) != i || src0Sel(w0) != 0
                    || src0Chan(w0) != i)
                    return SimplePixelShaderKind::None;
            }
            if (!isLast(program[36 * 2]) || isLast(program[37 * 2]) || isLast(program[38 * 2]) || !isLast(program[39 * 2]))
                return SimplePixelShaderKind::None;
            return SimplePixelShaderKind::TextureParamRegs;
        }

    } // namespace

    void Gx2Replayer::buildClearFrame()
    {
        m_fb.assign(static_cast<std::size_t>(kWidth) * kHeight * 4, 0);
        const auto toByte = [](float f) { return static_cast<std::uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
        const std::uint8_t r = toByte(m_clear[0]);
        const std::uint8_t g = toByte(m_clear[1]);
        const std::uint8_t b = toByte(m_clear[2]);
        for (std::size_t i = 0; i < m_fb.size(); i += 4) {
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

    Gx2Replayer::SurfaceKey Gx2Replayer::makeSurfaceKey(const Surface &s)
    {
        return {s.imagePtr, s.width, s.height, s.pitch, s.format & 0x3F, s.tileMode, s.swizzle};
    }

    std::vector<std::uint8_t> &Gx2Replayer::viewBacking(const Surface &s)
    {
        resolveViewBacking(s);
        auto &backing = m_viewBackings[makeSurfaceKey(s)];
        const std::size_t bytes = static_cast<std::size_t>(s.pitch) * s.height * 4;
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
        if (m_pendingReadbacks.empty()) return;
        const auto key = makeSurfaceKey(s);
        const auto pending = m_pendingReadbacks.find(key);
        if (pending == m_pendingReadbacks.end()) return;
        const auto &pixels = pending->second->resolve();
        auto &backing = m_viewBackings.at(key);
        if (pixels.size() != backing.size())
            throw std::runtime_error("Deferred raster target size changed");
        std::copy(pixels.begin(), pixels.end(), backing.begin());
        m_pendingReadbacks.erase(pending);
    }

    void Gx2Replayer::resolvePendingBackings() const
    {
        // Software workers can select array slices dynamically. Resolve on the
        // replay thread before dispatch so sampling never mutates shared maps.
        for (const auto &[key, pending] : m_pendingReadbacks) {
            const auto &pixels = pending->resolve();
            auto &backing = m_viewBackings.at(key);
            if (pixels.size() != backing.size())
                throw std::runtime_error("Deferred raster target size changed");
            std::copy(pixels.begin(), pixels.end(), backing.begin());
        }
        m_pendingReadbacks.clear();
    }

    std::array<std::uint8_t, 4> Gx2Replayer::sampleViewBacking(const Surface &s, const std::vector<std::uint8_t> &backing, float u,
                                                               float v) const
    {
        if (!s.width || !s.height || !s.pitch || backing.empty())
            return {128, 128, 128, 255};
        u -= std::floor(u); // wrap
        v -= std::floor(v);
        const auto tx = std::min(s.width - 1, static_cast<std::uint32_t>(u * static_cast<float>(s.width)));
        const auto ty = std::min(s.height - 1, static_cast<std::uint32_t>(v * static_cast<float>(s.height)));
        const std::size_t off = (static_cast<std::size_t>(ty) * s.pitch + tx) * 4;
        if (off + 3 >= backing.size())
            return {128, 128, 128, 255};
        return {backing[off + 0], backing[off + 1], backing[off + 2], backing[off + 3]};
    }

    std::optional<Gx2Replayer::Surface> Gx2Replayer::arraySlice(const Surface &original, unsigned relative) const
    {
            if (original.dimension != 5 || !original.depth || !original.sliceCount || original.firstMip || original.aa
                || original.firstSlice >= original.depth || original.sliceCount > original.depth - original.firstSlice
                || relative >= original.sliceCount
                || !original.imageSize || original.imageSize % original.depth || !original.imagePtr || !m_mem)
                return {};
            const auto fmt = original.format & 0x3F;
            const bool compressed = fmt >= 0x31 && fmt <= 0x35;
            if (!compressed && fmt != 1 && fmt != 7 && fmt != 0x1A)
                return {};
            const unsigned bytes = compressed ? (fmt == 0x31 || fmt == 0x34 ? 8 : 16) : fmt == 1 ? 1 : fmt == 7 ? 2 : 4;
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
            if (!width || !height || original.pitch < width || original.pitch % pitchAlign
                || std::uint64_t(original.pitch) * height * bytes > stride
                || (original.tileMode == 4 && stride % 2048))
                return {};
            const auto slice = original.firstSlice + relative;
            const std::uint64_t address = original.imagePtr + stride * slice;
            if (std::uint64_t(original.imagePtr) + original.imageSize > 0x100000000ull
                || !m_mem->hostPtr(static_cast<std::uint32_t>(address))
                || !m_mem->hostPtr(static_cast<std::uint32_t>(address + stride - 1)))
                return {};
            Surface selected = original;
            selected.dimension = 1;
            selected.imagePtr = static_cast<std::uint32_t>(address);
            selected.imageSize = static_cast<std::uint32_t>(stride);
            if (original.tileMode == 4) {
                // Thin 2D macro tiles rotate the bank/pipe selection between slices.
                const auto swizzle = (((original.swizzle >> 8) & 7) + slice * 2) & 7;
                selected.swizzle = (original.swizzle & ~0x700u) | (swizzle << 8);
            }
            return selected;
    }

    std::shared_ptr<Gx2Replayer::ArraySnapshot> Gx2Replayer::arraySnapshot(const Surface &surface)
    {
        constexpr std::uint64_t budget = 64ull * 1024 * 1024;
        const std::uint64_t decodedBytes = std::uint64_t(surface.width) * surface.height * surface.sliceCount * 4;
        if (!surface.width || !surface.height || surface.width > 4096 || surface.height > 4096
            || !surface.sliceCount || surface.sliceCount > 256 || decodedBytes + surface.imageSize > budget
            || !useViewBacking()) return {};
        const auto sourceEnd = std::uint64_t(surface.imagePtr) + surface.imageSize;
        for (const auto &[key, backing] : m_viewBackings)
            if (std::uint64_t(key.imagePtr) < sourceEnd
                && std::uint64_t(surface.imagePtr) < std::uint64_t(key.imagePtr) + backing.size()) return {};
        std::vector<Surface> slices;
        for (unsigned layer = 0; layer < surface.sliceCount; ++layer) {
            const auto selected = arraySlice(surface, layer);
            // Rendered aliases must use versioned snapshots, not a raw-memory cache.
            if (!selected || findViewBacking(*selected) || m_cpuWritten.contains(selected->imagePtr)) return {};
            slices.push_back(*selected);
        }
        const auto *begin = m_mem->hostPtr(surface.imagePtr);
        const auto *end = m_mem->hostPtr(surface.imagePtr + surface.imageSize - 1);
        if (!begin || !end || std::uintptr_t(end) - std::uintptr_t(begin) != surface.imageSize - 1) return {};
        for (const auto &entry : m_arraySnapshots)
            if (entry->surface == surface && entry->source.size() == surface.imageSize
                && std::memcmp(entry->source.data(), begin, surface.imageSize) == 0) return entry;
        auto result = std::make_shared<ArraySnapshot>();
        result->surface = surface;
        result->source.assign(begin, begin + surface.imageSize);
        result->rgba.resize(decodedBytes);
        for (unsigned layer = 0; layer < slices.size(); ++layer)
            for (unsigned y = 0; y < surface.height; ++y)
                for (unsigned x = 0; x < surface.width; ++x) {
                    const auto color = sample(slices[layer], (float(x) + 0.5f) / surface.width,
                                              (float(y) + 0.5f) / surface.height);
                    const auto offset = ((std::size_t(layer) * surface.height + y) * surface.width + x) * 4;
                    std::copy(color.begin(), color.end(), result->rgba.begin() + offset);
                }
        std::erase_if(m_arraySnapshots, [&](const auto &entry) { return entry->surface == surface; });
        auto bytes = decodedBytes + surface.imageSize;
        for (const auto &entry : m_arraySnapshots) bytes += entry->source.size() + entry->rgba.size();
        while (!m_arraySnapshots.empty() && (bytes > budget || m_arraySnapshots.size() >= 8)) {
            bytes -= m_arraySnapshots.front()->source.size() + m_arraySnapshots.front()->rgba.size();
            m_arraySnapshots.erase(m_arraySnapshots.begin());
        }
        m_arraySnapshots.push_back(result);
        return result;
    }

    bool Gx2Replayer::supportsGather(const Surface &surface, const TextureSampler &sampler)
    {
        const auto state = sampler.regs[0];
        return surface.dimension == 1 && surface.mipCount == 1 && !surface.firstMip && !surface.aa
            && !surface.firstSlice && surface.sliceCount == 1 && surface.imagePtr && surface.width && surface.height
            && (state & 7) <= 2 && ((state >> 3) & 7) <= 2 && ((state >> 9) & 7) <= 1
            && ((state >> 12) & 7) <= 1 && !((state >> 19) & 7);
    }

    std::array<float, 4> Gx2Replayer::sampleTexture(const Surface &original, const TextureSampler &sampler, float u, float v,
                                                 float layer,
                                                 const std::vector<std::uint8_t> *feedback, bool gather) const
    {
        if (original.dimension == 5) {
            if (!std::isfinite(layer) || !original.sliceCount) return sampler.border();
            const auto relative = static_cast<std::uint32_t>(std::clamp(std::floor(double(layer) + 0.5),
                                                                       0.0, double(original.sliceCount - 1)));
            const auto slice = arraySlice(original, relative);
            if (!slice) return sampler.border();
            const auto &selected = *slice;
            // Only the base slice can reuse this draw's base-layer feedback snapshot.
            const auto *selectedFeedback = selected.imagePtr == original.imagePtr && selected.swizzle == original.swizzle
                ? feedback : nullptr;
            return sampleTexture(selected, sampler, u, v, 0, selectedFeedback);
        }
        const Surface &s = original;
        auto fetch = [&](unsigned x, unsigned y) {
            if (s.format == 0x80E && !feedback && !findViewBacking(s) && m_mem && s.imagePtr) {
                const auto offset = tiledElementOffset(x, y, s.pitch, 32, s.tileMode, s.swizzle);
                if (std::uint64_t(offset) + 4 <= s.imageSize
                    && std::uint64_t(s.imagePtr) + offset + 4 <= 0x100000000ull) {
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
                    const float tu = (x + 0.5f) / s.width, tv = (y + 0.5f) / s.height;
                    raw = sample(s, tu, tv);
                }
            }
            const auto mapped = mapTextureChannels(raw, s.compMap);
            std::array<float, 4> rgba{};
            for (unsigned c = 0; c < 4; c++) rgba[c] = mapped[c] / 255.0f;
            return rgba;
        };
        // Unbound resources still honor constant component selectors.
        if (!s.width || !s.height) return fetch(0, 0);
        return gather ? sampler.gather(s.width, s.height, u, v, fetch) : sampler.sample(s.width, s.height, u, v, fetch);
    }

    std::array<std::uint8_t, 4> Gx2Replayer::blendTexel(const std::array<std::uint8_t, 4> &src, const std::uint8_t *dst,
                                                        const BlendState &blend) const
    {
        const auto factor = [&](const std::uint32_t mode, const int c) -> int {
            switch (mode) {
                case 0: return 0; // ZERO
                case 1: return 255; // ONE
                case 2: return src[c]; // SRC_COLOR
                case 3: return 255 - src[c]; // INV_SRC_COLOR
                case 4: return src[3]; // SRC_ALPHA
                case 5: return 255 - src[3]; // INV_SRC_ALPHA
                case 6: return dst[3]; // DST_ALPHA
                case 7: return 255 - dst[3]; // INV_DST_ALPHA
                case 8: return dst[c]; // DST_COLOR
                case 9: return 255 - dst[c]; // INV_DST_COLOR
                case 10: return c == 3 ? 255 : std::min<int>(src[3], 255 - dst[3]); // SRC_ALPHA_SAT
                case 11: return src[3]; // BOTH_SRC_ALPHA
                case 12: return 255 - src[3]; // BOTH_INV_SRC_ALPHA
                case 13: return m_blendConstant[c]; // BLEND_FACTOR
                case 14: return 255 - m_blendConstant[c]; // INV_BLEND_FACTOR
                case 15: return 0; // SRC1_COLOR: unsupported dual-source blend
                case 16: return 255; // INV_SRC1_COLOR: unsupported dual-source blend
                case 17: return 0; // SRC1_ALPHA: unsupported dual-source blend
                case 18: return 255; // INV_SRC1_ALPHA: unsupported dual-source blend
                case 19: return m_blendConstant[3]; // CONSTANT_ALPHA
                case 20: return 255 - m_blendConstant[3]; // INV_CONSTANT_ALPHA
                default: return 255;
            }
        };
        const auto apply = [&](const int s, const int d, const int sf, const int df, const std::uint32_t op) -> std::uint8_t {
            switch (op) {
                case 1: return clampByte((s * sf - d * df) / 255); // SUB
                case 2: return static_cast<std::uint8_t>(std::min(s, d)); // MIN
                case 3: return static_cast<std::uint8_t>(std::max(s, d)); // MAX
                case 4: return clampByte((d * df - s * sf) / 255); // REV_SUB
                case 0:
                default: return clampByte((s * sf + d * df) / 255); // ADD
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
            s.aa = cmd.payload[0x18 / 4];
            s.imageSize = cmd.payload[0x20 / 4];
            s.height = cmd.payload[SURF_HEIGHT / 4];
            s.format = cmd.payload[SURF_FORMAT / 4];
            s.imagePtr = cmd.payload[SURF_IMAGE_PTR / 4];
            s.tileMode = cmd.payload[SURF_TILE_MODE / 4];
            s.swizzle = cmd.payload[SURF_SWIZZLE / 4];
            s.pitch = cmd.payload[SURF_PITCH / 4];
            if (s.width == 0 || s.height == 0 || s.width > 4096 || s.height > 4096 || s.pitch == 0 || s.pitch > 8192)
                return {};
            return s;
        }
        return parseSurface(cmd.gpr[0]);
    }

    void Gx2Replayer::clearSurface(const Surface &s, const float rgba[4])
    {
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
            if (!base || !m_mem->hostPtr(s.imagePtr + s.pitch * s.height * 4 - 1))
                return;
        }
        const auto toByte = [](float f) { return static_cast<std::uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
        const std::uint8_t px[4] = {toByte(rgba[0]), toByte(rgba[1]), toByte(rgba[2]), toByte(rgba[3])};
        for (std::uint32_t y = 0; y < s.height; y++) {
            std::uint8_t *row = base + static_cast<std::size_t>(y) * s.pitch * 4;
            for (std::uint32_t x = 0; x < s.width; x++)
                std::memcpy(row + x * 4, px, 4);
        }
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
            const float *z = &m_vsRegs[k * 4];
            if (!(z[2] < -0.9f && z[2] > -1.1f && z[3] > 800.0f && z[3] < 950.0f))
                continue;
            float *rowX = &m_vsRegs[(k - 2) * 4];
            float *rowY = &m_vsRegs[(k - 1) * 4];
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
                const auto t = sample(s, (static_cast<float>(x) + 0.5f) / static_cast<float>(tw),
                                      (static_cast<float>(y) + 0.5f) / static_cast<float>(th));
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
        // Everything else: treat as RGBA8 (exact for our own intermediates and format 0x1A).
        const std::uint32_t off = tiledElementOffset(tx, ty, s.pitch, 32, tile, s.swizzle);
        const std::uint8_t *px = m_mem->hostPtr(s.imagePtr + off);
        if (!px || !m_mem->hostPtr(s.imagePtr + off + 3))
            return {128, 128, 128, 255};
        return {px[0], px[1], px[2], px[3]};
    }

    bool Gx2Replayer::fetchAttributes(std::uint32_t vertex, std::array<std::array<float, 4>, 4> &out) const
    {
        if (!m_mem || m_fetchLayout.empty())
            return false;
        out = {};
        for (auto &a: out)
            a[3] = 1.0f;
        for (std::size_t i = 0; i + 7 < m_fetchLayout.size(); i += 8) {
            const auto location = m_fetchLayout[i], buffer = m_fetchLayout[i + 1], offset = m_fetchLayout[i + 2];
            const auto format = m_fetchLayout[i + 3], mask = m_fetchLayout[i + 6];
            auto endian = m_fetchLayout[i + 7];
            const auto semantic = std::find(m_vertexSemantics.begin(), m_vertexSemantics.end(), location);
            if (semantic == m_vertexSemantics.end())
                continue;
            if (buffer >= m_attribs.size())
                return false;
            const auto &binding = m_attribs[buffer];
            const auto type = format & 0xFF;
            const bool floating = type == 6 || type == 0xD || type == 0x11 || type == 0x13;
            unsigned components = 0, bytes = 0;
            if (floating) {
                components = type == 6 ? 1 : type == 0xD ? 2 : type == 0x11 ? 3 : 4;
                bytes = 4;
            } else if (type == 0 || type == 4 || type == 0xA) {
                components = type == 0 ? 1 : type == 4 ? 2 : 4;
                bytes = 1;
            } else {
                return false;
            }
            if (endian == 3)
                endian = bytes == 4 ? 2 : 0;
            const std::uint32_t index = m_fetchLayout[i + 4] == 1 ? 0 : vertex;
            const std::uint64_t start = std::uint64_t(index) * binding.stride + offset;
            if (!binding.addr || start + components * bytes > binding.size
                || std::uint64_t(binding.addr) + start + components * bytes > 0x100000000ull)
                return false;
            const auto addr = binding.addr + static_cast<std::uint32_t>(start);
            const auto *p = m_mem->hostPtr(addr);
            if (!p || !m_mem->hostPtr(addr + components * bytes - 1))
                return false;
            std::array<float, 4> value{0, 0, 0, 1};
            for (unsigned c = 0; c < components; c++) {
                std::uint32_t bits = 0;
                for (unsigned b = 0; b < bytes; b++) {
                    const auto byteIndex = c * bytes + b;
                    const auto swapped = endian == 2 ? byteIndex ^ 3u : endian == 1 ? byteIndex ^ 1u : byteIndex;
                    if (swapped >= components * bytes)
                        return false;
                    bits |= std::uint32_t(p[swapped]) << (b * 8);
                }
                if (floating)
                    value[c] = asFloat(bits);
                else if (format & 0x100)
                    value[c] = asFloat((format & 0x200) ? static_cast<std::uint32_t>(static_cast<std::int8_t>(bits)) : bits);
                else if (format & 0x200)
                    value[c] = (format & 0x800) ? static_cast<std::int8_t>(bits)
                        : std::max(-1.0f, static_cast<std::int8_t>(bits) / 127.0f);
                else
                    value[c] = (format & 0x800) ? float(bits) : bits / 255.0f;
            }
            auto &a = out[std::distance(m_vertexSemantics.begin(), semantic)];
            for (unsigned c = 0; c < 4; c++) {
                const auto select = (mask >> (24 - c * 8)) & 7;
                if (select < 4)
                    a[c] = value[select];
                else if (select == 4 || select == 5)
                    a[c] = select == 5 ? 1.0f : 0.0f;
            }
        }
        return true;
    }

    void Gx2Replayer::drawPrimitives(const Gx2Command &cmd, const bool indexed)
    {
        static const bool nativeAudit = [] {
            const auto *value = std::getenv("WEMU_NATIVE_SHADER_AUDIT");
            return value && value[0] == '1';
        }();
        static const bool profile = [] {
            const auto *value = std::getenv("WEMU_RASTER_PROFILE");
            return value && value[0] == '1';
        }();
        const auto profileStart = (profile || nativeAudit) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const auto pixelsBefore = m_stats.pixels;
        m_stats.draws++;
        // The software pixel path samples one resource. Use the shader's resource ID,
        // not the last unit-zero binding (post-process passes commonly bind other units).
        unsigned textureUnit = 0;
        unsigned textureParam = 0, textureSelectX = 0, textureSelectY = 1;
        for (std::size_t cf = 0; cf + 1 < m_psProgram.size(); cf += 2) {
            const auto w0 = m_psProgram[cf], w1 = m_psProgram[cf + 1];
            if (((w1 >> 26) & 15) >= 8)
                continue;
            if (((w1 >> 23) & 127) == 1 && std::uint64_t(w0) * 2 + 2 < m_psProgram.size()) {
                const auto t0 = m_psProgram[w0 * 2], t2 = m_psProgram[w0 * 2 + 2];
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
                if (pending != m_pendingReadbacks.end()) renderedTarget = pending->second;
            }
            target = renderedTarget ? m_viewBackings.at(makeSurfaceKey(m_color)).data() : viewBacking(m_color).data();
        } else {
            target = m_mem->hostPtr(m_color.imagePtr);
            if (!target || !m_mem->hostPtr(m_color.imagePtr + m_color.pitch * m_color.height * 4 - 1)) {
                m_stats.skipBadPtr++;
                return;
            }
        }
        m_lastDrawColor = m_color;

        const std::uint32_t mode = cmd.gpr[0];
        const std::uint32_t count = std::min<std::uint32_t>(cmd.gpr[1], 0x10000);
        const Attrib &ab = m_attribs[0];
        if (m_fetchLayout.empty() && (!ab.addr || ab.stride < 8)) {
            m_stats.skipNoAttrib++;
            return;
        }
        m_cpuWritten.insert(m_color.imagePtr);
        auto &w = m_cpuWrittenSurfaces[m_color.imagePtr];
        w.surface = m_color;
        w.frame = m_presentCount + 1;
        w.writes++;

        // Gather indices.
        std::vector<std::uint32_t> idx;
        idx.reserve(count);
        if (indexed) {
            const std::uint32_t type = cmd.gpr[2] & 0xF;
            if (type != 0 && type != 1 && type != 4 && type != 9)
                return;
            const std::uint32_t indexBytes = type == 9 || type == 1 ? 4 : 2;
            for (std::uint32_t i = 0; i < count; i++) {
                const std::uint64_t address = std::uint64_t(cmd.gpr[3]) + i * indexBytes;
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

        // Fetch one vertex: position from floats[0..1], uv heuristics by stride.
        const std::uint32_t nFloats = ab.stride / 4;
        auto fetch = [&](std::uint32_t vi, float &x, float &y, float &u, float &v) -> bool {
            std::array<std::array<float, 4>, 4> attributes{};
            if (fetchAttributes(vi, attributes)) {
                x = attributes[0][0]; y = attributes[0][1];
                u = attributes[1][0]; v = attributes[1][1];
                return true;
            }
            if (!m_fetchLayout.empty())
                return false;
            const std::uint8_t *p = m_mem->hostPtr(ab.addr + vi * ab.stride);
            if (!p || !m_mem->hostPtr(ab.addr + vi * ab.stride + ab.stride - 1))
                return false;
            auto f = [&](std::uint32_t i) {
                return asFloat(static_cast<std::uint32_t>(p[i * 4]) << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3]);
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

        // Execute the real vertex shader for one vertex; returns false if unavailable so the
        // caller falls back to the [0,1]/matrix heuristics. PARAM exports are optionally returned
        // because the pixel shader consumes them as interpolated inputs.
        auto runShader = [&](std::uint32_t vi, float &sx, float &sy, float &u, float &v,
                             std::array<std::array<float, 4>, 4> *params = nullptr,
                             std::array<bool, 4> *paramValid = nullptr) -> bool {
            // Real vertex-shader execution. ON by default: combined with the UI-composite fallback
            // it produces the placed, clean title screen (logo + background, no stray squares). Set
            // WEMU_VS_EXEC=0 to fall back to the [0,1] heuristic (the old fullscreen-layer look).
            static const bool enabled = []() {
                const char *e = std::getenv("WEMU_VS_EXEC");
                return !e || e[0] != '0';
            }();
            if (!enabled || m_vsProgram.empty())
                return false;
            std::array<std::array<float, 4>, 4> attribs{};
            if (!m_fetchLayout.empty()) {
                if (!fetchAttributes(vi, attribs))
                    return false;
            } else {
                const std::uint8_t *p = m_mem->hostPtr(ab.addr + vi * ab.stride);
                if (!p || !m_mem->hostPtr(ab.addr + vi * ab.stride + ab.stride - 1))
                    return false;
                auto f = [&](std::uint32_t i) {
                    return asFloat(static_cast<std::uint32_t>(p[i * 4]) << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3]);
                };
                attribs[0] = {f(0), f(1), nFloats >= 3 ? f(2) : 0.0f, nFloats >= 4 ? f(3) : 1.0f};
                if (nFloats >= 8)
                    attribs[1] = {f(4), f(5), f(6), f(7)};
                else
                    attribs[1] = {f(0), f(1), 0.0f, 1.0f};
            }
            const auto kcache = [&](std::uint32_t bank, std::uint32_t vec4Idx, std::uint32_t chan) -> float {
                // kcache bank N maps to some GX2 uniform-block location N+bias. The +1 was inferred
                // from MK8 binding locations 1..4 against CF banks starting at 0, but it has never
                // been confirmed against a draw that lands correctly; WEMU_KCACHE_BIAS overrides it
                // so the mapping can be settled by experiment rather than inference.
                static const std::uint32_t bias = []() {
                    const char *e = std::getenv("WEMU_KCACHE_BIAS");
                    return e ? static_cast<std::uint32_t>(std::atoi(e)) : 1u;
                }();
                bank += bias;
                if (bank >= m_vsBlocks.size())
                    return 0.0f;
                const auto &blk = m_vsBlocks[bank];
                const std::uint32_t idx = vec4Idx * 4 + chan;
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
            const bool dbg = ((dbgDraw >= 0 && static_cast<std::uint32_t>(dbgDraw) == m_stats.draws)
                              || (dbgTex && m_texture.imagePtr == dbgTex))
                             && vi == 0 && (!debugTraceOnly || m_trace);
            const bool sum = summary && vi == 0 && (!debugTraceOnly || m_trace);
            if (dbg) {
                std::fprintf(stderr, "[VSDBG] draw#%u regsFresh=%d prog=%zudw stride=%u attrib0=(%g,%g,%g,%g)\n", m_stats.draws,
                             m_regsFresh ? 1 : 0, m_vsProgram.size(), ab.stride, attribs[0][0], attribs[0][1], attribs[0][2],
                             attribs[0][3]);
                for (int c = 0; c < 20; c++)
                    std::fprintf(stderr, "[VSDBG]   c%-2d %g %g %g %g\n", c, m_vsRegs[c * 4], m_vsRegs[c * 4 + 1],
                                 m_vsRegs[c * 4 + 2], m_vsRegs[c * 4 + 3]);
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
            const auto o = LatteVsInterp::run(m_vsProgram, attribs, m_vsRegs, 64, kcache, vertexSample);
            if (params)
                *params = o.params;
            if (paramValid)
                *paramValid = o.paramValid;
            if (dbg) {
                LatteVsInterp::setDebugOnce(false);
                std::fprintf(stderr, "[VSDBG] draw#%u -> valid=%d pos=(%g,%g,%g,%g)\n", m_stats.draws, o.valid ? 1 : 0, o.pos[0],
                             o.pos[1], o.pos[2], o.pos[3]);
            }
            if (sum) {
                const std::uint32_t h = shaderHash(m_vsProgram);
                std::fprintf(stderr,
                             "[VSSUM] draw#%02u tgt=0x%08X(%ux%u) prog=%zudw hash=%08X stride=%u tex=0x%08X %ux%u | pos=(%g,%g,%g,%g)"
                             " valid=%d p0=(%g,%g,%g,%g)%c p1=(%g,%g,%g,%g)%c | c4=(%g,%g,%g,%g) c5=(%g,%g,%g,%g) c7=(%g,%g,%g,%g) c8=(%g,%g,%g,%g)"
                             " c9=(%g,%g,%g,%g) c15=(%g,%g,%g,%g) c16=(%g,%g,%g,%g)\n",
                             m_stats.draws, m_color.imagePtr, m_color.width, m_color.height, m_vsProgram.size(), h, ab.stride,
                             m_texture.imagePtr, m_texture.width, m_texture.height,
                             o.pos[0], o.pos[1], o.pos[2], o.pos[3], o.valid ? 1 : 0, o.params[0][0], o.params[0][1],
                             o.params[0][2], o.params[0][3], o.paramValid[0] ? 'v' : '-', o.params[1][0], o.params[1][1],
                             o.params[1][2], o.params[1][3], o.paramValid[1] ? 'v' : '-', m_vsRegs[16], m_vsRegs[17],
                             m_vsRegs[18], m_vsRegs[19], m_vsRegs[20], m_vsRegs[21], m_vsRegs[22], m_vsRegs[23], m_vsRegs[28],
                             m_vsRegs[29], m_vsRegs[30], m_vsRegs[31], m_vsRegs[32], m_vsRegs[33], m_vsRegs[34], m_vsRegs[35],
                             m_vsRegs[36], m_vsRegs[37], m_vsRegs[38], m_vsRegs[39], m_vsRegs[60], m_vsRegs[61], m_vsRegs[62],
                             m_vsRegs[63], m_vsRegs[64], m_vsRegs[65], m_vsRegs[66], m_vsRegs[67]);
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
                const bool projectionXyMissing = m_vsRegs[32] == 0.0f && m_vsRegs[33] == 0.0f && m_vsRegs[34] == 0.0f
                                                 && m_vsRegs[35] == 0.0f && m_vsRegs[36] == 0.0f && m_vsRegs[37] == 0.0f
                                                 && m_vsRegs[42] < -0.9f && m_vsRegs[42] > -1.1f && m_vsRegs[43] > 100.0f;
                const float *rowX = &m_vsRegs[16]; // c4
                const float *rowY = &m_vsRegs[20]; // c5
                const float *rect = &m_vsRegs[60]; // c15: local scale/offset in observed UI shaders
                const bool plausibleRect = std::isfinite(rect[0]) && std::isfinite(rect[1]) && std::isfinite(rect[2])
                                           && std::isfinite(rect[3]) && std::fabs(rect[0]) > 2.0f && std::fabs(rect[1]) > 2.0f
                                           && std::fabs(rect[0]) < 4096.0f && std::fabs(rect[1]) < 4096.0f;
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
                    if (std::isfinite(tsx) && std::isfinite(tsy) && tsx > -static_cast<float>(kWidth) * 2.0f
                        && tsx < static_cast<float>(kWidth) * 3.0f && tsy > -static_cast<float>(kHeight) * 2.0f
                        && tsy < static_cast<float>(kHeight) * 3.0f) {
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
            if (collapsed && ab.stride == 8) {
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
            if (collapsed && m_vsRegs[32] == 0.0f && m_vsRegs[33] == 0.0f && m_vsRegs[34] == 0.0f && m_vsRegs[35] == 0.0f
                && m_vsRegs[36] == 0.0f && m_vsRegs[37] == 0.0f)
                return false;
            const float w = std::fabs(o.pos[3]) > 1e-6f ? o.pos[3] : 1.0f;
            sx = m_vp[0] + (o.pos[0] / w * 0.5f + 0.5f) * m_vp[2];
            sy = m_vp[1] + (0.5f - o.pos[1] / w * 0.5f) * m_vp[3]; // NDC y-up -> raster y-down
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
                std::array<std::array<float, 4>, 4> params{
                    {{1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}};
                std::array<bool, 4> paramValid{};
        };

        auto interpolateParams = [](const RasterVertex (&rv)[3], const float w0, const float w1, const float w2) {
            std::array<std::array<float, 4>, 4> out{};
            for (int p = 0; p < 4; p++)
                for (int c = 0; c < 4; c++)
                    out[p][c] = w0 * rv[0].params[p][c] + w1 * rv[1].params[p][c] + w2 * rv[2].params[p][c];
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
                    Utils::Log::error("[GX2TRACE]   vs#{} out v0 screen=({:.1f},{:.1f}) uv=({:.3f},{:.3f}) prog={}dw", m_stats.draws,
                                      s0x, s0y, su, sv, m_vsProgram.size());
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
                        for (std::size_t i = 0; i + 3 < m_vsBlocks[b].size() && i < 160 * 4; i += 4)
                            std::fprintf(fp, "b%zu.%-3zu %g %g %g %g\n", b, i / 4, asFloat(m_vsBlocks[b][i]),
                                         asFloat(m_vsBlocks[b][i + 1]), asFloat(m_vsBlocks[b][i + 2]), asFloat(m_vsBlocks[b][i + 3]));
                    }
                    std::fclose(fp);
                }
            }
            Utils::Log::error("[GX2TRACE] draw#{} mode={} cnt={} stride={} tex=0x{:08X}(f{:02X},t{},{}x{}) tgt=0x{:08X}({}x{}) "
                              "v0=({:.2f},{:.2f})uv({:.2f},{:.2f})->({:.0f},{:.0f}) midTexel=({},{},{},{})",
                              m_stats.draws, mode, count, ab.stride, m_texture.imagePtr, m_texture.format & 0x3F, m_texture.tileMode,
                              m_texture.width, m_texture.height, m_color.imagePtr, m_color.width, m_color.height, tx0, ty0, tu0, tv0, s0x,
                              s0y, mid[0], mid[1], mid[2], mid[3]);
            if (!m_psProgram.empty()) {
                std::size_t psBlockWords = 0;
                for (const auto &blk: m_psBlocks)
                    psBlockWords += blk.size();
                Utils::Log::error(
                    "[PSSUM] draw#{} psProg={}dw hash={:08X} psRegsFresh={} p0=({:.4g},{:.4g},{:.4g},{:.4g})"
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
        const bool residentTextures = residentTexturesEnabled && m_rasterBackend
            && m_rasterBackend->supportsRenderedTextures();
        const auto textureBacking = [&](const Surface &surface) -> const std::vector<std::uint8_t> * {
            if (!residentTextures) return findViewBacking(surface);
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
        } else if (m_texture.imagePtr && texTile <= 1 && texFmt != 0x01 && texFmt != 0x07 && texFmt != 0x31 && texFmt != 0x32
                   && texFmt != 0x33 && texFmt != 0x34 && texFmt != 0x35) {
            texBase = m_mem->hostPtr(m_texture.imagePtr);
            if (texBase && !m_mem->hostPtr(m_texture.imagePtr + (m_texture.pitch * (m_texture.height - 1) + m_texture.width) * 4 - 1))
                texBase = nullptr;
        }
        auto sampleTexel = [&](float uu, float vv) -> std::array<std::uint8_t, 4> {
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
        const auto pixelProgram = pixelExecution && !m_psProgram.empty() ? LatteVsInterp::compile(m_psProgram) : nullptr;
        std::array<int, 4> pixelInputParams{-1, -1, -1, -1};
        for (unsigned i = 0; i < pixelInputParams.size(); i++) {
            const auto semantic = m_pixelInputSemantics[i];
            const auto it = std::find(m_vertexOutputSemantics.begin(), m_vertexOutputSemantics.end(), semantic);
            if (semantic != 255 && it != m_vertexOutputSemantics.end())
                pixelInputParams[i] = std::distance(m_vertexOutputSemantics.begin(), it);
        }
        // CPU feedback snapshots are created only if software rasterization runs.
        // Native inputs are consumed before committing output, or use GPU versions.
        std::array<std::vector<std::uint8_t>, 16> pixelFeedback;
        std::array<const std::vector<std::uint8_t> *, 16> pixelBackings{};
        for (unsigned i = 0; pixelProgram && i < m_pixelTextures.size(); i++) {
            if (useViewBacking() && m_pixelTextures[i].dimension != 5)
                pixelBackings[i] = textureBacking(m_pixelTextures[i]);
        }
        bool softwareInputsReady = false;
        const auto prepareSoftwareInputs = [&] {
            if (softwareInputsReady) return;
            resolvePendingBackings();
            if (texBacking && makeSurfaceKey(m_color) == makeSurfaceKey(m_texture)) {
                textureSnapshot = *texBacking;
                texBase = textureSnapshot.data();
            }
            for (unsigned i = 0; pixelProgram && i < m_pixelTextures.size(); ++i) {
                if (makeSurfaceKey(m_pixelTextures[i]) == makeSurfaceKey(m_color)) {
                    if (const auto *backing = findViewBacking(m_pixelTextures[i])) {
                        pixelFeedback[i] = *backing;
                        if (!pixelFeedback[i].empty()) pixelBackings[i] = &pixelFeedback[i];
                    }
                }
            }
            softwareInputsReady = true;
        };
        const LatteVsInterp::TextureSample pixelSample = [&](unsigned resource, unsigned sampler, const std::array<float, 4> &coords,
                                                             std::array<float, 4> &rgba) {
            if (resource >= m_pixelTextures.size() || sampler >= m_pixelSamplers.size())
                return false;
            const auto &surface = m_pixelTextures[resource];
            rgba = sampleTexture(surface, m_pixelSamplers[sampler], coords[0], coords[1],
                                 coords[2],
                                 pixelBackings[resource]);
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
            if (resource >= m_pixelTextures.size() || sampler >= m_pixelSamplers.size()
                || !supportsGather(m_pixelTextures[resource], m_pixelSamplers[sampler])) return false;
            rgba = sampleTexture(m_pixelTextures[resource], m_pixelSamplers[sampler], coords[0], coords[1], 0,
                                 pixelBackings[resource], true);
            return true;
        };
        bool pixelFallbackReported = false;
        auto shadeTexel = [&](float u, float v, const std::array<std::array<float, 4>, 4> &params) {
            if (pixelProgram) {
                std::array<std::array<float, 4>, 4> inputs{};
                for (unsigned i = 0; i < inputs.size(); i++)
                    if (pixelInputParams[i] >= 0)
                        inputs[i] = params[pixelInputParams[i]];
                const auto pixel = LatteVsInterp::runPixel(*pixelProgram, inputs, m_psRegs, 64, pixelSample, pixelConstants, pixelGather);
                if (pixel.colorValid)
                    return std::array<std::uint8_t, 4>{byteFromUnit(pixel.color[0]), byteFromUnit(pixel.color[1]),
                                                       byteFromUnit(pixel.color[2]), byteFromUnit(pixel.color[3])};
                if (m_trace && !pixelFallbackReported) {
                    Utils::Log::error("[GX2TRACE] draw#{} unsupported pixel execution; using legacy texture approximation",
                                      m_stats.draws);
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

        const int sx0 = std::max(m_scissor[0], 0);
        const int sy0 = std::max(m_scissor[1], 0);
        const int sx1 = std::min<std::int32_t>(m_scissor[0] + m_scissor[2], static_cast<std::int32_t>(m_color.width));
        const int sy1 = std::min<std::int32_t>(m_scissor[1] + m_scissor[3], static_cast<std::int32_t>(m_color.height));
        const bool targetBlendEnabled = ((m_targetBlendEnable >> (m_colorTarget & 7)) & 1) != 0;
        const BlendState &blend = m_blend[m_colorTarget & 7];
        const std::uint32_t channelMask = m_colorWriteEnable ? (m_channelMask[m_colorTarget & 7] & 0xF) : 0;

        // Array slices can alias a target without matching its base-view key.
        // Keep those draws and raw guest-memory targets serial until all aliases
        // participate in the same pre-draw snapshot scheme.
        const bool parallelSafe = useViewBacking() && !m_trace && m_color.pitch >= m_color.width
            && m_texture.dimension != 5
            && std::none_of(m_pixelTextures.begin(), m_pixelTextures.end(),
                            [](const auto &surface) { return surface.imagePtr && surface.dimension == 5; });
        const auto workerCount = [&] {
            if (m_rasterWorkerCount) return m_rasterWorkerCount;
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
            if (!value || *value < '0' || *value > '9') return std::uint64_t(0);
            char *end = nullptr;
            const auto interval = std::strtoull(value, &end, 10);
            return end != value && *end == '\0' ? std::uint64_t(interval) : std::uint64_t(0);
        }();
        const auto verifyEvery = m_rasterVerifyEvery.value_or(verifyEnvironment);

        // Defer geometry only when vertex execution cannot sample an earlier
        // triangle's output. Vertex-texture draws retain immediate execution.
        const bool collectBackend = m_rasterBackend && !m_gpuRenderer && pixelProgram && useViewBacking()
            && m_color.pitch >= m_color.width && !m_color.aa
            && std::none_of(m_vertexTextures.begin(), m_vertexTextures.end(),
                            [&](const auto &surface) { return surface.imagePtr &&
                                (surface.dimension == 5 || makeSurfaceKey(surface) == makeSurfaceKey(m_color)); });
        std::vector<std::array<RasterVertex, 3>> preparedTriangles;
        auto rasterPrepared = [&](const RasterVertex (&rv)[3]) {
            prepareSoftwareInputs();
            const float area = (rv[1].x - rv[0].x) * (rv[2].y - rv[0].y) - (rv[2].x - rv[0].x) * (rv[1].y - rv[0].y);
            if (std::fabs(area) < 0.5f)
                return;
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
            const TriangleCoverage coverage({rv[0].x, rv[0].y}, {rv[1].x, rv[1].y}, {rv[2].x, rv[2].y});
            // Incremental barycentrics: w0/w1 are affine in x and y — one add per pixel instead
            // of the full edge-function evaluation (and no per-pixel divides).
            // Accumulate in double so long scanlines do not drift across texel
            // centers. Guest shader inputs are still converted to float below.
            const double preciseArea = (double(rv[1].x) - rv[0].x) * (double(rv[2].y) - rv[0].y)
                - (double(rv[2].x) - rv[0].x) * (double(rv[1].y) - rv[0].y);
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
                    std::uint8_t *row = target + static_cast<std::size_t>(yy) * m_color.pitch * 4;
                    double weight0 = result.w0, weight1 = result.w1;
                    for (int xx = minX; xx <= maxX; xx++, weight0 += w0dx, weight1 += w1dx) {
                        const float w0 = float(weight0), w1 = float(weight1), w2 = float(1.0 - weight0 - weight1);
                        if (!coverage.contains(double(xx) + 0.5, double(yy) + 0.5))
                            continue;
                        const auto texel = shadeTexel(w0 * rv[0].u + w1 * rv[1].u + w2 * rv[2].u,
                                                     w0 * rv[0].v + w1 * rv[1].v + w2 * rv[2].v,
                                                     interpolateParams(rv, w0, w1, w2));
                        std::uint8_t *dst = row + static_cast<std::size_t>(xx) * 4;
                        const std::uint32_t a = texel[3];
                        result.pixels++;
                        result.alphaSum += a;
                        const auto out = targetBlendEnabled ? blendTexel(texel, dst, blend) : texel;
                        if (channelMask & 0x1)
                            dst[0] = out[0];
                        if (channelMask & 0x2)
                            dst[1] = out[1];
                        if (channelMask & 0x4)
                            dst[2] = out[2];
                        if (channelMask & 0x8)
                            dst[3] = out[3];
                    }
                }
            };
            const auto areaPixels = std::uint64_t(maxX - minX + 1) * rows.size();
            if (parallelSafe && workerCount > 1 && rows.size() > 1 && areaPixels >= m_parallelMinPixels) {
                if (!m_rasterWorkers) m_rasterWorkers = std::make_unique<RasterWorkers>(workerCount);
                const bool verify = verifyEvery && (m_parallelTriangles % verifyEvery == 0);
                ++m_parallelTriangles;
                std::vector<std::uint8_t> before;
                const auto targetBytes = std::size_t(m_color.pitch) * m_color.height * 4;
                if (verify) before.assign(target, target + targetBytes);
                m_rasterWorkers->run(unsigned(rows.size()), rasterRows);
                if (verify) {
                    // Reuse the same shader inputs and immutable feedback snapshots.
                    // Restore all target bytes so blending starts from identical data.
                    const std::vector<std::uint8_t> parallelOutput(target, target + targetBytes);
                    const auto parallelRows = rows;
                    std::copy(before.begin(), before.end(), target);
                    for (auto &row : rows) row.pixels = row.alphaSum = 0;
                    rasterRows(0, unsigned(rows.size()));
                    const auto mismatch = std::mismatch(parallelOutput.begin(), parallelOutput.end(), target);
                    const bool countersMatch = std::equal(rows.begin(), rows.end(), parallelRows.begin(),
                        [](const Row &a, const Row &b) { return a.pixels == b.pixels && a.alphaSum == b.alphaSum; });
                    if (mismatch.first != parallelOutput.end() || !countersMatch)
                        throw std::runtime_error(std::format(
                            "Raster verification mismatch: frame={} draw={} triangle={} target=0x{:08X} byte={}",
                            m_presentCount, m_stats.draws, m_stats.tris, m_color.imagePtr,
                            std::distance(parallelOutput.begin(), mismatch.first)));
                    ++m_verifiedRasterTriangles;
                    Utils::Log::error("[RASTER_VERIFY] matched={} eligible={} frame={} ps={:016X}",
                                      m_verifiedRasterTriangles, m_parallelTriangles, m_presentCount, shaderHash(m_psProgram));
                }
            } else {
                rasterRows(0, unsigned(rows.size()));
            }
            for (const auto &row : rows) {
                m_stats.pixels += row.pixels;
                m_stats.alphaSum += row.alphaSum;
            }
        };

        auto rasterTri = [&](const std::uint32_t i0, const std::uint32_t i1, const std::uint32_t i2) {
            const std::uint32_t vis[3] = {idx[i0], idx[i1], idx[i2]};
            RasterVertex rv[3];
            for (int i = 0; i < 3; i++) {
                if (runShader(vis[i], rv[i].x, rv[i].y, rv[i].u, rv[i].v, &rv[i].params, &rv[i].paramValid)) {
                    if (!rv[i].paramValid[0])
                        rv[i].params[0] = {1.0f, 1.0f, 1.0f, 1.0f};
                    continue;
                }
                float x = 0, y = 0;
                if (!fetch(vis[i], x, y, rv[i].u, rv[i].v))
                    return;
                mapVertex(x, y, rv[i].x, rv[i].y);
            }
            if (collectBackend)
                preparedTriangles.push_back({rv[0], rv[1], rv[2]});
            else
                rasterPrepared(rv);
        };
        if (mode == 0x13) { // QUADS
            for (std::uint32_t i = 0; i + 3 < idx.size(); i += 4) {
                rasterTri(i, i + 1, i + 2);
                rasterTri(i, i + 2, i + 3);
            }
        } else { // treat everything else as a triangle list
            for (std::uint32_t i = 0; i + 2 < idx.size(); i += 3)
                rasterTri(i, i + 1, i + 2);
        }
        std::int64_t nativePrepareUs{}, nativeExecuteUs{}, nativeVerifyUs{}, fallbackUs{};
        bool nativeCommitted = false;
        if (collectBackend && !preparedTriangles.empty()) {
            const auto prepareStart = (profile || nativeAudit) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            std::vector<Latte::TextureType> resourceTypes(m_pixelTextures.size(), Latte::TextureType::TwoD);
            for (unsigned i = 0; i < m_pixelTextures.size(); ++i)
                if (m_pixelTextures[i].dimension == 5) resourceTypes[i] = Latte::TextureType::TwoDArray;
            const auto translatedShader = m_fragmentShaderCache.get(m_psProgram, resourceTypes);
            const auto &shader = *translatedShader;
            bool supported = bool(shader);
            std::vector<RasterDraw::Texture> textures;
            std::vector<std::vector<std::uint8_t>> ownedTextures;
            std::vector<std::vector<float>> ownedFloatTextures;
            std::vector<std::shared_ptr<ArraySnapshot>> arrays;
            ownedTextures.reserve(shader.textures.size());
            ownedFloatTextures.reserve(shader.textures.size());
            for (const auto &binding: shader.textures) {
                if (binding.resource >= m_pixelTextures.size() || binding.sampler >= m_pixelSamplers.size()) {
                    supported = false;
                    break;
                }
                const auto surface = m_pixelTextures[binding.resource];
                if (shader.usesGather && !supportsGather(surface, m_pixelSamplers[binding.sampler])) {
                    supported = false;
                    break;
                }
                if (surface.dimension == 5) {
                    auto snapshot = arraySnapshot(surface);
                    if (!snapshot) { supported = false; break; }
                    textures.push_back({binding, surface.width, surface.height, m_pixelSamplers[binding.sampler],
                        [&, surface](unsigned x, unsigned y) {
                            return sampleTexture(surface, TextureSampler{}, (float(x) + 0.5f) / surface.width,
                                                 (float(y) + 0.5f) / surface.height, 0, nullptr);
                        }});
                    textures.back().unorm8 = snapshot->rgba;
                    textures.back().unorm8Pitch = surface.width;
                    textures.back().unorm8Map = surface.compMap;
                    textures.back().layers = surface.sliceCount;
                    arrays.push_back(std::move(snapshot));
                    continue;
                }
                if (!surface.imagePtr || !surface.width || !surface.height || surface.dimension != 1 || surface.aa
                    || surface.firstMip || surface.firstSlice || surface.sliceCount != 1) {
                    supported = false;
                    break;
                }
                const auto *backing = pixelBackings[binding.resource];
                const auto format = surface.format & 0x3F;
                // Match sample()'s four-byte representation, with typed R32 float
                // conversion below. Other float/integer formats remain incomplete.
                const bool rawBytes = format < 0x31 || format > 0x35;
                const unsigned texelBytes = format == 1 ? 1 : format == 7 ? 2 : 4;
                textures.push_back({binding, surface.width, surface.height, m_pixelSamplers[binding.sampler],
                    [&, surface, backing](unsigned x, unsigned y) {
                        if (x >= surface.width || y >= surface.height)
                            throw std::out_of_range("Raster backend texel coordinate");
                        return sampleTexture(surface, TextureSampler{}, (float(x) + 0.5f) / float(surface.width),
                                             (float(y) + 0.5f) / float(surface.height), 0, backing);
                    }});
                if (residentTextures) {
                    const auto pending = m_pendingReadbacks.find(makeSurfaceKey(surface));
                    if (pending != m_pendingReadbacks.end()) {
                        textures.back().rendered = pending->second;
                        textures.back().unorm8Pitch = surface.pitch;
                        textures.back().unorm8Map = surface.compMap;
                        continue;
                    }
                }
                if (backing && surface.pitch >= surface.width
                    && std::uint64_t(surface.pitch) * surface.height * 4 <= backing->size()) {
                    textures.back().unorm8 = *backing;
                    textures.back().unorm8Pitch = surface.pitch;
                    textures.back().unorm8Map = surface.compMap;
                } else if (rawBytes && surface.imageSize
                           && (surface.tileMode <= 4 || surface.tileMode == 16)
                           && std::uint64_t(surface.width) * surface.height * 4 <= 64ull * 1024 * 1024
                           && std::uint64_t(surface.imagePtr) + surface.imageSize <= 0x100000000ull && m_mem) {
                    const auto *base = m_mem->hostPtr(surface.imagePtr);
                    const auto *last = m_mem->hostPtr(surface.imagePtr + surface.imageSize - 1);
                    if (base && last && std::uintptr_t(last) - std::uintptr_t(base) == surface.imageSize - 1) {
                        if (surface.format != 0x80E && (surface.tileMode <= 1 || surface.tileMode == 16)
                            && surface.pitch >= surface.width
                            && std::uint64_t(surface.pitch) * surface.height * texelBytes <= surface.imageSize) {
                            textures.back().unorm8 = {base, std::size_t(surface.pitch) * surface.height * texelBytes};
                            textures.back().unorm8Pitch = surface.pitch;
                            textures.back().unorm8Map = surface.compMap;
                            textures.back().unorm8Channels = texelBytes;
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
                            if (surface.format == 0x80E) {
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
            std::vector<RasterDraw::Vertex> vertices;
            for (const auto &triangle: preparedTriangles) {
                const auto &a = triangle[0], &b = triangle[1], &c = triangle[2];
                const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                if (std::fabs(area) < 0.5f) continue;
                for (const auto &rv: triangle) {
                    if (!std::isfinite(rv.x) || !std::isfinite(rv.y)) supported = false;
                    RasterDraw::Vertex vertex{rv.x, rv.y, {}};
                    for (unsigned i = 0; i < pixelInputParams.size(); ++i)
                        if (pixelInputParams[i] >= 0)
                            vertex.inputs[i] = rv.params[pixelInputParams[i]];
                    vertices.push_back(vertex);
                }
            }
            bool committed = false;
            if (profile || nativeAudit)
                nativePrepareUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - prepareStart).count();
            if (supported && !vertices.empty()) {
                const auto targetBytes = std::size_t(m_color.pitch) * m_color.height * 4;
                RasterDraw draw{shader, vertices, m_psRegs, textures, {target, targetBytes},
                    m_color.width, m_color.height, m_color.pitch,
                    {m_scissor[0], m_scissor[1], m_scissor[2], m_scissor[3]}, channelMask,
                    {targetBlendEnabled, blend.colorSrcBlend, blend.colorDstBlend, blend.colorCombine,
                     blend.useAlphaBlend ? blend.alphaSrcBlend : blend.colorSrcBlend,
                     blend.useAlphaBlend ? blend.alphaDstBlend : blend.colorDstBlend,
                     blend.useAlphaBlend ? blend.alphaCombine : blend.colorCombine, m_blendConstant}};
                draw.renderedTarget = renderedTarget;
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
                    static const std::uint64_t nativeVerifyEnvironment = [] {
                        const auto *value = std::getenv("WEMU_NATIVE_VERIFY_EVERY");
                        if (!value || *value < '0' || *value > '9') return std::uint64_t(0);
                        char *end = nullptr;
                        const auto interval = std::strtoull(value, &end, 10);
                        return end != value && *end == '\0' ? std::uint64_t(interval) : std::uint64_t(0);
                    }();
                    const auto interval = m_nativeVerifyEvery.value_or(nativeVerifyEnvironment);
                    const bool compare = interval && m_nativeDraws % interval == 0;
                    ++m_nativeDraws;
                    if (compare) {
                        const auto verifyStart = (profile || nativeAudit) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                        result->resolve();
                        // The backend has not changed target; feedback snapshots and
                        // prepared vertices are shared by both executions of this draw.
                        const auto savedStats = m_stats;
                        for (const auto &triangle : preparedTriangles) {
                            const RasterVertex rv[]{triangle[0], triangle[1], triangle[2]};
                            rasterPrepared(rv);
                        }
                        m_stats = savedStats;
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
                            m_presentCount, m_stats.draws, shaderHash(m_psProgram), targetBytes,
                            different, overOne, maxError, first);
                        if (different && (m_differingNativeDraws == 1 || maxError > m_nativeWorstError)) {
                            m_nativeWorstError = std::max(m_nativeWorstError, maxError);
                            const auto pixel = (worst / 4) * 4;
                            Utils::Log::error("[NATIVE_VERIFY_DETAIL] x={} y={} ref=({},{},{},{}) native=({},{},{},{}) "
                                "channel_different=({},{},{},{}) channel_over_one=({},{},{},{}) vertices={} textures={}",
                                (pixel / 4) % m_color.pitch, (pixel / 4) / m_color.pitch,
                                target[pixel], target[pixel + 1], target[pixel + 2], target[pixel + 3],
                                result->rgba[pixel], result->rgba[pixel + 1], result->rgba[pixel + 2], result->rgba[pixel + 3],
                                channelDifferences[0], channelDifferences[1], channelDifferences[2], channelDifferences[3],
                                channelOverOne[0], channelOverOne[1], channelOverOne[2], channelOverOne[3],
                                vertices.size(), textures.size());
                            for (const auto &texture : textures)
                                Utils::Log::error("[NATIVE_VERIFY_TEXTURE] resource={} sampler={} size={}x{} map={:08X} "
                                    "sampler0={:08X} snapshot={} r32={} format={:X}", texture.binding.resource, texture.binding.sampler,
                                    texture.width, texture.height, m_pixelTextures[texture.binding.resource].compMap,
                                    texture.sampler.regs[0], !texture.unorm8.empty(), !texture.r32.empty(),
                                    m_pixelTextures[texture.binding.resource].format);
                            Utils::Log::error("[NATIVE_VERIFY_BLEND] enabled={} color=({},{},{}) alpha=({},{},{}) mask={:X}",
                                draw.blend.enabled, draw.blend.colorSource, draw.blend.colorDestination, draw.blend.colorOperation,
                                draw.blend.alphaSource, draw.blend.alphaDestination, draw.blend.alphaOperation, draw.channelMask);
                            for (unsigned i = 0; i < std::min<std::size_t>(vertices.size(), 6); ++i)
                                Utils::Log::error("[NATIVE_VERIFY_VERTEX] i={} pos=({:.5f},{:.5f}) param0=({:.5f},{:.5f},{:.5f},{:.5f})",
                                    i, vertices[i].x, vertices[i].y, vertices[i].inputs[0][0], vertices[i].inputs[0][1],
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
                        if (profile || nativeAudit)
                            nativeVerifyUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - verifyStart).count();
                    }
                    if (result->readback)
                        m_pendingReadbacks[makeSurfaceKey(m_color)] = std::move(result->readback);
                    else {
                        m_pendingReadbacks.erase(makeSurfaceKey(m_color));
                        std::copy(result->rgba.begin(), result->rgba.end(), target);
                    }
                    m_stats.tris += vertices.size() / 3;
                    m_stats.pixels += result->pixels;
                    m_stats.alphaSum += result->alphaSum;
                    committed = true;
                }
            }
            if (!committed) {
                const auto fallbackStart = (profile || nativeAudit) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                for (const auto &triangle: preparedTriangles) {
                    const RasterVertex rv[]{triangle[0], triangle[1], triangle[2]};
                    rasterPrepared(rv);
                }
                if (profile || nativeAudit)
                    fallbackUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - fallbackStart).count();
            }
            nativeCommitted = committed;
            if (profile || nativeAudit)
                Utils::Log::error("[NATIVERASTER] frame={} draw={} committed={}", m_presentCount, m_stats.draws, committed);
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
                    m_gpuRenderer->gpuDrawTexture(m_texture.imagePtr, it->second.rgba.data(), it->second.w, it->second.h,
                                                  m_gpuTri.data(), verts);
                else
                    m_gpuRenderer->gpuDrawTriangles(m_texture.imagePtr, it->second.rgba.data(), it->second.w, it->second.h,
                                                    m_gpuTri.data(), verts);
            }
            m_gpuTri.clear();
        }
        if (m_trace && shouldDumpAfterDraw(m_stats.draws)) {
            char kind[32];
            std::snprintf(kind, sizeof(kind), "after_draw%03u", m_stats.draws);
            dumpSurface(m_color, kind, true);
        }
        m_regsFresh = false; // uniform regs are per-draw: the next draw needs its own set
        m_psRegsFresh = false;
        if (profile || nativeAudit) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - profileStart).count();
            std::uint64_t hash = 14695981039346656037ull;
            for (const auto word : m_psProgram) {
                for (unsigned shift = 0; shift < 32; shift += 8) {
                    hash ^= (word >> shift) & 255;
                    hash *= 1099511628211ull;
                }
            }
            Utils::Log::error("[RASTER] frame={} draw={} us={} pixels={} ps={:016X} words={} target={}x{}",
                              m_presentCount, m_stats.draws, elapsed, m_stats.pixels - pixelsBefore,
                              hash, m_psProgram.size(), m_color.width, m_color.height);
            Utils::Log::error("[RASTER_STAGES] frame={} draw={} native={} prepare_us={} execute_us={} verify_us={} fallback_us={} other_us={}",
                              m_presentCount, m_stats.draws, nativeCommitted, nativePrepareUs, nativeExecuteUs, nativeVerifyUs, fallbackUs,
                              elapsed - nativePrepareUs - nativeExecuteUs - nativeVerifyUs - fallbackUs);
            if (nativeAudit) {
                auto found = m_nativeAuditCache.find(m_psProgram);
                if (found == m_nativeAuditCache.end()) {
                    if (m_nativeAuditCache.size() >= 128)
                        m_nativeAuditCache.clear();
                    found = m_nativeAuditCache.emplace(m_psProgram, Latte::lowerFragmentShader(*Latte::decodeProgram(m_psProgram))).first;
                    const auto &shader = found->second;
                    Utils::Log::error("[NATIVEPS] ps={:016X} lowered={} textures={} base_only={} reason={}",
                                      hash, bool(shader), shader.textures.size(), shader.requiresBaseLevelOnly,
                                      shader ? "supported shader subset" : shader.error);
                }
                const auto &shader = found->second;
                const auto &blend = m_blend[m_colorTarget & 7];
                Utils::Log::error("[NATIVEDRAW] frame={} draw={} ps={:016X} lowered={} target_fmt={:X} aa={} slot={} mask={:X} "
                                  "blend={} color={},{},{} alpha={},{},{} scissor={},{},{},{}",
                                  m_presentCount, m_stats.draws, hash, bool(shader), m_color.format, m_color.aa, m_colorTarget,
                                  channelMask, targetBlendEnabled,
                                  blend.colorSrcBlend, blend.colorDstBlend, blend.colorCombine,
                                  blend.useAlphaBlend ? blend.alphaSrcBlend : blend.colorSrcBlend,
                                  blend.useAlphaBlend ? blend.alphaDstBlend : blend.colorDstBlend,
                                  blend.useAlphaBlend ? blend.alphaCombine : blend.colorCombine,
                                  m_scissor[0], m_scissor[1], m_scissor[2], m_scissor[3]);
                for (const auto &binding: shader.textures) {
                    if (binding.resource >= m_pixelTextures.size() || binding.sampler >= m_pixelSamplers.size()) {
                        Utils::Log::error("[NATIVETEX] resource={} sampler={} out_of_range=1", binding.resource, binding.sampler);
                        continue;
                    }
                    const auto &texture = m_pixelTextures[binding.resource];
                    const auto &sampler = m_pixelSamplers[binding.sampler];
                    Utils::Log::error("[NATIVETEX] resource={} sampler={} size={}x{} dim={} fmt={:X} mip={} slice={} aa={} "
                                      "feedback={} regs={:08X},{:08X},{:08X}",
                                      binding.resource, binding.sampler, texture.width, texture.height, texture.dimension,
                                      texture.format, texture.firstMip, texture.firstSlice, texture.aa,
                                      makeSurfaceKey(texture) == makeSurfaceKey(m_color), sampler.regs[0], sampler.regs[1], sampler.regs[2]);
                }
            }
        }
    }

    void Gx2Replayer::copyToScanBuffer(const Surface &s)
    {
        buildClearFrame();
        m_stats.scanPtr = s.imagePtr;
        if (!s.imagePtr)
            return;
        const auto *backing = useViewBacking() ? findViewBacking(s) : nullptr;
        const std::uint8_t *base = backing ? backing->data() : nullptr;
        if (!base) {
            if (!m_mem)
                return;
            base = m_mem->hostPtr(s.imagePtr);
            if (!base || !m_mem->hostPtr(s.imagePtr + s.pitch * s.height * 4 - 1))
                return;
        }
        const std::uint32_t rows = std::min(kHeight, s.height);
        const std::uint32_t cols = std::min(kWidth, s.width);
        for (std::uint32_t y = 0; y < rows; y++) {
            const std::uint8_t *src = base + static_cast<std::size_t>(y) * s.pitch * 4;
            std::uint8_t *dst = m_fb.data() + static_cast<std::size_t>(y) * kWidth * 4;
            std::memcpy(dst, src, static_cast<std::size_t>(cols) * 4);
            if ((y & 15) == 0) // nonzero telemetry: sample every 16th row (it's a health metric)
                for (std::uint32_t x = 0; x < cols * 4; x++)
                    m_stats.scanNonZero += (src[x] != 0);
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
        if (!force && (std::find(dumped.begin(), dumped.end(), s.imagePtr) != dumped.end() || (!selectedImage && dumped.size() > 24)))
            return;
        if (!force)
            dumped.push_back(s.imagePtr);
        const std::uint8_t *base = m_mem->hostPtr(s.imagePtr);
        if (!base || !m_mem->hostPtr(s.imagePtr + s.pitch * s.height * 4 - 1))
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
            Utils::Log::error("[GX2] surface dumped: {} pitch={} swizzle={:08X} compMap={:08X}",
                              path, s.pitch, s.swizzle, s.compMap);
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
        const bool periodic = dumpAt == 0 && m_presentCount > 0 && (m_presentCount % dumpEvery) == 0;
        if ((!periodic && m_presentCount != dumpAt) || m_fb.empty())
            return;
        const char *dir = std::getenv("WEMU_FB_DUMP_DIR");
        const std::string path = std::string(dir ? dir : ".") + "/wemu_frame.ppm";
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
            if (written.width == view.width && written.height == view.height && written.pitch == view.pitch
                && (written.format & 0x3F) == (view.format & 0x3F) && written.tileMode == view.tileMode)
                return;
            const std::uint32_t frame = m_presentCount + 1;
            const std::uint64_t kindBit = (kind && kind[0] == 't') ? 1ull : 0ull;
            const std::uint64_t traceKey = (static_cast<std::uint64_t>(frame) << 33) ^ (static_cast<std::uint64_t>(view.imagePtr) << 1)
                                           ^ kindBit;
            if (!m_surfaceAliasTraced.insert(traceKey).second)
                return;
            Utils::Log::error(
                "[GX2] alias view frame#{} {} image=0x{:08X}: written@frame#{} writes={} {}x{} pitch={} f{:02X} t{} -> view {}x{} pitch={} f{:02X} t{}",
                frame, kind, view.imagePtr, writtenInfo.frame, writtenInfo.writes, written.width, written.height, written.pitch,
                written.format & 0x3F, written.tileMode, view.width, view.height, view.pitch, view.format & 0x3F, view.tileMode);
        };
        bool presented = false;
        for (const auto &cmd: stream.commands()) {
            switch (cmd.type) {
                case Gx2Cmd::ClearColor:
                case Gx2Cmd::ClearBuffersEx: {
                    // ClearColor(colorBuffer, r, g, b, a): colour in f1..f4 (captured to fpr[0..3]).
                    for (int i = 0; i < 4; i++)
                        m_clear[i] = static_cast<float>(cmd.fpr[i]);
                    const Surface cs = parseSurface(cmd);
                    if (m_gpuGraph && m_gpuRenderer)
                        m_gpuRenderer->gpuClearTarget(cs.imagePtr, cs.width, cs.height, m_clear);
                    else
                        clearSurface(cs, m_clear);
                    break;
                }
                case Gx2Cmd::SetColorBuffer:
                    m_color = parseSurface(cmd);
                    m_colorTarget = cmd.gpr[1] & 7;
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
                            for (unsigned i = 0; i < 3; i++) regs[i] = m_mem->read<std::uint32_t>(cmd.gpr[0] + 4 * i);
                    }
                    break;
                }
                case Gx2Cmd::SetPixelSamplerBorderColor:
                case Gx2Cmd::SetVertexSamplerBorderColor: {
                    auto &samplers = cmd.type == Gx2Cmd::SetVertexSamplerBorderColor ? m_vertexSamplers : m_pixelSamplers;
                    if (cmd.gpr[0] < samplers.size())
                        for (unsigned i = 0; i < 4; i++) samplers[cmd.gpr[0]].customBorder[i] = cmd.fpr[i];
                    break;
                }
                case Gx2Cmd::SetVertexTexture:
                case Gx2Cmd::SetPixelTexture: {
                    if (m_trace) { // UI panes may bind textures on units > 0 — log them all
                        const Surface t = parseSurface(cmd);
                        Utils::Log::error("[GX2TRACE] setPixTex unit={} img=0x{:08X} {}x{} f{:02X} t{}", cmd.gpr[1], t.imagePtr,
                                          t.width, t.height, t.format & 0x3F, t.tileMode);
                        if (cmd.payload.size() >= 0x9C / 4)
                            Utils::Log::error("[GX2TRACE] texture ptr={:08X} dim={} depth={} mip={}/{} slice={}/{} compMap={:08X} regs={:08X},{:08X},{:08X},{:08X},{:08X}",
                                              cmd.gpr[0], cmd.payload[0], cmd.payload[0xC / 4], cmd.payload[0x74 / 4],
                                              cmd.payload[0x78 / 4], cmd.payload[0x7C / 4], cmd.payload[0x80 / 4],
                                              cmd.payload[0x84 / 4], cmd.payload[0x88 / 4], cmd.payload[0x8C / 4],
                                              cmd.payload[0x90 / 4], cmd.payload[0x94 / 4], cmd.payload[0x98 / 4]);
                    }
                    auto &textures = cmd.type == Gx2Cmd::SetVertexTexture ? m_vertexTextures : m_pixelTextures;
                    if (cmd.gpr[1] < textures.size()) {
                        auto &texture = textures[cmd.gpr[1]];
                        texture = parseSurface(cmd);
                        if (cmd.payload.size() > 0x80 / 4) {
                            texture.firstMip = cmd.payload[0x74 / 4];
                            texture.firstSlice = cmd.payload[0x7C / 4];
                            texture.sliceCount = cmd.payload[0x80 / 4];
                        } else if (m_mem && cmd.gpr[0]) {
                            texture.firstMip = m_mem->read<std::uint32_t>(cmd.gpr[0] + 0x74);
                            texture.firstSlice = m_mem->read<std::uint32_t>(cmd.gpr[0] + 0x7C);
                            texture.sliceCount = m_mem->read<std::uint32_t>(cmd.gpr[0] + 0x80);
                        }
                        if (cmd.payload.size() > 0x84 / 4)
                            texture.compMap = cmd.payload[0x84 / 4];
                        else if (m_mem && cmd.gpr[0])
                            texture.compMap = m_mem->read<std::uint32_t>(cmd.gpr[0] + 0x84);
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
                            Utils::Log::error("[GX2FETCH] loc={} buf={} off={} fmt={:X} mask={:08X} endian={}",
                                              m_fetchLayout[i], m_fetchLayout[i+1], m_fetchLayout[i+2], m_fetchLayout[i+3],
                                              m_fetchLayout[i+6], m_fetchLayout[i+7]);
                    break;
                case Gx2Cmd::SetVertexShader: {
                    // Capture the shader bytecode (GX2VertexShader: size @+0xD0, program @+0xD4)
                    // so draws can run the real vertex transform via LatteVsInterp.
                    const std::uint32_t sh = cmd.gpr[0];
                    m_vsProgram.clear();
                    if (!m_mem || !sh)
                        break;
                    const auto semanticCount = std::min(m_mem->read<std::uint32_t>(sh + 0x40), 4u);
                    m_vertexSemantics = {0, 1, 2, 3};
                    for (std::uint32_t i = 0; i < semanticCount; i++)
                        m_vertexSemantics[i] = m_mem->read<std::uint32_t>(sh + 0x44 + i * 4) & 0xFF;
                    m_vertexOutputSemantics = {0, 1, 2, 3};
                    if (m_mem->read<std::uint32_t>(sh + 0xC)) {
                        const auto ids = m_mem->read<std::uint32_t>(sh + 0x10);
                        for (unsigned i = 0; i < 4; i++)
                            m_vertexOutputSemantics[i] = (ids >> (i * 8)) & 0xFF;
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
                        Utils::Log::error("[GX2] VS 0x{:08X}: size={} program=0x{:08X} -> {} dwords captured", sh, size, ptr,
                                          prog.size());
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
                    m_pixelInputSemantics = {0, 1, 2, 3};
                    const auto inputCount = std::min(m_mem->read<std::uint32_t>(sh + 0x10), 4u);
                    for (unsigned i = 0; i < inputCount; i++)
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
                        Utils::Log::error("[GX2] PS 0x{:08X}: size={} program=0x{:08X} -> {} dwords captured", sh, size, ptr,
                                          prog.size());
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
                            Utils::Log::error(
                                "[GX2TRACE] [beforeDraw#{}] setVsRegs off={} cnt={} (c{}..) ptr=0x{:08X} LR=0x{:08X} payload:{}",
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
                            Utils::Log::error(
                                "[GX2TRACE] [beforeDraw#{}] setPsRegs off={} cnt={} (p{}..) ptr=0x{:08X} LR=0x{:08X} payload:{}",
                                m_stats.draws, off, n, off / 4, cmd.gpr[2], cmd.callerLr, data);
                        }
                    }
                    break;
                }
                case Gx2Cmd::SetViewport:
                    for (int i = 0; i < 4; i++)
                        m_vp[i] = static_cast<float>(cmd.fpr[i]);
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
                                          b.colorSrcBlend, b.colorDstBlend, b.colorCombine, b.useAlphaBlend ? 1 : 0, b.alphaSrcBlend,
                                          b.alphaDstBlend, b.alphaCombine);
                    break;
                }
                case Gx2Cmd::SetBlendConstantColor:
                    for (std::size_t i = 0; i < m_blendConstant.size(); i++)
                        m_blendConstant[i] = byteFromUnit(cmd.fpr[i]);
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] blendConstant {} {} {} {}", m_blendConstant[0], m_blendConstant[1],
                                          m_blendConstant[2], m_blendConstant[3]);
                    break;
                case Gx2Cmd::SetColorControl:
                    m_targetBlendEnable = cmd.gpr[1] & 0xFF;
                    m_colorWriteEnable = cmd.gpr[3] != 0;
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] colorControl rop={} blendEnable=0x{:02X} multiWrite={} colorWrite={}", cmd.gpr[0],
                                          m_targetBlendEnable, cmd.gpr[2], m_colorWriteEnable ? 1 : 0);
                    break;
                case Gx2Cmd::SetTargetChannelMasks:
                    for (std::size_t i = 0; i < m_channelMask.size(); i++)
                        m_channelMask[i] = cmd.gpr[i] & 0xF;
                    if (m_trace)
                        Utils::Log::error("[GX2TRACE] channelMasks {:X} {:X} {:X} {:X} {:X} {:X} {:X} {:X}", m_channelMask[0],
                                          m_channelMask[1], m_channelMask[2], m_channelMask[3], m_channelMask[4], m_channelMask[5],
                                          m_channelMask[6], m_channelMask[7]);
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
                    if (cmd.gpr[1] & 1) {
                        m_lastScan = parseSurface(cmd);
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
                            Utils::Log::error(
                                "[GX2] target compare at frame #{} draws={} tris={} scan=0x{:08X} color=0x{:08X} lastDraw=0x{:08X}",
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
                    m_presentCount++;
                    if ((m_presentCount % 100) == 0)
                        Utils::Log::error("[GX2] replay #{}: draws={} tris={} px={} avgA={} skip(t/a/p)={}/{}/{} scan=0x{:08X} nz={}",
                                          m_presentCount, m_stats.draws, m_stats.tris, m_stats.pixels,
                                          m_stats.pixels ? m_stats.alphaSum / m_stats.pixels : 0, m_stats.skipNoTarget,
                                          m_stats.skipNoAttrib, m_stats.skipBadPtr, m_stats.scanPtr, m_stats.scanNonZero);
                    // WEMU_FB_DUMP_RICH=1: snapshot the frame whenever it beats the most triangles
                    // seen so far, to a distinctly-named PPM. The steady state is a 55-tri
                    // post-process pattern; any menu/scene geometry produces far more, and those
                    // frames are rare and timing-dependent, so catch them automatically.
                    if (std::getenv("WEMU_FB_DUMP_RICH") && m_stats.tris > m_richestTris && !m_fb.empty()) {
                        m_richestTris = m_stats.tris;
                        const char *dir = std::getenv("WEMU_FB_DUMP_DIR");
                        const std::string path =
                            std::string(dir ? dir : ".") + "/wemu_rich_" + std::to_string(m_stats.tris) + "tris.ppm";
                        if (FILE *f = std::fopen(path.c_str(), "wb")) {
                            std::fprintf(f, "P6\n%u %u\n255\n", kWidth, kHeight);
                            for (std::size_t i = 0; i < m_fb.size(); i += 4)
                                std::fwrite(m_fb.data() + i, 1, 3, f);
                            std::fclose(f);
                            Utils::Log::error("[GX2] RICH frame #{}: {} tris, {} draws -> {}", m_presentCount, m_stats.tris,
                                              m_stats.draws, path);
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
