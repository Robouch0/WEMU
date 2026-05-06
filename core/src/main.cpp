/*
** EPITECH PROJECT, 2025
** core
** File description:
** main
*/
/*
** EPITECH PROJECT, 2025
** WemuEmulator
** File description:
** main
*/

#include <bitset>
#include <fstream>
#include <iostream>
#include <map>

#include "binary/Binary.hpp"
#include "binary/Loader.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/memory/Memory.hpp"
#include "hle/Coreinit.hpp"
#include "hle/Libc.hpp"
#include "hle/Whb.hpp"
#include "lib/coreinit/Coreinint.hpp"
#include "utils/BeDecoder.hpp"


void print_elf32_ehdr(const Elf32_Ehdr &ehdr)
{
    dprintf(1, "ELF header:\n");
    dprintf(1, "e_ident\t\t%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n", ehdr.e_ident[0], ehdr.e_ident[1], ehdr.e_ident[2],
            ehdr.e_ident[3], ehdr.e_ident[4], ehdr.e_ident[5], ehdr.e_ident[6], ehdr.e_ident[7], ehdr.e_ident[8], ehdr.e_ident[9], ehdr.e_ident[10],
            ehdr.e_ident[11], ehdr.e_ident[12], ehdr.e_ident[13], ehdr.e_ident[14], ehdr.e_ident[15]);
    dprintf(1, "e_type\t\t0x%04x [%s]\n", ehdr.e_type, (ehdr.e_type == 0xFE01) ? "RPL" : "UNKNOWN");
    dprintf(1, "e_machine\t0x%04x [%s]\n", ehdr.e_machine, (ehdr.e_machine == 0x0014) ? "PowerPC" : "UNKNOWN");
    dprintf(1, "e_version\t0x%08x\n", ehdr.e_version);
    dprintf(1, "e_entry\t\t0x%08x\n", ehdr.e_entry);
    dprintf(1, "e_phoff\t\t0x%08x\n", ehdr.e_phoff);
    dprintf(1, "e_shoff\t\t0x%08x\n", ehdr.e_shoff);
    dprintf(1, "e_flags\t\t0x%08x\n", ehdr.e_flags);
    dprintf(1, "e_ehsize\t0x%04x\n", ehdr.e_ehsize);
    dprintf(1, "e_phentsize\t0x%04x\n", ehdr.e_phentsize);
    dprintf(1, "e_phnum\t\t0x%04x\n", ehdr.e_phnum);
    dprintf(1, "e_shentsize\t0x%04x\n", ehdr.e_shentsize);
    dprintf(1, "e_shnum\t\t0x%04x\n", ehdr.e_shnum);
    dprintf(1, "e_shstrndx\t0x%04x\n", ehdr.e_shstrndx);
    dprintf(1, "\n");
}

void print_symbols(Core::Binary &binary)
{
    std::size_t i = 0;
    std::cout << "Total of " << binary.symbols.size() << " symbols:" << std::endl;
    for (const auto &symbol: binary.symbols) {
        std::cout << "index " << i << " ";
        if (symbol.raw.header.st_shndx < binary.sections.size()) {
            auto &sec = binary.sections[symbol.raw.header.st_shndx];
            std::cout << "[" << sec.name << "] " << symbol.name << " -> " << std::hex << symbol.meta.virtAddress << std::dec << std::endl;
        } else {
            std::cout << symbol.name << " -> " << std::hex << symbol.raw.header.st_value << std::dec << std::endl;
        }
        i++;
    }
    std::cout << std::endl;
}

void print_section(const Core::Section &section)
{
    auto instructionDecoder = Utils::BeDecoder(section.raw.data);

    std::cout << "Content of section " << section.name << std::endl;
    for (Elf32_Off offset = 0; offset < section.raw.header.sh_size; offset += 4) {
        const EncodedInstruction encodedInstruction(instructionDecoder.extractSwap<uint32_t>());

        std::cout << " " << std::hex << (offset + section.raw.header.sh_addr) << std::dec << "\t"
                  << std::bitset<sizeof(uint32_t) * 8>(encodedInstruction.raw) << "\t" << std::endl;
    }
}

int main(const int ac, char const *const *av)
{
    if (ac != 2) {
        std::cerr << "Invalid arguments." << std::endl;
        return ERROR_VALUE;
    }
    const Core::Loader loader(av[1]);

    Core::Binary binary = loader.getBinary();
    std::cout << "main MEM -> " << std::hex << binary.m_memory.read<uint32_t>(268437924) << std::dec << std::endl;

    print_elf32_ehdr(binary.header);
    print_symbols(binary);

    RegisterCoreinitFunctions();
    RegisterLibcFunctions();
    RegisterWhbFunctions();

    Core::Interpreter interpreter(binary);

    std::cout << "CPU 268437924 MEM -> " << std::hex << interpreter.m_memory.read<uint32_t>(268437924) << std::dec << std::endl;

    // 4. Connect renderer to interpreter
    Renderer renderer;
    interpreter.m_renderer = &renderer;

    // 5. Register address hooks for compiled-in stdlib (bypasses broken WUT sbrk heap)
    struct {
            const char *name;
            Core::Interpreter::HookFn fn;
    } stdlib_hooks[] = {
            {"memalign", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], cpu.m_gpr[3] ? cpu.m_gpr[3] : 8); }},
            {"malloc", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[3], 8); }},
            {"calloc",
             [](Core::Interpreter &cpu) {
                 uint32_t n = cpu.m_gpr[3] * cpu.m_gpr[4];
                 uint32_t a = cpu.m_memory.heapAllocate(n, 8);
                 if (a) {
                     uint8_t *p = cpu.m_memory.hostPtr(a);
                     if (p)
                         std::memset(p, 0, n);
                 }
                 cpu.m_gpr[3] = a;
             }},
            {"realloc", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 8); }},
            {"free", [](Core::Interpreter &) {}},
            {"__wut_sbrk_r", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 4); }},
            {"_sbrk_r", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 4); }},
    };

    for (auto &h: stdlib_hooks) {
        for (const auto &sym: binary.symbols) {
            if (sym.name == h.name && sym.raw.header.st_value >= 0x02000000u && sym.raw.header.st_value < 0x10000000u) {
                interpreter.m_hooks[sym.raw.header.st_value] = h.fn;
                fprintf(stderr, "[hook] %s @ 0x%08X\n", h.name, sym.raw.header.st_value);
            }
        }
    }

    // 6. Init CPU state
    interpreter.m_gpr[1] = 0xC0FFFFF0u; // r1 = stack top
    interpreter.m_gpr[2] = 0u; // r2 = TOC (unused)
    interpreter.m_gpr[3] = 0u; // argc
    interpreter.m_gpr[4] = 0u; // argv

    // PowerPC back-chain word (ABI requirement)
    interpreter.m_memory.write<std::uint32_t>(0xC0FFFFF0u, 0);


    std::cout << "Loaded module!" << std::endl;
    std::cout << "Code: " << std::hex << "0x" << loader.codeAddressRange.first << ":" << "0x" << loader.codeAddressRange.second << std::endl;
    std::cout << "Data: " << std::hex << "0x" << loader.dataAddressRange.first << ":" << "0x" << loader.dataAddressRange.second << std::endl;

    interpreter.run();

    Core::Memory mem;

    return SUCCESS_VALUE;
}
