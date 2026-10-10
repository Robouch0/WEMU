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

namespace {
    constexpr std::uint32_t app = Core::Memory::ApplicationCode;
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 3> regions{{
            {app, 16},
            {Core::Memory::STACK_BASE, Core::Memory::STACK_SIZE},
            {Core::Memory::DIMPORT_BASE, Core::Memory::DIMPORT_SIZE},
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
