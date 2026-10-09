#include "gfx/LatteShaderLowering.hpp"

#include <algorithm>
#include <bit>
#include <format>
#include <stdexcept>

namespace Core::Gfx::Latte {
    std::shared_ptr<const FragmentShader> FragmentShaderCache::get(const std::vector<std::uint32_t> &words,
                                                                  std::span<const TextureType> resourceTypes)
    {
        // Missing resource types default to 2D, so trailing defaults are equivalent.
        while (!resourceTypes.empty() && resourceTypes.back() == TextureType::TwoD)
            resourceTypes = resourceTypes.first(resourceTypes.size() - 1);
        Key key{words, {resourceTypes.begin(), resourceTypes.end()}};
        if (const auto found = m_entries.find(key); found != m_entries.end()) {
            ++m_hits;
            return found->second;
        }
        ++m_misses;
        auto shader = std::make_shared<const FragmentShader>(lowerFragmentShader(*decodeProgram(words), resourceTypes));
        constexpr std::size_t maxBytes = 32 * 1024 * 1024;
        const auto bytes = words.size() * sizeof(std::uint32_t) + resourceTypes.size() * sizeof(TextureType) +
                           shader->source.size() + shader->error.size() + shader->textures.size() * sizeof(TextureBinding);
        if (bytes <= maxBytes) {
            if (m_entries.size() >= 128 || bytes > maxBytes - m_bytes) {
                m_entries.clear();
                m_bytes = 0;
            }
            m_entries.emplace(std::move(key), shader);
            m_bytes += bytes;
        }
        return shader;
    }

    namespace {

        std::string source(const Src &s, const AluInst &instruction)
        {
            if (s.rel || s.chan > 3)
                throw std::runtime_error("relative or invalid ALU source");
            std::string value;
            if (s.sel < 128)
                value = std::format("r[{}][{}]", s.sel, s.chan);
            else if (s.sel < 192)
                value = std::format("u.c[{}][{}]", s.sel - 128, s.chan);
            else if (s.sel >= 256 && s.sel < 512)
                value = std::format("u.c[{}][{}]", s.sel - 256, s.chan);
            else
                switch (s.sel) {
                    case SRC_0:
                        value = "0.0";
                        break;
                    case SRC_1:
                        value = "1.0";
                        break;
                    case SRC_1_INT:
                        value = "uintBitsToFloat(1u)";
                        break;
                    case SRC_HALF:
                        value = "0.5";
                        break;
                    case SRC_LITERAL:
                        value = std::format("uintBitsToFloat({}u)", std::bit_cast<std::uint32_t>(instruction.literal[s.chan]));
                        break;
                    case SRC_PV:
                        value = std::format("pv[{}]", s.chan);
                        break;
                    case SRC_PS:
                        value = "ps";
                        break;
                    default:
                        throw std::runtime_error("unsupported ALU source selector");
                }
            if (s.abs)
                value = "abs(" + value + ")";
            if (s.neg)
                value = "(-(" + value + "))";
            return value;
        }

        std::string predicateCondition(const AluInst &in)
        {
            if (in.op3) return {};
            std::string comparison;
            switch (in.op) {
                case OP2_PRED_SETE: case OP2_PRED_SETE_PUSH: case OP2_PRED_SETE_INT: comparison = "=="; break;
                case OP2_PRED_SETGT: case OP2_PRED_SETGT_PUSH: case OP2_PRED_SETGT_INT: comparison = ">"; break;
                case OP2_PRED_SETGE: case OP2_PRED_SETGE_PUSH: case OP2_PRED_SETGE_INT: comparison = ">="; break;
                case OP2_PRED_SETNE: case OP2_PRED_SETNE_PUSH: case OP2_PRED_SETNE_INT: comparison = "!="; break;
                default: return {};
            }
            auto a = source(in.src[0], in), b = source(in.src[1], in);
            if (in.op >= OP2_PRED_SETE_INT) {
                a = "floatBitsToInt(" + a + ")";
                b = "floatBitsToInt(" + b + ")";
            }
            return "(" + a + comparison + b + ")";
        }

        std::string expression(const AluInst &in)
        {
            // Mask updates are modeled only for predicate-set operations.
            const auto condition = predicateCondition(in);
            constexpr std::uint32_t sourceMask = 0xE3BFFDFFu;
            const std::uint32_t destinationMask = in.op3 ? 0xEFFFFDFFu : condition.empty() ? 0xEFFFFFF3u : 0xEFFFFFFFu;
            if ((in.raw0 & ~sourceMask) || (in.raw1 & ~destinationMask) || in.predSel == 1)
                throw std::runtime_error("unsupported ALU addressing or control flags");
            // R700 BANK_SWIZZLE schedules register-bank reads, not operand components.
            // Functional execution reads the pre-group state without bank contention.
            const auto bankSwizzle = (in.raw1 >> 18) & 7;
            if (bankSwizzle > (in.scalarSlot ? 3u : 5u))
                throw std::runtime_error("reserved ALU bank swizzle");
            if (!condition.empty())
                return in.op >= OP2_PRED_SETE_INT ? "uintBitsToFloat(" + condition + " ? 4294967295u : 0u)"
                                                : "(" + condition + " ? 1.0 : 0.0)";
            const auto a = source(in.src[0], in), b = source(in.src[1], in);
            if (in.op3) {
                const auto c = source(in.src[2], in);
                switch (in.op) {
                    case OP3_MULADD:
                    case OP3_MULADD_IEEE:
                        return "(" + a + " * " + b + " + " + c + ")";
                    case OP3_MULADD_M2:
                    case OP3_MULADD_IEEE_M2:
                        return "((" + a + " * " + b + " + " + c + ") * 2.0)";
                    case OP3_MULADD_M4:
                    case OP3_MULADD_IEEE_M4:
                        return "((" + a + " * " + b + " + " + c + ") * 4.0)";
                    case OP3_MULADD_D2:
                    case OP3_MULADD_IEEE_D2:
                        return "((" + a + " * " + b + " + " + c + ") * 0.5)";
                    case OP3_CNDE:
                        return "(" + a + " == 0.0 ? " + b + " : " + c + ")";
                    case OP3_CNDGT:
                        return "(" + a + " > 0.0 ? " + b + " : " + c + ")";
                    case OP3_CNDGE:
                        return "(" + a + " >= 0.0 ? " + b + " : " + c + ")";
                    case OP3_CNDE_INT:
                        return "(floatBitsToUint(" + a + ") == 0u ? " + b + " : " + c + ")";
                    default:
                        throw std::runtime_error("unsupported OP3 instruction");
                }
            }
            switch (in.op) {
                case OP2_ADD:
                    return "(" + a + " + " + b + ")";
                case OP2_MUL:
                case OP2_MUL_IEEE:
                    return "(" + a + " * " + b + ")";
                case OP2_MIN:
                case OP2_MIN_DX10:
                    return "minReference(" + a + "," + b + ")";
                case OP2_MAX:
                case OP2_MAX_DX10:
                    return "maxReference(" + a + "," + b + ")";
                case OP2_AND_INT:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") & floatBitsToUint(" + b + "))";
                case OP2_OR_INT:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") | floatBitsToUint(" + b + "))";
                case OP2_XOR_INT:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") ^ floatBitsToUint(" + b + "))";
                case OP2_ADD_INT:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") + floatBitsToUint(" + b + "))";
                case OP2_SUB_INT:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") - floatBitsToUint(" + b + "))";
                case OP2_ASHR:
                    return "intBitsToFloat(floatBitsToInt(" + a + ") >> (floatBitsToUint(" + b + ") & 31u))";
                case OP2_LSHR:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") >> (floatBitsToUint(" + b + ") & 31u))";
                case OP2_LSHL:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") << (floatBitsToUint(" + b + ") & 31u))";
                case OP2_SETGT_DX10:
                    return "uintBitsToFloat(" + a + " > " + b + " ? 4294967295u : 0u)";
                case OP2_MOV:
                    return a;
                case OP2_NOP:
                    return "0.0";
                case OP2_FLOOR:
                    return "floor(" + a + ")";
                case OP2_RNDNE:
                    return "roundEvenReference(" + a + ")";
                case OP2_FRACT:
                    return "fract(" + a + ")";
                case OP2_EXP_IEEE:
                    return "exp2(" + a + ")";
                case OP2_LOG_IEEE:
                    return "logReference(" + a + ")";
                case OP2_LOG_CLAMPED:
                    return "logClamped(" + a + ")";
                case OP2_RECIP_IEEE:
                    return "(" + a + " != 0.0 ? 1.0 / " + a + " : 0.0)";
                case OP2_RECIPSQRT_IEEE:
                    return "(" + a + " > 0.0 ? inversesqrt(" + a + ") : 0.0)";
                default:
                    throw std::runtime_error(std::format("unsupported OP2 instruction: op={:02X} words={:08X},{:08X}", in.op, in.raw0, in.raw1));
            }
        }

        std::string select(const std::string &reg, unsigned selector)
        {
            if (selector < 4)
                return std::format("{}[{}]", reg, selector);
            if (selector == 4)
                return "0.0";
            if (selector == 5)
                return "1.0";
            throw std::runtime_error("unsupported component selector");
        }
    } // namespace

    FragmentShader lowerFragmentShader(const Program &program, std::span<const TextureType> resourceTypes)
    {
        FragmentShader result;
        try {
            if (!program.valid || program.words.size() < 2)
                throw std::runtime_error("invalid shader structure");
            std::string body = "void main() {\nvec4 r[128];\nfor (int i=0;i<128;++i) r[i]=vec4(0.0);\n"
                               "for (int i=0;i<4;++i) r[i]=inputs[i];\nvec4 pv=vec4(0.0); float ps=0.0;\n"
                               "bool pred=true, laneActive=true, wroteColor=false; bool activeStack[32]; uint pc=0u;\n";
            bool exported = false, terminated = false;
            unsigned depth = 0;
            std::vector<unsigned> entryDepth;
            std::vector<std::pair<unsigned, unsigned>> jumpTargets;
            const auto &words = program.words;
            for (std::size_t cf = 0; cf + 1 < words.size(); cf += 2) {
                const auto w0 = words[cf], w1 = words[cf + 1];
                entryDepth.push_back(depth);
                body += std::format("if(pc<={}u) {{\n", cf / 2);
                const auto push = [&] {
                    if (depth == 32) throw std::runtime_error("control-flow stack overflow");
                    body += std::format("activeStack[{}]=laneActive;\n", depth++);
                };
                const auto pop = [&](unsigned count) {
                    if (count > depth) throw std::runtime_error("control-flow stack underflow");
                    if (count) {
                        depth -= count;
                        body += std::format("laneActive=activeStack[{}];\n", depth);
                    }
                };
                if (isAluClause(w1)) {
                    const auto kind = cfAluInst(w1);
                    if (kind < 8 || kind > 11 || (w0 >> 30) || (w1 & 3) || (w1 & (1u << 30)))
                        throw std::runtime_error(std::format("unsupported ALU clause: cf={} kind={} uniform={} whole_quad={}",
                            cf / 2, kind, bool((w0 >> 30) || (w1 & 3)), bool(w1 & (1u << 30))));
                    if (kind == 9) push();
                    const auto key = (std::uint64_t(w0 & 0x3FFFFF) << 32) | (((w1 >> 18) & 127) + 1);
                    const auto it = program.clauses.find(key);
                    if (it == program.clauses.end())
                        throw std::runtime_error("missing ALU clause");
                    body += "if(laneActive) {\n";
                    for (const auto &group: it->second) {
                        body += "{ bool nextPred=pred, nextActive=laneActive;\n";
                        for (unsigned i = 0; i < group.size(); ++i) {
                            const auto &in = group[i];
                            body += std::format("bool e{}={};\n", i, in.predSel == 2 ? "!pred" : in.predSel == 3 ? "pred" : "true");
                            const auto condition = predicateCondition(in);
                            if (!condition.empty()) body += std::format("bool p{}={};\n", i, condition);
                            auto value = expression(in);
                            if (in.omod)
                                value = "(" + value + (in.omod == 1 ? " * 2.0)" : in.omod == 2 ? " * 4.0)" : " * 0.5)");
                            if (in.clamp)
                                value = "clampReference(" + value + ")";
                            body += std::format("precise float t{} = {};\n", i, value);
                        }
                        // All sources read the pre-group state. Commit only after every RHS.
                        for (unsigned i = 0; i < group.size(); ++i) {
                            const auto &in = group[i];
                            body += std::format("if(e{}) {{\n", i);
                            if (!in.op3 && (in.raw1 & 8u)) body += std::format("nextPred=p{};\n", i);
                            if (!in.op3 && (in.raw1 & 4u)) body += std::format("nextActive=p{};\n", i);
                            if (in.writeMask)
                                body += std::format("r[{}][{}]=t{};\n", in.dstGpr, in.dstChan, i);
                            body += in.scalarSlot ? std::format("ps=t{};\n", i) : std::format("pv[{}]=t{};\n", in.dstChan, i);
                            body += "}\n";
                        }
                        body += "pred=nextPred; laneActive=nextActive; }\n";
                    }
                    body += "}\n";
                    if (kind == 10) pop(1);
                    if (kind == 11) pop(2);
                    body += "}\n";
                    continue;
                }
                const auto op = cfInst(w1);
                if (op == CF_TEX) {
                    body += "if(laneActive) {\n";
                    // Whole-quad TEX and non-ACTIVE clause conditions remain rejected.
                    constexpr auto texCfMask = (7u << 10) | (1u << 19) | (1u << 21) | (1u << 22) | (127u << 23) | (1u << 31);
                    if (w1 & ~texCfMask)
                        throw std::runtime_error("unsupported texture clause flags");
                    const auto count = (((w1 >> 10) & 7) | ((w1 >> 16) & 8)) + 1;
                    for (unsigned i = 0; i < count; ++i) {
                        const auto off = std::uint64_t(w0) * 2 + i * 4;
                        if (off + 3 >= words.size())
                            throw std::runtime_error("truncated texture clause");
                        const auto t0 = words[off], t1 = words[off + 1], t2 = words[off + 2];
                        const auto texOp = t0 & 31;
                        constexpr auto tex0Mask = 31u | (255u << 8) | (127u << 16);
                        constexpr auto tex1Mask = 127u | (0xFFFu << 9) | (15u << 28);
                        // TEX has three instruction words in a four-word slot. The
                        // final word is padding, not additional instruction flags.
                        if ((texOp != 0x0F && texOp != 0x10 && texOp != 0x13) || (t0 & ~tex0Mask) || (t1 & ~tex1Mask) || (t1 & 0x30000000) != 0x30000000 ||
                            (t2 & 0x7FFF))
                            throw std::runtime_error(std::format("unsupported texture instruction: cf={} slot={} op={} words={:08X},{:08X},{:08X}",
                                                                 cf / 2, i, texOp, t0, t1, t2));
                        if (texOp == 0x10 || texOp == 0x0F)
                            result.requiresBaseLevelOnly = true;
                        if (texOp == 0x0F) result.usesGather = true;
                        const unsigned resource = (t0 >> 8) & 255, sampler = (t2 >> 15) & 31;
                        auto binding = std::find_if(result.textures.begin(), result.textures.end(),
                                                    [&](const auto &b) { return b.resource == resource && b.sampler == sampler; });
                        if (binding == result.textures.end()) {
                            const auto type = resource < resourceTypes.size() ? resourceTypes[resource] : TextureType::TwoD;
                            if (type != TextureType::TwoD && type != TextureType::TwoDArray)
                                throw std::runtime_error("unsupported texture resource type");
                            result.textures.push_back({resource, sampler, unsigned(result.textures.size() + 1), type});
                            binding = result.textures.end() - 1;
                        }
                        const auto reg = std::format("r[{}]", (t0 >> 16) & 127);
                        auto coords = std::format("{},{}", select(reg, (t2 >> 20) & 7), select(reg, (t2 >> 23) & 7));
                        const bool array = binding->type == TextureType::TwoDArray;
                        if (array) coords += std::format(",arrayLayerReference({},textureSize(tex{},0).z)",
                                                       select(reg, (t2 >> 26) & 7), binding->binding);
                        if (texOp == 0x0F) {
                            if (array) throw std::runtime_error("array gather not yet supported");
                            // Validate all source selectors consistently with the interpreter.
                            for (unsigned c = 0; c < 4; ++c) select(reg, (t2 >> (20 + c * 3)) & 7);
                            body += std::format("{{ vec2 coord=vec2({}); vec4 sampled; "
                                "if(any(isnan(coord))||any(isinf(coord))) sampled=vec4(u.borders[{}].r); "
                                "else sampled=textureGather(tex{},coord,0);\n", coords, binding->binding - 1, binding->binding);
                        } else if (array) {
                            body += std::format("{{ vec3 originalCoord=vec3({},{},{}); vec4 sampled; "
                                "if(any(isnan(originalCoord))||any(isinf(originalCoord))) sampled=u.borders[{}]; "
                                "else sampled=sampleArrayReference(tex{},vec3({}),u.sampling[{}]);\n",
                                select(reg, (t2 >> 20) & 7), select(reg, (t2 >> 23) & 7), select(reg, (t2 >> 26) & 7),
                                binding->binding - 1, binding->binding, coords, binding->binding - 1);
                        } else {
                            body += std::format("{{ vec4 sampled=textureLod(tex{},vec2({}),0.0);\n", binding->binding, coords);
                        }
                        for (unsigned c = 0; c < 4; ++c) {
                            const auto selector = (t1 >> (9 + c * 3)) & 7;
                            if (selector != 7)
                                body += std::format("r[{}][{}]={};\n", t1 & 127, c, select("sampled", selector));
                        }
                        body += "}\n";
                    }
                    body += "}\n";
                } else if (op == CF_EXP || op == CF_EXP_DONE) {
                    constexpr auto export1Mask = 0xFFFu | (1u << 21) | (1u << 22) | (127u << 23) | (1u << 31);
                    if (exported || (w0 & ~(127u << 15)) || (w1 & ~export1Mask))
                        throw std::runtime_error(std::format("unsupported export target, addressing or burst: cf={} type={} base={} repeated={} words={:08X},{:08X}",
                                                             cf / 2, (w0 >> 13) & 3, w0 & 0x1FFF, exported, w0, w1));
                    const auto reg = std::format("r[{}]", (w0 >> 15) & 127);
                    body += std::format("if(!laneActive) discard; color=vec4({},{},{},{});\n", select(reg, w1 & 7), select(reg, (w1 >> 3) & 7), select(reg, (w1 >> 6) & 7),
                                        select(reg, (w1 >> 9) & 7));
                    exported = true;
                    body += "wroteColor=true;\n";
                } else if (op == 0x0A || op == 0x0B || op == 0x0D || op == 0x0E) {
                    // VALID_PIXEL_MODE is equivalent here: kill/demote are not supported.
                    constexpr auto mask = 7u | (1u << 22) | (127u << 23) | (1u << 31);
                    if (w1 & ~mask) throw std::runtime_error("unsupported conditional control-flow flags");
                    const auto count = w1 & 7u;
                    if (op == 0x0A || op == 0x0D) {
                        if (w0 <= cf / 2 || count > depth)
                            throw std::runtime_error("unsupported backward jump or stack pop");
                        jumpTargets.emplace_back(w0, depth - count);
                        if (op == 0x0D && !depth) throw std::runtime_error("ELSE without stack entry");
                        body += op == 0x0A ? "if(!laneActive) {\n" : "if(laneActive) {\n";
                        if (count) body += std::format("laneActive=activeStack[{}];\n", depth - count);
                        body += std::format("pc={}u;\n}}", w0);
                        if (op == 0x0D) body += std::format(" else {{ laneActive=activeStack[{}]&&!laneActive; }}", depth - 1);
                        body += "\n";
                    } else {
                        if (w0 || (op == 0x0B && count)) throw std::runtime_error("unsupported stack control fields");
                        if (op == 0x0B) push(); else pop(count);
                    }
                } else {
                    constexpr auto controlMask = (1u << 21) | (127u << 23) | (1u << 31);
                    if ((op != CF_NOP && op != CF_RETURN) || w0 || (w1 & ~controlMask))
                        throw std::runtime_error("unsupported control flow");
                }
                body += "}\n";
                if (cfEop(w1) || op == CF_RETURN) {
                    terminated = true;
                    break;
                }
            }
            if (!exported || !terminated)
                throw std::runtime_error("missing color export or shader termination");
            if (depth) throw std::runtime_error("unbalanced control-flow stack");
            for (const auto [target, expectedDepth] : jumpTargets)
                if (target >= entryDepth.size() || entryDepth[target] != expectedDepth)
                    throw std::runtime_error("jump target has incompatible control-flow stack");
            result.source = "#version 450\nlayout(location=0) in vec4 inputs[4];\nlayout(location=0) out vec4 color;\n"
                            "layout(set=0,binding=0,std140) uniform Constants { vec4 c[256]; vec4 borders[16]; vec4 sampling[16]; } u;\n"
                            "float arrayLayerReference(float x,int layers) { float v=clamp(x,0.0,float(layers-1)); "
                            "return floor(v)+step(0.5,fract(v)); }\n"
                            "float clampReference(float x) { return isnan(x) ? 0.0 : clamp(x,0.0,1.0); }\n"
                            "float minReference(float a,float b) { return isnan(a) ? b : isnan(b) ? a : min(a,b); }\n"
                            "float maxReference(float a,float b) { return isnan(a) ? b : isnan(b) ? a : max(a,b); }\n"
                            "float roundEvenReference(float x) { if(isnan(x)||isinf(x)) return x; float v=roundEven(x); "
                            "return v==0.0 ? uintBitsToFloat(floatBitsToUint(x)&2147483648u) : v; }\n"
                            "float logReference(float x) { if(isnan(x)) return x; if(x==0.0) return uintBitsToFloat(4286578688u); "
                            "if(x<0.0) return uintBitsToFloat(2143289344u); if(isinf(x)) return x; return log2(x); }\n"
                            "float logClamped(float x) { float v=logReference(x); return isinf(v)&&v<0.0 ? -uintBitsToFloat(2139095039u) : v; }\n";
            result.source += R"(
float arrayCoordinate(float v,int mode) {
    if(mode==0) return fract(v);
    if(mode==1) { float t=mod(v,2.0); return t>1.0 ? 2.0-t : t; }
    return clamp(v,0.0,1.0);
}
int arrayIndex(int v,int size,int mode) {
    return mode==0 ? ((v%size)+size)%size : clamp(v,0,size-1);
}
vec4 arrayTexel(sampler2DArray tex,ivec2 p,int layer,ivec2 size,ivec2 modes) {
    return texelFetch(tex,ivec3(arrayIndex(p.x,size.x,modes.x),arrayIndex(p.y,size.y,modes.y),layer),0);
}
vec4 sampleArrayReference(sampler2DArray tex,vec3 p,vec4 state) {
    ivec2 size=textureSize(tex,0).xy;
    ivec2 modes=ivec2(state.xy);
    vec2 xy=vec2(arrayCoordinate(p.x,modes.x),arrayCoordinate(p.y,modes.y))*vec2(size);
    int layer=int(p.z);
    if(state.z==0.0) return arrayTexel(tex,ivec2(floor(xy)),layer,size,modes);
    vec2 pos=xy-vec2(0.5);
    ivec2 base=ivec2(floor(pos));
    vec2 fraction=fract(pos);
    vec4 a=arrayTexel(tex,base,layer,size,modes);
    vec4 b=arrayTexel(tex,base+ivec2(1,0),layer,size,modes);
    vec4 c=arrayTexel(tex,base+ivec2(0,1),layer,size,modes);
    vec4 d=arrayTexel(tex,base+ivec2(1,1),layer,size,modes);
    return mix(mix(a,b,fraction.x),mix(c,d,fraction.x),fraction.y);
}
)";
            for (const auto &b: result.textures)
                result.source += std::format("layout(set=0,binding={}) uniform sampler2D{} tex{};\n", b.binding,
                                            b.type == TextureType::TwoDArray ? "Array" : "", b.binding);
            result.source += body + "if(!wroteColor) discard;\n}\n";
        } catch (const std::runtime_error &error) {
            result = {};
            result.error = error.what();
        }
        return result;
    }
} // namespace Core::Gfx::Latte
