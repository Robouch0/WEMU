#include <fstream>
#include <iostream>
#include <charconv>
#include <format>
#include <string_view>

#include "gfx/LatteShaderLowering.hpp"

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "Usage: wemu_shader_lower <decoded-dwords.hex> <output.frag|--inspect>\n";
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "Cannot open shader input\n";
        return 2;
    }
    std::vector<std::uint32_t> words;
    std::string token;
    while (input >> token) {
        std::uint32_t word;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), word, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) {
            std::cerr << "Invalid hexadecimal shader input\n";
            return 2;
        }
        words.push_back(word);
    }
    if (!input.eof()) {
        std::cerr << "Invalid hexadecimal shader input\n";
        return 2;
    }
    const auto program = Core::Gfx::Latte::decodeProgram(words);
    if (std::string_view(argv[2]) == "--inspect") {
        using namespace Core::Gfx::Latte;
        if (!program->valid) {
            std::cerr << "Invalid shader structure\n";
            return 1;
        }
        for (std::size_t cf = 0; cf + 1 < words.size(); cf += 2) {
            const auto w0 = words[cf], w1 = words[cf + 1];
            if (isAluClause(w1)) {
                const auto address = w0 & 0x3FFFFF, count = ((w1 >> 18) & 127) + 1;
                std::cout << std::format("CF{} ALU kind={} addr={} count={} whole_quad={} banks={},{} modes={},{} windows={},{}\n",
                    cf / 2, cfAluInst(w1), address, count, (w1 >> 30) & 1, (w0 >> 22) & 15, (w0 >> 26) & 15,
                    w0 >> 30, w1 & 3, (w1 >> 2) & 255, (w1 >> 10) & 255);
                const auto clause = program->clauses.find((std::uint64_t(address) << 32) | count);
                if (clause == program->clauses.end()) {
                    std::cerr << "Missing decoded clause\n";
                    return 1;
                }
                unsigned groupIndex = 0;
                for (const auto &group : clause->second) {
                    for (const auto &in : group) {
                        std::cout << std::format("  group={} slot={} {} op={:02X} dst=R{}.{} write={} pred_sel={} update_exec={} update_pred={} raw={:08X},{:08X}\n",
                            groupIndex, in.scalarSlot ? "T" : "V", in.op3 ? "OP3" : "OP2", in.op, in.dstGpr, in.dstChan,
                            in.writeMask, in.predSel, !in.op3 && bool(in.raw1 & 4), !in.op3 && bool(in.raw1 & 8), in.raw0, in.raw1);
                    }
                    ++groupIndex;
                }
            } else {
                const auto op = cfInst(w1);
                std::cout << std::format("CF{} op={:02X} addr={} pop={} cond={} whole_quad={} eop={} raw={:08X},{:08X}\n",
                    cf / 2, op, w0, w1 & 7, (w1 >> 8) & 3, (w1 >> 30) & 1, cfEop(w1), w0, w1);
                if (cfEop(w1) || op == CF_RETURN) break;
            }
        }
        return 0;
    }
    const auto shader = Core::Gfx::Latte::lowerFragmentShader(*program);
    if (!shader) {
        std::cerr << shader.error << '\n';
        return 1;
    }
    std::ofstream output(argv[2]);
    output << shader.source;
    output.close();
    if (!output) {
        std::cerr << "Cannot write shader output\n";
        return 2;
    }
    std::cerr << "Texture bindings: " << shader.textures.size() << "; implicit samples restricted to base level: " << shader.requiresBaseLevelOnly
              << '\n';
    return 0;
}
