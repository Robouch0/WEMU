// RPX/ELF loader input validation: a malformed executable must be rejected with
// a LoaderException that says what is wrong, before any size taken from the file
// is used to allocate or index memory.

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>
#include <zlib.h>

#include "binary/Loader.hpp"

namespace {
    constexpr unsigned HeaderSize = 52, SectionHeaderSize = 40;

    // A minimal valid RPX: null, .shstrtab, .symtab (one null symbol), .strtab,
    // .text (4 bytes) and an optional SHF_DEFLATED .data section.
    class ImageBuilder {
        public:
            explicit ImageBuilder(bool compressedData = false, const std::vector<char> &inflated = {'W', 'E', 'M', 'U'})
            {
                const unsigned count = compressedData ? 6 : 5;
                const unsigned table = HeaderSize, payload = table + count * SectionHeaderSize;
                static constexpr char names[] = "\0.shstrtab\0.symtab\0.strtab\0.text\0.data\0";
                bytes.assign(payload, 0);
                std::memcpy(bytes.data(),
                            "\x7f"
                            "ELF\x01\x02\x01",
                            7);
                half(16, 0xFE01);
                half(18, EM_PPC);
                word(20, 1);
                word(32, table);
                half(40, HeaderSize);
                half(46, SectionHeaderSize);
                half(48, static_cast<std::uint16_t>(count));
                half(50, 1);

                const unsigned namesAt = append(names, sizeof(names));
                section(1, 1, SHT_STRTAB, 0, 0, namesAt, sizeof(names), 0);
                const unsigned symbolsAt = append(std::vector<char>(16, 0));
                section(2, 11, SHT_SYMTAB, 0, 0, symbolsAt, 16, 16);
                const unsigned stringsAt = append(std::vector<char>(1, 0));
                section(3, 19, SHT_STRTAB, 0, 0, stringsAt, 1, 0);
                const unsigned textAt = append(std::vector<char>{0x60, 0, 0, 0}); // nop
                section(4, 27, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x02000000, textAt, 4, 0);
                if (compressedData) {
                    uLongf size = compressBound(static_cast<uLong>(inflated.size()));
                    std::vector<char> deflated(size);
                    compress(reinterpret_cast<Bytef *>(deflated.data()), &size, reinterpret_cast<const Bytef *>(inflated.data()),
                             static_cast<uLong>(inflated.size()));
                    deflated.resize(size);
                    std::vector<char> data(4);
                    const auto inflatedSize = std::byteswap(static_cast<std::uint32_t>(inflated.size()));
                    std::memcpy(data.data(), &inflatedSize, 4);
                    data.insert(data.end(), deflated.begin(), deflated.end());
                    const unsigned dataAt = append(data);
                    // The loader reads the 4-byte inflated size, then sh_size compressed bytes.
                    section(5, 33, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE | SHF_DEFLATED, 0x10000000, dataAt, static_cast<unsigned>(deflated.size()), 0);
                }
            }

            void word(unsigned off, std::uint32_t value)
            {
                value = std::byteswap(value);
                std::memcpy(bytes.data() + off, &value, 4);
            }

            void half(unsigned off, std::uint16_t value)
            {
                value = std::byteswap(value);
                std::memcpy(bytes.data() + off, &value, 2);
            }

            // Offset of field `field` (byte offset inside Elf32_Shdr) of section `index`.
            static unsigned sectionField(unsigned index, unsigned field) { return HeaderSize + index * SectionHeaderSize + field; }

            std::vector<char> bytes;

        private:
            unsigned append(const char *data, std::size_t size)
            {
                const auto at = static_cast<unsigned>(bytes.size());
                bytes.insert(bytes.end(), data, data + size);
                return at;
            }

            unsigned append(const std::vector<char> &data) { return append(data.data(), data.size()); }

            void section(unsigned index, unsigned name, unsigned type, unsigned flags, unsigned address, unsigned offset, unsigned size,
                         unsigned entry)
            {
                const auto off = HeaderSize + index * SectionHeaderSize;
                word(off, name);
                word(off + 4, type);
                word(off + 8, flags);
                word(off + 12, address);
                word(off + 16, offset);
                word(off + 20, size);
                word(off + 32, 4);
                word(off + 36, entry);
            }
    };

    constexpr unsigned ShName = 0, ShOffset = 16, ShSize = 20, ShEntSize = 36;

    class TempFile {
        public:
            explicit TempFile(const std::vector<char> &bytes)
            {
                char pattern[] = "/tmp/wemu-loader-validation-XXXXXX";
                const int fd = mkstemp(pattern);
                if (fd >= 0)
                    close(fd);
                path = pattern;
                std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }

            ~TempFile() { std::filesystem::remove(path); }

            TempFile(const TempFile &) = delete;
            TempFile &operator=(const TempFile &) = delete;

            std::string path;
    };

    // Loads `bytes` and returns the LoaderException message ("" when it loaded).
    std::string rejection(const std::vector<char> &bytes)
    {
        TempFile file(bytes);
        try {
            Core::Loader loader(file.path);
            return {};
        } catch (const Core::LoaderException &error) {
            return error.what();
        }
    }
} // namespace

TEST(LoaderValidationTest, MinimalValidImageLoads)
{
    ImageBuilder image;
    TempFile file(image.bytes);
    Core::Loader loader(file.path);
    const auto &binary = loader.getBinary();
    ASSERT_EQ(binary.sections.size(), 5u);
    EXPECT_EQ(binary.sections[4].name, ".text");
    EXPECT_EQ(binary.m_memory.read<std::uint32_t>(0x02000000), 0x60000000u);
}

TEST(LoaderValidationTest, CompressedSectionInflatesToItsDeclaredContents)
{
    const std::vector<char> payload = {'R', 'P', 'X', '-', 'D', 'A', 'T', 'A'};
    ImageBuilder image(true, payload);
    TempFile file(image.bytes);
    Core::Loader loader(file.path);
    EXPECT_EQ(loader.getBinary().sections[5].raw.data, payload);
}

TEST(LoaderValidationTest, MissingFileIsReported) { EXPECT_THROW(Core::Loader("/nonexistent/wemu/missing.rpx"), Utils::BeDecoderException); }

TEST(LoaderValidationTest, EveryRejectionNamesTheFile)
{
    ImageBuilder image;
    image.bytes[0] = 0; // bad magic: a check inside the loader, not a read past the end
    TempFile file(image.bytes);
    try {
        Core::Loader loader(file.path);
        FAIL() << "a file without the ELF magic loaded";
    } catch (const Core::LoaderException &error) {
        EXPECT_EQ(error.message(), file.path + ": not an ELF/RPX file (bad magic)");
    }
}

TEST(LoaderValidationTest, EmptyFileIsRejectedAsTruncated) { EXPECT_NE(rejection({}).find("truncated"), std::string::npos); }

TEST(LoaderValidationTest, BadMagicIsRejected)
{
    ImageBuilder image;
    image.bytes[1] = 'X';
    EXPECT_NE(rejection(image.bytes).find("bad magic"), std::string::npos);
}

TEST(LoaderValidationTest, LittleEndianOr64BitElfIsRejected)
{
    ImageBuilder little;
    little.bytes[EI_DATA] = ELFDATA2LSB;
    EXPECT_NE(rejection(little.bytes).find("32-bit big-endian"), std::string::npos);
    ImageBuilder wide;
    wide.bytes[EI_CLASS] = ELFCLASS64;
    EXPECT_NE(rejection(wide.bytes).find("32-bit big-endian"), std::string::npos);
}

TEST(LoaderValidationTest, NonPowerPcMachineIsRejected)
{
    ImageBuilder image;
    image.half(18, EM_X86_64);
    EXPECT_NE(rejection(image.bytes).find("machine type"), std::string::npos);
}

TEST(LoaderValidationTest, SectionTableOutsideTheFileIsRejected)
{
    ImageBuilder image;
    image.word(32, 0xFFFFFF00);
    EXPECT_NE(rejection(image.bytes).find("section table lies outside"), std::string::npos);
}

TEST(LoaderValidationTest, UndersizedSectionHeadersAreRejected)
{
    ImageBuilder image;
    image.half(46, 8);
    EXPECT_NE(rejection(image.bytes).find("invalid section table"), std::string::npos);
}

TEST(LoaderValidationTest, SectionNameTableIndexOutOfRangeIsRejected)
{
    ImageBuilder image;
    image.half(50, 200);
    EXPECT_NE(rejection(image.bytes).find("section-name table index"), std::string::npos);
}

TEST(LoaderValidationTest, SectionNameOffsetOutsideTheNameTableIsRejected)
{
    ImageBuilder image;
    image.word(ImageBuilder::sectionField(4, ShName), 0x7FFFFFFF);
    EXPECT_NE(rejection(image.bytes).find("name offset"), std::string::npos);
}

TEST(LoaderValidationTest, HugeSectionSizeIsRejectedBeforeAllocating)
{
    // 4 GiB claimed by a 300-byte file: must fail fast, not allocate.
    ImageBuilder image;
    image.word(ImageBuilder::sectionField(4, ShSize), 0xFFFFFFF0);
    EXPECT_NE(rejection(image.bytes).find("lies outside the file"), std::string::npos);
}

TEST(LoaderValidationTest, SectionDataOffsetPastTheEndIsRejected)
{
    ImageBuilder image;
    image.word(ImageBuilder::sectionField(4, ShOffset), static_cast<std::uint32_t>(image.bytes.size()) + 1);
    EXPECT_NE(rejection(image.bytes).find("lies outside the file"), std::string::npos);
}

TEST(LoaderValidationTest, CompressedSectionWithAbsurdInflatedSizeIsRejected)
{
    ImageBuilder image(true);
    const auto dataAt = std::byteswap(*reinterpret_cast<const std::uint32_t *>(image.bytes.data() + ImageBuilder::sectionField(5, ShOffset)));
    image.word(dataAt, 0xFFFFFFFF);
    EXPECT_NE(rejection(image.bytes).find("once inflated"), std::string::npos);
}

TEST(LoaderValidationTest, CorruptCompressedStreamIsRejected)
{
    ImageBuilder image(true, std::vector<char>(64, 'x'));
    const auto dataAt = std::byteswap(*reinterpret_cast<const std::uint32_t *>(image.bytes.data() + ImageBuilder::sectionField(5, ShOffset)));
    // Overwrite the zlib header (after the 4-byte size) with bytes that are not a stream.
    std::memset(image.bytes.data() + dataAt + 4, 0xFF, 4);
    EXPECT_NE(rejection(image.bytes).find("corrupt compressed section"), std::string::npos);
}

TEST(LoaderValidationTest, ZeroSymbolEntrySizeIsRejectedInsteadOfDividingByZero)
{
    ImageBuilder image;
    image.word(ImageBuilder::sectionField(2, ShEntSize), 0);
    EXPECT_NE(rejection(image.bytes).find("symbol table entry size"), std::string::npos);
}

// Resilience: a file cut short anywhere is rejected cleanly. Sampled at every
// structural boundary (each load sets up guest memory, so not every byte).
TEST(LoaderValidationTest, TruncatedFilesAreRejectedCleanly)
{
    ImageBuilder image(true);
    const auto size = image.bytes.size();
    std::vector<std::size_t> lengths = {0, 1, 4, 16, 51, HeaderSize, HeaderSize + SectionHeaderSize / 2};
    for (unsigned section = 1; section <= 6; ++section)
        lengths.push_back(HeaderSize + section * SectionHeaderSize - 1);
    for (std::size_t length = HeaderSize + 6 * SectionHeaderSize; length < size; length += (size - length) / 4 + 1)
        lengths.push_back(length);
    lengths.push_back(size - 1);
    for (const auto length: lengths) {
        std::vector<char> prefix(image.bytes.begin(), image.bytes.begin() + static_cast<std::ptrdiff_t>(length));
        TempFile file(prefix);
        EXPECT_THROW(Core::Loader loader(file.path), Core::Exception) << "a " << length << "-byte prefix of " << size << " loaded";
    }
}

// Resilience: random byte corruption of headers never crashes the loader.
TEST(LoaderValidationTest, CorruptedHeadersNeverCrash)
{
    std::mt19937 random(0x57454D55); // fixed seed: reproducible failures
    const ImageBuilder pristine(true);
    const auto headerBytes = static_cast<int>(HeaderSize + 6 * SectionHeaderSize);
    std::uniform_int_distribution<int> position(0, headerBytes - 1), value(0, 255), flips(1, 4);
    for (int round = 0; round < 40; ++round) {
        auto bytes = pristine.bytes;
        for (int i = flips(random); i > 0; --i)
            bytes[static_cast<std::size_t>(position(random))] = static_cast<char>(value(random));
        TempFile file(bytes);
        try {
            Core::Loader loader(file.path);
        } catch (const Core::Exception &) {
            // Clean rejection; anything else (crash, std::bad_alloc) fails the test.
        }
    }
}

// Real titles must keep loading. Opt-in (needs local game files):
//   WEMU_TEST_RPX_DIR=games ./wemu_tests --gtest_filter='LoaderValidationTest.RealTitles*'
TEST(LoaderValidationTest, RealTitlesStillLoad)
{
    const char *root = std::getenv("WEMU_TEST_RPX_DIR");
    if (!root)
        GTEST_SKIP() << "set WEMU_TEST_RPX_DIR to a folder containing .rpx files";
    int loaded = 0;
    for (const auto &entry: std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".rpx")
            continue;
        SCOPED_TRACE(entry.path().string());
        EXPECT_NO_THROW(Core::Loader loader(entry.path().string()));
        ++loaded;
    }
    EXPECT_GT(loaded, 0) << "no .rpx found under " << root;
}
