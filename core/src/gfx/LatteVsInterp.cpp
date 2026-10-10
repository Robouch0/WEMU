#include "gfx/LatteVsInterp.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

#include "gfx/LatteProgram.hpp"

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

        std::uint32_t asU32(float f)
        {
            std::uint32_t u;
            std::memcpy(&u, &f, 4);
            return u;
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
        std::uint64_t hash = 14695981039346656037ull;
        for (auto word: words)
            hash = (hash ^ word) * 1099511628211ull;
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

    LatteVsInterp::Output LatteVsInterp::run(const std::vector<std::uint32_t> &program, const std::array<std::array<float, 4>, 4> &attribs,
                                             const float *consts, const std::uint32_t constCount, const KcacheFetch &kcache,
                                             const TextureSample &textureSample)
    {
        return execute(*compile(program), attribs, consts, constCount, kcache, textureSample, false);
    }

    LatteVsInterp::Output LatteVsInterp::runPixel(const Program &program, const std::array<std::array<float, 4>, 4> &inputs, const float *consts,
                                                  std::uint32_t constCount, const TextureSample &textureSample, const KcacheFetch &kcache,
                                                  const TextureGather &textureGather)
    {
        return execute(program, inputs, consts, constCount, kcache, textureSample, true, textureGather);
    }

    LatteVsInterp::Output LatteVsInterp::execute(const Program &compiled, const std::array<std::array<float, 4>, 4> &attribs, const float *consts,
                                                 std::uint32_t constCount, const KcacheFetch &kcache, const TextureSample &textureSample,
                                                 bool pixelStage, const TextureGather &textureGather)
    {
        const auto &program = compiled.words;
        Output out;
        if (program.size() < 2 || !compiled.valid)
            return out;

        Regs regs;
        std::memset(regs.gpr, 0, compiled.registerCount * sizeof(regs.gpr[0]));
        bool pred = true;
        bool active = true;
        bool skippedPixelExport = false;
        std::array<bool, 32> activeStack{};
        unsigned activeDepth = 0;
        auto popActive = [&](std::uint32_t n) {
            while (n--) {
                active = activeDepth ? activeStack[--activeDepth] : true;
            }
        };
        // Attribute convention after CALL_FS: R0 = index/system, R1.. = fetched attributes.
        for (int a = 0; a < 4; a++)
            std::memcpy(regs.gpr[a + (pixelStage ? 0 : 1)], attribs[a].data(), 16);

        // Active kcache windows for the CURRENT ALU clause (set from CF_ALU_WORD0/1 fields).
        struct KcacheWin {
                std::uint32_t bank{0}, addr{0}, mode{0};
        } kc[2];

        auto readSrc = [&](const Src &s, const AluInst &inst) -> float {
            float v = 0.0f;
            if (s.sel < 128) {
                v = regs.gpr[s.sel][s.chan];
            } else if (s.sel >= 128 && s.sel < 192) { // kcache-style selects in some encodings
                const std::uint32_t idx = (s.sel - 128) * 4 + s.chan;
                v = idx < constCount * 4 ? consts[idx] : 0.0f;
            } else if (s.sel >= 256 && s.sel < 288 && kc[0].mode && kcache) {
                // Uniform-block mode: sel 256..287 reads kcache bank 0's window.
                v = kcache(kc[0].bank, kc[0].addr * 16 + (s.sel - 256), s.chan);
            } else if (s.sel >= 288 && s.sel < 320 && kc[1].mode && kcache) {
                v = kcache(kc[1].bank, kc[1].addr * 16 + (s.sel - 288), s.chan);
            } else if (s.sel >= 256) { // uniform-register mode: constant file c0..c255
                const std::uint32_t idx = (s.sel - 256) * 4 + s.chan;
                v = idx < constCount * 4 ? consts[idx] : 0.0f;
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
            for (const auto &group: clause->second) {
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
                struct Write {
                        std::uint32_t gpr, chan;
                        float val;
                        bool enable;
                };
                std::array<Write, 5> writes{};
                unsigned writeCount = 0;
                float newPv[4] = {regs.pv[0], regs.pv[1], regs.pv[2], regs.pv[3]};
                float newPs = regs.ps;
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
                        if (pixelStage && (in.src[0].rel || in.src[1].rel || (in.op3 && in.src[2].rel)))
                            return false;
                        if (isDot && !in.scalarSlot)
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
                                case OP2_RNDNE:
                                    v = roundNearestEven(a);
                                    break;
                                case OP2_EXP_IEEE:
                                    v = std::exp2(a);
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
                                    v = fromU32(static_cast<std::uint32_t>(static_cast<std::int32_t>(a)));
                                    break;
                                case OP2_INT_TO_FLT:
                                    v = static_cast<float>(static_cast<std::int32_t>(asU32(a)));
                                    break;
                                case OP2_UINT_TO_FLT:
                                    v = static_cast<float>(asU32(a));
                                    break;
                                case OP2_FLT_TO_UINT:
                                    v = fromU32(static_cast<std::uint32_t>(a < 0.0f ? 0.0f : a));
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
                continue; // ALU CF has no EOP bit in this position; next CF follows
            }

            const std::uint32_t inst = cfInst(w1);
            if (vsDebug())
                std::fprintf(stderr, "CF%u: inst=0x%02X w0=%08X pred=%d\n", cf, inst, w0, pred);
            if (inst == CF_EXP || inst == CF_EXP_DONE) {
                if (!active) {
                    if (pixelStage && ((w0 >> 13) & 3) == 0 && ((w0 & 0x1FFF) == 0 || (w0 & 0x1FFF) == 61))
                        skippedPixelExport = true;
                    if (cfEop(w1))
                        break;
                    continue;
                }
                const std::uint32_t arrayBase = w0 & 0x1FFF;
                const std::uint32_t type = (w0 >> 13) & 3;
                const std::uint32_t rwGpr = (w0 >> 15) & 0x7F;
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
                    std::memcpy(out.pos.data(), v, 16);
                    out.valid = true;
                } else if (type == 2 && arrayBase < 4) { // PARAM export
                    std::memcpy(out.params[arrayBase].data(), v, 16);
                    out.paramValid[arrayBase] = true;
                } else if (pixelStage && type == 0 && arrayBase == 0) {
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
            if (inst == CF_TEX) {
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
                    const bool gather = pixelStage && op == 0x0F;
                    const auto &fetch = gather ? textureGather : textureSample;
                    if (!fetch || (op != 0x13 && !(pixelStage && op == 0x10) && !gather) || (t0 & (1u << 23)) || (t1 & (1u << 7)) ||
                        (t1 & 0x30000000) != 0x30000000 || (t2 & 0x7FFF))
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
                    if (!fetch((t0 >> 8) & 255, (t2 >> 15) & 31, coords, rgba))
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
        out.discarded = skippedPixelExport && !out.colorValid && !out.depthValid;
        return out;
    }

} // namespace Core::Gfx
