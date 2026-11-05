#include <gtest/gtest.h>

#include "gfx/ShaderInvalidations.hpp"

using Core::Gfx::ShaderInvalidations;

TEST(ShaderInvalidationsTest, ProgramInsideAnInvalidatedRangeIsAnnouncedByTheLatestCoveringCall)
{
    ShaderInvalidations hints;
    const auto start = ShaderInvalidations::Clock::time_point{} + std::chrono::seconds(10);
    hints.record(0x10000000, 0x1000, 3, start);
    hints.record(0x10000800, 0x100, 7, start + std::chrono::seconds(1));
    const auto inner = hints.covering(0x10000800, 0x80);
    ASSERT_TRUE(inner);
    EXPECT_EQ(inner->swap, 7u);
    EXPECT_EQ(inner->address, 0x10000800u);
    const auto outer = hints.covering(0x10000000, 0x200);
    ASSERT_TRUE(outer);
    EXPECT_EQ(outer->swap, 3u);
    EXPECT_EQ(outer->size, 0x1000u);
}

TEST(ShaderInvalidationsTest, PartialOverlapEmptyAndWholeMemoryRangesAreNotHints)
{
    ShaderInvalidations hints;
    EXPECT_TRUE(hints.record(0x20000000, 0x100, 1));
    EXPECT_FALSE(hints.record(0x30000000, 0, 1));
    EXPECT_FALSE(hints.record(0, 0xFFFFFFFF, 1));
    EXPECT_TRUE(hints.record(0xFFFFFF00, 0x100, 2));
    EXPECT_FALSE(hints.covering(0x200000F0, 0x20));
    EXPECT_FALSE(hints.covering(0x30000000, 4));
    EXPECT_FALSE(hints.covering(0x40000000, 0x100));
    EXPECT_EQ(hints.size(), 2u);
    ASSERT_TRUE(hints.covering(0xFFFFFF80, 0x80));
    EXPECT_FALSE(hints.covering(0xFFFFFF80, 0x100));
}

TEST(ShaderInvalidationsTest, RepeatedRangeIsUpdatedInPlaceAndOldestRangeIsDroppedAtCapacity)
{
    ShaderInvalidations hints;
    hints.record(0x1000, 0x10, 1);
    hints.record(0x1000, 0x10, 9);
    EXPECT_EQ(hints.size(), 1u);
    EXPECT_EQ(hints.covering(0x1000, 0x10)->swap, 9u);
    for (std::uint32_t i = 1; i <= ShaderInvalidations::maxRanges; ++i)
        hints.record(0x1000 + i * 0x100, 0x10, i);
    EXPECT_EQ(hints.size(), ShaderInvalidations::maxRanges);
    EXPECT_FALSE(hints.covering(0x1000, 0x10));
    EXPECT_TRUE(hints.covering(0x1100, 0x10));
}
