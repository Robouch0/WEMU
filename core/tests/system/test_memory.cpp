#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <type_traits>
#include <utility>

#include "cpu/memory/Memory.hpp"

static_assert(!std::is_copy_constructible_v<Core::Memory>);
static_assert(!std::is_copy_assignable_v<Core::Memory>);
static_assert(std::is_nothrow_move_constructible_v<Core::Memory>);
static_assert(std::is_nothrow_move_assignable_v<Core::Memory>);

TEST(MemoryTest, MainStackSupportsFramesLargerThan64KiB)
{
    Core::Memory memory(16);
    // A 0x10018-byte frame below a normal main-thread SP, as used during title startup.
    constexpr unsigned previousSp = 0xC0FFFD58, frame = previousSp - 0x10018;
    ASSERT_NO_THROW(memory.write<unsigned>(frame, previousSp));
    EXPECT_EQ(memory.read<unsigned>(frame), previousSp);
    EXPECT_NE(memory.hostPtr(0xC0F00000, 0x100000), nullptr);
    EXPECT_EQ(memory.hostPtr(0xC1000000), nullptr);
}

namespace {
    constexpr std::uint32_t app = Core::Memory::ApplicationCode;
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 6> regions{{
            {app, 16},
            {Core::Memory::STACK_BASE, Core::Memory::STACK_SIZE},
            {Core::Memory::DIMPORT_BASE, Core::Memory::DIMPORT_SIZE},
            {Core::Memory::LOCKED_CACHE_BASES[0], Core::Memory::LOCKED_CACHE_SIZE},
            {Core::Memory::LOCKED_CACHE_BASES[1], Core::Memory::LOCKED_CACHE_SIZE},
            {Core::Memory::LOCKED_CACHE_BASES[2], Core::Memory::LOCKED_CACHE_SIZE},
    }};

    template<typename T>
    class MemoryAccessTest : public ::testing::Test {
        protected:
            Core::Memory memory{16};
    };
    using AccessTypes = ::testing::Types<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t>;
    TYPED_TEST_SUITE(MemoryAccessTest, AccessTypes);

    TYPED_TEST(MemoryAccessTest, LastCompleteValueFitsAndUsesBigEndianBytes)
    {
        constexpr auto value = static_cast<TypeParam>(0x123456789ABCDEF0ull);
        for (const auto &[base, size]: regions) {
            SCOPED_TRACE(base);
            const auto address = base + size - static_cast<std::uint32_t>(sizeof(TypeParam));
            this->memory.template write<TypeParam>(address, value);
            EXPECT_EQ(this->memory.template read<TypeParam>(address), value);
            const auto *bytes = this->memory.hostPtr(address, sizeof(TypeParam));
            ASSERT_NE(bytes, nullptr);
            for (unsigned i = 0; i < sizeof(TypeParam); ++i)
                EXPECT_EQ(bytes[i], (value >> (8 * (sizeof(TypeParam) - i - 1))) & 0xFF);
        }
    }

    TYPED_TEST(MemoryAccessTest, BoundaryCrossingThrowsWithoutPartiallyWriting)
    {
        constexpr auto value = static_cast<TypeParam>(0x123456789ABCDEF0ull);
        for (const auto &[base, size]: regions) {
            SCOPED_TRACE(base);
            const auto address = base + size - static_cast<std::uint32_t>(sizeof(TypeParam));
            this->memory.template write<TypeParam>(address, value);
            EXPECT_THROW(this->memory.template read<TypeParam>(address + 1), Core::MemoryException);
            EXPECT_THROW(this->memory.template write<TypeParam>(address + 1, 0), Core::MemoryException);
            EXPECT_EQ(this->memory.template read<TypeParam>(address), value);
            EXPECT_EQ(this->memory.hostPtr(address + 1, sizeof(TypeParam)), nullptr);
        }
    }

    TYPED_TEST(MemoryAccessTest, UnalignedValuesRoundTripWithinEachRegion)
    {
        constexpr auto value = static_cast<TypeParam>(0x123456789ABCDEF0ull);
        for (const auto &[base, size]: regions) {
            SCOPED_TRACE(base);
            this->memory.template write<TypeParam>(base + 1, value);
            EXPECT_EQ(this->memory.template read<TypeParam>(base + 1), value);
        }
    }
} // namespace

TEST(MemoryTest, RejectsThePreviouslyUncheckedShortBufferReadAndWrite)
{
    Core::Memory memory(6);
    memory.write<std::uint16_t>(app + 4, 0xCAFE);
    EXPECT_THROW(memory.read<std::uint32_t>(app + 4), Core::MemoryException);
    EXPECT_THROW(memory.write<std::uint32_t>(app + 4, 0), Core::MemoryException);
    EXPECT_EQ(memory.read<std::uint16_t>(app + 4), 0xCAFE);
}

TEST(MemoryTest, RejectsEmptyUnmappedAndOversizedHostRanges)
{
    Core::Memory memory(16);
    EXPECT_EQ(memory.hostPtr(app, 0), nullptr);
    EXPECT_EQ(memory.hostPtr(app - 1, 2), nullptr);
    EXPECT_EQ(memory.hostPtr(app + 16), nullptr);
    EXPECT_EQ(memory.hostPtr(UINT32_MAX, 8), nullptr);
    EXPECT_EQ(memory.hostPtr(app, std::numeric_limits<std::size_t>::max()), nullptr);
    EXPECT_THROW(memory.read<std::uint64_t>(UINT32_MAX), Core::MemoryException);
    EXPECT_THROW(memory.write<std::uint64_t>(UINT32_MAX, 0), Core::MemoryException);
    EXPECT_EQ(std::as_const(memory).hostPtr(app, 16), memory.hostPtr(app, 16));
}

TEST(MemoryTest, EmptyMainRegionStillAllowsStackAndImportAccess)
{
    Core::Memory memory(0);
    EXPECT_EQ(memory.hostPtr(app), nullptr);
    EXPECT_THROW(memory.read<std::uint8_t>(app), Core::MemoryException);
    memory.write<std::uint32_t>(Core::Memory::STACK_BASE, 0x12345678);
    memory.write<std::uint32_t>(Core::Memory::DIMPORT_BASE, 0xABCDEF01);
    EXPECT_EQ(memory.read<std::uint32_t>(Core::Memory::STACK_BASE), 0x12345678u);
    EXPECT_EQ(memory.read<std::uint32_t>(Core::Memory::DIMPORT_BASE), 0xABCDEF01u);
}

TEST(MemoryTest, LoaderAllocationValidatesTheCompleteMainMemoryRange)
{
    Core::Memory memory(6);
    const auto &view = std::as_const(memory);
    EXPECT_NE(view.allocate(app, 6), 0u);
    EXPECT_NE(view.allocate(app + 4, 2), 0u);
    EXPECT_EQ(view.allocate(app + 4, 4), 0u);
    EXPECT_EQ(view.allocate(app, 0), 0u);
    EXPECT_EQ(view.allocate(app - 1, 2), 0u);
    EXPECT_EQ(view.allocate(app, std::numeric_limits<std::size_t>::max()), 0u);
    EXPECT_EQ(view.translate(app + 6), 0u);
}

TEST(MemoryTest, MovesKeepAllRegionPointersAndHeapProgressStable)
{
    Core::Memory source(16);
    auto *mainPointer = source.hostPtr(app);
    auto *stackPointer = source.hostPtr(Core::Memory::STACK_BASE);
    auto *importPointer = source.hostPtr(Core::Memory::DIMPORT_BASE);
    source.write<std::uint32_t>(app, 0xABCDEF01);
    source.write<std::uint32_t>(Core::Memory::STACK_BASE, 0x12345678);
    source.write<std::uint32_t>(Core::Memory::DIMPORT_BASE, 0x87654321);
    EXPECT_EQ(source.heapAllocate(16, 16), Core::Memory::HEAP_BASE);
    Core::Memory moved(std::move(source));
    Core::Memory destination(8);
    destination = std::move(moved);
    EXPECT_EQ(destination.hostPtr(app), mainPointer);
    EXPECT_EQ(destination.hostPtr(Core::Memory::STACK_BASE), stackPointer);
    EXPECT_EQ(destination.hostPtr(Core::Memory::DIMPORT_BASE), importPointer);
    EXPECT_EQ(destination.read<std::uint32_t>(app), 0xABCDEF01u);
    EXPECT_EQ(destination.read<std::uint32_t>(Core::Memory::STACK_BASE), 0x12345678u);
    EXPECT_EQ(destination.read<std::uint32_t>(Core::Memory::DIMPORT_BASE), 0x87654321u);
    EXPECT_EQ(destination.heapAllocate(16, 16), Core::Memory::HEAP_BASE + 16);
}

TEST(MemoryTest, WriteGenerationsTrackStoresIntoWatchedPages)
{
    Core::Memory memory(0x10000);
    constexpr std::uint32_t texture = app + 0x2000, other = app + 0x8000;
    EXPECT_FALSE(memory.writeGeneration(texture, 0x1000));
    memory.watchWrites(texture, 0x1000);
    const auto initial = memory.writeGeneration(texture, 0x1000);
    ASSERT_TRUE(initial);
    // Stores elsewhere, or reads, leave the generation alone.
    memory.write<std::uint32_t>(other, 1);
    (void) memory.read<std::uint32_t>(texture);
    (void) std::as_const(memory).hostPtr(texture, 16);
    EXPECT_EQ(memory.writeGeneration(texture, 0x1000), initial);
    // Stores and writable views, including ones straddling into the range, advance it.
    memory.write<std::uint32_t>(texture + 0x40, 2);
    const auto stored = memory.writeGeneration(texture, 0x1000);
    EXPECT_NE(stored, initial);
    memory.write<std::uint32_t>(texture - 2, 3);
    EXPECT_NE(memory.writeGeneration(texture, 0x1000), stored);
    const auto straddled = memory.writeGeneration(texture, 0x1000);
    ASSERT_NE(memory.hostPtr(texture + 0x800, 8), nullptr);
    EXPECT_NE(memory.writeGeneration(texture, 0x1000), straddled);
    // Unwatched pages, ranges outside main RAM and untracked stores report nothing.
    EXPECT_FALSE(memory.writeGeneration(texture, 0x2000));
    EXPECT_FALSE(memory.writeGeneration(Core::Memory::STACK_BASE, 16));
    memory.setUntrackedStores(true);
    EXPECT_FALSE(memory.writeGeneration(texture, 0x1000));
}

TEST(MemoryTest, WatchedRangesPreserveHolesAndExactBitmapWordBoundaries)
{
    Core::Memory memory(0x100000);
    constexpr auto pageBytes = 0x1000u;
    constexpr auto first = app + 63 * pageBytes;
    memory.watchWrites(first, pageBytes);
    memory.watchWrites(first + 2 * pageBytes, 65 * pageBytes);
    EXPECT_FALSE(memory.writeGeneration(first, 67 * pageBytes)); // page 64 is still a hole
    EXPECT_TRUE(memory.writeGeneration(first + 2 * pageBytes, 65 * pageBytes));
    memory.watchWrites(first + pageBytes, pageBytes);
    EXPECT_EQ(memory.slowPages()[0].load(), std::uint64_t{1} << 63);
    EXPECT_EQ(memory.slowPages()[1].load(), ~std::uint64_t{0});
    EXPECT_EQ(memory.slowPages()[2].load(), 3u);
    const auto before = memory.writeGeneration(first, 67 * pageBytes);
    ASSERT_TRUE(before);
    for (unsigned page = 0; page < 67; ++page)
        memory.write<std::uint32_t>(first + page * pageBytes, page);
    EXPECT_EQ(memory.writeGeneration(first, 67 * pageBytes), *before + 67);
    // Neighboring pages must remain unwatched and must not affect the range.
    memory.write<std::uint32_t>(first - pageBytes, 1);
    memory.write<std::uint32_t>(first + 67 * pageBytes, 2);
    EXPECT_EQ(memory.writeGeneration(first, 67 * pageBytes), *before + 67);
    EXPECT_FALSE(memory.writeGeneration(first - pageBytes, pageBytes));
    EXPECT_FALSE(memory.writeGeneration(first + 67 * pageBytes, pageBytes));
}
