#include <array>
#include <cstring>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/Coreinit.hpp"
#include "hle/CoreinitExtra.hpp"

TEST_F(InstructionTest, OfflineAccountReturnsAStableValidMiiStoreRecord)
{
    RegisterCoreinitFunctions();
    constexpr unsigned output = 0x28001001, name = 0x28002000;
    std::array<std::uint8_t, 96> first{};
    for (const char *function: {"GetMii__Q2_2nn3actFP12FFLStoreData", "GetMiiEx__Q2_2nn3actFP12FFLStoreDataUc"}) {
        for (unsigned i = 0; i < 98; ++i)
            cpu->m_memory.write<std::uint8_t>(output - 1 + i, 0xCC);
        cpu->m_gpr[3] = output;
        cpu->m_gpr[4] = 1;
        Core::syscallHandler.get(function)(*cpu);
        ASSERT_EQ(cpu->m_gpr[3], 0u);
        EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(output - 1), 0xCC);
        EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(output + 96), 0xCC);
        const auto *data = std::as_const(cpu->m_memory).hostPtr(output, 96);
        ASSERT_NE(data, nullptr);
        // Independent, byte-fed CRC-16/CCITT validation, including the stored checksum.
        unsigned crc = 0;
        for (unsigned i = 0; i < 96; ++i) {
            crc ^= unsigned(data[i]) << 8;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = ((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0)) & 0xFFFF;
        }
        EXPECT_EQ(crc, 0u);
        EXPECT_EQ(data[0], 3); // Mii version
        EXPECT_EQ(data[3] >> 4, 4); // Wii U birth platform
        EXPECT_NE(data[12] & 0x80, 0); // normal creator ID
        const auto littleWord = [&](unsigned offset) { return unsigned(data[offset]) | (unsigned(data[offset + 1]) << 8); };
        EXPECT_GE((littleWord(0x3A) >> 9) & 31, 3u); // minimum eyebrow position
        EXPECT_LE(data[0x2E], 128);
        EXPECT_LE(data[0x2F], 128);
        if (first[0])
            EXPECT_EQ(std::memcmp(first.data(), data, first.size()), 0);
        else
            std::memcpy(first.data(), data, first.size());
        cpu->m_gpr[3] = name;
        Core::syscallHandler.get("GetMiiName__Q2_2nn3actFPw")(*cpu);
        for (unsigned i = 0; i < 10; ++i)
            EXPECT_EQ(littleWord(0x1A + i * 2), i < 4 ? cpu->m_memory.read<std::uint16_t>(name + i * 2) : 0);
    }
}

TEST(SystemConfigTest, ValidatesLanguageAndRegionWithoutReplacingTheTitleRegion)
{
    const auto defaults = Core::SystemConfig::fromOptions({}, {}, {});
    EXPECT_EQ(defaults.language, 1u);
    EXPECT_EQ(defaults.platformRegion, 4u);
    EXPECT_EQ(defaults.titleRegion, 4u);
    for (unsigned language = 0; language < 12; ++language) {
        EXPECT_EQ(Core::SystemConfig::fromOptions(std::to_string(language), "2", "4").language, language);
        EXPECT_EQ(Core::SystemConfig::fromOptions(Core::SystemConfig::languages[language], "2", "4").language, language);
    }
    for (const auto value: {"-1", "12", "1junk", "unknown", "99999999999999999999"})
        EXPECT_EQ(Core::SystemConfig::fromOptions(value, {}, {}).language, 1u);
    for (unsigned region: {1u, 2u, 4u, 16u, 32u, 64u}) {
        const auto config = Core::SystemConfig::fromOptions("fr", std::to_string(region), "4");
        EXPECT_EQ(config.language, 2u);
        EXPECT_EQ(config.platformRegion, region);
        EXPECT_EQ(config.titleRegion, 4u);
    }
    for (const auto value: {"0", "3", "8", "127", "-1", "bad"})
        EXPECT_EQ(Core::SystemConfig::fromOptions("en", value, "2").platformRegion, 2u);
    EXPECT_EQ(Core::SystemConfig::chooseTitleRegion(0x7F), 4u);
    EXPECT_EQ(Core::SystemConfig::chooseTitleRegion(0x3), 2u);
    EXPECT_EQ(Core::SystemConfig::chooseTitleRegion(0x1), 1u);
    EXPECT_EQ(Core::SystemConfig::chooseTitleRegion(0), 4u);
}

class SystemConfigHleTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            RegisterCoreinitExtraFunctions();
            cpu->m_systemConfig = {};
        }
};

TEST_F(SystemConfigHleTest, McpWritesDistinctRegionBytesWithinTheDocumentedStruct)
{
    constexpr std::uint32_t output = 0x28001000;
    for (unsigned i = 0; i < 0x48; ++i)
        cpu->m_memory.write<std::uint8_t>(output + i, 0xCC);
    cpu->m_systemConfig = {2, 2, 4}; // French, US console, European title.
    cpu->m_gpr[3] = 1;
    cpu->m_gpr[4] = output + 1;
    Core::syscallHandler.get("MCP_GetSysProdSettings")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(output), 0xCC);
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(output + 0x47), 0xCC);
    for (unsigned i = 0; i < 0x46; ++i)
        EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(output + 1 + i), i == 3 ? 2 : i == 0xB ? 4 : 0);
}

TEST_F(SystemConfigHleTest, UcLanguageUsesTheSessionAndLeavesOtherKeysAndAdjacentBytesAlone)
{
    constexpr std::uint32_t settings = 0x28002000, output = 0x28003000;
    const std::array names{"cafe.language", "other.language", "cafe.language"};
    for (unsigned i = 0; i < names.size(); ++i) {
        const auto entry = settings + i * 0x54;
        std::memcpy(cpu->m_memory.hostPtr(entry, 64), names[i], std::strlen(names[i]) + 1);
        cpu->m_memory.write<std::uint32_t>(entry + 0x44, 3);
        cpu->m_memory.write<std::uint32_t>(entry + 0x48, 123);
        cpu->m_memory.write<std::uint32_t>(entry + 0x4C, 4);
        cpu->m_memory.write<std::uint32_t>(entry + 0x50, i == 2 ? 0 : output + i * 8);
        cpu->m_memory.write<std::uint32_t>(output + i * 8, 0xFFFFFFFF);
        cpu->m_memory.write<std::uint32_t>(output + i * 8 + 4, 0xAABBCCDD);
    }
    for (unsigned language: {1u, 2u, 0u, 11u}) {
        cpu->m_systemConfig.language = language;
        cpu->m_gpr[3] = 1;
        cpu->m_gpr[4] = names.size();
        cpu->m_gpr[5] = settings;
        Core::syscallHandler.get("UCReadSysConfig")(*cpu);
        EXPECT_EQ(cpu->m_gpr[3], 0u);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output), language);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output + 8), 0u);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output + 16), 0xFFFFFFFFu);
        for (unsigned i = 0; i < names.size(); ++i) {
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(settings + i * 0x54 + 0x48), 0u);
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output + i * 8 + 4), 0xAABBCCDDu);
        }
    }
}
