#include "gfx/LatteVsInterp.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

#include "gfx/LatteProgram.hpp"
#include "gfx/UniformBufferFetch.hpp"

// WEMU_VS_DEBUG=1: print every ALU group and export (offline harness diagnostics).
// setDebugOnce() additionally arms the trace for a single run() so one draw can be isolated.
static bool g_vsDebugOnce = false;

static bool vsDebug()
{
    static const bool on = []() {
        const char *e = std::getenv("WEMU_VS_DEBUG");
        return e && e[0] == '1';
    }();
    return on || g_vsDebugOnce;
}

namespace Core::Gfx {

    namespace {

        using namespace Latte;

        bool supportsPixelTextureOperation(unsigned op) { return op == 0x0E || op == 0x0F || op == 0x10 || op == 0x11 || op == 0x13; }

        std::uint32_t asU32(float f)
        {
            std::uint32_t u;
            std::memcpy(&u, &f, 4);
            return u;
        }
        std::uint32_t floatToIntBits(float value)
        {
            const auto bits = asU32(value);
            const auto sign = bits >> 31;
            const int exponent = int((bits >> 23) & 255) - 127;
            const auto fraction = bits & 0x7FFFFFu;
            if (exponent == 128)
                return fraction ? 0u : sign ? 0x80000000u : 0x7FFFFFFFu;
            if (exponent < 0 || exponent >= 55)
                return 0;
            const auto mantissa = fraction | 0x800000u;
            const auto magnitude = exponent < 23 ? mantissa >> (23 - exponent) : mantissa << (exponent - 23);
            return sign ? 0u - magnitude : magnitude;
        }
        std::uint32_t floatToUintBits(float value)
        {
            if (std::isnan(value) || value <= 0.0f)
                return 0;
            if (value >= 4294967296.0f)
                return 0xFFFFFFFFu;
            return static_cast<std::uint32_t>(value);
        }

        float fromU32(std::uint32_t u)
        {
            float f;
            std::memcpy(&f, &u, 4);
            return f;
        }
        struct Regs {
                float gpr[128][4]; // only the program's referenced prefix is initialized
                float pv[4]{};
                float ps{0.0f};
        };

        float roundNearestEven(float value)
        {
            if (!std::isfinite(value) || std::fabs(value) >= 8388608.0f)
                return value;
            const float lower = std::floor(value);
            const float fraction = value - lower;
            const float rounded = fraction < 0.5f ? lower : fraction > 0.5f ? lower + 1.0f : std::fmod(lower, 2.0f) == 0.0f ? lower : lower + 1.0f;
            return rounded == 0.0f ? std::copysign(0.0f, value) : rounded;
        }

        float applyOmodClamp(float v, std::uint32_t omod, bool clamp)
        {
            switch (omod) {
                case 1:
                    v *= 2.0f;
                    break;
                case 2:
                    v *= 4.0f;
                    break;
                case 3:
                    v *= 0.5f;
                    break;
                default:
                    break;
            }
            if (clamp)
                v = std::fmin(1.0f, std::fmax(0.0f, v));
            return v;
        }

    } // namespace

    std::shared_ptr<const LatteVsInterp::Program> LatteVsInterp::compile(const std::vector<std::uint32_t> &words)
    {
        const std::uint64_t hash = Latte::ProgramWordsHash{}(words);
        static std::unordered_map<std::uint64_t, std::shared_ptr<const Program>> cache;
        if (const auto it = cache.find(hash); it != cache.end() && it->second->words == words)
            return it->second;
        const auto result = Latte::decodeProgram(words);
        if (cache.size() >= 1024)
            cache.clear();
        cache[hash] = result;
        return result;
    }

    void LatteVsInterp::setDebugOnce(bool on) { g_vsDebugOnce = on; }

    bool LatteVsInterp::rejectsEveryPixel(const Program &program)
    {
        if (!program.valid)
            return false;
        const auto &words = program.words;
        for (std::size_t cf = 0; cf + 1 < words.size(); cf += 2) {
            const auto w0 = words[cf], w1 = words[cf + 1];
            if (isAluClause(w1)) {
                if (cfAluInst(w1) != 8)
                    return false;
                const auto clause = program.clauses.find((std::uint64_t(w0 & 0x3FFFFF) << 32) | (((w1 >> 18) & 127) + 1));
                if (clause == program.clauses.end())
                    return false;
                for (const auto &group: clause->second)
                    for (const auto &in: group)
                        if (!in.op3 && (in.raw1 & 4u))
                            return false;
                continue;
            }
            const auto op = cfInst(w1);
            if (op == CF_TEX) {
                constexpr auto cfMask = (7u << 10) | (1u << 19) | (1u << 21) | (1u << 22) | (127u << 23) | (1u << 31);
                if (w1 & ~cfMask)
                    return false;
                const auto count = (((w1 >> 10) & 7) | ((w1 >> 16) & 8)) + 1;
                for (unsigned i = 0; i < count; ++i) {
                    const auto offset = std::uint64_t(w0) * 2 + std::uint64_t(i) * 4;
                    if (offset + 3 >= words.size())
                        return false;
                    if (!supportsPixelTextureOperation(words[offset] & 31))
                        return true;
                }
            } else if (op != CF_EXP && op != CF_EXP_DONE && op != CF_NOP && op != CF_RETURN)
                return false;
            if (cfEop(w1) || op == CF_RETURN)
                break;
        }
        return false;
    }

    LatteVsInterp::Output LatteVsInterp::run(const std::vector<std::uint32_t> &program, std::span<const std::array<float, 4>> attribs,
                                             const float *consts, const std::uint32_t constCount, const KcacheFetch &kcache,
                                             const TextureSample &textureSample)
    {
        return runVertex(*compile(program), attribs, consts, constCount, kcache, textureSample);
    }

    LatteVsInterp::Output LatteVsInterp::runVertex(const Program &program, std::span<const std::array<float, 4>> attribs, const float *consts,
                                                   std::uint32_t constCount, const KcacheFetch &kcache, const TextureSample &textureSample,
                                                   unsigned vertexIndex)
    {
        return execute(program, attribs, consts, constCount, kcache, textureSample, false, {}, {}, 0, vertexIndex);
    }

    LatteVsInterp::Output LatteVsInterp::runPixel(const Program &program, std::span<const std::array<float, 4>> inputs, const float *consts,
                                                  std::uint32_t constCount, const TextureSample &textureSample, const KcacheFetch &kcache,
                                                  const TextureGather &textureGather, const TextureFetch &textureFetch, unsigned colorExport)
    {
        return execute(program, inputs, consts, constCount, kcache, textureSample, true, textureGather, textureFetch, colorExport);
    }

    LatteVsInterp::Output LatteVsInterp::execute(const Program &compiled, std::span<const std::array<float, 4>> attribs, const float *consts,
                                                 std::uint32_t constCount, const KcacheFetch &kcache, const TextureSample &textureSample,
                                                 bool pixelStage, const TextureGather &textureGather, const TextureFetch &textureFetch,
                                                 unsigned colorExport, unsigned vertexIndex)
    {
        const auto &program = compiled.words;
        Output out;
        if (program.size() < 2 || !compiled.valid || attribs.size() > 32 || colorExport >= 8)
            return out;

        Regs regs;
        std::memset(regs.gpr, 0, compiled.registerCount * sizeof(regs.gpr[0]));
        if (!pixelStage)
            regs.gpr[0][0] = std::bit_cast<float>(vertexIndex);
        bool pred = true;
        bool active = true;
        std::array<int, 4> address{};
        std::array<bool, 4> addressValid{};
        bool skippedPixelExport = false;
        bool killed = false; // KILL* ran with a true condition: the pixel is discarded at the end
        std::array<bool, 32> activeStack{};
        unsigned activeDepth = 0;
        std::array<float, 256> cubeIndices{};
        auto popActive = [&](std::uint32_t n) {
            while (n--) {
                active = activeDepth ? activeStack[--activeDepth] : true;
            }
        };
        // Attribute convention after CALL_FS: R0 = index/system, R1.. = fetched attributes.
        for (std::size_t a = 0; a < attribs.size(); a++)
            std::memcpy(regs.gpr[a + (pixelStage ? 0 : 1)], attribs[a].data(), 16);

        // Active kcache windows for the CURRENT ALU clause (set from CF_ALU_WORD0/1 fields).
        struct KcacheWin {
                std::uint32_t bank{0}, addr{0}, mode{0};
        } kc[2];

        auto readSrc = [&](const Src &s, const AluInst &inst) -> float {
            float v = 0.0f;
            if (s.sel < 128) {
                const int index = int(s.sel) + (s.rel ? address[(inst.raw0 >> 26) & 3] : 0);
                v = index >= 0 && index < 128 ? regs.gpr[unsigned(index)][s.chan] : fromU32(0x7FFFFFFFu);
            } else if (s.sel >= 128 && s.sel < 192) {
                const auto window = (s.sel - 128) / 32;
                if (kc[window].mode) {
                    // R700 KCACHE selectors are 128..191; CONST_FILE is 256..511.
                    v = kcache ? kcache(kc[window].bank, kc[window].addr * 16 + (s.sel - 128) % 32, s.chan) : 0.0f;
                } else {
                    const std::uint32_t idx = (s.sel - 128) * 4 + s.chan;
                    v = idx < constCount * 4 ? consts[idx] : 0.0f;
                }
            } else if (s.sel >= 256) { // uniform-register mode: constant file c0..c255
                const int index = int(s.sel - 256) + (s.rel ? address[(inst.raw0 >> 26) & 3] : 0);
                if (index < 0 || index >= 256)
                    v = fromU32(0x7FFFFFFFu);
                else {
                    const auto idx = unsigned(index) * 4 + s.chan;
                    v = idx < constCount * 4 ? consts[idx] : 0.0f;
                }
            } else {
                switch (s.sel) {
                    case SRC_0:
                        v = 0.0f;
                        break;
                    case SRC_1:
                        v = 1.0f;
                        break;
                    case SRC_1_INT:
                        v = fromU32(1u);
                        break;
                    case SRC_M_1_INT:
                        v = fromU32(0xFFFFFFFFu);
                        break;
                    case SRC_HALF:
                        v = 0.5f;
                        break;
                    case SRC_LITERAL:
                        v = inst.literal[s.chan & 3];
                        break;
                    case SRC_PV:
                        v = regs.pv[s.chan];
                        break;
                    case SRC_PS:
                        v = regs.ps;
                        break;
                    default:
                        v = 0.0f;
                        break;
                }
            }
            if (s.abs)
                v = std::fabs(v);
            if (s.neg)
                v = -v;
            return v;
        };

        // Execute one ALU clause: 64-bit slots [addr, addr+count).
        auto runAluClause = [&](std::uint32_t addr, std::uint32_t count) -> bool {
            const auto clause = compiled.clauses.find((std::uint64_t(addr) << 32) | count);
            if (clause == compiled.clauses.end())
                return false;
            if (compiled.usesAddressing)
                addressValid.fill(false);
            for (const auto &group: clause->second) {
                if (compiled.usesAddressing) {
                    const bool addressWrite = std::any_of(group.begin(), group.end(), [](const auto &in) { return in.isAddressWrite(); });
                    for (const auto &in: group) {
                        // AR-relative constant reads require a previous group in
                        // this clause. Loop indexing and relative destinations remain unsupported.
                        if (in.raw1 & (1u << 28))
                            return false;
                        for (unsigned i = 0; i < (in.op3 ? 3u : 2u); ++i)
                            if (in.src[i].rel && ((in.src[i].sel >= 128 && in.src[i].sel < 256) || in.src[i].sel >= 512 ||
                                                  ((in.raw0 >> 26) & 7) > 3 || addressWrite || !addressValid[(in.raw0 >> 26) & 3]))
                                return false;
                    }
                }
                bool nextPred = pred, nextActive = active;
                auto predicateResult = [&](bool condition, const AluInst &in, bool integer = false) {
                    if (in.raw1 & (1u << 3))
                        nextPred = condition;
                    if (in.raw1 & (1u << 2))
                        nextActive = condition;
                    return integer ? fromU32(condition ? 0xFFFFFFFFu : 0u) : (condition ? 1.0f : 0.0f);
                };
                // DOT4: the group's four vector slots supply the four component products.
                const bool isDot = !group.empty() && !group[0].op3 && (group[0].op == OP2_DOT4 || group[0].op == OP2_DOT4_IEEE);
                const bool isCube = std::any_of(group.begin(), group.end(), [](const auto &in) { return !in.op3 && in.op == OP2_CUBE; });
                if (isCube && !isCanonicalCubeGroup(group))
                    return false;
                struct Write {
                        std::uint32_t gpr, chan;
                        float val;
                        bool enable;
                };
                std::array<Write, 5> writes{};
                unsigned writeCount = 0;
                float newPv[4] = {regs.pv[0], regs.pv[1], regs.pv[2], regs.pv[3]};
                float newPs = regs.ps;
                if (isCube && !((group[0].predSel == 2 && pred) || (group[0].predSel == 3 && !pred))) {
                    const float x = readSrc(group[1].src[1], group[1]);
                    const float y = readSrc(group[0].src[1], group[0]);
                    const float z = readSrc(group[0].src[0], group[0]);
                    const float ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
                    const std::array<float, 4> cube = az >= ax && az >= ay ? std::array{-y, z < 0.f ? -x : x, z * 2.f, z < 0.f ? 5.f : 4.f}
                                                      : ay >= ax           ? std::array{y < 0.f ? -z : z, x, y * 2.f, y < 0.f ? 3.f : 2.f}
                                                                           : std::array{-y, x < 0.f ? z : -z, x * 2.f, x < 0.f ? 1.f : 0.f};
                    for (unsigned lane = 0; lane < 4; ++lane) {
                        const auto &in = group[lane];
                        const float value = applyOmodClamp(cube[lane], in.omod, in.clamp);
                        if (in.writeMask)
                            writes[writeCount++] = {in.dstGpr, in.dstChan, value, true};
                        newPv[in.dstChan] = value;
                    }
                }
                if (isDot) {
                    const std::uint32_t gps = group[0].predSel;
                    if (!((gps == 2 && pred) || (gps == 3 && !pred))) {
                        float dot = 0.0f;
                        for (std::size_t i = 0; i < group.size() && i < 4; i++)
                            dot += readSrc(group[i].src[0], group[i]) * readSrc(group[i].src[1], group[i]);
                        for (auto &in: group) {
                            if (in.scalarSlot)
                                continue;
                            const float v = applyOmodClamp(dot, in.omod, in.clamp);
                            if (in.writeMask)
                                writes[writeCount++] = {in.dstGpr, in.dstChan, v, true};
                            newPv[in.dstChan] = v;
                        }
                    }
                }
                {
                    for (auto &in: group) {
                        if ((isDot || isCube) && !in.scalarSlot)
                            continue;
                        // Per-instruction predication via PRED_SEL (0 = always run).
                        if ((in.predSel == 2 && pred) || (in.predSel == 3 && !pred))
                            continue;
                        const float a = readSrc(in.src[0], in);
                        const float b = readSrc(in.src[1], in);
                        float v = 0.0f;
                        if (in.op3) {
                            const float c = readSrc(in.src[2], in);
                            switch (in.op) {
                                case OP3_MULADD:
                                case OP3_MULADD_IEEE:
                                    v = a * b + c;
                                    break;
                                case OP3_MULADD_M2:
                                case OP3_MULADD_IEEE_M2:
                                    v = (a * b + c) * 2.0f;
                                    break;
                                case OP3_MULADD_M4:
                                case OP3_MULADD_IEEE_M4:
                                    v = (a * b + c) * 4.0f;
                                    break;
                                case OP3_MULADD_D2:
                                case OP3_MULADD_IEEE_D2:
                                    v = (a * b + c) * 0.5f;
                                    break;
                                case OP3_CNDE:
                                    v = (a == 0.0f) ? b : c;
                                    break;
                                case OP3_CNDGT:
                                    v = (a > 0.0f) ? b : c;
                                    break;
                                case OP3_CNDGE:
                                    v = (a >= 0.0f) ? b : c;
                                    break;
                                case OP3_CNDE_INT:
                                    v = (asU32(a) == 0u) ? b : c;
                                    break;
                                case OP3_CNDGT_INT:
                                    v = (static_cast<std::int32_t>(asU32(a)) > 0) ? b : c;
                                    break;
                                case OP3_CNDGE_INT:
                                    v = (static_cast<std::int32_t>(asU32(a)) >= 0) ? b : c;
                                    break;
                                default:
                                    if (pixelStage)
                                        return false;
                                    break;
                            }
                        } else {
                            switch (in.op) {
                                case OP2_ADD:
                                    v = a + b;
                                    break;
                                case OP2_MUL:
                                case OP2_MUL_IEEE:
                                    v = a * b;
                                    break;
                                case OP2_MAX:
                                case OP2_MAX_DX10:
                                    v = std::fmax(a, b);
                                    break;
                                case OP2_MIN:
                                case OP2_MIN_DX10:
                                    v = std::fmin(a, b);
                                    break;
                                case OP2_SETE:
                                    v = (a == b) ? 1.0f : 0.0f;
                                    break;
                                case OP2_SETGT:
                                    v = (a > b) ? 1.0f : 0.0f;
                                    break;
                                case OP2_SETGE:
                                    v = (a >= b) ? 1.0f : 0.0f;
                                    break;
                                case OP2_SETNE:
                                    v = (a != b) ? 1.0f : 0.0f;
                                    break;
                                case OP2_SETE_DX10:
                                    v = fromU32(a == b ? 0xFFFFFFFFu : 0u);
                                    break;
                                case OP2_SETGE_DX10:
                                    v = fromU32(a >= b ? 0xFFFFFFFFu : 0u);
                                    break;
                                case OP2_SETNE_DX10:
                                    v = fromU32(a != b ? 0xFFFFFFFFu : 0u);
                                    break;
                                case OP2_SETGT_DX10:
                                    v = fromU32(a > b ? 0xFFFFFFFFu : 0u);
                                    break;
                                case OP2_MOV:
                                    v = a;
                                    break;
                                case OP2_FRACT:
                                    v = a - std::floor(a);
                                    break;
                                case OP2_FLOOR:
                                    v = std::floor(a);
                                    break;
                                case OP2_TRUNC:
                                    v = std::trunc(a);
                                    break;
                                case OP2_MOVA:
                                case OP2_MOVA_FLOOR:
                                case OP2_MOVA_INT: {
                                    const float rounded = in.op == OP2_MOVA_INT ? float(std::bit_cast<std::int32_t>(a))
                                                          : in.op == OP2_MOVA   ? std::floor(a + .5f)
                                                                                : std::floor(a);
                                    const int index = !std::isfinite(rounded) || rounded < -256.f || rounded > 255.f ? -256 : int(rounded);
                                    // Relative reads cannot share this group, so
                                    // no source can observe a partial AR update.
                                    address[in.dstChan] = index;
                                    addressValid[in.dstChan] = true;
                                    v = fromU32(std::uint32_t(index));
                                    break;
                                }
                                case OP2_SIN:
                                    // R700/Latte trigonometric inputs use turns, as
                                    // emitted by LLVM R600 and Cemu's Latte backend.
                                    v = std::sin(a / 0.1591549367f);
                                    break;
                                case OP2_COS:
                                    v = std::cos(a / 0.1591549367f);
                                    break;
                                case OP2_RNDNE:
                                    v = roundNearestEven(a);
                                    break;
                                case OP2_EXP_IEEE:
                                    v = std::exp2(a);
                                    break;
                                case OP2_SQRT_IEEE:
                                    v = std::sqrt(a);
                                    break;
                                case OP2_LOG_IEEE:
                                    v = std::log2(a);
                                    break;
                                case OP2_LOG_CLAMPED:
                                    v = std::log2(a);
                                    if (v == -std::numeric_limits<float>::infinity())
                                        v = -std::numeric_limits<float>::max();
                                    break;
                                case OP2_RECIP_IEEE:
                                    v = (a != 0.0f) ? 1.0f / a : 0.0f;
                                    break;
                                case OP2_RECIPSQRT_IEEE:
                                    v = (a > 0.0f) ? 1.0f / std::sqrt(a) : 0.0f;
                                    break;
                                case OP2_FLT_TO_INT:
                                    v = fromU32(floatToIntBits(a));
                                    break;
                                case OP2_INT_TO_FLT:
                                    v = static_cast<float>(static_cast<std::int32_t>(asU32(a)));
                                    break;
                                case OP2_UINT_TO_FLT:
                                    v = static_cast<float>(asU32(a));
                                    break;
                                case OP2_FLT_TO_UINT:
                                    v = fromU32(floatToUintBits(a));
                                    break;
                                case OP2_AND_INT:
                                    v = fromU32(asU32(a) & asU32(b));
                                    break;
                                case OP2_OR_INT:
                                    v = fromU32(asU32(a) | asU32(b));
                                    break;
                                case OP2_XOR_INT:
                                    v = fromU32(asU32(a) ^ asU32(b));
                                    break;
                                case OP2_ADD_INT:
                                    v = fromU32(asU32(a) + asU32(b));
                                    break;
                                case OP2_MULLO_INT:
                                case OP2_MULLO_UINT:
                                    v = fromU32(asU32(a) * asU32(b));
                                    break;
                                case OP2_SUB_INT:
                                    v = fromU32(asU32(a) - asU32(b));
                                    break;
                                case OP2_ASHR:
                                    v = fromU32(static_cast<std::uint32_t>(static_cast<std::int32_t>(asU32(a)) >> (asU32(b) & 31u)));
                                    break;
                                case OP2_LSHR:
                                    v = fromU32(asU32(a) >> (asU32(b) & 31u));
                                    break;
                                case OP2_LSHL:
                                    v = fromU32(asU32(a) << (asU32(b) & 31u));
                                    break;
                                case OP2_SETE_INT:
                                    v = fromU32(asU32(a) == asU32(b) ? 0xFFFFFFFFu : 0u);
                                    break;
                                case OP2_SETNE_INT:
                                    v = fromU32(asU32(a) != asU32(b) ? 0xFFFFFFFFu : 0u);
                                    break;
                                case OP2_SETGT_INT:
                                    v = fromU32(static_cast<std::int32_t>(asU32(a)) > static_cast<std::int32_t>(asU32(b)) ? 0xFFFFFFFFu : 0u);
                                    break;
                                case OP2_SETGE_INT:
                                    v = fromU32(static_cast<std::int32_t>(asU32(a)) >= static_cast<std::int32_t>(asU32(b)) ? 0xFFFFFFFFu : 0u);
                                    break;
                                case OP2_PRED_SETE:
                                case OP2_PRED_SETE_PUSH:
                                    v = predicateResult(a == b, in);
                                    break;
                                case OP2_PRED_SETGT:
                                case OP2_PRED_SETGT_PUSH:
                                    v = predicateResult(a > b, in);
                                    break;
                                case OP2_PRED_SETGE:
                                case OP2_PRED_SETGE_PUSH:
                                    v = predicateResult(a >= b, in);
                                    break;
                                case OP2_PRED_SETNE:
                                case OP2_PRED_SETNE_PUSH:
                                    v = predicateResult(a != b, in);
                                    break;
                                case OP2_PRED_SETE_INT:
                                    v = predicateResult(asU32(a) == asU32(b), in, true);
                                    break;
                                case OP2_PRED_SETGT_INT:
                                    v = predicateResult(static_cast<std::int32_t>(asU32(a)) > static_cast<std::int32_t>(asU32(b)), in, true);
                                    break;
                                case OP2_PRED_SETGE_INT:
                                    v = predicateResult(static_cast<std::int32_t>(asU32(a)) >= static_cast<std::int32_t>(asU32(b)), in, true);
                                    break;
                                case OP2_PRED_SETNE_INT:
                                    v = predicateResult(asU32(a) != asU32(b), in, true);
                                    break;
                                case OP2_NOP:
                                    v = 0.0f;
                                    break;
                                case OP2_KILLE:
                                case OP2_KILLGT:
                                case OP2_KILLGE:
                                case OP2_KILLNE:
                                case OP2_KILLGT_UINT:
                                case OP2_KILLGE_UINT:
                                case OP2_KILLE_INT:
                                case OP2_KILLGT_INT:
                                case OP2_KILLGE_INT:
                                case OP2_KILLNE_INT: {
                                    if (!pixelStage)
                                        return false;
                                    const auto ia = std::int32_t(asU32(a)), ib = std::int32_t(asU32(b));
                                    bool condition{};
                                    switch (in.op) {
                                        case OP2_KILLE:
                                            condition = a == b;
                                            break;
                                        case OP2_KILLGT:
                                            condition = a > b;
                                            break;
                                        case OP2_KILLGE:
                                            condition = a >= b;
                                            break;
                                        case OP2_KILLNE:
                                            condition = a != b;
                                            break;
                                        case OP2_KILLGT_UINT:
                                            condition = asU32(a) > asU32(b);
                                            break;
                                        case OP2_KILLGE_UINT:
                                            condition = asU32(a) >= asU32(b);
                                            break;
                                        case OP2_KILLE_INT:
                                            condition = ia == ib;
                                            break;
                                        case OP2_KILLGT_INT:
                                            condition = ia > ib;
                                            break;
                                        case OP2_KILLGE_INT:
                                            condition = ia >= ib;
                                            break;
                                        default:
                                            condition = ia != ib;
                                            break;
                                    }
                                    killed |= condition;
                                    // Integer forms return integer one/zero, like SETxx_INT.
                                    const bool integer = in.op >= OP2_KILLGT_UINT;
                                    v = integer ? fromU32(condition ? 1u : 0u) : (condition ? 1.0f : 0.0f);
                                    break;
                                }
                                default:
                                    if (pixelStage)
                                        return false;
                                    v = 0.0f;
                                    break;
                            }
                        }
                        v = applyOmodClamp(v, in.op3 ? 0 : in.omod, in.clamp);
                        if (in.writeMask || in.op3)
                            writes[writeCount++] = {in.dstGpr, in.dstChan, v, true};
                        if (in.scalarSlot)
                            newPs = v;
                        else
                            newPv[in.dstChan] = v;
                    }
                }
                if (vsDebug()) {
                    for (const auto &in: group) {
                        const float a = readSrc(in.src[0], in);
                        const float b = readSrc(in.src[1], in);
                        const float c = in.op3 ? readSrc(in.src[2], in) : 0.0f;
                        std::fprintf(stderr, "  alu %s op=0x%02X src0=%u.%u(%g) src1=%u.%u(%g) src2=%u.%u(%g) -> R%u.%u wm=%d lit=(%g,%g)\n",
                                     in.op3 ? "OP3" : "OP2", in.op, in.src[0].sel, in.src[0].chan, a, in.src[1].sel, in.src[1].chan, b, in.src[2].sel,
                                     in.src[2].chan, c, in.dstGpr, in.dstChan, in.writeMask, in.literal[0], in.literal[1]);
                    }
                }
                for (const auto &w: writes)
                    if (w.enable && w.gpr < 128)
                        regs.gpr[w.gpr][w.chan] = w.val;
                if (vsDebug() && writeCount) {
                    std::fprintf(stderr, "  writes:");
                    for (const auto &w: writes)
                        if (w.enable)
                            std::fprintf(stderr, " R%u.%u=%g", w.gpr, w.chan, w.val);
                    std::fprintf(stderr, "\n");
                }
                std::memcpy(regs.pv, newPv, 16);
                regs.ps = newPs;
                pred = nextPred;
                active = nextActive;
            }
            return true;
        };

        // ---- CF walk -------------------------------------------------------------------------
        for (std::uint32_t cf = 0;; cf++) {
            const std::uint64_t base = static_cast<std::uint64_t>(cf) * 2;
            if (base + 1 >= program.size())
                break;
            const std::uint32_t w0 = program[base];
            const std::uint32_t w1 = program[base + 1];

            if (isAluClause(w1)) {
                const std::uint32_t addr = w0 & 0x3FFFFF;
                const std::uint32_t count = ((w1 >> 18) & 0x7F) + 1;
                const std::uint32_t kind = cfAluInst(w1);
                // KCACHE window setup: CF_ALU_WORD0 bank0 22-25, bank1 26-29, mode0 30-31;
                // CF_ALU_WORD1 mode1 0-1, addr0 2-9, addr1 10-17.
                kc[0] = {(w0 >> 22) & 0xF, (w1 >> 2) & 0xFF, (w0 >> 30) & 3};
                kc[1] = {(w0 >> 26) & 0xF, (w1 >> 10) & 0xFF, w1 & 3};
                if (vsDebug())
                    std::fprintf(stderr, "CF%u: ALU kind=%u addr=%u count=%u pred=%d kc0={b%u a%u m%u} kc1={b%u a%u m%u}\n", cf, kind, addr, count,
                                 pred, kc[0].bank, kc[0].addr, kc[0].mode, kc[1].bank, kc[1].addr, kc[1].mode);
                if (kind == 0x9) { // ALU_PUSH_BEFORE
                    if (activeDepth == activeStack.size())
                        return {};
                    activeStack[activeDepth++] = active;
                }
                if (active && !runAluClause(addr, count))
                    return {};
                if (kind == 0xA) // ALU_POP_AFTER
                    popActive(1);
                else if (kind == 0xB) // ALU_POP2_AFTER
                    popActive(2);
                else if (kind == 0xF) { // ALU_ELSE_AFTER retains the saved parent mask.
                    if (!activeDepth)
                        return {};
                    active = activeStack[activeDepth - 1] && !active;
                }
                continue; // ALU CF has no EOP bit in this position; next CF follows
            }

            const std::uint32_t inst = cfInst(w1);
            if (vsDebug())
                std::fprintf(stderr, "CF%u: inst=0x%02X w0=%08X pred=%d\n", cf, inst, w0, pred);
            if (inst >= 0x20 && inst <= 0x23) {
                if (pixelStage || (w0 & ((3u << 13) | (255u << 22))) || ((w1 >> 17) & 15) || (w1 & 0xFFF) > 3)
                    return {};
                if (active) {
                    Output::StreamWrite write;
                    write.buffer = inst - 0x20;
                    write.offset = w0 & 0x1FFF;
                    write.mask = ((w1 >> 12) & 15) & ((1u << ((w1 & 0xFFF) + 1)) - 1);
                    std::copy_n(regs.gpr[(w0 >> 15) & 127], 4, write.value.begin());
                    if (out.streamWrites.empty())
                        out.streamWrites.reserve(4);
                    out.streamWrites.push_back(write);
                }
                if (cfEop(w1))
                    break;
                continue;
            }
            if (inst == CF_EXP || inst == CF_EXP_DONE) {
                if (!active) {
                    if (pixelStage && ((w0 >> 13) & 3) == 0 &&
                        (((w0 & 0x1FFF) <= colorExport && colorExport < (w0 & 0x1FFF) + ((w1 >> 17) & 15) + 1) || (w0 & 0x1FFF) == 61))
                        skippedPixelExport = true;
                    if (cfEop(w1))
                        break;
                    continue;
                }
                const std::uint32_t arrayBase = w0 & 0x1FFF;
                const std::uint32_t type = (w0 >> 13) & 3;
                const auto burst = ((w1 >> 17) & 15) + 1;
                const bool selected = type == 0 && arrayBase <= colorExport && colorExport < arrayBase + burst;
                const std::uint32_t rwGpr = ((w0 >> 15) & 0x7F) + (pixelStage && selected ? colorExport - arrayBase : 0);
                const std::uint32_t sel[4] = {w1 & 7, (w1 >> 3) & 7, (w1 >> 6) & 7, (w1 >> 9) & 7};
                float v[4];
                for (int i = 0; i < 4; i++) {
                    if (sel[i] < 4)
                        v[i] = rwGpr < 128 ? regs.gpr[rwGpr][sel[i]] : 0.0f;
                    else if (sel[i] == 5)
                        v[i] = 1.0f;
                    else
                        v[i] = 0.0f;
                }
                if (vsDebug())
                    std::fprintf(stderr, "  export type=%u base=%u gpr=%u sel=(%u,%u,%u,%u) v=(%g,%g,%g,%g)\n", type, arrayBase, rwGpr, sel[0],
                                 sel[1], sel[2], sel[3], v[0], v[1], v[2], v[3]);
                if (type == 1 && arrayBase >= 60) { // POS export
                    for (unsigned channel = 0; channel < 4; ++channel)
                        if (sel[channel] != 7) {
                            out.pos[channel] = v[channel];
                            out.valid = true;
                        }
                } else if (type == 2 && arrayBase < out.params.size()) { // PARAM export
                    for (unsigned channel = 0; channel < 4; ++channel)
                        if (sel[channel] != 7) {
                            out.params[arrayBase][channel] = v[channel];
                            out.paramValid[arrayBase] = true;
                        }
                } else if (pixelStage && selected) {
                    std::memcpy(out.color.data(), v, 16);
                    out.colorValid = true;
                } else if (pixelStage && type == 0 && arrayBase == 61 && sel[0] != 7) {
                    out.depth = v[0];
                    out.depthValid = true;
                }
                if (cfEop(w1))
                    break;
                continue;
            }
            if (inst == CF_TEX || (!pixelStage && (inst == CF_VTX || inst == CF_VTX_TC))) {
                if (!active) {
                    if (cfEop(w1))
                        break;
                    continue;
                }
                const auto count = (((w1 >> 10) & 7) | ((w1 >> 16) & 8)) + 1;
                for (unsigned i = 0; i < count; i++) {
                    const std::uint64_t offset = std::uint64_t(w0) * 2 + std::uint64_t(i) * 4;
                    if (offset + 3 >= program.size())
                        return {};
                    const auto t0 = program[offset], t1 = program[offset + 1], t2 = program[offset + 2];
                    // Sampling and gather use separate callbacks; unsupported operations
                    // must not silently become ordinary filtered texture samples.
                    const auto op = t0 & 31;
                    if (pixelStage && op == 0x0E) {
                        const auto selector = (t2 >> 20) & 7;
                        if ((t0 & ~(31u | (255u << 8) | (127u << 16))) || t1 != 0x001FFE00u || (t2 & 0xFFFFFu) || selector > 5)
                            return {};
                        cubeIndices[(t0 >> 8) & 255] = selector < 4 ? regs.gpr[(t0 >> 16) & 127][selector] : selector == 5 ? 1.f : 0.f;
                        continue;
                    }
                    if (!pixelStage && op == 0) {
                        const auto instruction = decodeUniformBufferFetch(t0, t1, t2);
                        if (!instruction || !kcache)
                            return {};
                        const auto &f = *instruction;
                        const auto index = std::uint64_t(asU32(regs.gpr[f.source][f.channel])) + f.offset;
                        std::array<float, 4> values{};
                        for (unsigned c = 0; c < 4; ++c)
                            values[c] = index <= UINT32_MAX ? kcache(f.bank, unsigned(index), c) : 0.f;
                        for (unsigned c = 0; c < 4; ++c)
                            if (f.selectors[c] != 7)
                                regs.gpr[f.destination][c] = f.selectors[c] < 4 ? values[f.selectors[c]] : f.selectors[c] == 5 ? 1.f : 0.f;
                        continue;
                    }
                    const bool gather = pixelStage && op == 0x0F;
                    const auto &fetch = gather ? textureGather : textureSample;
                    // Texel offsets go through the detailed fetch, which knows the sampled level's size.
                    const bool offsets = (t2 & 0x7FFF) != 0;
                    if ((!fetch && !(pixelStage && !gather && textureFetch)) || (pixelStage ? !supportsPixelTextureOperation(op) : op != 0x13) ||
                        (op == 0x11 && !textureFetch) || (t0 & (1u << 23)) || (t1 & (1u << 7)) || (t1 & 0x30000000) != 0x30000000 ||
                        (offsets && (gather || !pixelStage || !textureFetch)))
                        return {};
                    // Gather currently accepts only direct, normalized, zero-offset TEX.
                    if (gather && ((t0 & ~(31u | (255u << 8) | (127u << 16))) || (t1 & ~(127u | (0xFFFu << 9) | (15u << 28)))))
                        return {};
                    const auto src = (t0 >> 16) & 127, dst = t1 & 127;
                    std::array<float, 4> coords{}, rgba{};
                    for (unsigned c = 0; c < 4; c++) {
                        const auto select = (t2 >> (20 + c * 3)) & 7;
                        if (gather && select > 5)
                            return {};
                        coords[c] = select < 4 ? regs.gpr[src][select] : select == 5 ? 1.0f : 0.0f;
                    }
                    const auto resource = (t0 >> 8) & 255;
                    const auto sampler = (t2 >> 15) & 31;
                    const auto texelOffset = [&](unsigned shift) {
                        const auto bits = (t2 >> shift) & 31u;
                        return float(std::int32_t(bits << 27) >> 27) * .5f;
                    };
                    if (pixelStage && !gather && textureFetch ? !textureFetch({resource,
                                                                               sampler,
                                                                               coords,
                                                                               cubeIndices[resource],
                                                                               op == 0x11,
                                                                               {texelOffset(0), texelOffset(5), texelOffset(10)}},
                                                                              rgba)
                                                              : !fetch(resource, sampler, coords, rgba))
                        return {};
                    for (unsigned c = 0; c < 4; c++) {
                        const auto select = (t1 >> (9 + c * 3)) & 7;
                        if (gather && select == 6)
                            return {};
                        if (select != 7)
                            regs.gpr[dst][c] = select < 4 ? rgba[select] : select == 5 ? 1.0f : 0.0f;
                    }
                }
                if (cfEop(w1))
                    break;
                continue;
            }
            if (inst == CF_VTX || inst == CF_VTX_TC)
                return {}; // unsupported in the VS subset: caller falls back to heuristics
            // This scalar reference tracks one lane. The stack saves execution
            // state, independently of the per-instruction predicate register.
            if (inst == 0x0A) { // JUMP when this lane is inactive.
                // WEMU_VS_NO_JUMP=1 forces the fall-through (if-) path for bring-up experiments:
                // MK8's UI shaders branch on a mode flag, and this isolates whether the branch
                // selection (rather than the constants) is what degenerates the output.
                static const bool noJump = []() {
                    const char *e = std::getenv("WEMU_VS_NO_JUMP");
                    return e && e[0] == '1';
                }();
                if (noJump && !pixelStage)
                    continue;
                if (!active && w0 > cf && w0 < program.size() / 2) {
                    cf = w0 - 1;
                    popActive(w1 & 7);
                }
                continue;
            }
            if (inst == 0x0D) { // ELSE cannot reactivate an inactive parent.
                if (active && w0 > cf && w0 < program.size() / 2) {
                    cf = w0 - 1;
                    popActive(w1 & 7);
                } else {
                    active = (activeDepth ? activeStack[activeDepth - 1] : true) && !active;
                }
                continue;
            }
            if (inst == 0x0E) { // POP
                popActive(w1 & 7);
                continue;
            }
            if (inst == 0x0B) { // PUSH
                if (activeDepth == activeStack.size())
                    return {};
                activeStack[activeDepth++] = active;
                continue;
            }
            // NOP / CALL_FS (attributes preloaded) / PUSH / RETURN: nothing to do.
            if (cfEop(w1) || inst == CF_RETURN)
                break;
            if (pixelStage && inst != CF_NOP)
                return {};
        }
        out.discarded = (skippedPixelExport && !out.colorValid && !out.depthValid) || killed;
        if (killed)
            out.colorValid = out.depthValid = false;
        out.completed = true;
        return out;
    }

} // namespace Core::Gfx
