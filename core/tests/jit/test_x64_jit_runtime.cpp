/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_x64_jit_runtime -- block entry, exits and invalidation of the x86-64 recompiler
*/

#include "jit/JitHarness.hpp"

#ifdef WEMU_HAS_X64_JIT

using Core::Jit::ExitReason;

TEST_F(X64JitTest, IntegerAndMemoryBlockMatchesInterpreter)
{
    Random rng(1);
    const std::array<std::uint32_t, 8> words{
            0x38630001, // addi r3,r3,1
            0x7CA32214, // add r5,r3,r4
            0x54A6402E, // rlwinm r6,r5,8,0,23
            0x90A70010, // stw r5,16(r7)
            0x81070010, // lwz r8,16(r7)
            0x7D284B78, // mr r8,r9
            0x2C030000, // cmpwi r3,0
            0x48000010, // b +16
    };
    for (int round = 0; round < 32; ++round)
        expectSameBlock(words, [&] {
            randomize(rng);
            cpu->m_gpr[7] = kData + (rng() & 0xFF0);
        });
}

TEST_F(X64JitTest, BranchTargetsMatchInterpreter)
{
    Random rng(2);
    for (const std::uint32_t branch: {0x41820010u, 0x4082FFF0u, 0x4E800020u, 0x4E800420u, 0x4E800021u, 0x42000008u, 0x48000005u}) {
        const std::array<std::uint32_t, 2> words{0x38600005, branch};
        for (int round = 0; round < 8; ++round)
            expectSameBlock(words, [&] {
                randomize(rng);
                cpu->m_ctr = (rng() & 1) ? 1 : kApp + 0x200000;
            });
    }
}

TEST_F(X64JitTest, LoopsChainUntilTheServiceTick)
{
    const std::array<std::uint32_t, 2> loop{0x38630001, 0x4BFFFFFC}; // addi r3,r3,1 ; b -4
    load(loop);
    cpu->m_pc = kCode;
    cpu->m_gpr[3] = 0;
    std::uint64_t retired = 0, lastStore = 0;
    const auto exit = enter(retired, 100, lastStore);
    EXPECT_EQ(exit.reason, ExitReason::Budget);
    EXPECT_EQ(exit.pc, kCode);
    EXPECT_EQ(retired, 100u);
    EXPECT_EQ(cpu->m_gpr[3], 50u);
}

TEST_F(X64JitTest, StoresIntoDecodedCodeLeaveRightAfterTheStore)
{
    const std::array<std::uint32_t, 3> words{0x90640000, 0x38A00001, 0x48000004}; // stw r3,0(r4) ; li r5,1 ; b +4
    load(words);
    cpu->m_pc = kCode;
    cpu->m_gpr[3] = 0x38A00007; // li r5,7
    cpu->m_gpr[4] = kApp + kCode + 4;
    cpu->m_gpr[5] = 0;
    std::uint64_t retired = 0, lastStore = 0;
    const auto exit = enter(retired, 1000, lastStore);
    EXPECT_EQ(exit.reason, ExitReason::Done);
    EXPECT_EQ(exit.pc, kCode + 4);
    EXPECT_TRUE(exit.stores);
    EXPECT_EQ(retired, 1u);
    EXPECT_EQ(cpu->m_gpr[5], 0u);
    EXPECT_FALSE(cpu->m_memory.codeWritten());

    // The interpreter loop then sees the new epoch and runs the replaced instruction.
    settle(exit, retired);
    const auto next = enter(retired, 1000, lastStore);
    EXPECT_EQ(next.reason, ExitReason::LookupMiss);
    EXPECT_EQ(cpu->m_gpr[5], 7u);
}

TEST_F(X64JitTest, StorelessLoopsLeaveAtTheSpinThreshold)
{
    const std::array<std::uint32_t, 2> loop{0x38630001, 0x4BFFFFFC};
    load(loop);
    cpu->m_pc = kCode;
    jit().setSpinThreshold(10);
    std::uint64_t retired = 0, lastStore = 0;
    const auto exit = enter(retired, 1000, lastStore);
    EXPECT_EQ(exit.reason, ExitReason::Done);
    EXPECT_FALSE(exit.stores);
    EXPECT_EQ(exit.pc, kCode);
    EXPECT_EQ(retired, 10u);
}

TEST_F(X64JitTest, BlocksWithStoresRecordTheirRetiredCount)
{
    const std::array<std::uint32_t, 3> loop{0x90640000, 0x38630001, 0x4BFFFFF8}; // stw r3,0(r4) ; addi ; b -8
    load(loop);
    cpu->m_pc = kCode;
    cpu->m_gpr[4] = kData;
    jit().setSpinThreshold(10);
    std::uint64_t retired = 0, lastStore = 0;
    const auto exit = enter(retired, 300, lastStore);
    EXPECT_EQ(exit.reason, ExitReason::Budget);
    EXPECT_EQ(retired, 300u);
    EXPECT_EQ(lastStore, retired);
}

TEST_F(X64JitTest, CodeChangesRevalidateAndRecompile)
{
    const std::array<std::uint32_t, 2> first{0x38600001, 0x48000008}; // li r3,1 ; b +8
    load(first);
    cpu->m_pc = kCode;
    std::uint64_t retired = 0, lastStore = 0;
    EXPECT_EQ(enter(retired, 1000, lastStore).reason, ExitReason::LookupMiss);
    EXPECT_EQ(cpu->m_gpr[3], 1u);

    // Same words written again: a new epoch, but the block survives revalidation.
    load(first);
    cpu->m_pc = kCode;
    auto exit = enter(retired, 1000, lastStore, false);
    ASSERT_EQ(exit.reason, ExitReason::Revalidate);
    jit().revalidate(exit.pc);
    EXPECT_EQ(jit().stats().revalidated, 1u);
    EXPECT_EQ(enter(retired, 1000, lastStore).reason, ExitReason::LookupMiss);

    // Different words: dropped, then compiled again from live memory.
    const std::array<std::uint32_t, 2> second{0x38600002, 0x48000008};
    load(second);
    cpu->m_pc = kCode;
    exit = enter(retired, 1000, lastStore, false);
    ASSERT_EQ(exit.reason, ExitReason::Revalidate);
    jit().revalidate(exit.pc);
    EXPECT_EQ(jit().stats().recompiled, 1u);
    EXPECT_FALSE(jit().prepare(kCode));
    EXPECT_EQ(enter(retired, 1000, lastStore).reason, ExitReason::LookupMiss);
    EXPECT_EQ(cpu->m_gpr[3], 2u);
}

TEST_F(X64JitTest, UnmappedAccessFollowsTheInterpreterFaultPolicy)
{
    const std::array<std::uint32_t, 3> words{0x38A00001, 0x80640000, 0x38A00002}; // li r5,1 ; lwz r3,0(r4) ; li r5,2
    cpu->m_scheduler.bootstrap(*cpu); // the fault hits the main thread
    expectSameBlock(words, [&] { cpu->m_gpr[4] = 0x100; });
    load(words);
    cpu->m_pc = kCode;
    cpu->m_gpr[4] = 0x100;
    std::uint64_t retired = 0, lastStore = 0;
    // The load leaves before itself; the interpreter then raises the fault on it.
    const auto exit = enter(retired, 1000, lastStore);
    EXPECT_EQ(exit.reason, ExitReason::Suffix);
    EXPECT_EQ(exit.pc, kCode + 4);
    EXPECT_EQ(exit.index, 1u);
    EXPECT_EQ(retired, 1u);
    EXPECT_TRUE(cpu->m_running);
    settle(exit, retired);
    EXPECT_EQ(retired, 2u);
    EXPECT_FALSE(cpu->m_running);
    EXPECT_TRUE(cpu->m_failed);
    EXPECT_EQ(cpu->m_gpr[5], 1u);
}

TEST_F(X64JitTest, ImportCallsContinueInCompiledCode)
{
    const std::array<std::uint32_t, 1> bctr{0x4E800420};
    expectSameBlock(bctr, [&] {
        cpu->m_ctr = 0xC0000010;
        cpu->m_lr = kCode + 0x40;
    });
    load(bctr);
    cpu->m_pc = kCode;
    cpu->m_ctr = 0xC0000010;
    cpu->m_lr = kCode + 0x40;
    std::uint64_t retired = 0, lastStore = 0;
    // The import runs through the interpreter's handler and compiled code goes on at LR.
    const auto exit = enter(retired, 1000, lastStore);
    EXPECT_EQ(exit.reason, ExitReason::LookupMiss);
    EXPECT_EQ(exit.pc, kCode + 0x40);
    EXPECT_EQ(retired, 1u);
}

TEST_F(X64JitTest, NativeFamiliesDoNotCallOut)
{
    // One instruction of each native family, then a branch: none may fall back to its call-out.
    const std::array<std::uint32_t, 14> words{
            0x7CA32214, // add r5,r3,r4
            0x2C030000, // cmpwi r3,0
            0x7C6802A6, // mflr r3
            0x80640010, // lwz r3,16(r4)
            0x90640010, // stw r3,16(r4)
            0xFC22182A, // fadd f1,f2,f3
            0xEC22182A, // fadds f1,f2,f3
            0xC0240010, // lfs f1,16(r4)
            0x1022182A, // ps_add f1,f2,f3
            0xE0240010, // psq_l f1,16(r4),0,0
            0x7C6B1B78, // mr r11,r3
            0xBBC10008, // lmw r30,8(r1)
            0xBFC10008, // stmw r30,8(r1)
            0x48000008, // b +8
    };
    load(words);
    const auto &b = block(kCode);
    ASSERT_EQ(b.instrs.size(), words.size());
    std::vector<Core::Jit::GuestInstruction> guest;
    for (const auto &i: b.instrs)
        guest.push_back({i.instr.raw, i.id});
    ASSERT_TRUE(jit().compile(kCode, guest, b.stores));
    EXPECT_EQ(jit().stats().instructions, words.size());
    EXPECT_EQ(jit().stats().callouts, 0u);
}

TEST_F(X64JitTest, DirectExitsLinkToCompiledBlocks)
{
    // A: addi r3,r3,1 ; b B    B: addi r3,r3,2 ; b A    (A at kCode, B at kCode + 0x40)
    load(std::array<std::uint32_t, 2>{0x38630001, 0x4800003C});
    load(std::array<std::uint32_t, 2>{0x38630002, 0x4BFFFFBC}, kCode + 0x40);
    cpu->m_pc = kCode;
    cpu->m_gpr[3] = 0;
    std::uint64_t retired = 0, lastStore = 0;
    const auto first = enter(retired, 1000, lastStore);
    EXPECT_EQ(first.reason, ExitReason::LookupMiss); // B not compiled yet
    settle(first, retired);
    EXPECT_EQ(cpu->m_pc, kCode + 0x40);
    const auto exit = enter(retired, 10, lastStore); // compiles B, which links both ways
    EXPECT_EQ(exit.reason, ExitReason::Budget);
    EXPECT_EQ(exit.pc, kCode + 0x40);
    EXPECT_EQ(retired, 10u);
    EXPECT_EQ(cpu->m_gpr[3], 7u); // A B A B A
    EXPECT_GE(jit().stats().links, 2u);

    // Rewriting B unlinks it: A's jump goes back through the lookup and B is recompiled.
    load(std::array<std::uint32_t, 2>{0x38630004, 0x4BFFFFBC}, kCode + 0x40);
    cpu->m_pc = kCode;
    cpu->m_gpr[3] = 0;
    retired = 0;
    EXPECT_EQ(enter(retired, 10, lastStore).reason, ExitReason::Budget);
    EXPECT_EQ(cpu->m_gpr[3], 11u); // 1 + 4 + 1 + 4 + 1
    EXPECT_EQ(jit().stats().recompiled, 1u);
}

TEST_F(X64JitTest, HooksRunFromCompiledCode)
{
    // bl hook ; addi r3,r3,1 ; b +8, the hook adds 10 to r3 and returns.
    load(std::array<std::uint32_t, 3>{0x48000041, 0x38630001, 0x48000008});
    cpu->m_hooks[kApp + kCode + 0x40] = [](Core::Interpreter &c) { c.m_gpr[3] += 10; };
    cpu->m_hooks_min = cpu->m_hooks_max = kApp + kCode + 0x40;
    jit().installHook(kCode + 0x40);
    cpu->m_pc = kCode;
    cpu->m_gpr[3] = 0;
    std::uint64_t retired = 0, lastStore = 0;
    jit().sync(cpu->m_memory.codeEpoch(), cpu->m_hooks.size());
    jit().installHook(kCode + 0x40);
    auto exit = enter(retired, 1000, lastStore);
    EXPECT_EQ(exit.reason, ExitReason::LookupMiss); // returned to LR, not compiled yet
    EXPECT_EQ(exit.pc, kCode + 4);
    EXPECT_EQ(cpu->m_gpr[3], 10u);
    EXPECT_EQ(retired, 1u);
    cpu->m_pc = exit.pc;
    exit = enter(retired, 1000, lastStore);
    EXPECT_EQ(exit.pc, kCode + 16);
    EXPECT_EQ(cpu->m_gpr[3], 11u);
    EXPECT_EQ(retired, 3u);
    cpu->m_hooks.clear();
    cpu->m_hooks_min = 0xFFFFFFFFu;
    cpu->m_hooks_max = 0;
}

TEST_F(X64JitTest, HookChangesDropCompiledCode)
{
    const std::array<std::uint32_t, 2> words{0x38600001, 0x48000008};
    load(words);
    cpu->m_pc = kCode;
    std::uint64_t retired = 0, lastStore = 0;
    enter(retired, 1000, lastStore);
    EXPECT_TRUE(jit().prepare(kCode));
    jit().sync(cpu->m_memory.codeEpoch(), cpu->m_hooks.size() + 1);
    EXPECT_FALSE(jit().prepare(kCode));
    EXPECT_EQ(jit().stats().flushes, 1u);
}

#endif
