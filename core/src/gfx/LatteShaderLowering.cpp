#include "gfx/LatteShaderLowering.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <set>
#include <stdexcept>

#include "gfx/UniformBufferFetch.hpp"

namespace Core::Gfx::Latte {
    std::shared_ptr<const FragmentShader> FragmentShaderCache::get(const std::vector<std::uint32_t> &words,
                                                                   std::span<const TextureType> resourceTypes, bool onlyColorZero,
                                                                   unsigned colorExport)
    {
        // Missing resource types default to 2D, so trailing defaults are equivalent.
        while (!resourceTypes.empty() && resourceTypes.back() == TextureType::TwoD)
            resourceTypes = resourceTypes.first(resourceTypes.size() - 1);
        Key key{words, {resourceTypes.begin(), resourceTypes.end()}, onlyColorZero, colorExport};
        if (const auto found = m_entries.find(key); found != m_entries.end()) {
            ++m_hits;
            return found->second;
        }
        ++m_misses;
        auto shader = std::make_shared<const FragmentShader>(lowerFragmentShader(*decodeProgram(words), resourceTypes, onlyColorZero, colorExport));
        constexpr std::size_t maxBytes = std::size_t{256} * 1024 * 1024;
        const auto bytes = words.size() * sizeof(std::uint32_t) + resourceTypes.size() * sizeof(TextureType) + shader->source.size() +
                           shader->error.size() + shader->textures.size() * sizeof(TextureBinding);
        if (bytes <= maxBytes) {
            if (m_entries.size() >= maxEntries || bytes > maxBytes - m_bytes) {
                m_entries.clear();
                m_bytes = 0;
            }
            m_entries.emplace(std::move(key), shader);
            m_bytes += bytes;
        }
        return shader;
    }

    namespace {

        // Monotone uniformity facts: masked writes can lose a fact but cannot
        // invent one. Only a proven all-lane overwrite can replace it. Constants
        // are uniform; interpolated inputs, relative reads and samples are not.
        class UniformValues {
            public:
                explicit UniformValues(bool vertexStage = false)
                {
                    for (auto &reg: registers)
                        reg.fill(true);
                    for (unsigned i = 0; i < 16; ++i)
                        registers[i + unsigned(vertexStage)].fill(false);
                }
                bool read(const Src &src) const
                {
                    if (src.rel)
                        return false;
                    if (src.sel < 128)
                        return registers[src.sel][src.chan];
                    if (src.sel == SRC_PV)
                        return previous[src.chan];
                    return src.sel != SRC_PS || scalar;
                }
                void write(const AluInst &in, bool value, bool allActive, bool maskUniform)
                {
                    const auto update = [&](bool &fact) {
                        if (allActive && !in.predSel)
                            fact = value;
                        else
                            fact = fact && value && maskUniform && (!in.predSel || predicate);
                    };
                    if (in.writeMask)
                        update(registers[in.dstGpr][in.dstChan]);
                    update(in.scalarSlot ? scalar : previous[in.dstChan]);
                }
                void merge(const UniformValues &other)
                {
                    for (unsigned reg = 0; reg < registers.size(); ++reg)
                        for (unsigned channel = 0; channel < 4; ++channel)
                            registers[reg][channel] = registers[reg][channel] && other.registers[reg][channel];
                    for (unsigned channel = 0; channel < 4; ++channel)
                        previous[channel] = previous[channel] && other.previous[channel];
                    scalar = scalar && other.scalar;
                    predicate = predicate && other.predicate;
                }
                std::array<std::array<bool, 4>, 128> registers{};
                std::array<bool, 4> previous{true, true, true, true};
                bool scalar{true}, predicate{true};
        };

        // Whole-quad ALU may write registers for inactive lanes. A lane-local
        // lowering is equivalent only while those extra writes stay unobservable:
        // no derivatives, mask updates, or reads after their mask is restored.
        class WholeQuadWrites {
            public:
                void read(const Src &src, unsigned depth) const
                {
                    if (src.sel < 128 && src.chan < 4) {
                        if (src.rel)
                            for (const auto &reg: m_registers)
                                check(reg[src.chan], depth, src);
                        else
                            check(m_registers[src.sel][src.chan], depth, src);
                    } else if (src.sel == SRC_PV && src.chan < 4)
                        check(m_previous[src.chan], depth, src);
                    else if (src.sel == SRC_PS)
                        check(m_scalar, depth, src);
                }
                void write(const AluInst &in, unsigned depth, bool wholeQuad, bool unconditional)
                {
                    const auto update = [&](unsigned &scope) {
                        if (wholeQuad)
                            scope = std::max(scope, depth);
                        else if (in.predSel == 0 && (depth || unconditional))
                            scope = std::min(scope, depth);
                    };
                    if (in.writeMask)
                        update(m_registers[in.dstGpr][in.dstChan]);
                    update(in.scalarSlot ? m_scalar : m_previous[in.dstChan]);
                }
                void restore(unsigned depth)
                {
                    for (auto &reg: m_registers)
                        for (auto &scope: reg)
                            escape(scope, depth);
                    for (auto &scope: m_previous)
                        escape(scope, depth);
                    escape(m_scalar, depth);
                }
                void textureWrite(unsigned reg, unsigned channel, unsigned depth, bool unconditional)
                {
                    if (depth || unconditional)
                        m_registers[reg][channel] = std::min(m_registers[reg][channel], depth);
                }
                void merge(const WholeQuadWrites &other)
                {
                    for (unsigned reg = 0; reg < m_registers.size(); ++reg)
                        for (unsigned channel = 0; channel < 4; ++channel)
                            m_registers[reg][channel] = std::max(m_registers[reg][channel], other.m_registers[reg][channel]);
                    for (unsigned channel = 0; channel < 4; ++channel)
                        m_previous[channel] = std::max(m_previous[channel], other.m_previous[channel]);
                    m_scalar = std::max(m_scalar, other.m_scalar);
                }

            private:
                static void check(unsigned scope, unsigned depth, const Src &src)
                {
                    if (scope > depth)
                        throw std::runtime_error(std::format("whole-quad value escapes masked region: source={}.{} scope={} depth={}", src.sel,
                                                             src.chan, scope, depth));
                }
                static void escape(unsigned &scope, unsigned depth)
                {
                    // Once a scope has ended, another PUSH must not hide the
                    // escaped value. Only a proven unconditional overwrite can
                    // remove it; masked or predicated writes cannot.
                    if (scope > depth)
                        scope = 33;
                }
                std::array<std::array<unsigned, 4>, 128> m_registers{};
                std::array<unsigned, 4> m_previous{};
                unsigned m_scalar{};
        };

        struct LoweringConstants {
                FragmentShader &shader;
                std::array<unsigned, 2> bank{}, address{}, mode{};
                std::array<bool, 4> addressValid{};
                std::string read(const Src &src)
                {
                    if (src.sel >= 128 && src.sel < 192 && mode[(src.sel - 128) / 32]) {
                        const auto window = (src.sel - 128) / 32;
                        FragmentShader::UniformReference ref{bank[window], address[window] * 16 + (src.sel - 128) % 32};
                        auto it = std::find(shader.uniforms.begin(), shader.uniforms.end(), ref);
                        if (it == shader.uniforms.end()) {
                            if (shader.uniforms.size() >= 512)
                                throw std::runtime_error("uniform reference budget exceeded");
                            it = shader.uniforms.insert(it, ref);
                        }
                        return std::format("u.k[{}][{}]", std::distance(shader.uniforms.begin(), it), src.chan);
                    }
                    return std::format("u.c[{}][{}]", src.sel >= 256 ? src.sel - 256 : src.sel - 128, src.chan);
                }
        };
        std::string source(const Src &s, const AluInst &instruction, LoweringConstants &constants)
        {
            if (s.chan > 3)
                throw std::runtime_error("invalid ALU source channel");
            std::string value;
            if (s.rel) {
                const auto indexMode = (instruction.raw0 >> 26) & 7;
                if ((s.sel >= 128 && s.sel < 256) || s.sel >= 512 || indexMode > 3 || !constants.addressValid[indexMode])
                    throw std::runtime_error("unsupported or uninitialized relative ALU source");
                if (s.sel < 128) {
                    const auto index = std::format("({}+ar[{}])", s.sel, indexMode);
                    value = std::format("({0}>=0 && {0}<128 ? r[clamp({0},0,127)][{1}] : uintBitsToFloat(2147483647u))", index, s.chan);
                } else
                    value = std::format("relativeConstant({}+ar[{}],{})", s.sel - 256, indexMode, s.chan);
            } else if (s.sel < 128)
                value = std::format("r[{}][{}]", s.sel, s.chan);
            else if (s.sel < 192 || (s.sel >= 256 && s.sel < 512))
                value = constants.read(s);
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

        std::string predicateCondition(const AluInst &in, LoweringConstants &constants)
        {
            if (in.op3)
                return {};
            std::string comparison;
            switch (in.op) {
                case OP2_PRED_SETE:
                case OP2_PRED_SETE_PUSH:
                case OP2_PRED_SETE_INT:
                    comparison = "==";
                    break;
                case OP2_PRED_SETGT:
                case OP2_PRED_SETGT_PUSH:
                case OP2_PRED_SETGT_INT:
                    comparison = ">";
                    break;
                case OP2_PRED_SETGE:
                case OP2_PRED_SETGE_PUSH:
                case OP2_PRED_SETGE_INT:
                    comparison = ">=";
                    break;
                case OP2_PRED_SETNE:
                case OP2_PRED_SETNE_PUSH:
                case OP2_PRED_SETNE_INT:
                    comparison = "!=";
                    break;
                default:
                    return {};
            }
            auto a = source(in.src[0], in, constants), b = source(in.src[1], in, constants);
            if (in.op >= OP2_PRED_SETE_INT) {
                a = "floatBitsToInt(" + a + ")";
                b = "floatBitsToInt(" + b + ")";
            }
            return "(" + a + comparison + b + ")";
        }

        std::string expression(const AluInst &in, LoweringConstants &constants)
        {
            // Mask updates are modeled only for predicate-set operations.
            const auto condition = predicateCondition(in, constants);
            // INDEX_MODE only affects relative operands; their AR validation is
            // performed by source(). Accept the four AR components and LOOP.
            const std::uint32_t destinationMask = in.op3 ? 0xEFFFFDFFu : condition.empty() ? 0xEFFFFFF3u : 0xEFFFFFFFu;
            if ((in.raw1 & ~destinationMask) || in.predSel == 1)
                throw std::runtime_error("unsupported ALU addressing or control flags");
            if (((in.raw0 >> 26) & 7) > 4)
                throw std::runtime_error("reserved ALU index mode");
            // R700 BANK_SWIZZLE schedules register-bank reads, not operand components.
            // Functional execution reads the pre-group state without bank contention.
            const auto bankSwizzle = (in.raw1 >> 18) & 7;
            if (bankSwizzle > (in.scalarSlot ? 3u : 5u))
                throw std::runtime_error("reserved ALU bank swizzle");
            if (!condition.empty())
                return in.op >= OP2_PRED_SETE_INT ? "uintBitsToFloat(" + condition + " ? 4294967295u : 0u)" : "(" + condition + " ? 1.0 : 0.0)";
            auto a = source(in.src[0], in, constants);
            const auto b = source(in.src[1], in, constants);
            if (in.op3) {
                const auto c = source(in.src[2], in, constants);
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
                case OP2_MULLO_INT:
                case OP2_MULLO_UINT:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") * floatBitsToUint(" + b + "))";
                case OP2_SUB_INT:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") - floatBitsToUint(" + b + "))";
                case OP2_ASHR:
                    return "intBitsToFloat(floatBitsToInt(" + a + ") >> (floatBitsToUint(" + b + ") & 31u))";
                case OP2_LSHR:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") >> (floatBitsToUint(" + b + ") & 31u))";
                case OP2_LSHL:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") << (floatBitsToUint(" + b + ") & 31u))";
                case OP2_SETE:
                    return "(" + a + " == " + b + " ? 1.0 : 0.0)";
                case OP2_SETGT:
                    return "(" + a + " > " + b + " ? 1.0 : 0.0)";
                case OP2_SETGE:
                    return "(" + a + " >= " + b + " ? 1.0 : 0.0)";
                case OP2_SETNE:
                    return "(" + a + " != " + b + " ? 1.0 : 0.0)";
                case OP2_SETE_DX10:
                    return "uintBitsToFloat(" + a + " == " + b + " ? 4294967295u : 0u)";
                case OP2_SETGE_DX10:
                    return "uintBitsToFloat(" + a + " >= " + b + " ? 4294967295u : 0u)";
                case OP2_SETNE_DX10:
                    return "uintBitsToFloat(" + a + " != " + b + " ? 4294967295u : 0u)";
                case OP2_SETE_INT:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") == floatBitsToUint(" + b + ") ? 4294967295u : 0u)";
                case OP2_SETNE_INT:
                    return "uintBitsToFloat(floatBitsToUint(" + a + ") != floatBitsToUint(" + b + ") ? 4294967295u : 0u)";
                case OP2_SETGE_INT:
                    return "uintBitsToFloat(floatBitsToInt(" + a + ") >= floatBitsToInt(" + b + ") ? 4294967295u : 0u)";
                case OP2_SETGT_INT:
                    return "uintBitsToFloat(floatBitsToInt(" + a + ") > floatBitsToInt(" + b + ") ? 4294967295u : 0u)";
                case OP2_FLT_TO_INT:
                    return "uintBitsToFloat(floatToIntBits(" + a + "))";
                case OP2_FLT_TO_UINT:
                    return "uintBitsToFloat(floatToUintBits(" + a + "))";
                case OP2_INT_TO_FLT:
                    return "float(floatBitsToInt(" + a + "))";
                case OP2_UINT_TO_FLT:
                    return "float(floatBitsToUint(" + a + "))";
                case OP2_SETGT_DX10:
                    return "uintBitsToFloat(" + a + " > " + b + " ? 4294967295u : 0u)";
                case OP2_DOT4:
                case OP2_DOT4_IEEE:
                    return "dotValue";
                case OP2_CUBE:
                    return std::format("cubeValue[{}]", in.dstChan);
                case OP2_MOV:
                    return a;
                case OP2_NOP:
                    return "0.0";
                case OP2_FLOOR:
                    return "floor(" + a + ")";
                case OP2_TRUNC:
                    return "truncReference(" + a + ")";
                case OP2_MOVA:
                    return "intBitsToFloat(addressReference(floor(" + a + "+0.5)))";
                case OP2_MOVA_FLOOR:
                    return "intBitsToFloat(addressReference(floor(" + a + ")))";
                case OP2_MOVA_INT:
                    return "intBitsToFloat(addressIntReference(floatBitsToInt(" + a + ")))";
                case OP2_SIN:
                    return "sin(" + a + "/0.1591549367)";
                case OP2_COS:
                    return "cos(" + a + "/0.1591549367)";
                case OP2_RNDNE:
                    return "roundEvenReference(" + a + ")";
                case OP2_FRACT:
                    return "fract(" + a + ")";
                case OP2_EXP_IEEE:
                    return "exp2(" + a + ")";
                case OP2_SQRT_IEEE:
                    return "sqrtReference(" + a + ")";
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

        // The accepted subset has only static GPR addressing. Separate locals
        // avoid a dynamically initialized 128-register private array and let both
        // SPIR-V and device compilers eliminate unused registers.
        std::string scalarizeRegisters(std::string body)
        {
            std::array<bool, 128> used{};
            std::size_t at = 0;
            while ((at = body.find("r[", at)) != std::string::npos) {
                // Match the GPR identifier, not a suffix such as the AR's ar[].
                if (at && ((body[at - 1] >= 'a' && body[at - 1] <= 'z') || (body[at - 1] >= 'A' && body[at - 1] <= 'Z') ||
                           (body[at - 1] >= '0' && body[at - 1] <= '9') || body[at - 1] == '_')) {
                    at += 2;
                    continue;
                }
                auto end = at + 2;
                unsigned reg = 0;
                if (end == body.size() || body[end] < '0' || body[end] > '9')
                    throw std::runtime_error("dynamic GPR addressing reached native emission");
                while (end < body.size() && body[end] >= '0' && body[end] <= '9')
                    reg = reg * 10 + unsigned(body[end++] - '0');
                if (reg >= used.size() || end == body.size() || body[end] != ']')
                    throw std::runtime_error("invalid native register index");
                used[reg] = true;
                const auto name = std::format("reg{}", reg);
                body.replace(at, end - at + 1, name);
                at += name.size();
            }
            std::string declarations;
            for (unsigned reg = 0; reg < used.size(); ++reg)
                if (used[reg])
                    declarations += std::format("vec4 reg{}=vec4(0.0);\n", reg);
            body.insert(body.find('\n') + 1, declarations);
            return body;
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

    static FragmentShader lowerShader(const Program &program, std::span<const TextureType> resourceTypes, bool vertexStage,
                                      bool onlyColorZero = false, unsigned colorExport = 0)
    {
        FragmentShader result;
        result.exportsColor = false;
        LoweringConstants constants{result, {}, {}, {}};
        try {
            if (!program.valid || program.words.size() < 2 || colorExport >= 8 || (!onlyColorZero && colorExport))
                throw std::runtime_error("invalid shader structure");
            std::string body = "void main() {\n";
            std::set<unsigned> cubeIndices, uniformBuffers;
            for (unsigned i = 0; i < 16; ++i)
                body += vertexStage ? std::format("r[{}]=attributes[{}];\n", i + 1, i) : std::format("r[{}]=inputs[{}];\n", i, i);
            if (vertexStage)
                body += "vec4 position=vec4(0.0,0.0,0.0,1.0); vec4 parameters[32]; for(int i=0;i<32;++i) parameters[i]=vec4(0.0);\n";
            body += "vec4 pv=vec4(0.0); float ps=0.0;\nbool pred=true,laneActive=true,wroteOutput=false; bool activeStack[32]; uint pc=0u;\n";
            bool exported = false, terminated = false, ignoredColorExports = false;
            bool terminalUnconditionalColor = false;
            bool varyingWholeQuadUsed = false, implicitSamples = false;
            bool maskedControl = false;
            bool rootMaskChanged = false;
            WholeQuadWrites wholeQuadWrites;
            std::map<unsigned, WholeQuadWrites> incomingWholeQuadWrites;
            UniformValues uniformValues(vertexStage);
            std::map<unsigned, UniformValues> incomingUniformValues;
            bool maskUniform = true, allActive = true;
            bool varyingControlSeen = false;
            std::array<bool, 32> uniformMaskStack{}, allActiveStack{};
            unsigned depth = 0;
            std::vector<unsigned> entryDepth;
            std::vector<std::pair<unsigned, unsigned>> jumpTargets;
            const auto &words = program.words;
            for (std::size_t cf = 0; cf + 1 < words.size(); cf += 2) {
                const auto w0 = words[cf], w1 = words[cf + 1];
                if (const auto incoming = incomingWholeQuadWrites.find(cf / 2); incoming != incomingWholeQuadWrites.end())
                    wholeQuadWrites.merge(incoming->second);
                if (const auto incoming = incomingUniformValues.find(cf / 2); incoming != incomingUniformValues.end())
                    uniformValues.merge(incoming->second);
                entryDepth.push_back(depth);
                body += std::format("if(pc<={}u) {{\n", cf / 2);
                const auto push = [&] {
                    maskedControl = true;
                    if (depth == 32)
                        throw std::runtime_error("control-flow stack overflow");
                    uniformMaskStack[depth] = maskUniform;
                    allActiveStack[depth] = allActive;
                    body += std::format("activeStack[{}]=laneActive;\n", depth++);
                };
                const auto pop = [&](unsigned count) {
                    if (count > depth)
                        throw std::runtime_error("control-flow stack underflow");
                    if (count) {
                        depth -= count;
                        maskUniform = uniformMaskStack[depth];
                        allActive = allActiveStack[depth];
                        wholeQuadWrites.restore(depth);
                        body += std::format("laneActive=activeStack[{}];\n", depth);
                    }
                };
                if (isAluClause(w1)) {
                    const auto kind = cfAluInst(w1);
                    const bool wholeQuad = w1 & (1u << 30);
                    if (kind < 8 || (kind > 11 && kind != 15) || (kind == 15 && !depth) || (w0 >> 30) == 3 || (w1 & 3) == 3 ||
                        (vertexStage && wholeQuad) || (wholeQuad && (kind != 8 || !depth)))
                        throw std::runtime_error(std::format("unsupported ALU clause: cf={} kind={} uniform={} whole_quad={}", cf / 2, kind,
                                                             bool((w0 >> 30) || (w1 & 3)), bool(w1 & (1u << 30))));
                    constants.bank = {(w0 >> 22) & 15, (w0 >> 26) & 15};
                    constants.address = {(w1 >> 2) & 255, (w1 >> 10) & 255};
                    constants.mode = {(w0 >> 30) & 3, w1 & 3};
                    if (kind == 9)
                        push();
                    const auto key = (std::uint64_t(w0 & 0x3FFFFF) << 32) | (((w1 >> 18) & 127) + 1);
                    const auto it = program.clauses.find(key);
                    if (it == program.clauses.end())
                        throw std::runtime_error("missing ALU clause");
                    constants.addressValid.fill(false);
                    const bool clauseAllActive = allActive && std::none_of(jumpTargets.begin(), jumpTargets.end(),
                                                                           [&](const auto &target) { return target.first > cf / 2; });
                    // After any varying execute mask, keep this proof disabled:
                    // stack joins would need additional per-edge mask facts.
                    const bool clauseMaskUniform = maskUniform && !varyingControlSeen;
                    varyingWholeQuadUsed |= wholeQuad && !clauseMaskUniform;
                    body += "if(laneActive) { ivec4 ar=ivec4(0);\n";
                    for (const auto &group: it->second) {
                        const bool unconditional =
                                depth == 0 &&
                                std::none_of(jumpTargets.begin(), jumpTargets.end(), [&](const auto &target) { return target.first > cf / 2; }) &&
                                !rootMaskChanged;
                        if (std::any_of(group.begin(), group.end(), [](const auto &in) { return in.isAddressWrite(); })) {
                            for (const auto &in: group) {
                                if (in.isAddressWrite() && (in.predSel || in.omod || in.clamp))
                                    throw std::runtime_error("unsupported predicated or modified address write");
                                for (unsigned operand = 0; operand < (in.op3 ? 3u : 2u); ++operand)
                                    if (in.src[operand].rel)
                                        throw std::runtime_error("address write and relative read share an ALU group");
                            }
                        }
                        body += "{ bool nextPred=pred, nextActive=laneActive;\n";
                        const bool dotGroup = !group.empty() && !group[0].op3 && (group[0].op == OP2_DOT4 || group[0].op == OP2_DOT4_IEEE);
                        const bool cubeGroup = std::any_of(group.begin(), group.end(), [](const auto &in) { return !in.op3 && in.op == OP2_CUBE; });
                        if (cubeGroup) {
                            if (!isCanonicalCubeGroup(group))
                                throw std::runtime_error("unsupported CUBE group layout");
                            body += std::format("vec4 cubeValue=cubeReference({},{},{});\n", source(group[1].src[1], group[1], constants),
                                                source(group[0].src[1], group[0], constants), source(group[0].src[0], group[0], constants));
                        }
                        if (dotGroup) {
                            if (group.size() < 4 || group.size() > 5)
                                throw std::runtime_error("incomplete DOT4 reduction");
                            body += "precise float dotValue=0.0;\n";
                            for (unsigned i = 0; i < 4; ++i) {
                                const auto &slot = group[i];
                                if (slot.op3 || slot.scalarSlot || slot.op != group[0].op || slot.dstChan != i || slot.predSel != group[0].predSel ||
                                    (slot.raw1 & 12u))
                                    throw std::runtime_error("unsupported DOT4 group layout");
                                body += std::format("dotValue=dotValue+({}*{});\n", source(slot.src[0], slot, constants),
                                                    source(slot.src[1], slot, constants));
                            }
                            if (group.size() == 5 &&
                                (!group[4].scalarSlot || (!group[4].op3 && (group[4].op == OP2_DOT4 || group[4].op == OP2_DOT4_IEEE))))
                                throw std::runtime_error("unsupported DOT4 scalar slot");
                        } else if (std::any_of(group.begin(), group.end(),
                                               [](const auto &in) { return !in.op3 && (in.op == OP2_DOT4 || in.op == OP2_DOT4_IEEE); }))
                            throw std::runtime_error("DOT4 must start its vector group");

                        std::array<bool, 5> uniformResults{};
                        const bool groupPredicateUniform = uniformValues.predicate;
                        bool nextPredicateUniform = groupPredicateUniform, nextMaskUniform = maskUniform, nextAllActive = allActive;
                        bool dotUniform = true;
                        if (dotGroup || cubeGroup)
                            for (unsigned i = 0; i < 4; ++i)
                                dotUniform &= uniformValues.read(group[i].src[0]) && uniformValues.read(group[i].src[1]);
                        for (unsigned i = 0; i < group.size(); ++i) {
                            const auto &in = group[i];
                            uniformResults[i] = !(dotGroup || cubeGroup) || in.scalarSlot
                                                        ? uniformValues.read(in.src[0]) && uniformValues.read(in.src[1]) &&
                                                                  (!in.op3 || uniformValues.read(in.src[2]))
                                                        : dotUniform;
                            if (wholeQuad && !in.op3 && (in.raw1 & 12u))
                                throw std::runtime_error("whole-quad ALU mask updates require quad execution");
                            for (unsigned operand = 0; operand < (in.op3 ? 3u : 2u); ++operand)
                                wholeQuadWrites.read(in.src[operand], depth);
                            body += std::format("bool e{}={};\n", i, in.predSel == 2 ? "!pred" : in.predSel == 3 ? "pred" : "true");
                            const auto condition = predicateCondition(in, constants);
                            if (!condition.empty())
                                body += std::format("bool p{}={};\n", i, condition);
                            auto value = expression(in, constants);
                            if (in.omod)
                                value = std::format("({}{}", value, in.omod == 1 ? " * 2.0)" : in.omod == 2 ? " * 4.0)" : " * 0.5)");
                            if (in.clamp)
                                value = std::format("clampReference({})", value);
                            body += std::format("precise float t{} = {};\n", i, value);
                        }
                        // All sources read the pre-group state. Commit only after every RHS.
                        for (unsigned i = 0; i < group.size(); ++i) {
                            const auto &in = group[i];
                            wholeQuadWrites.write(in, depth, wholeQuad && !clauseMaskUniform, unconditional);
                            uniformValues.write(in, uniformResults[i], clauseAllActive, clauseMaskUniform && (!in.predSel || groupPredicateUniform));
                            if (!in.op3 && (in.raw1 & 8u))
                                nextPredicateUniform = clauseAllActive && !in.predSel
                                                               ? uniformResults[i]
                                                               : nextPredicateUniform && uniformResults[i] && clauseMaskUniform &&
                                                                         (!in.predSel || groupPredicateUniform);
                            if (!in.op3 && (in.raw1 & 4u)) {
                                nextMaskUniform = nextMaskUniform && uniformResults[i] && (!in.predSel || groupPredicateUniform);
                                nextAllActive = false;
                            }
                            body += std::format("if(e{}) {{\n", i);
                            if (!in.op3 && (in.raw1 & 8u))
                                body += std::format("nextPred=p{};\n", i);
                            if (!in.op3 && (in.raw1 & 4u))
                                maskedControl = true;
                            if (!in.op3 && (in.raw1 & 4u) && depth == 0)
                                rootMaskChanged = true;
                            if (!in.op3 && (in.raw1 & 4u))
                                body += std::format("nextActive=p{};\n", i);
                            if (in.writeMask)
                                body += std::format("r[{}][{}]=t{};\n", in.dstGpr, in.dstChan, i);
                            if (in.isAddressWrite()) {
                                body += std::format("ar[{}]=floatBitsToInt(t{});\n", in.dstChan, i);
                                constants.addressValid[in.dstChan] = true;
                            }
                            body += in.scalarSlot ? std::format("ps=t{};\n", i) : std::format("pv[{}]=t{};\n", in.dstChan, i);
                            body += "}\n";
                        }
                        uniformValues.predicate = nextPredicateUniform;
                        maskUniform = nextMaskUniform;
                        varyingControlSeen |= !maskUniform;
                        allActive = nextAllActive;
                        body += "pred=nextPred; laneActive=nextActive; }\n";
                    }
                    body += "}\n";
                    if (kind == 10)
                        pop(1);
                    if (kind == 11)
                        pop(2);
                    if (kind == 15) {
                        wholeQuadWrites.restore(depth - 1);
                        maskUniform = maskUniform && uniformMaskStack[depth - 1];
                        allActive = false;
                        body += std::format("laneActive=activeStack[{}]&&!laneActive;\n", depth - 1);
                    }
                    body += "}\n";
                    continue;
                }
                const auto op = cfInst(w1);
                if (op == CF_TEX || (vertexStage && (op == CF_VTX || op == CF_VTX_TC))) {
                    body += "if(laneActive) {\n";
                    // Whole-quad TEX and non-ACTIVE clause conditions remain rejected.
                    constexpr auto texCfMask = (7u << 10) | (1u << 19) | (1u << 21) | (1u << 22) | (127u << 23) | (1u << 31);
                    if (w1 & ~texCfMask)
                        throw std::runtime_error("unsupported texture clause flags");
                    const auto count = (((w1 >> 10) & 7) | ((w1 >> 16) & 8)) + 1;
                    for (unsigned i = 0; i < count; ++i) {
                        const auto off = std::uint64_t(w0) * 2 + std::uint64_t(i) * 4;
                        if (off + 3 >= words.size())
                            throw std::runtime_error("truncated texture clause");
                        const auto t0 = words[off], t1 = words[off + 1], t2 = words[off + 2];
                        const auto texOp = t0 & 31;
                        if (vertexStage && texOp == 0) {
                            const auto instruction = decodeUniformBufferFetch(t0, t1, t2);
                            if (!instruction)
                                throw std::runtime_error("unsupported uniform buffer fetch flags");
                            const auto &f = *instruction;
                            uniformBuffers.insert(f.bank);
                            wholeQuadWrites.read({f.source, f.channel, false, false, false}, depth);
                            body += std::format("{{ vec4 fetched=fetchBuffer{}(floatBitsToUint(r[{}][{}]),{}u);\n", f.bank, f.source, f.channel,
                                                f.offset);
                            for (unsigned c = 0; c < 4; ++c)
                                if (f.selectors[c] != 7) {
                                    uniformValues.registers[f.destination][c] = false;
                                    wholeQuadWrites.textureWrite(f.destination, c, depth, !rootMaskChanged && jumpTargets.empty());
                                    body += std::format("r[{}][{}]={};\n", f.destination, c, select("fetched", f.selectors[c]));
                                }
                            body += "}\n";
                            continue;
                        }
                        if (vertexStage && texOp != 0x13)
                            throw std::runtime_error("vertex texture opcode requires explicit base LOD");
                        constexpr auto tex0Mask = 31u | (255u << 8) | (127u << 16);
                        constexpr auto tex1Mask = 127u | (0xFFFu << 9) | (15u << 28);
                        if (texOp == 0x0E) { // SET_CUBEMAP_INDEX: per-resource array selector, no GPR write.
                            const auto selector = (t2 >> 20) & 7;
                            if ((t0 & ~tex0Mask) || t1 != 0x001FFE00u || (t2 & 0xFFFFFu) || selector > 5)
                                throw std::runtime_error("unsupported cube index instruction flags");
                            const auto resource = (t0 >> 8) & 255;
                            cubeIndices.insert(resource);
                            if (selector < 4)
                                wholeQuadWrites.read({(t0 >> 16) & 127, selector, false, false, false}, depth);
                            body += std::format("cubeIndex{}={};\n", resource, select(std::format("r[{}]", (t0 >> 16) & 127), selector));
                            continue;
                        }
                        // TEX has three instruction words in a four-word slot. The
                        // final word is padding, not additional instruction flags.
                        // 0x18/0x1B: SAMPLE_C/SAMPLE_C_LZ depth comparison against src.w.
                        const bool compare = texOp == 0x18 || texOp == 0x1B;
                        if ((texOp != 0x0F && texOp != 0x10 && texOp != 0x11 && texOp != 0x13 && !compare) || (t0 & ~tex0Mask) || (t1 & ~tex1Mask) ||
                            (t1 & 0x30000000) != 0x30000000 || (t2 & 0x7FFF))
                            throw std::runtime_error(std::format("unsupported texture instruction: cf={} slot={} op={} words={:08X},{:08X},{:08X}",
                                                                 cf / 2, i, texOp, t0, t1, t2));
                        if (texOp == 0x10 || texOp == 0x0F || texOp == 0x18)
                            result.requiresBaseLevelOnly = true;
                        if (texOp == 0x0F)
                            result.usesGather = true;
                        const bool explicitLod = texOp == 0x11 || texOp == 0x13 || texOp == 0x1B;
                        implicitSamples |= !explicitLod; // SAMPLE/gather/SAMPLE_C; the _LZ forms are explicit.
                        const unsigned resource = (t0 >> 8) & 255, sampler = (t2 >> 15) & 31;
                        auto binding = std::find_if(result.textures.begin(), result.textures.end(),
                                                    [&](const auto &b) { return b.resource == resource && b.sampler == sampler; });
                        if (binding == result.textures.end()) {
                            const auto type = resource < resourceTypes.size() ? resourceTypes[resource] : TextureType::TwoD;
                            if (type != TextureType::TwoD && type != TextureType::TwoDArray && type != TextureType::ThreeD &&
                                type != TextureType::Cube)
                                throw std::runtime_error("unsupported texture resource type");
                            result.textures.push_back({resource, sampler, unsigned(result.textures.size() + 1), type});
                            binding = result.textures.end() - 1;
                        }
                        binding->implicitLod |= !explicitLod;
                        binding->explicitLod |= texOp == 0x11;
                        if (compare && binding->type != TextureType::TwoD && std::getenv("WEMU_ARRAYDIAG") == nullptr) // TEMP-ARRAYDIAG
                            throw std::runtime_error("array depth-compare sampling not yet supported");
                        const auto reg = std::format("r[{}]", (t0 >> 16) & 127);
                        for (unsigned c = 0; c < 4; ++c) {
                            const auto selector = (t2 >> (20 + c * 3)) & 7;
                            if (selector < 4)
                                wholeQuadWrites.read({(t0 >> 16) & 127, selector, false, false, false}, depth);
                        }
                        auto coords = std::format("{},{}", select(reg, (t2 >> 20) & 7), select(reg, (t2 >> 23) & 7));
                        const bool array = binding->type == TextureType::TwoDArray;
                        const bool volume = binding->type == TextureType::ThreeD;
                        const bool cube = binding->type == TextureType::Cube;
                        const auto lod = texOp == 0x11 ? select(reg, (t2 >> 29) & 7) : "0.0";
                        if (cube && (compare || texOp == 0x0F))
                            throw std::runtime_error("cube gather/depth-compare sampling not yet supported");
                        if (volume && (compare || texOp == 0x0F || texOp == 0x11))
                            throw std::runtime_error("3D gather/depth-compare/mip sampling not yet supported");
                        if (volume)
                            coords += std::format(",{}", select(reg, (t2 >> 26) & 7));
                        if (array)
                            coords += std::format(",arrayLayerReference({},textureSize(tex{},0).z)", select(reg, (t2 >> 26) & 7), binding->binding);
                        if (cube) {
                            cubeIndices.insert(resource);
                            body += std::format("{{ vec3 coord=vec3({},{}); vec4 sampled; "
                                                "if(any(isnan(coord))||any(isinf(coord))||coord.z<0.0||coord.z>=6.0||isnan({})||isinf({})||isnan("
                                                "cubeIndex{})||isinf(cubeIndex{})) "
                                                "sampled=u.borders[{}]; else "
                                                "sampled=textureLod(tex{},vec4(cubeDirectionReference(coord),arrayLayerReference(cubeIndex{},"
                                                "textureSize(tex{},0).z)),{});\n",
                                                coords, select(reg, (t2 >> 26) & 7), lod, lod, resource, resource, binding->binding - 1,
                                                binding->binding, resource, binding->binding, lod);
                        } else if (texOp == 0x0F) {
                            if (array)
                                throw std::runtime_error("array gather not yet supported");
                            // Validate all source selectors consistently with the interpreter.
                            for (unsigned c = 0; c < 4; ++c)
                                select(reg, (t2 >> (20 + c * 3)) & 7);
                            body += std::format("{{ vec2 coord=vec2({}); vec4 sampled; "
                                                "if(any(isnan(coord))||any(isinf(coord))) sampled=vec4(u.borders[{}].r); "
                                                "else sampled=textureGather(tex{},coord,0);\n",
                                                coords, binding->binding - 1, binding->binding);
                        } else if (array && texOp == 0x11) {
                            body += std::format("{{ vec4 sampled=textureLod(tex{},vec3({}),{});\n", binding->binding, coords, lod);
                        } else if (array) {
                            body += std::format("{{ vec3 originalCoord=vec3({},{},{}); vec4 sampled; "
                                                "if(any(isnan(originalCoord))||any(isinf(originalCoord))) sampled=u.borders[{}]; "
                                                "else sampled=sampleArrayReference(tex{},vec3({}),u.sampling[{}]);\n",
                                                select(reg, (t2 >> 20) & 7), select(reg, (t2 >> 23) & 7), select(reg, (t2 >> 26) & 7),
                                                binding->binding - 1, binding->binding, coords, binding->binding - 1);
                        } else if (compare) {
                            // Shadow sample: (reference FUNC texel) with the sampler's
                            // DEPTH_COMPARE_FUNCTION, stored in u.sampling[].w by the backend.
                            body += std::format("{{ float reference={}; float texel=textureLod(tex{},vec2({}),0.0).r; "
                                                "int compareFunction=int(u.sampling[{}].w); bool pass="
                                                "compareFunction==1?reference<texel:compareFunction==2?reference==texel:"
                                                "compareFunction==3?reference<=texel:compareFunction==4?reference>texel:"
                                                "compareFunction==5?reference!=texel:compareFunction==6?reference>=texel:compareFunction==7; "
                                                "vec4 sampled=vec4(pass?1.0:0.0);\n",
                                                select(reg, (t2 >> 29) & 7), binding->binding, coords, binding->binding - 1);
                        } else {
                            body += std::format("{{ vec4 sampled=textureLod(tex{},vec{}({}),{});\n", binding->binding, volume ? 3 : 2, coords, lod);
                        }
                        for (unsigned c = 0; c < 4; ++c) {
                            const auto selector = (t1 >> (9 + c * 3)) & 7;
                            if (selector != 7) {
                                uniformValues.registers[t1 & 127][c] = false;
                                body += std::format("r[{}][{}]={};\n", t1 & 127, c, select("sampled", selector));
                                wholeQuadWrites.textureWrite(t1 & 127, c, depth,
                                                             !rootMaskChanged &&
                                                                     std::none_of(jumpTargets.begin(), jumpTargets.end(),
                                                                                  [&](const auto &target) { return target.first > cf / 2; }));
                            }
                        }
                        body += "}\n";
                    }
                    body += "}\n";
                } else if (op == CF_EXP || op == CF_EXP_DONE) {
                    if (vertexStage) {
                        const auto target = w0 & 0x1FFF, type = (w0 >> 13) & 3;
                        constexpr auto word0Mask = 0x1FFFu | (3u << 13) | (127u << 15);
                        constexpr auto word1Mask = 0xFFFu | (1u << 21) | (1u << 22) | (127u << 23) | (1u << 31);
                        if ((w0 & ~word0Mask) || (w1 & ~word1Mask) || !((type == 1 && target == 60) || (type == 2 && target < 32)))
                            throw std::runtime_error("unsupported vertex export addressing or target");
                        const auto reg = std::format("r[{}]", (w0 >> 15) & 127);
                        const auto dst = type == 1 ? std::string("position") : std::format("parameters[{}]", target);
                        bool writesComponent = false;
                        body += "if(laneActive) {\n";
                        for (unsigned channel = 0; channel < 4; ++channel) {
                            const auto selector = (w1 >> (channel * 3)) & 7;
                            if (selector != 7) {
                                body += std::format("{}[{}]={};\n", dst, channel, select(reg, selector));
                                writesComponent = true;
                            }
                        }
                        if (writesComponent) {
                            if (type == 1) {
                                body += "wroteOutput=true;\n";
                                exported = true;
                            } else
                                result.parameterMask |= 1u << target;
                        }
                        body += "}\n";
                    } else {
                        const auto target = w0 & 0x1FFF;
                        const bool depthExport = target == 61;
                        const auto burst = ((w1 >> 17) & 15) + 1;
                        const auto export1Mask =
                                0xFFFu | (1u << 21) | (1u << 22) | (127u << 23) | (1u << 31) | (onlyColorZero && !depthExport ? 15u << 17 : 0u);
                        const bool selected = !depthExport && target <= colorExport && colorExport < target + burst;
                        const bool ignored = onlyColorZero && target < 8 && !selected;
                        ignoredColorExports |= ignored || (onlyColorZero && !depthExport && (burst > 1 || colorExport != 0));
                        if ((target != 0 && !depthExport && !ignored && !onlyColorZero) || (!depthExport && onlyColorZero && target + burst > 8) ||
                            (!ignored && (depthExport ? result.writesDepth : result.exportsColor)) || (w0 & ~(0x1FFFu | (127u << 15))) ||
                            (w1 & ~export1Mask) || (depthExport && ((w1 & 7) == 7 || ((w1 >> 3) & 0x1FF) != 0x1FF)))
                            throw std::runtime_error(std::format(
                                    "unsupported export target, addressing or burst: cf={} type={} base={} repeated={} words={:08X},{:08X}", cf / 2,
                                    (w0 >> 13) & 3, w0 & 0x1FFF, exported, w0, w1));
                        if (!ignored) {
                            const auto gpr = ((w0 >> 15) & 127) + (depthExport ? 0 : colorExport - target);
                            if (gpr >= 128)
                                throw std::runtime_error("color export register out of range");
                            const auto reg = std::format("r[{}]", gpr);
                            for (unsigned c = 0; c < 4; ++c) {
                                const auto selector = (w1 >> (c * 3)) & 7;
                                if (selector < 4)
                                    wholeQuadWrites.read({gpr, selector, false, false, false}, depth);
                            }
                            if (depthExport) {
                                body += std::format("if(laneActive) {{ gl_FragDepth=clampReference({}); wroteOutput=true; }}\n", select(reg, w1 & 7));
                                result.writesDepth = true;
                            } else {
                                body += std::format("if(laneActive) {{ color=vec4({},{},{},{}); wroteOutput=true; }}\n", select(reg, w1 & 7),
                                                    select(reg, (w1 >> 3) & 7), select(reg, (w1 >> 6) & 7), select(reg, (w1 >> 9) & 7));
                                result.exportsColor = true;
                                // With no jumps and no root-mask updates, balanced
                                // PUSH/POP regions restore every lane before this
                                // terminal export, even if their arithmetic was masked.
                                terminalUnconditionalColor = cfEop(w1) && !depth && !rootMaskChanged && jumpTargets.empty();
                            }
                            exported = true;
                        }
                    }
                } else if (op == 0x0A || op == 0x0B || op == 0x0D || op == 0x0E) {
                    // VALID_PIXEL_MODE is equivalent here: kill/demote are not supported.
                    constexpr auto mask = 7u | (1u << 22) | (127u << 23) | (1u << 31);
                    if (w1 & ~mask)
                        throw std::runtime_error("unsupported conditional control-flow flags");
                    const auto count = w1 & 7u;
                    if (op == 0x0A || op == 0x0D) {
                        if (w0 <= cf / 2 || count > depth)
                            throw std::runtime_error("unsupported backward jump or stack pop");
                        jumpTargets.emplace_back(w0, depth - count);
                        // A conditional jump restores the mask only on its taken
                        // edge. Poisoning the fall-through here wrongly rejects
                        // values still confined to that branch's original mask.
                        auto takenWrites = wholeQuadWrites;
                        takenWrites.restore(depth - count);
                        incomingWholeQuadWrites[w0].merge(takenWrites);
                        if (const auto incoming = incomingUniformValues.find(w0); incoming != incomingUniformValues.end())
                            incoming->second.merge(uniformValues);
                        else
                            incomingUniformValues.emplace(w0, uniformValues);
                        if (op == 0x0D && !depth)
                            throw std::runtime_error("ELSE without stack entry");
                        if (op == 0x0D) {
                            wholeQuadWrites.restore(depth - 1);
                            maskUniform = maskUniform && uniformMaskStack[depth - 1];
                            allActive = false;
                        }
                        body += op == 0x0A ? "if(!laneActive) {\n" : "if(laneActive) {\n";
                        if (count)
                            body += std::format("laneActive=activeStack[{}];\n", depth - count);
                        body += std::format("pc={}u;\n}}", w0);
                        if (op == 0x0D)
                            body += std::format(" else {{ laneActive=activeStack[{}]&&!laneActive; }}", depth - 1);
                        body += "\n";
                    } else {
                        // R700 POP never jumps; its address field is ignored.
                        // Retain strict PUSH handling until its reconvergence fields are modeled.
                        if (op == 0x0B && (w0 || count))
                            throw std::runtime_error(std::format("unsupported stack control fields: cf={} op={} count={} words={:08X},{:08X}", cf / 2,
                                                                 op, count, w0, w1));
                        if (op == 0x0B)
                            push();
                        else
                            pop(count);
                    }
                } else {
                    constexpr auto controlMask = (1u << 21) | (127u << 23) | (1u << 31);
                    if ((op != CF_NOP && op != CF_RETURN && !(vertexStage && op == CF_CALL_FS)) || w0 || (w1 & ~controlMask))
                        throw std::runtime_error("unsupported control flow");
                }
                body += "}\n";
                if (cfEop(w1) || op == CF_RETURN) {
                    terminated = true;
                    break;
                }
            }
            if (!exported || !terminated)
                throw std::runtime_error("missing color/depth export or shader termination");
            if (result.exportsColor && result.writesDepth && maskedControl)
                throw std::runtime_error("conditional mixed color/depth exports require per-output lane masks");
            // Emission uses explicit base LOD. With one source mip, identical
            // min/mag filters and no anisotropy, implicit sampling produces the
            // same texels without helper derivatives. Validate that per draw;
            // shader-wide rejection would also discard these safe resources.
            result.requiresDerivativeFreeSampling = varyingWholeQuadUsed && implicitSamples;
            if (depth)
                throw std::runtime_error("unbalanced control-flow stack");
            for (const auto [target, expectedDepth]: jumpTargets)
                if (target >= entryDepth.size() || entryDepth[target] != expectedDepth)
                    throw std::runtime_error("jump target has incompatible control-flow stack");
            if (ignoredColorExports && (maskedControl || rootMaskChanged || !jumpTargets.empty()))
                throw std::runtime_error("inactive color export specialization requires unconditional exports");
            result.requiresOnlyColorZero = ignoredColorExports;
            result.alwaysWritesColor = !vertexStage && result.exportsColor && !result.writesDepth &&
                                       (terminalUnconditionalColor || (!maskedControl && jumpTargets.empty()));
            // Reserve the remaining existing UBO slots for dynamic buffer reads.
            // Replay proves each bound block fits before native dispatch; larger
            // blocks retain reference execution rather than truncating a palette.
            if (!uniformBuffers.empty()) {
                const auto capacity = unsigned((512 - result.uniforms.size()) / uniformBuffers.size());
                if (!capacity)
                    throw std::runtime_error("dynamic uniform buffer budget exceeded");
                for (const auto bank: uniformBuffers) {
                    result.uniformBuffers.push_back({bank, unsigned(result.uniforms.size()), capacity});
                    for (unsigned index = 0; index < capacity; ++index)
                        result.uniforms.push_back({bank, index});
                }
            }
            result.source = "#version 450\nlayout(location=0) in vec4 inputs[16];\nlayout(location=0) out vec4 color;\n"
                            "layout(set=0,binding=0,std140) uniform Constants { vec4 c[256]; vec4 borders[16]; vec4 sampling[16]; } u;\n"
                            "vec4 cubeReference(float x,float y,float z) { vec3 a=abs(vec3(x,y,z)); "
                            "if(a.z>=a.x && a.z>=a.y) return vec4(-y,z<0.0?-x:x,z*2.0,z<0.0?5.0:4.0); "
                            "if(a.y>=a.x) return vec4(y<0.0?-z:z,x,y*2.0,y<0.0?3.0:2.0); "
                            "return vec4(-y,x<0.0?z:-z,x*2.0,x<0.0?1.0:0.0); }\n"
                            "float arrayLayerReference(float x,int layers) { float v=clamp(x,0.0,float(layers-1)); "
                            "return floor(v)+step(0.5,fract(v)); }\n"
                            "float clampReference(float x) { return isnan(x) ? 0.0 : clamp(x,0.0,1.0); }\n"
                            "float minReference(float a,float b) { return isnan(a) ? b : isnan(b) ? a : min(a,b); }\n"
                            "float maxReference(float a,float b) { return isnan(a) ? b : isnan(b) ? a : max(a,b); }\n"
                            "float roundEvenReference(float x) { if(isnan(x)||isinf(x)) return x; float v=roundEven(x); "
                            "return v==0.0 ? uintBitsToFloat(floatBitsToUint(x)&2147483648u) : v; }\n"
                            "float truncReference(float x) { if(isnan(x)||isinf(x)) return x; float v=trunc(x); "
                            "return v==0.0 ? uintBitsToFloat(floatBitsToUint(x)&2147483648u) : v; }\n"
                            "float sqrtReference(float x) { if(isnan(x)) return x; if(x<0.0) return uintBitsToFloat(2143289344u); "
                            "if(x==0.0||isinf(x)) return x; return sqrt(x); }\n"
                            "float logReference(float x) { if(isnan(x)) return x; if(x==0.0) return uintBitsToFloat(4286578688u); "
                            "if(x<0.0) return uintBitsToFloat(2143289344u); if(isinf(x)) return x; return log2(x); }\n"
                            "float logClamped(float x) { float v=logReference(x); return isinf(v)&&v<0.0 ? -uintBitsToFloat(2139095039u) : v; }\n";
            if (vertexStage) {
                const auto end = result.source.find("float arrayLayerReference");
                result.source.replace(
                        0, end,
                        "#version 450\nlayout(location=1) in vec4 attributes[16];\nlayout(location=0) out vec4 inputs[16];\n"
                        "layout(location=16) noperspective out float windowDepth;\n"
                        "invariant gl_Position; precise gl_Position; precise windowDepth;\n"
                        "layout(set=0,binding=17,std140) uniform Constants { vec4 c[256]; vec4 borders[16]; vec4 sampling[16]; vec4 k[512]; } u;\n"
                        "layout(push_constant) uniform Viewport { vec2 scale; float depthEnabled; float unused; vec4 window; vec2 depth; "
                        "layout(offset=48) ivec4 inputParams[4]; } viewport;\n");
            } else if (!result.uniforms.empty()) {
                const auto end = result.source.find(" } u;");
                result.source.insert(end, " vec4 k[512];");
            }
            result.source += R"(
// R700 truncates finite values and retains the low integer bits. Decode the
// float explicitly so host casts and SPIR-V conversion overflow cannot disagree.
uint floatToIntBits(float value) {
    uint bits=floatBitsToUint(value), sign=bits>>31;
    int exponent=int((bits>>23)&255u)-127;
    uint fraction=bits&8388607u;
    if(exponent==128) return fraction!=0u ? 0u : sign!=0u ? 2147483648u : 2147483647u;
    if(exponent<0||exponent>=55) return 0u;
    uint magnitude=fraction|8388608u;
    if(exponent<23) magnitude>>=uint(23-exponent);
    else magnitude<<=uint(exponent-23);
    return sign!=0u ? 0u-magnitude : magnitude;
}
uint floatToUintBits(float value) {
    if(isnan(value)||value<=0.0) return 0u;
    if(value>=4294967296.0) return 4294967295u;
    return uint(value);
}
int addressReference(float value) {
    return isnan(value)||isinf(value)||value < -256.0||value > 255.0 ? -256 : int(value);
}
int addressIntReference(int value) {
    return value < -256||value > 255 ? -256 : value;
}
float relativeConstant(int index,int channel) {
    return index < 0||index >= 256 ? uintBitsToFloat(2147483647u) : u.c[index][channel];
}
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
            result.source += R"(
vec3 cubeDirectionReference(vec3 coord) {
    vec2 p=(coord.xy-vec2(1.5))*2.0;
    int face=int(coord.z);
    if(face==0) return vec3(1.0,-p.y,-p.x);
    if(face==1) return vec3(-1.0,-p.y,p.x);
    if(face==2) return vec3(p.x,1.0,p.y);
    if(face==3) return vec3(p.x,-1.0,-p.y);
    if(face==4) return vec3(p.x,-p.y,1.0);
    return vec3(-p.x,-p.y,-1.0);
}
)";
            for (const auto &buffer: result.uniformBuffers)
                result.source += std::format("vec4 fetchBuffer{}(uint index,uint offset) {{ "
                                             "return index<{}u && offset<{}u-index ? u.k[{}u+index+offset] : vec4(0.0); }}\n",
                                             buffer.bank, buffer.count, buffer.count, buffer.first);
            for (const auto &b: result.textures)
                result.source += std::format("layout(set=0,binding={}) uniform sampler{} tex{};\n", b.binding,
                                             b.type == TextureType::TwoDArray ? "2DArray"
                                             : b.type == TextureType::ThreeD  ? "3D"
                                             : b.type == TextureType::Cube    ? "CubeArray"
                                                                              : "2D",
                                             b.binding);
            for (const auto resource: cubeIndices)
                body.insert(body.find('\n') + 1, std::format("float cubeIndex{}=0.0;\n", resource));
            // Static shaders retain scalar registers. AR-relative GPR sources
            // need the complete register file, including zero-initialized gaps.
            bool dynamicGpr = false;
            for (const auto &[key, groups]: program.clauses)
                for (const auto &group: groups)
                    for (const auto &instruction: group)
                        for (unsigned operand = 0; operand < (instruction.op3 ? 3u : 2u); ++operand)
                            dynamicGpr |= instruction.src[operand].rel && instruction.src[operand].sel < 128;
            if (dynamicGpr)
                body.insert(body.find('\n') + 1, "vec4 r[128]; for(int i=0;i<128;++i) r[i]=vec4(0.0);\n");
            else
                body = scalarizeRegisters(std::move(body));
            if (result.writesDepth)
                body.insert(body.find('\n') + 1, "gl_FragDepth=gl_FragCoord.z;\n");
            result.source += body;
            if (vertexStage)
                result.source += R"(
    gl_PointSize=1.0;
    float divisor=abs(position.w)>1e-6 ? position.w : 1.0;
    vec2 ndc=position.xy/divisor;
    vec2 screen=viewport.window.xy+(ndc*vec2(1.0,-1.0)+vec2(1.0))*viewport.window.zw;
    windowDepth=(viewport.depth.x+viewport.depth.y)*0.5+(position.z/divisor)*(viewport.depth.y-viewport.depth.x)*0.5;
    gl_Position=wroteOutput ? vec4((screen*viewport.scale-vec2(1.0))*divisor,viewport.depthEnabled!=0.0 ? windowDepth*divisor : 0.0,divisor) : vec4(2.0,2.0,2.0,1.0);
    for(int i=0;i<16;++i) { int p=viewport.inputParams[i/4][i%4]; inputs[i]=p>=0 ? parameters[p] : vec4(0.0); }
}
)";
            else
                result.source += "if(!wroteOutput) discard;\n}\n";
        } catch (const std::runtime_error &error) {
            result = {};
            result.error = error.what();
        }
        return result;
    }
    FragmentShader lowerFragmentShader(const Program &program, std::span<const TextureType> resourceTypes, bool onlyColorZero, unsigned colorExport)
    {
        return lowerShader(program, resourceTypes, false, onlyColorZero, colorExport);
    }
    bool FragmentShaderCache::hasUnconditionalColorOnlyEffects(const std::vector<std::uint32_t> &words)
    {
        if (const auto found = m_colorOnlyEffects.find(words); found != m_colorOnlyEffects.end())
            return found->second;
        const bool safe = [&] {
            auto program = *decodeProgram(words);
            if (!program.valid)
                return false;
            // Texture results cannot matter to a proven unconditional color-only
            // export when every color output is disabled. Remove only known
            // read-only TEX clauses for effect analysis, never for execution.
            // The ordinary lowering still validates all ALU/control/export effects,
            // rejecting kills, memory operations, depth exports and masked coverage.
            for (std::size_t cf = 0; cf + 1 < words.size(); cf += 2) {
                const auto w0 = words[cf], w1 = words[cf + 1];
                if (isAluClause(w1))
                    continue;
                if (cfInst(w1) == CF_TEX) {
                    constexpr auto cfMask = (7u << 10) | (1u << 19) | (1u << 21) | (1u << 22) | (127u << 23) | (1u << 31);
                    if (w1 & ~cfMask)
                        return false;
                    const auto count = (((w1 >> 10) & 7) | ((w1 >> 16) & 8)) + 1;
                    for (unsigned i = 0; i < count; ++i) {
                        const auto offset = std::uint64_t(w0) * 2 + std::uint64_t(i) * 4;
                        if (offset + 3 >= words.size())
                            return false;
                        const auto op = words[offset] & 31;
                        if (op != 0x0E && op != 0x0F && op != 0x10 && op != 0x11 && op != 0x13 && op != 0x18 && op != 0x1B)
                            return false;
                        if ((words[offset] & ~(31u | (255u << 8) | (127u << 16))) || (words[offset + 1] & ~(127u | (0xFFFu << 9) | (15u << 28))) ||
                            (words[offset + 2] & 0x7FFF))
                            return false;
                    }
                    program.words[cf] = 0;
                    program.words[cf + 1] = w1 & ((1u << 21) | (1u << 31)); // NOP with the original termination/barrier.
                }
                if (cfEop(w1) || cfInst(w1) == CF_RETURN)
                    break;
            }
            const auto effects = lowerFragmentShader(program, {}, true);
            return effects && effects.alwaysWritesColor;
        }();
        if (m_colorOnlyEffects.size() >= maxEntries)
            m_colorOnlyEffects.clear();
        if (words.size() <= 16384)
            m_colorOnlyEffects.emplace(words, safe);
        return safe;
    }
    VertexShader lowerVertexShader(const Program &program, std::span<const TextureType> resourceTypes)
    {
        VertexShader shader;
        static_cast<FragmentShader &>(shader) = lowerShader(program, resourceTypes, true);
        return shader;
    }
    std::shared_ptr<const VertexShader> VertexShaderCache::get(const std::vector<std::uint32_t> &words)
    {
        if (const auto it = m_entries.find(words); it != m_entries.end())
            return it->second;
        auto shader = std::make_shared<const VertexShader>(lowerVertexShader(*decodeProgram(words)));
        if (m_entries.size() >= 4096)
            m_entries.clear();
        if (words.size() <= 16384 && shader->source.size() <= std::size_t{256} * 1024)
            m_entries.emplace(words, shader);
        return shader;
    }
} // namespace Core::Gfx::Latte
