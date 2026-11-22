#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/Coreinit.hpp"

namespace {
    class LockedCacheTest : public InstructionTest {
        protected:
            void SetUp() override
            {
                InstructionTest::SetUp();
                cpu->m_scheduler = {};
                cpu->m_scheduler.bootstrap(*cpu);
                RegisterCoreinitFunctions();
            }
            std::uint32_t call(const char *name, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0)
            {
                cpu->m_gpr[3] = a;
                cpu->m_gpr[4] = b;
                cpu->m_gpr[5] = c;
                Core::syscallHandler.get(name)(*cpu);
                return cpu->m_gpr[3];
            }
            void select(unsigned core) { cpu->m_scheduler.setAffinity(cpu->m_scheduler.currentHandle(), 1u << core); }
    };
    constexpr auto size = Core::Memory::LOCKED_CACHE_SIZE;
    constexpr auto unit = Core::LockedCacheState::allocationUnit;
    constexpr auto ram = Core::Memory::ApplicationCode + 0x10000;
} // namespace

TEST_F(LockedCacheTest, AllocationRoundsAndRejectsExhaustionInvalidAndInteriorFrees)
{
    const auto base = Core::Memory::LOCKED_CACHE_BASES[1];
    EXPECT_EQ(call("LCHardwareIsAvailable"), 1u);
    EXPECT_EQ(call("LCGetMaxSize"), size);
    EXPECT_EQ(call("LCGetAllocatableSize"), size);
    EXPECT_EQ(call("LCAlloc", 0), 0u);
    EXPECT_EQ(call("LCAlloc", UINT32_MAX), 0u);
    EXPECT_EQ(call("LCAlloc", 1), base);
    EXPECT_EQ(call("LCAlloc", unit + 1), base + unit);
    EXPECT_EQ(call("LCGetUnallocated"), size - 3 * unit);
    call("LCDealloc", base + unit + 1);
    call("LCDealloc", base + 2 * unit);
    EXPECT_EQ(call("LCGetUnallocated"), size - 3 * unit);
    call("LCDealloc", base + unit);
    call("LCDealloc", base + unit);
    EXPECT_EQ(call("LCGetUnallocated"), size - unit);
    EXPECT_EQ(call("LCAlloc", size - unit), base + unit);
    EXPECT_EQ(call("LCAlloc", 1), 0u);
    EXPECT_EQ(call("LCGetAllocatableSize"), 0u);
    call("LCDealloc", base);
    call("LCDealloc", base + unit);
    EXPECT_EQ(call("LCAlloc", size), base);
}

TEST_F(LockedCacheTest, FragmentationReportsLargestFreeRunIncludingTrailingSpace)
{
    const auto a = call("LCAlloc", 4096);
    const auto b = call("LCAlloc", 4096);
    const auto c = call("LCAlloc", 4096);
    EXPECT_EQ(call("LCGetAllocatableSize"), 4096u);
    call("LCDealloc", b);
    EXPECT_EQ(call("LCGetUnallocated"), 8192u);
    EXPECT_EQ(call("LCGetAllocatableSize"), 4096u);
    EXPECT_EQ(call("LCAlloc", 8192), 0u);
    call("LCDealloc", c);
    EXPECT_EQ(call("LCGetAllocatableSize"), 12288u);
    call("LCDealloc", a);
    EXPECT_EQ(call("LCGetAllocatableSize"), size);
}

TEST_F(LockedCacheTest, EachCoreHasIndependentAllocationMemoryAndDmaReferenceCounts)
{
    for (unsigned core = 0; core < 3; ++core) {
        select(core);
        const auto address = call("LCAlloc", size);
        EXPECT_EQ(address, Core::Memory::LOCKED_CACHE_BASES[core]);
        cpu->m_memory.write<std::uint32_t>(address, 0x12340000 + core);
        EXPECT_EQ(call("LCIsDMAEnabled"), 0u);
        EXPECT_EQ(call("LCEnableDMA"), 1u);
        EXPECT_EQ(call("LCEnableDMA"), 1u);
    }
    for (unsigned core = 0; core < 3; ++core) {
        select(core);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Core::Memory::LOCKED_CACHE_BASES[core]), 0x12340000 + core);
        EXPECT_EQ(call("LCAlloc", 1), 0u);
        call("LCDealloc", Core::Memory::LOCKED_CACHE_BASES[(core + 1) % 3]);
        EXPECT_EQ(call("LCGetUnallocated"), 0u);
        call("LCDisableDMA");
        EXPECT_EQ(call("LCIsDMAEnabled"), 1u);
        call("LCDisableDMA");
        call("LCDisableDMA");
        EXPECT_EQ(call("LCIsDMAEnabled"), 0u);
    }
    cpu->m_scheduler.setAffinity(cpu->m_scheduler.currentHandle(), 7);
    EXPECT_EQ(call("LCEnableDMA"), 0u);
    cpu->reset();
    EXPECT_EQ(call("LCGetUnallocated"), size);
    EXPECT_EQ(call("LCIsDMAEnabled"), 0u);
}

TEST_F(LockedCacheTest, DmaCopiesRealBytesInBothDirectionsAndZeroMeans128Blocks)
{
    const auto cache = call("LCAlloc", size);
    for (unsigned i = 0; i < 4096; ++i)
        cpu->m_memory.write<std::uint8_t>(ram + i, i * 37 + 11);
    call("LCLoadDMABlocks", cache, ram, 0);
    EXPECT_EQ(std::memcmp(cpu->m_memory.hostPtr(cache, 4096), cpu->m_memory.hostPtr(ram, 4096), 4096), 0);
    cpu->m_memory.write<std::uint32_t>(cache + 8, 0xDEADBEEF);
    call("LCStoreDMABlocks", ram + 8192, cache, 3);
    EXPECT_EQ(std::memcmp(cpu->m_memory.hostPtr(ram + 8192, 96), cpu->m_memory.hostPtr(cache, 96), 96), 0);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(ram + 8200), 0xDEADBEEFu);
    EXPECT_EQ(call("LCGetDMAQueueLength"), 0u);
    EXPECT_EQ(call("LCWaitDMAQueue", 0), 0u);
}

TEST_F(LockedCacheTest, InvalidDmaRangesFaultBeforeWritingAndCacheHolesStayUnmapped)
{
    const auto cache = call("LCAlloc", size);
    cpu->m_memory.write<std::uint32_t>(cache, 0xCAFEBABE);
    EXPECT_THROW(call("LCLoadDMABlocks", cache, 0, 1), Core::MemoryException);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(cache), 0xCAFEBABEu);
    EXPECT_THROW(call("LCLoadDMABlocks", cache + size - 16, ram, 1), Core::MemoryException);
    EXPECT_THROW(call("LCStoreDMABlocks", ram, cache + size - 16, 1), Core::MemoryException);
    EXPECT_THROW(call("LCLoadDMABlocks", cache, ram, UINT32_MAX), Core::MemoryException);
    for (auto base: Core::Memory::LOCKED_CACHE_BASES) {
        EXPECT_NE(cpu->m_memory.hostPtr(base + size - 4, 4), nullptr);
        EXPECT_EQ(cpu->m_memory.hostPtr(base + size - 4, 5), nullptr);
        EXPECT_EQ(cpu->m_memory.hostPtr(base + size), nullptr);
        EXPECT_EQ(cpu->m_memory.hostPtr(base - 1), nullptr);
    }
    EXPECT_EQ(cpu->m_memory.hostPtr(0xFFFFFFFF, 2), nullptr);
}
