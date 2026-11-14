// coreinit primitives that titles build their threading and frame loops on:
// message queues, atomics, spinlocks, block memory operations, thread-local
// slots and OSScreen text.

#include <array>
#include <cstdint>
#include <string>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "gfx/BitmapFont.hpp"
#include "hle/Coreinit.hpp"
#include "hle/CoreinitExtra.hpp"

namespace {
    constexpr std::uint32_t App = Core::Memory::MemoryMap::ApplicationCode;
    constexpr std::uint32_t Worker = 0x2000;
} // namespace

class CoreinitSyncTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_hle_redirected = false;
            cpu->m_interruptsDisabled = false;
            RegisterCoreinitFunctions();
        }

        std::uint32_t call(const char *name, std::uint32_t r3 = 0, std::uint32_t r4 = 0, std::uint32_t r5 = 0, std::uint32_t r6 = 0)
        {
            cpu->m_gpr[3] = r3;
            cpu->m_gpr[4] = r4;
            cpu->m_gpr[5] = r5;
            cpu->m_gpr[6] = r6;
            Core::syscallHandler.get(name)(*cpu);
            return cpu->m_gpr[3];
        }

        // A 16-byte OSMessage whose four words are tag, tag+1, tag+2, tag+3.
        void putMessage(std::uint32_t at, std::uint32_t tag)
        {
            for (std::uint32_t i = 0; i < 4; ++i)
                cpu->m_memory.write<std::uint32_t>(at + i * 4, tag + i);
        }

        // Tag of the message at `at`, or 0 when its four words are not tag..tag+3.
        std::uint32_t messageTag(std::uint32_t at) const
        {
            const auto tag = cpu->m_memory.read<std::uint32_t>(at);
            for (std::uint32_t i = 1; i < 4; ++i)
                if (cpu->m_memory.read<std::uint32_t>(at + i * 4) != tag + i)
                    return 0;
            return tag;
        }

        // Main thread plus one ready worker, so blocking calls have somewhere to switch to.
        void startWorker()
        {
            cpu->m_scheduler.bootstrap(*cpu);
            cpu->m_scheduler.create(*cpu, Worker, App + 0x200, 77, 0, 0xC0000000);
            cpu->m_scheduler.resume(Worker);
        }
};

TEST_F(CoreinitSyncTest, MessageQueueDeliversInOrderAndJamJumpsTheQueue)
{
    constexpr std::uint32_t Queue = 0x28100000, Buffer = 0x28101000, In = 0x28102000, Out = 0x28103000;
    call("OSInitMessageQueue", Queue, Buffer, 3);

    putMessage(In, 0x100);
    EXPECT_EQ(call("OSSendMessage", Queue, In, 0), 1u);
    putMessage(In, 0x200);
    EXPECT_EQ(call("OSSendMessage", Queue, In, 0), 1u);
    putMessage(In, 0x300);
    EXPECT_EQ(call("OSJamMessage", Queue, In, 0), 1u);

    putMessage(In, 0x400);
    EXPECT_EQ(call("OSSendMessage", Queue, In, 0), 0u) << "a full queue refuses a non-blocking send";
    EXPECT_EQ(call("OSJamMessage", Queue, In, 0), 0u) << "and a non-blocking jam";

    EXPECT_EQ(call("OSPeekMessage", Queue, Out), 1u);
    EXPECT_EQ(messageTag(Out), 0x300u) << "the jammed message is first";
    for (const std::uint32_t expected: {0x300u, 0x100u, 0x200u}) {
        EXPECT_EQ(call("OSReceiveMessage", Queue, Out, 0), 1u);
        EXPECT_EQ(messageTag(Out), expected);
    }

    putMessage(Out, 0xAAA);
    EXPECT_EQ(call("OSReceiveMessage", Queue, Out, 0), 0u);
    EXPECT_EQ(call("OSPeekMessage", Queue, Out), 0u);
    EXPECT_EQ(messageTag(Out), 0xAAAu) << "an empty queue leaves the destination alone";
}

TEST_F(CoreinitSyncTest, MessageQueueRingWrapsWithoutLosingOrCorruptingMessages)
{
    constexpr std::uint32_t Queue = 0x28110000, Buffer = 0x28111000, In = 0x28112000, Out = 0x28113000;
    call("OSInitMessageQueue", Queue, Buffer, 2);
    // Ten messages through a two-slot ring: head and tail wrap several times, jams included.
    std::uint32_t next = 1, expected = 1;
    for (int round = 0; round < 5; ++round) {
        putMessage(In, next++ << 8);
        ASSERT_EQ(call("OSSendMessage", Queue, In, 0), 1u);
        putMessage(In, next++ << 8);
        ASSERT_EQ(call(round % 2 ? "OSJamMessage" : "OSSendMessage", Queue, In, 0), 1u);
        const std::uint32_t first = round % 2 ? expected + 1 : expected;
        const std::uint32_t second = round % 2 ? expected : expected + 1;
        ASSERT_EQ(call("OSReceiveMessage", Queue, Out, 0), 1u);
        EXPECT_EQ(messageTag(Out), first << 8) << "round " << round;
        ASSERT_EQ(call("OSReceiveMessage", Queue, Out, 0), 1u);
        EXPECT_EQ(messageTag(Out), second << 8) << "round " << round;
        expected += 2;
    }
    // The ring never writes outside its two 16-byte slots.
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Buffer + 32), 0u);
}

TEST_F(CoreinitSyncTest, BlockingSendOnAFullQueueWaitsForTheReceiverAndThenDelivers)
{
    startWorker();
    const auto *main = cpu->m_scheduler.current();
    constexpr std::uint32_t Queue = 0x28120000, Buffer = 0x28121000, In = 0x28122000, Out = 0x28123000;
    call("OSInitMessageQueue", Queue, Buffer, 1);
    putMessage(In, 0x100);
    ASSERT_EQ(call("OSSendMessage", Queue, In, 0), 1u);

    cpu->m_pc = 0x1234;
    putMessage(In, 0x200);
    call("OSSendMessage", Queue, In, 1); // OS_MESSAGE_FLAGS_BLOCKING on a full queue
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), Worker) << "the sender parks and the worker runs";
    EXPECT_EQ(main->state, Core::ThreadContext::State::Waiting);
    EXPECT_EQ(main->pc, 0x1234u) << "the send is retried from its own call site";

    ASSERT_EQ(call("OSReceiveMessage", Queue, Out, 0), 1u);
    EXPECT_EQ(messageTag(Out), 0x100u);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Ready) << "freeing a slot wakes the sender";

    cpu->m_scheduler.yield(*cpu);
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), main->osThreadPtr);
    cpu->m_hle_redirected = false;
    EXPECT_EQ(call("OSSendMessage", Queue, In, 1), 1u);
    ASSERT_EQ(call("OSReceiveMessage", Queue, Out, 0), 1u);
    EXPECT_EQ(messageTag(Out), 0x200u);
}

TEST_F(CoreinitSyncTest, CompareAndSwapOnlyStoresWhenTheExpectedValueIsThere)
{
    constexpr std::uint32_t Word = 0x28130000, Old = 0x28130010;
    cpu->m_memory.write<std::uint32_t>(Word, 5);
    EXPECT_EQ(call("OSCompareAndSwapAtomic", Word, 4, 9), 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Word), 5u);
    EXPECT_EQ(call("OSCompareAndSwapAtomic", Word, 5, 9), 1u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Word), 9u);

    // The Ex form also reports what it found, on success and on failure.
    EXPECT_EQ(call("OSCompareAndSwapAtomicEx", Word, 1, 2, Old), 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Old), 9u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Word), 9u);
    EXPECT_EQ(call("OSCompareAndSwapAtomicEx", Word, 9, 12, Old), 1u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Old), 9u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Word), 12u);
    EXPECT_EQ(call("OSCompareAndSwapAtomicEx", Word, 12, 13, 0), 1u) << "a null old-value pointer is allowed";
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Word), 13u);
}

TEST_F(CoreinitSyncTest, BlockMoveHandlesOverlapBothWaysAndReturnsTheDestination)
{
    constexpr std::uint32_t Block = 0x28140000;
    const auto fill = [&] {
        for (std::uint32_t i = 0; i < 8; ++i)
            cpu->m_memory.write<std::uint8_t>(Block + i, static_cast<std::uint8_t>('A' + i)); // ABCDEFGH
    };
    const auto text = [&] { return std::string(reinterpret_cast<const char *>(cpu->m_memory.hostPtr(Block)), 8); };

    fill();
    EXPECT_EQ(call("OSBlockMove", Block + 2, Block, 5, 0), Block + 2);
    EXPECT_EQ(text(), "ABABCDEH") << "forward overlap must copy like memmove";
    fill();
    EXPECT_EQ(call("OSBlockMove", Block, Block + 2, 5, 0), Block);
    EXPECT_EQ(text(), "CDEFGFGH") << "backward overlap must copy like memmove";
    fill();
    call("OSBlockMove", Block, Block + 4, 0, 0);
    EXPECT_EQ(text(), "ABCDEFGH") << "size 0 copies nothing";
}

TEST_F(CoreinitSyncTest, BlockSetUsesOnlyTheLowByteAndStopsAtTheGivenSize)
{
    constexpr std::uint32_t Block = 0x28150000;
    for (std::uint32_t i = 0; i < 8; ++i)
        cpu->m_memory.write<std::uint8_t>(Block + i, 0xEE);
    EXPECT_EQ(call("OSBlockSet", Block + 1, 0x1234, 6), Block + 1);
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(Block), 0xEEu);
    for (std::uint32_t i = 1; i < 7; ++i)
        EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(Block + i), 0x34u) << i;
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(Block + 7), 0xEEu);
}

TEST_F(CoreinitSyncTest, NestedSpinLockRestoresInterruptsOnlyAtTheOutermostRelease)
{
    cpu->m_scheduler.bootstrap(*cpu);
    constexpr std::uint32_t Lock = 0x28160000;
    ASSERT_FALSE(cpu->m_interruptsDisabled);
    EXPECT_EQ(call("OSUninterruptibleSpinLock_Acquire", Lock), 1u);
    EXPECT_TRUE(cpu->m_interruptsDisabled);
    EXPECT_EQ(call("OSUninterruptibleSpinLock_TryAcquire", Lock), 1u) << "the owner may re-enter";
    call("OSUninterruptibleSpinLock_Release", Lock);
    EXPECT_TRUE(cpu->m_interruptsDisabled) << "still inside the outer critical section";
    call("OSUninterruptibleSpinLock_Release", Lock);
    EXPECT_FALSE(cpu->m_interruptsDisabled) << "interrupts left disabled would stop preemption for good";

    // A caller that already had interrupts off keeps them off after its critical section.
    cpu->m_interruptsDisabled = true;
    call("OSUninterruptibleSpinLock_Acquire", Lock);
    call("OSUninterruptibleSpinLock_Release", Lock);
    EXPECT_TRUE(cpu->m_interruptsDisabled);
}

TEST_F(CoreinitSyncTest, SpinLockHeldByAnotherThreadCannotBeTakenOrReleasedFromOutside)
{
    startWorker();
    constexpr std::uint32_t Lock = 0x28170000;
    ASSERT_EQ(call("OSUninterruptibleSpinLock_Acquire", Lock), 1u);
    cpu->m_interruptsDisabled = false; // the worker runs with its own interrupt state
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), Worker);

    EXPECT_EQ(call("OSUninterruptibleSpinLock_TryAcquire", Lock), 0u);
    EXPECT_FALSE(cpu->m_interruptsDisabled) << "a failed try-acquire changes nothing";
    cpu->m_interruptsDisabled = true;
    call("OSUninterruptibleSpinLock_Release", Lock);
    EXPECT_TRUE(cpu->m_interruptsDisabled) << "releasing a lock you do not hold must not restore someone else's state";
    EXPECT_EQ(call("OSUninterruptibleSpinLock_TryAcquire", Lock), 0u) << "and must not free the lock";
}

TEST_F(CoreinitSyncTest, ThreadSpecificSlotsArePerThreadAndBounded)
{
    startWorker();
    call("wut_set_thread_specific", 3, 0xAAAA0003);
    call("wut_set_thread_specific", 63, 0xAAAA003F);
    call("wut_set_thread_specific", 64, 0xBAD); // out of range: ignored
    EXPECT_EQ(call("wut_get_thread_specific", 3), 0xAAAA0003u);
    EXPECT_EQ(call("wut_get_thread_specific", 63), 0xAAAA003Fu);
    EXPECT_EQ(call("wut_get_thread_specific", 64), 0u);

    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), Worker);
    EXPECT_EQ(call("wut_get_thread_specific", 3), 0u) << "another thread sees its own slots";
    call("wut_set_thread_specific", 3, 0xBBBB0003);
    EXPECT_EQ(call("wut_get_thread_specific", 3), 0xBBBB0003u);
}

TEST_F(CoreinitSyncTest, ScreenTextDrawsGlyphPixelsAndClipsAtTheVisibleEdge)
{
    constexpr std::uint32_t Tv = 0x29000000, Drc = 0x2A000000, Text = 0x28180000;
    constexpr std::uint32_t Background = 0x11223344;
    call("OSScreenInit");
    call("OSScreenSetBufferEx", 0, Tv);
    call("OSScreenSetBufferEx", 1, Drc);
    call("OSScreenClearBufferEx", 0, Background);
    call("OSScreenClearBufferEx", 1, Background);
    const auto pixel = [&](std::uint32_t base, std::uint32_t pitch, std::uint32_t x, std::uint32_t y) {
        return cpu->m_memory.read<std::uint32_t>(base + (y * pitch + x) * 4);
    };
    const auto put = [&](std::uint32_t screen, std::uint32_t column, std::uint32_t row, const std::string &text) {
        for (std::size_t i = 0; i <= text.size(); ++i)
            cpu->m_memory.write<std::uint8_t>(Text + static_cast<std::uint32_t>(i), i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0);
        call("OSScreenPutFontEx", screen, column, row, Text);
    };

    // "A\xC3" at column 2, row 1: 'A' at x 16..23, the non-ASCII byte drawn as '?' at x 24..31.
    put(0, 2, 1, "A\xC3");
    const auto checkGlyph = [&](char glyph, std::uint32_t left) {
        for (std::uint32_t y = 0; y < 16; ++y)
            for (std::uint32_t x = 0; x < 8; ++x) {
                const bool on = Core::Gfx::kFont8x16[static_cast<unsigned char>(glyph)][y] & (0x80 >> x);
                ASSERT_EQ(pixel(Tv, 1280, left + x, 16 + y), on ? 0xFFFFFFFFu : Background) << glyph << " at " << x << "," << y;
            }
    };
    checkGlyph('A', 16);
    checkGlyph('?', 24);
    EXPECT_EQ(pixel(Tv, 1280, 32, 16), Background) << "nothing past the string";

    // GamePad: 854 visible pixels in an 896-pixel pitch. A glyph at x 848 is cut at 854.
    bool glyphReachesTheCut = false; // 'M' must have pixels in columns 6-7 for the clip to matter
    for (std::uint32_t y = 0; y < 16; ++y)
        glyphReachesTheCut |= (Core::Gfx::kFont8x16['M'][y] & 0x03) != 0;
    ASSERT_TRUE(glyphReachesTheCut);
    put(1, 106, 0, "M");
    bool drewVisible = false;
    for (std::uint32_t y = 0; y < 16; ++y) {
        for (std::uint32_t x = 848; x < 854; ++x)
            drewVisible |= pixel(Drc, 896, x, y) == 0xFFFFFFFFu;
        for (std::uint32_t x = 854; x < 896; ++x)
            ASSERT_EQ(pixel(Drc, 896, x, y), Background) << "padding column " << x << " was drawn";
    }
    EXPECT_TRUE(drewVisible);

    call("OSScreenPutFontEx", 2, 0, 0, Text); // no third screen: nothing happens
    EXPECT_EQ(pixel(Tv, 1280, 0, 0), Background);
}

TEST_F(CoreinitSyncTest, NamedSemaphoreCountsDownWithoutGoingNegativeAndSignalWakesAWaiter)
{
    RegisterCoreinitExtraFunctions();
    startWorker();
    const auto *main = cpu->m_scheduler.current();
    constexpr std::uint32_t Sem = 0x28190000, Name = 0x28190100;
    call("OSInitSemaphoreEx", Sem, 2, Name);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Sem), 0x73506852u) << "the 'sPhR' tag titles check";
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Sem + 4), Name);
    EXPECT_EQ(call("OSGetSemaphoreCount", Sem), 2u);

    EXPECT_EQ(call("OSTryWaitSemaphore", Sem), 2u) << "returns the count before taking one";
    EXPECT_EQ(call("OSTryWaitSemaphore", Sem), 1u);
    EXPECT_EQ(call("OSTryWaitSemaphore", Sem), 0u) << "empty: nothing taken";
    EXPECT_EQ(call("OSGetSemaphoreCount", Sem), 0u) << "the count never goes negative";

    call("OSWaitSemaphore", Sem); // empty: main blocks, the worker runs
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), Worker);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Waiting);
    cpu->m_hle_redirected = false;
    EXPECT_EQ(call("OSSignalSemaphore", Sem), 0u) << "returns the count before the signal";
    EXPECT_EQ(main->state, Core::ThreadContext::State::Ready) << "the signal wakes the waiter";
    EXPECT_EQ(call("OSGetSemaphoreCount", Sem), 1u) << "the woken waiter takes the unit when it retries";
}

TEST_F(CoreinitSyncTest, MutexTryLockFailsForOtherThreadsAndReinitialisingReleasesIt)
{
    startWorker();
    const auto main = cpu->m_scheduler.currentHandle();
    constexpr std::uint32_t Mutex = 0x281A0000;
    call("OSInitMutex", Mutex);
    EXPECT_EQ(call("OSGetCurrentThread"), main);
    EXPECT_EQ(call("OSTryLockMutex", Mutex), 1u);
    EXPECT_EQ(call("OSTryLockMutex", Mutex), 1u) << "recursive for its owner";

    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    ASSERT_EQ(call("OSGetCurrentThread"), Worker);
    EXPECT_EQ(call("OSTryLockMutex", Mutex), 0u) << "held by main";

    // OSInitMutexEx on a held mutex starts it over: nobody owns it afterwards.
    call("OSInitMutexEx", Mutex, 0x281A0100);
    EXPECT_EQ(call("OSTryLockMutex", Mutex), 1u);
}

TEST_F(CoreinitSyncTest, ConditionVariableWaitReleasesTheMutexUntilSignalled)
{
    RegisterCoreinitExtraFunctions();
    startWorker();
    const auto *main = cpu->m_scheduler.current();
    constexpr std::uint32_t Mutex = 0x281B0000, Cond = 0x281B0100;
    call("OSInitMutex", Mutex);
    EXPECT_EQ(call("OSInitCond", Cond), 0u);
    call("OSLockMutex", Mutex);
    call("OSWaitCond", Cond, Mutex); // main sleeps on the condition and gives up the mutex
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), Worker);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Waiting);
    EXPECT_EQ(call("OSTryLockMutex", Mutex), 1u) << "the waiter released the mutex";
    call("OSUnlockMutex", Mutex);
    cpu->m_hle_redirected = false;
    call("OSSignalCond", Cond);
    EXPECT_NE(main->state, Core::ThreadContext::State::Waiting) << "the signal wakes the waiter";
}
