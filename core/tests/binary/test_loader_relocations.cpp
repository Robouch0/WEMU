// RPX loader linking: relocations, import thunks, data-import slots and the
// FILEINFO small-data bases. Retail titles reach every library call and most
// pointers through these, so each field a relocation patches is checked bit
// for bit, together with the instruction bits it must leave alone.

#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "binary/Loader.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace {
    constexpr std::uint32_t Text = 0x02000000, TextSize = 0x100;
    constexpr std::uint32_t Data = 0x10000000, DataSize = 0x40;
    constexpr std::uint32_t FimportSentinel = 0xC0000000, DimportSentinel = 0xC1000000;

    void put32(std::vector<char> &bytes, std::size_t at, std::uint32_t value)
    {
        value = std::byteswap(value);
        std::memcpy(bytes.data() + at, &value, 4);
    }

    void put16(std::vector<char> &bytes, std::size_t at, std::uint16_t value)
    {
        value = std::byteswap(value);
        std::memcpy(bytes.data() + at, &value, 2);
    }

    // Builds an RPX from a list of sections, symbols and RELA entries. Sections
    // 0-3 are always null, .shstrtab, .symtab and .strtab; addSection returns
    // the index of the section it adds.
    class RpxBuilder {
        public:
            struct Reloc {
                    std::uint32_t offset, info;
                    std::int32_t addend;
            };

            unsigned addSection(const std::string &name, std::uint32_t type, std::uint32_t flags, std::uint32_t address, std::vector<char> data)
            {
                m_sections.push_back({name, type, flags, address, std::move(data)});
                return static_cast<unsigned>(m_sections.size() + 3);
            }

            unsigned addSymbol(const std::string &name, std::uint32_t value, unsigned char type, unsigned section)
            {
                m_symbols.push_back({name, value, static_cast<unsigned char>(ELF32_ST_INFO(STB_GLOBAL, type)), static_cast<std::uint16_t>(section)});
                return static_cast<unsigned>(m_symbols.size()); // symbol 0 is the null symbol
            }

            void addReloc(std::uint32_t offset, unsigned symbol, unsigned type, std::int32_t addend = 0)
            {
                m_relocs.push_back({offset, ELF32_R_INFO(symbol, type), addend});
            }

            [[nodiscard]] std::vector<char> build() const
            {
                std::vector<char> strtab(1, 0), symtab(16, 0), rela;
                for (const auto &symbol: m_symbols) {
                    std::vector<char> entry(16, 0);
                    put32(entry, 0, static_cast<std::uint32_t>(strtab.size()));
                    put32(entry, 4, symbol.value);
                    entry[12] = static_cast<char>(symbol.info);
                    put16(entry, 14, symbol.section);
                    symtab.insert(symtab.end(), entry.begin(), entry.end());
                    strtab.insert(strtab.end(), symbol.name.begin(), symbol.name.end());
                    strtab.push_back(0);
                }
                for (const auto &reloc: m_relocs) {
                    std::vector<char> entry(12, 0);
                    put32(entry, 0, reloc.offset);
                    put32(entry, 4, reloc.info);
                    put32(entry, 8, static_cast<std::uint32_t>(reloc.addend));
                    rela.insert(rela.end(), entry.begin(), entry.end());
                }

                std::vector<Section> all{{"", SHT_NULL, 0, 0, {}},
                                         {".shstrtab", SHT_STRTAB, 0, 0, {}},
                                         {".symtab", SHT_SYMTAB, 0, 0, symtab, 16},
                                         {".strtab", SHT_STRTAB, 0, 0, strtab}};
                all.insert(all.end(), m_sections.begin(), m_sections.end());
                if (!rela.empty())
                    all.push_back({".rela.text", SHT_RELA, 0, 0, rela, 12});

                std::vector<char> names(1, 0);
                std::vector<std::uint32_t> nameAt;
                for (const auto &section: all) {
                    nameAt.push_back(section.name.empty() ? 0 : static_cast<std::uint32_t>(names.size()));
                    names.insert(names.end(), section.name.begin(), section.name.end());
                    if (!section.name.empty())
                        names.push_back(0);
                }
                all[1].data = names;

                const auto count = static_cast<std::uint32_t>(all.size());
                std::vector<char> bytes(52 + count * 40, 0);
                std::memcpy(bytes.data(),
                            "\x7f"
                            "ELF\x01\x02\x01",
                            7);
                put16(bytes, 16, 0xFE01);
                put16(bytes, 18, EM_PPC);
                put32(bytes, 20, 1);
                put32(bytes, 32, 52);
                put16(bytes, 40, 52);
                put16(bytes, 46, 40);
                put16(bytes, 48, static_cast<std::uint16_t>(count));
                put16(bytes, 50, 1);
                for (std::uint32_t i = 0; i < count; ++i) {
                    const auto &section = all[i];
                    const auto header = 52 + i * 40;
                    const auto offset = static_cast<std::uint32_t>(bytes.size());
                    bytes.insert(bytes.end(), section.data.begin(), section.data.end());
                    put32(bytes, header, nameAt[i]);
                    put32(bytes, header + 4, section.type);
                    put32(bytes, header + 8, section.flags);
                    put32(bytes, header + 12, section.address);
                    put32(bytes, header + 16, i ? offset : 0);
                    put32(bytes, header + 20, static_cast<std::uint32_t>(section.data.size()));
                    put32(bytes, header + 32, 4);
                    put32(bytes, header + 36, section.entrySize);
                }
                return bytes;
            }

        private:
            struct Section {
                    std::string name;
                    std::uint32_t type, flags, address;
                    std::vector<char> data;
                    std::uint32_t entrySize = 0;
            };

            struct SymbolEntry {
                    std::string name;
                    std::uint32_t value;
                    unsigned char info;
                    std::uint16_t section;
            };

            std::vector<Section> m_sections;
            std::vector<SymbolEntry> m_symbols;
            std::vector<Reloc> m_relocs;
    };

    class LoadedImage {
        public:
            explicit LoadedImage(const std::vector<char> &bytes)
            {
                char pattern[] = "/tmp/wemu-loader-relocations-XXXXXX";
                const int fd = mkstemp(pattern);
                if (fd >= 0)
                    close(fd);
                m_path = pattern;
                std::ofstream(m_path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                m_loader = std::make_unique<Core::Loader>(m_path);
            }

            ~LoadedImage() { std::filesystem::remove(m_path); }

            LoadedImage(const LoadedImage &) = delete;
            LoadedImage &operator=(const LoadedImage &) = delete;

            [[nodiscard]] const Core::Binary &binary() const { return m_loader->getBinary(); }
            [[nodiscard]] std::uint32_t word(std::uint32_t address) const { return binary().m_memory.read<std::uint32_t>(address); }
            [[nodiscard]] std::uint16_t half(std::uint32_t address) const { return binary().m_memory.read<std::uint16_t>(address); }
            [[nodiscard]] std::uint32_t symbolAddress(unsigned index) const
            {
                return static_cast<std::uint32_t>(binary().symbols.at(index).meta.virtAddress);
            }

        private:
            std::string m_path;
            std::unique_ptr<Core::Loader> m_loader;
    };

    std::string loadError(const std::vector<char> &bytes)
    {
        try {
            LoadedImage image(bytes);
            return {};
        } catch (const Core::LoaderException &error) {
            return error.what();
        }
    }

    // Branch target encoded in a b/bl (24-bit) or bc (14-bit) instruction at `at`.
    std::uint32_t branchTarget(std::uint32_t insn, std::uint32_t at)
    {
        const bool conditional = (insn >> 26) == 16;
        const std::uint32_t mask = conditional ? 0x0000FFFCu : 0x03FFFFFCu;
        const unsigned signBit = conditional ? 15 : 25;
        std::uint32_t displacement = insn & mask;
        if (displacement & (1u << signBit))
            displacement |= ~mask & ~3u;
        return (insn & 2) ? displacement : at + displacement;
    }

    // Code and data sections with the instructions the relocations patch.
    struct Program {
            RpxBuilder rpx;
            unsigned text, data, target, start, branchDest, table;

            Program()
            {
                std::vector<char> code(TextSize, 0);
                for (std::uint32_t at = 0; at < TextSize; at += 4)
                    put32(code, at, 0x60000000); // nop
                put32(code, 0x00, 0x48000001); // bl   -> target
                put32(code, 0x04, 0x48000000); // b    -> start (backwards)
                put32(code, 0x08, 0x41820000); // beq  -> branch_dest
                put32(code, 0x0C, 0x48000002); // ba   -> target (absolute)
                put32(code, 0x10, 0x41820002); // beqa -> target (absolute)
                put32(code, 0x14, 0x41820002); // beqa -> target (absolute, branch-taken hint)
                put32(code, 0x18, 0x3C600000); // lis  r3, table@ha
                put32(code, 0x1C, 0x38630000); // addi r3, r3, table@l
                put32(code, 0x20, 0x3C800000); // lis  r4, table@h
                put32(code, 0x24, 0x60840000); // ori  r4, r4, table (ADDR16)
                put32(code, 0x28, 0x41820000); // beq  -> branch_dest (branch-not-taken hint)
                text = rpx.addSection(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, Text, code);

                std::vector<char> words(DataSize, 0);
                put32(words, 0x20, 0xDEADBEEF);
                put32(words, 0x24, 0xCAFEF00D);
                put32(words, 0x28, 0x11111111);
                data = rpx.addSection(".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, Data, words);

                target = rpx.addSymbol("target", Text + 0x80, STT_FUNC, text);
                start = rpx.addSymbol("start", Text, STT_FUNC, text);
                branchDest = rpx.addSymbol("branch_dest", Text + 0x40, STT_FUNC, text);
                table = rpx.addSymbol("table", Data, STT_OBJECT, data);
            }
    };
} // namespace

TEST(LoaderRelocationTest, PcRelativeBranchesReachTheirTargetAndKeepTheirOpcodeBits)
{
    Program program;
    program.rpx.addReloc(Text + 0x00, program.target, R_PPC_REL24);
    program.rpx.addReloc(Text + 0x04, program.start, R_PPC_REL24);
    program.rpx.addReloc(Text + 0x08, program.branchDest, R_PPC_REL14);
    program.rpx.addReloc(Text + 0x28, program.branchDest, R_PPC_REL14_BRNTAKEN);
    LoadedImage image(program.rpx.build());

    const auto bl = image.word(Text);
    EXPECT_EQ(bl & 0xFC000003u, 0x48000001u) << "opcode and LK bit must survive";
    EXPECT_EQ(branchTarget(bl, Text), Text + 0x80u);

    const auto back = image.word(Text + 4);
    EXPECT_EQ(back, 0x4BFFFFFCu) << "a backward branch needs a sign-extended displacement";
    EXPECT_EQ(branchTarget(back, Text + 4), Text);

    for (const std::uint32_t at: {Text + 0x08, Text + 0x28}) {
        const auto beq = image.word(at);
        EXPECT_EQ(beq & 0xFFFF0003u, 0x41820000u) << "BO/BI fields must survive";
        EXPECT_EQ(branchTarget(beq, at), Text + 0x40u);
    }
}

TEST(LoaderRelocationTest, AbsoluteBranchesGetTheTargetAddressInTheirDisplacementField)
{
    Program program;
    program.rpx.addReloc(Text + 0x0C, program.target, R_PPC_ADDR24);
    program.rpx.addReloc(Text + 0x10, program.target, R_PPC_ADDR14);
    program.rpx.addReloc(Text + 0x14, program.target, R_PPC_ADDR14_BRTAKEN);
    LoadedImage image(program.rpx.build());

    EXPECT_EQ(image.word(Text + 0x0C), 0x48000002u | (Text + 0x80u));
    // ADDR14 keeps only the low 16 bits of the address (branches into the first or last 32 KiB).
    EXPECT_EQ(image.word(Text + 0x10), 0x41820082u);
    EXPECT_EQ(image.word(Text + 0x14), 0x41820082u);
}

TEST(LoaderRelocationTest, HalfWordRelocationsBuildAddressesWithLisAndAddi)
{
    // table + 0x8010 has bit 15 set in its low half: addi sign-extends it, so @ha must round up.
    constexpr std::uint32_t address = Data + 0x8010;
    Program program;
    program.rpx.addReloc(Text + 0x1A, program.table, R_PPC_ADDR16_HA, 0x8010);
    program.rpx.addReloc(Text + 0x1E, program.table, R_PPC_ADDR16_LO, 0x8010);
    program.rpx.addReloc(Text + 0x22, program.table, R_PPC_ADDR16_HI, 0x8010);
    program.rpx.addReloc(Text + 0x26, program.table, R_PPC_ADDR16, 0x8010);
    LoadedImage image(program.rpx.build());

    const auto lis = image.word(Text + 0x18), addi = image.word(Text + 0x1C);
    EXPECT_EQ(lis, 0x3C601001u);
    EXPECT_EQ(addi, 0x38638010u);
    // What the guest computes: (ha << 16) + sign-extended lo.
    const auto rebuilt = ((lis & 0xFFFFu) << 16) + static_cast<std::uint32_t>(static_cast<std::int16_t>(addi & 0xFFFFu));
    EXPECT_EQ(rebuilt, address);
    EXPECT_EQ(image.word(Text + 0x20), 0x3C801000u) << "@h does not round";
    EXPECT_EQ(image.word(Text + 0x24), 0x60848010u);
}

TEST(LoaderRelocationTest, DataWordRelocationsStoreAbsoluteAndRelativeAddresses)
{
    Program program;
    program.rpx.addReloc(Data + 0x00, program.target, R_PPC_ADDR32, 4);
    program.rpx.addReloc(Data + 0x04, program.target, R_PPC_UADDR32);
    program.rpx.addReloc(Data + 0x08, program.target, R_PPC_REL32);
    program.rpx.addReloc(Data + 0x0C, program.table, R_PPC_RELATIVE, 0x30);
    LoadedImage image(program.rpx.build());

    EXPECT_EQ(image.word(Data + 0x00), Text + 0x84u);
    EXPECT_EQ(image.word(Data + 0x04), Text + 0x80u);
    EXPECT_EQ(image.word(Data + 0x08), (Text + 0x80u) - (Data + 0x08u));
    EXPECT_EQ(image.word(Data + 0x0C), Data + 0x30u);
}

TEST(LoaderRelocationTest, GreenHillsRelativeHalfWordsUseTheCafeNumbering)
{
    // Cafe RPLs number REL16 as 251 = HA, 252 = HI, 253 = LO (GNU ELF uses 249-252).
    Program program;
    program.rpx.addReloc(Data + 0x10, program.table, 251, 0x9000);
    program.rpx.addReloc(Data + 0x12, program.table, 252, 0x9000);
    program.rpx.addReloc(Data + 0x14, program.table, 253, 0x9000);
    LoadedImage image(program.rpx.build());

    // value - P = 0x8FF0, 0x8FEE and 0x8FEC: only @ha rounds the high half up.
    EXPECT_EQ(image.half(Data + 0x10), 0x0001u);
    EXPECT_EQ(image.half(Data + 0x12), 0x0000u);
    EXPECT_EQ(image.half(Data + 0x14), 0x8FECu);
}

TEST(LoaderRelocationTest, UnsupportedNoneAndDanglingRelocationsLeaveTheirTargetUntouched)
{
    Program program;
    program.rpx.addReloc(Data + 0x20, program.table, R_PPC_EMB_SDA21);
    program.rpx.addReloc(Data + 0x24, program.table, R_PPC_NONE);
    program.rpx.addReloc(Data + 0x28, 99, R_PPC_ADDR32); // symbol index past the table
    program.rpx.addReloc(Data + 0x00, program.target, R_PPC_ADDR32); // still applied after them
    LoadedImage image(program.rpx.build());

    EXPECT_EQ(image.word(Data + 0x20), 0xDEADBEEFu);
    EXPECT_EQ(image.word(Data + 0x24), 0xCAFEF00Du);
    EXPECT_EQ(image.word(Data + 0x28), 0x11111111u);
    EXPECT_EQ(image.word(Data + 0x00), Text + 0x80u);
}

TEST(LoaderImportTest, ImportedFunctionCallLandsOnASyscallThunkRightAfterTheCode)
{
    Program program;
    const auto fimport =
            program.rpx.addSection(".fimport_coreinit", SHT_RPL_IMPORTS, SHF_ALLOC | SHF_EXECINSTR, FimportSentinel, std::vector<char>(8, 0));
    const auto osReport = program.rpx.addSymbol("OSReport", FimportSentinel + 4, STT_FUNC, fimport);
    program.rpx.addReloc(Text + 0x00, osReport, R_PPC_REL24);
    LoadedImage image(program.rpx.build());

    // The sentinel 0xC0000000 is out of branch range; the section is moved behind .text.
    const auto thunk = image.symbolAddress(osReport);
    EXPECT_EQ(thunk, Text + TextSize + 4);
    EXPECT_EQ(branchTarget(image.word(Text), Text), thunk);

    const EncodedInstruction sc(image.word(thunk));
    EXPECT_EQ(sc.opcd, 17u);
    EXPECT_EQ(sc.bd, osReport) << "the thunk carries the symbol index the HLE dispatcher looks up";
}

TEST(LoaderImportTest, DataImportSlotsHoldTheSentinelOfTheirObjectOnly)
{
    Program program;
    const auto dimport = program.rpx.addSection(".dimport_coreinit", SHT_RPL_IMPORTS, SHF_ALLOC, DimportSentinel, std::vector<char>(8, 0));
    const auto heapAlloc = program.rpx.addSymbol("MEMAllocFromDefaultHeap", DimportSentinel + 4, STT_OBJECT, dimport);
    const auto notData = program.rpx.addSymbol("NotAnObject", DimportSentinel, STT_FUNC, dimport);
    LoadedImage image(program.rpx.build());

    // Placed after .data, inside the application data window the title can read.
    const auto slot = image.symbolAddress(heapAlloc);
    EXPECT_EQ(slot, Data + DataSize + 4);
    EXPECT_EQ(image.word(slot), DimportSentinel + 4) << "BCTR dispatches on this sentinel";
    EXPECT_EQ(image.word(image.symbolAddress(notData)), 0u);
}

TEST(LoaderImportTest, ImportSymbolOutsideItsSectionIsRejected)
{
    Program program;
    const auto fimport =
            program.rpx.addSection(".fimport_coreinit", SHT_RPL_IMPORTS, SHF_ALLOC | SHF_EXECINSTR, FimportSentinel, std::vector<char>(8, 0));
    program.rpx.addSymbol("OSReport", FimportSentinel + 0x100, STT_FUNC, fimport);
    EXPECT_NE(loadError(program.rpx.build()).find("lies outside its section"), std::string::npos);
}

TEST(LoaderFileInfoTest, FileInfoProvidesTheSmallDataBases)
{
    Program program;
    std::vector<char> info(0x60, 0);
    put32(info, 0x24, 0x10008000);
    put32(info, 0x28, 0x10010000);
    program.rpx.addSection(".rpl_fileinfo", SHT_RPL_FILEINFO, 0, 0, info);
    LoadedImage image(program.rpx.build());

    EXPECT_EQ(image.binary().sdaBase, 0x10008000u);
    EXPECT_EQ(image.binary().sda2Base, 0x10010000u);
}

TEST(LoaderFileInfoTest, ShortFileInfoLeavesTheSmallDataBasesUnset)
{
    Program program;
    std::vector<char> info(0x28, 0); // ends before sda2Base
    put32(info, 0x24, 0x10008000);
    program.rpx.addSection(".rpl_fileinfo", SHT_RPL_FILEINFO, 0, 0, info);
    LoadedImage image(program.rpx.build());

    EXPECT_EQ(image.binary().sdaBase, 0u);
    EXPECT_EQ(image.binary().sda2Base, 0u);
}

TEST(LoaderSymbolTest, SymbolsKeepTheirNamesAndAddresses)
{
    Program program;
    LoadedImage image(program.rpx.build());

    const auto &symbols = image.binary().symbols;
    ASSERT_EQ(symbols.size(), 5u);
    EXPECT_EQ(symbols[program.target].name, "target");
    EXPECT_EQ(symbols[program.table].name, "table");
    EXPECT_EQ(image.symbolAddress(program.branchDest), Text + 0x40u);
    EXPECT_EQ(image.symbolAddress(program.table), Data);
}
