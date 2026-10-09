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
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "binary/Binary.hpp"
#include "binary/Loader.hpp"
#include "hle/CoreinitExtra.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/memory/Memory.hpp"
#include "hle/Coreinit.hpp"
#include "hle/H264.hpp"
#include "hle/Libc.hpp"
#include "hle/StdlibHooks.hpp"
#include "utils/Diagnostics.hpp"
#include "utils/Logger.hpp"
#include "hle/Whb.hpp"
#include "hle/Gx2.hpp"
#include "hle/Fs.hpp"
#include "lib/coreinit/Coreinint.hpp"
#include <filesystem>
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
    const bool guiSession = ac == 3 && std::string(av[1]) == "--gui-session";
    if (ac != 2 && !guiSession) {
        std::cerr << "Invalid arguments." << std::endl;
        return ERROR_VALUE;
    }
    try {
        const char *executable = av[guiSession ? 2 : 1];
        const Core::Loader loader(executable);

        Core::Binary binary = loader.getBinary();
        std::cout << "main MEM -> " << std::hex << binary.m_memory.read<uint32_t>(268437924) << std::dec << std::endl;

        // These dumps are enormous for real titles (MK8 has a huge symtab) and
        // block execution, so only emit them when built at Debug verbosity.
        if constexpr (Utils::Log::kLevel <= Utils::Log::Level::Debug) {
            print_elf32_ehdr(binary.header);
            print_symbols(binary);
        }

        RegisterCoreinitFunctions();
        RegisterH264Functions();
        RegisterLibcFunctions();
        RegisterWhbFunctions();
        RegisterGx2Functions();
        RegisterFsFunctions();

        // "/vol/content" maps to "<gamedir>/content"; the RPX lives at "<gamedir>/code/<name>.rpx".
        const std::filesystem::path rpxPath(executable);
        const std::filesystem::path contentRoot = rpxPath.parent_path().parent_path() / "content";
        SetFsContentRoot(contentRoot.string());
        std::cout << "FS content root: " << contentRoot.string() << std::endl;
        // WEMU_CONTENT_LAYER="/path/to/base/content:/more": fallback roots checked after the
        // primary one — lets an update title's partial content overlay the base game's.
        if (const char *layers = std::getenv("WEMU_CONTENT_LAYER")) {
            std::string s(layers);
            for (std::size_t pos = 0; pos != std::string::npos;) {
                const std::size_t next = s.find(':', pos);
                const std::string layer = s.substr(pos, next == std::string::npos ? next : next - pos);
                if (!layer.empty()) {
                    AddFsContentLayer(layer);
                    std::cout << "FS content layer: " << layer << std::endl;
                }
                pos = next == std::string::npos ? next : next + 1;
            }
        }

        Core::Interpreter interpreter(binary);

        std::cout << "CPU 268437924 MEM -> " << std::hex << interpreter.m_memory.read<uint32_t>(268437924) << std::dec << std::endl;

        // 4. Connect renderer to interpreter
        Renderer renderer;
        interpreter.m_renderer = &renderer;

        Core::installStdlibHooks(interpreter, binary);
        if (guiSession)
            std::cout << "[BOOT] GUI session: title-specific hooks disabled" << std::endl;
        if (!guiSession) {
        InstallMVPlayerHooks(interpreter); // per-method MVPlayer (menu movie) intercepts
        InstallTitleSeqPoolFix(interpreter); // seed the title-sequence frame pool (title -> menu gate)
        InstallSceneConfigWorkaround(interpreter); // skip the empty scene-config iteration (scene-init gate)
        InstallSceneUpdateForce(interpreter); // EXPERIMENT: WEMU_FORCE_SCENE=1 forces scene "not busy"
        InstallSubsysReadyForce(interpreter); // EXPERIMENT: WEMU_FORCE_SUBSYS=1 forces scene-obj subsystem ready
        }
        Core::Diag::installPcCountSignalDump(); // WEMU_PC_COUNT: report tallies even when a hung boot is killed

        // 6. Init CPU state (emulate the Wii U loader's hand-off to the module entry)
        interpreter.m_gpr[1] = 0xC0FFFFF0u; // r1 = stack top
        interpreter.m_gpr[2] = binary.sda2Base; // r2  = small-data area 2 base (from FILEINFO)
        interpreter.m_gpr[13] = binary.sdaBase; // r13 = small-data area base (from FILEINFO)
        interpreter.m_gpr[3] = 0u; // argc
        interpreter.m_gpr[4] = 0u; // argv

        // Seed LR with a trampoline so a return from the entry stops the emulator cleanly.
        interpreter.m_lr = Core::RETURN_SENTINEL - Core::Memory::MemoryMap::ApplicationCode;

        // PowerPC back-chain word (ABI requirement)
        interpreter.m_memory.write<std::uint32_t>(0xC0FFFFF0u, 0);

        std::cout << "Loaded module!" << std::endl;
        std::cout << "Code: " << std::hex << "0x" << loader.codeAddressRange.first << ":" << "0x" << loader.codeAddressRange.second << std::endl;
        std::cout << "Data: " << std::hex << "0x" << loader.dataAddressRange.first << ":" << "0x" << loader.dataAddressRange.second << std::endl;

        // WEMU_DUMP_STRINGS=addr1,addr2,... : print NUL-terminated guest strings at the given hex
        // addresses (design aid, e.g. reading .rodata shader-uniform names).
        if (const char *env = std::getenv("WEMU_DUMP_STRINGS")) {
            std::string spec = env;
            std::size_t pos = 0;
            while (pos < spec.size()) {
                const std::size_t comma = spec.find(',', pos);
                const std::string tok = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                const std::uint32_t addr = static_cast<std::uint32_t>(std::strtoul(tok.c_str(), nullptr, 16));
                std::string s;
                for (std::uint32_t i = 0; i < 128; i++) {
                    char c = 0;
                    try {
                        c = static_cast<char>(interpreter.m_memory.read<std::uint8_t>(addr + i));
                    } catch (...) {
                        break;
                    }
                    if (c == 0)
                        break;
                    s += c;
                }
                std::cout << std::format("[STR] 0x{:08X} = \"{}\"", addr, s) << std::endl;
                pos = comma == std::string::npos ? spec.size() : comma + 1;
            }
        }

        // WEMU_DUMP_MEM=ADDR:SIZE:/tmp/range.bin : dump guest memory after RPX load/relocation,
        // before execution. Useful for disassembling decompressed code ranges from real titles.
        if (const char *env = std::getenv("WEMU_DUMP_MEM")) {
            const std::string spec = env;
            const std::size_t sep1 = spec.find(':');
            const std::size_t sep2 = sep1 == std::string::npos ? std::string::npos : spec.find(':', sep1 + 1);
            if (sep1 != std::string::npos && sep2 != std::string::npos) {
                const std::uint32_t addr = static_cast<std::uint32_t>(std::strtoul(spec.substr(0, sep1).c_str(), nullptr, 16));
                const std::uint32_t size =
                    static_cast<std::uint32_t>(std::strtoul(spec.substr(sep1 + 1, sep2 - sep1 - 1).c_str(), nullptr, 0));
                const std::string path = spec.substr(sep2 + 1);
                const std::uint8_t *src = size ? interpreter.m_memory.hostPtr(addr) : nullptr;
                if (src && interpreter.m_memory.hostPtr(addr + size - 1)) {
                    std::ofstream out(path, std::ios::binary);
                    out.write(reinterpret_cast<const char *>(src), size);
                    std::cout << std::format("[DIAG] dumped guest memory 0x{:08X}:{} -> {}", addr, size, path)
                              << std::endl;
                } else {
                    std::cerr << std::format("[DIAG] WEMU_DUMP_MEM unreadable range 0x{:08X}:{}", addr, size)
                              << std::endl;
                }
            }
        }

        interpreter.run();

        Core::Diag::dumpUnknownImports();
        Core::Diag::dumpPcCounts();

        return interpreter.failed() ? ERROR_VALUE : SUCCESS_VALUE;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return ERROR_VALUE;
    } catch (...) {
        std::cerr << "Unknown fatal error." << std::endl;
        return ERROR_VALUE;
    }
}
