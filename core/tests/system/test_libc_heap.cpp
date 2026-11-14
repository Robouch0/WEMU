// The C library heap and the Cafe MEM heaps that titles allocate everything from.

#include <cstdint>
#include <set>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/Libc.hpp"
#include "hle/MemHeap.hpp"

class LibcHeapTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            RegisterLibcFunctions();
            RegisterMemHeapFunctions();
            cpu->m_running = true;
        }

        static std::uint32_t call(const char *name, std::initializer_list<std::uint32_t> args = {})
        {
            std::uint32_t reg = 3;
            for (const auto value: args)
                cpu->m_gpr[reg++] = value;
            Core::syscallHandler.get(name)(*cpu);
            return cpu->m_gpr[3];
        }
};

TEST_F(LibcHeapTest, MallocReallocAndFreeKeepContentsAndReuseMemory)
{
    const auto block = call("malloc", {64});
    ASSERT_NE(block, 0u);
    EXPECT_EQ(block % 8, 0u) << "malloc returns 8-byte aligned memory";
    for (std::uint32_t i = 0; i < 64; ++i)
        cpu->m_memory.write<std::uint8_t>(block + i, static_cast<std::uint8_t>(i));

    const auto grown = call("realloc", {block, 4096});
    ASSERT_NE(grown, 0u);
    for (std::uint32_t i = 0; i < 64; ++i)
        ASSERT_EQ(cpu->m_memory.read<std::uint8_t>(grown + i), i) << "realloc keeps the old contents";
    call("free", {grown});

    // Freed memory goes back to the heap: a run of same-size allocations does not grow forever.
    std::set<std::uint32_t> seen;
    for (int i = 0; i < 64; ++i) {
        const auto p = call("malloc", {4096});
        ASSERT_NE(p, 0u);
        seen.insert(p);
        call("free", {p});
    }
    EXPECT_LT(seen.size(), 4u) << "free blocks are reused";
}

TEST_F(LibcHeapTest, CallocZeroesAndRejectsSizesThatWouldOverflow)
{
    const auto dirty = call("malloc", {256});
    for (std::uint32_t i = 0; i < 256; i += 4)
        cpu->m_memory.write<std::uint32_t>(dirty + i, 0xDEADBEEF);
    call("free", {dirty});
    const auto zeroed = call("calloc", {16, 16});
    ASSERT_NE(zeroed, 0u);
    for (std::uint32_t i = 0; i < 256; i += 4)
        ASSERT_EQ(cpu->m_memory.read<std::uint32_t>(zeroed + i), 0u) << i;

    // 0x10000 * 0x10001 wraps to 0x10000 in 32 bits: allocating that and clearing the real size
    // would overwrite gigabytes of the guest heap.
    const auto guard = call("malloc", {16});
    cpu->m_memory.write<std::uint32_t>(guard, 0x600DF00D);
    EXPECT_EQ(call("calloc", {0x10000, 0x10001}), 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(guard), 0x600DF00Du) << "nothing was cleared";
}

TEST_F(LibcHeapTest, MemalignHonoursTheRequestedAlignment)
{
    for (const std::uint32_t align: {16u, 64u, 256u, 4096u}) {
        const auto p = call("memalign", {align, 100});
        ASSERT_NE(p, 0u);
        EXPECT_EQ(p % align, 0u) << align;
    }
    EXPECT_EQ(call("memalign", {0, 100}) % 8, 0u) << "alignment 0 falls back to 8";
}

TEST_F(LibcHeapTest, SrandMakesRandReproducible)
{
    call("srand", {1234});
    std::uint32_t first[4];
    for (auto &value: first)
        value = call("rand");
    call("srand", {1234});
    for (const auto expected: first)
        EXPECT_EQ(call("rand"), expected);
    call("srand", {99});
    bool different = false;
    for (const auto other: first)
        different |= call("rand") != other;
    EXPECT_TRUE(different) << "another seed gives another sequence";
}

TEST_F(LibcHeapTest, ExitAndAbortStopTheTitle)
{
    testing::internal::CaptureStderr();
    call("exit", {3});
    EXPECT_FALSE(cpu->m_running);
    cpu->m_running = true;
    call("abort");
    EXPECT_FALSE(cpu->m_running);
    const auto log = testing::internal::GetCapturedStderr();
    EXPECT_NE(log.find("[HLE] exit(3) called"), std::string::npos) << log;
    EXPECT_NE(log.find("[HLE] abort() called"), std::string::npos) << log;
    EXPECT_EQ(call("__ghsLock"), 0u);
    EXPECT_EQ(call("__ghsUnlock"), 0u);
}

TEST_F(LibcHeapTest, DefaultHeapFreeSizeShrinksOnAllocationAndRecoversOnFree)
{
    const auto handle = call("MEMGetBaseHeapHandle", {1}); // MEM2, which backs the default heap
    ASSERT_NE(handle, 0u);
    EXPECT_EQ(call("MEMGetBaseHeapHandle", {9}), 0u) << "only arenas 0-8 exist";
    const auto before = call("MEMGetTotalFreeSizeForExpHeap", {handle});
    ASSERT_GT(before, 0x1000u);
    const std::uint32_t size = (before / 4) & ~0x3Fu; // the heap is shared by the whole process
    const auto block = call("MEMAllocFromDefaultHeap", {size});
    ASSERT_NE(block, 0u);
    EXPECT_EQ(block % 0x40, 0u) << "the default heap aligns to 64 bytes";
    EXPECT_LE(call("MEMGetTotalFreeSizeForExpHeap", {handle}), before - size);
    call("MEMFreeToDefaultHeap", {block});
    EXPECT_EQ(call("MEMGetTotalFreeSizeForExpHeap", {handle}), before) << "freeing returns every byte";
}

TEST_F(LibcHeapTest, DestroyedHeapNoLongerHandsOutItsArena)
{
    const auto arena = call("malloc", {0x10000});
    const auto heap = call("MEMCreateExpHeapEx", {arena, 0x10000, 0});
    ASSERT_NE(heap, 0u);
    EXPECT_NE(call("MEMAllocFromExpHeapEx", {heap, 0x100, 4}), 0u);
    EXPECT_GT(call("MEMGetTotalFreeSizeForExpHeap", {heap}), 0u);
    call("MEMDestroyExpHeap", {heap});
    EXPECT_EQ(call("MEMGetTotalFreeSizeForExpHeap", {heap}), 0u);
    // Unknown handles are served from WEMU's internal pool (and logged), never from the old arena.
    testing::internal::CaptureStderr();
    const auto after = call("MEMAllocFromExpHeapEx", {heap, 0x100, 4});
    testing::internal::GetCapturedStderr();
    EXPECT_TRUE(after < arena || after >= arena + 0x10000) << "the destroyed heap's memory is not handed out again";
}
