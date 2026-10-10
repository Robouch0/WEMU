#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/Coreinit.hpp"
#include "hle/CoreinitExtra.hpp"

// MEM expanded-heap HLE: allocations must come from WITHIN the heap's own [base, base+size) region
// (not the global pool), so the game's pointer arithmetic over its heap region stays consistent.
class ExpHeapTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            RegisterCoreinitFunctions();
        }

        static void call(const char *name) { Core::syscallHandler.get(name)(*cpu); }

        std::uint32_t createHeap(std::uint32_t base, std::uint32_t size)
        {
            cpu->m_gpr[3] = base;
            cpu->m_gpr[4] = size;
            cpu->m_gpr[5] = 0;
            call("MEMCreateExpHeapEx");
            return cpu->m_gpr[3];
        }

        std::uint32_t alloc(std::uint32_t handle, std::uint32_t size, std::uint32_t align)
        {
            cpu->m_gpr[3] = handle;
            cpu->m_gpr[4] = size;
            cpu->m_gpr[5] = align;
            call("MEMAllocFromExpHeapEx");
            return cpu->m_gpr[3];
        }
};

TEST_F(ExpHeapTest, AllocationsStayWithinRegion)
{
    constexpr std::uint32_t BASE = 0x28000000, SIZE = 0x10000;
    EXPECT_EQ(createHeap(BASE, SIZE), BASE); // handle == region base

    const std::uint32_t a = alloc(BASE, 0x100, 0x20);
    EXPECT_GE(a, BASE);
    EXPECT_LT(a + 0x100, BASE + SIZE);
    EXPECT_EQ(a % 0x20, 0u); // honoured alignment

    const std::uint32_t b = alloc(BASE, 0x100, 0x20);
    EXPECT_GE(b, a + 0x100); // distinct, after the first allocation
    EXPECT_LT(b + 0x100, BASE + SIZE);
}

TEST_F(ExpHeapTest, OutOfSpaceReturnsZero)
{
    constexpr std::uint32_t BASE = 0x28100000, SIZE = 0x1000;
    createHeap(BASE, SIZE);
    EXPECT_EQ(alloc(BASE, 0x4000, 0x20), 0u); // larger than the heap
}

TEST_F(ExpHeapTest, AllocatableSizeShrinksAfterAlloc)
{
    constexpr std::uint32_t BASE = 0x28200000, SIZE = 0x10000;
    createHeap(BASE, SIZE);

    cpu->m_gpr[3] = BASE;
    cpu->m_gpr[4] = 4; // alignment argument (honoured since the real-ExpHeap backend)
    call("MEMGetAllocatableSizeForExpHeapEx");
    const std::uint32_t before = cpu->m_gpr[3];

    alloc(BASE, 0x1000, 0x20);

    cpu->m_gpr[3] = BASE;
    cpu->m_gpr[4] = 4;
    call("MEMGetAllocatableSizeForExpHeapEx");
    const std::uint32_t after = cpu->m_gpr[3];

    EXPECT_GE(before, after + 0x1000);
}

TEST_F(ExpHeapTest, UnknownHandleFallsBackToGlobalHeap)
{
    // An allocation against a handle we never created still returns valid (global-pool) memory.
    EXPECT_NE(alloc(0x00BADBAD, 0x40, 8), 0u);
}

TEST_F(ExpHeapTest, AllocatorDescriptorHonorsHeapAlignmentAndFree)
{
    constexpr std::uint32_t heap = 0x28400000, size = 0x10000, descriptor = 0x28001000, copy = 0x28001020;
    createHeap(heap, size);
    cpu->m_memory.write<std::uint32_t>(descriptor + 12, 0xAABBCCDD);
    cpu->m_gpr[3] = descriptor;
    cpu->m_gpr[4] = heap;
    cpu->m_gpr[5] = 0x400;
    call("MEMInitAllocatorForExpHeap");
    const auto table = cpu->m_memory.read<std::uint32_t>(descriptor);
    EXPECT_NE(table, 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(descriptor + 4), heap);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(descriptor + 8), 0x400u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(descriptor + 12), 0xAABBCCDDu);
    for (unsigned offset = 0; offset < 16; offset += 4)
        cpu->m_memory.write<std::uint32_t>(copy + offset, cpu->m_memory.read<std::uint32_t>(descriptor + offset));

    cpu->m_gpr[3] = copy; // dispatch must read the descriptor, not a host map keyed by its old address
    cpu->m_gpr[4] = 0x68;
    cpu->m_lr = 0x1234;
    call("MEMAllocFromAllocator");
    const auto allocation = cpu->m_gpr[3];
    ASSERT_GE(allocation, heap + 0x80);
    ASSERT_LT(allocation + 0x68, heap + size);
    EXPECT_EQ(allocation % 0x400, 0u);
    EXPECT_EQ(cpu->m_nextPc, 0x1234u);
    cpu->m_gpr[3] = copy;
    cpu->m_gpr[4] = allocation;
    call("MEMFreeToAllocator");
    EXPECT_EQ(alloc(heap, 0x68, 0x400), allocation);
}

TEST_F(ExpHeapTest, AllocatorFunctionPointersSupportIndirectCallsAndSignedAlignment)
{
    constexpr std::uint32_t heap = 0x28500000, descriptor = 0x28002000;
    createHeap(heap, 0x10000);
    cpu->m_gpr[3] = descriptor;
    cpu->m_gpr[4] = heap;
    cpu->m_gpr[5] = static_cast<std::uint32_t>(-0x100);
    call("MEMInitAllocatorForExpHeap");
    const auto table = cpu->m_memory.read<std::uint32_t>(descriptor);
    cpu->m_ctr = cpu->m_memory.read<std::uint32_t>(table);
    cpu->m_pc = 0x4560;
    cpu->m_gpr[3] = descriptor;
    cpu->m_gpr[4] = 0x100;
    EncodedInstruction branch(0);
    branch.rt = 20;
    branch.lk = 1;
    Core::Instruction::BCTR(*cpu, branch);
    EXPECT_EQ(cpu->m_gpr[3], heap + 0xFF00);
    EXPECT_EQ(cpu->m_nextPc, 0x4564u);
    cpu->m_ctr = cpu->m_memory.read<std::uint32_t>(table + 4);
    cpu->m_gpr[4] = cpu->m_gpr[3];
    cpu->m_gpr[3] = descriptor;
    Core::Instruction::BCTR(*cpu, branch);
    EXPECT_EQ(alloc(heap, 0x100, static_cast<std::uint32_t>(-0x100)), heap + 0xFF00);
}

TEST_F(ExpHeapTest, CustomAllocatorTailCallsGuestFunctionsWithOriginalArguments)
{
    constexpr std::uint32_t descriptor = 0x28003000, table = 0x28003020;
    cpu->m_memory.write<std::uint32_t>(descriptor, table);
    cpu->m_memory.write<std::uint32_t>(table, 0x02001200);
    cpu->m_memory.write<std::uint32_t>(table + 4, 0x02001400);
    cpu->m_pc = 0x1000;
    cpu->m_lr = 0x1600;
    cpu->m_gpr[3] = descriptor;
    cpu->m_gpr[4] = 0x80;
    call("MEMAllocFromAllocator");
    EXPECT_TRUE(cpu->m_hle_redirected);
    EXPECT_EQ(cpu->m_nextPc, 0x1200u);
    EXPECT_EQ(cpu->m_lr, 0x1600u);
    EXPECT_EQ(cpu->m_gpr[3], descriptor);
    EXPECT_EQ(cpu->m_gpr[4], 0x80u);
    cpu->m_gpr[4] = 0x28004000;
    call("MEMFreeToAllocator");
    EXPECT_EQ(cpu->m_nextPc, 0x1400u);
    EXPECT_EQ(cpu->m_lr, 0x1600u);
    EXPECT_EQ(cpu->m_gpr[3], descriptor);
    EXPECT_EQ(cpu->m_gpr[4], 0x28004000u);
}

TEST_F(ExpHeapTest, ReexportAttachesFunctionToCurrentInterpreter)
{
    const auto pointer = Core::Hle::exportFunction(*cpu, "__wemu_allocatorExpAlloc");
    cpu->m_importBySentinel.erase(pointer);
    EXPECT_EQ(Core::Hle::exportFunction(*cpu, "__wemu_allocatorExpAlloc"), pointer);
    ASSERT_TRUE(cpu->m_importBySentinel.contains(pointer));
    EXPECT_EQ(*cpu->m_importBySentinel.at(pointer), "__wemu_allocatorExpAlloc");
}

TEST_F(ExpHeapTest, FrameHeapSizeCanBeAllocated)
{
    constexpr std::uint32_t BASE = 0x28300000, SIZE = 0x10000;
    cpu->m_gpr[3] = BASE;
    cpu->m_gpr[4] = SIZE;
    cpu->m_gpr[5] = 0;
    call("MEMCreateFrmHeapEx");
    ASSERT_EQ(cpu->m_gpr[3], BASE);

    cpu->m_gpr[4] = 0x100;
    call("MEMGetAllocatableSizeForFrmHeapEx");
    const std::uint32_t available = cpu->m_gpr[3];
    ASSERT_GT(available, 0u);
    ASSERT_LT(available, SIZE);

    cpu->m_gpr[3] = BASE;
    cpu->m_gpr[4] = available;
    cpu->m_gpr[5] = 0x100;
    call("MEMAllocFromFrmHeapEx");
    EXPECT_EQ(cpu->m_gpr[3] % 0x100, 0u);
    EXPECT_EQ(cpu->m_gpr[3] + available, BASE + SIZE);

    cpu->m_gpr[3] = BASE;
    cpu->m_gpr[4] = 0x100;
    call("MEMGetAllocatableSizeForFrmHeapEx");
    EXPECT_EQ(cpu->m_gpr[3], 0u);
}
