#include <random>

#include "TestFixture.hpp"
#include "cpu/recompiler/Block.hpp"
#ifdef WEMU_HAS_LLVM
    #include "cpu/recompiler/Dispatcher.hpp"
    #include "cpu/recompiler/LlvmBackend.hpp"
#endif

using namespace Core::Ppc;

TEST(PpcFetchTest, BorrowedDecoderObservesWritesAndRejectsTruncatedWords)
{
    std::array<char, 4> bytes{char(0x38), char(0x60), 0, 1};
    Utils::BeDecoder reader{std::span<const char>(bytes)};
    EXPECT_EQ(reader.extractSwap<std::uint32_t>(), 0x38600001u);
    bytes[3] = 2;
    reader.seek(0);
    EXPECT_EQ(reader.extractSwap<std::uint32_t>(), 0x38600002u);
    reader.seek(1);
    EXPECT_THROW(reader.extractSwap<std::uint32_t>(), Utils::BeDecoderException);
}

class PpcFetchInterpreterTest : public InstructionTest {};

TEST_F(PpcFetchInterpreterTest, ObservedPcsBoundReferenceBlocks)
{
    constexpr unsigned app = Core::Memory::MemoryMap::ApplicationCode;
    Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
    const std::array<std::uint32_t, 4> words{0x38600001, 0x38800002, 0x38A00003, 0x48000004};
    for (unsigned i = 0; i < words.size(); ++i)
        cpu->m_memory.write<std::uint32_t>(app + i * 4, words[i]);
    cpu->m_observedPcs = {app + 8};
    const auto &before = cpu->getBlock(reader, 0);
    ASSERT_EQ(before.instrs.size(), 2u);
    cpu->m_pc = 0;
    cpu->runBlock(before);
    EXPECT_EQ(cpu->m_pc, 8u);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(cpu->m_gpr[4], 2u);
    EXPECT_EQ(cpu->m_gpr[5], 0u);
}

#ifdef WEMU_HAS_LLVM
TEST_F(PpcFetchInterpreterTest, ObservedPcsBoundNativeLookaheadAndPreventCollapsedLoopHits)
{
    constexpr unsigned app = Core::Memory::MemoryMap::ApplicationCode;
    Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
    const std::array<std::uint32_t, 6> words{0x38600001, 0x90640000, 0x38630001, 0x38630001, 0x38630001, 0x4BFFFFEC};
    for (unsigned i = 0; i < words.size(); ++i)
        cpu->m_memory.write<std::uint32_t>(app + i * 4, words[i]);
    cpu->m_observedPcs = {app + 12};
    const auto &before = cpu->getBlock(reader, 0, true);
    EXPECT_EQ(before.nativeWords.size(), 3u);
    EXPECT_EQ(cpu->getBlock(reader, 12, true).nativeWords.size(), 0u);
    cpu->clearBlockCache();
    cpu->m_observedPcs = {app};
    const auto &start = cpu->getBlock(reader, 0, true);
    EXPECT_TRUE(start.nativeWords.empty());
    // Without lookahead the interpreted block runs past the store to the loop branch: runBlock()
    // leaves it after a store into decoded code.
    EXPECT_EQ(start.instrs.size(), 6u);
}

TEST_F(PpcFetchInterpreterTest, RecordsUnsupportedPrefixBoundaryWithoutNativeExecution)
{
    constexpr unsigned app = Core::Memory::MemoryMap::ApplicationCode;
    Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
    const std::array<std::uint32_t, 4> words{0x38600001, 0x38800002, 0xFC63282A, 0x48000004};
    for (unsigned i = 0; i < words.size(); ++i)
        cpu->m_memory.write<std::uint32_t>(app + i * 4, words[i]);
    const auto &two = cpu->getBlock(reader, 0);
    EXPECT_EQ(two.nativePrefixLength, 2u);
    EXPECT_EQ(two.nativeWords.size(), 2u);
    EXPECT_EQ(two.instrs[two.nativePrefixLength].instr.raw >> 26, 63u); // fadd remains outside the integer ABI
    cpu->m_memory.write<std::uint32_t>(app + 4, words[2]);
    const auto &one = cpu->getBlock(reader, 0);
    EXPECT_EQ(one.nativePrefixLength, 1u);
    EXPECT_TRUE(one.nativeWords.empty());
    cpu->m_memory.write<std::uint32_t>(app, words[2]);
    const auto &zero = cpu->getBlock(reader, 0);
    EXPECT_EQ(zero.nativePrefixLength, 0u);
    EXPECT_TRUE(zero.nativeWords.empty());
}
#endif

TEST_F(PpcFetchInterpreterTest, RevalidatesCachedCodeAndLeavesBlocksAfterCodeStores)
{
    constexpr unsigned app = Core::Memory::MemoryMap::ApplicationCode;
    Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
    cpu->m_memory.write<std::uint32_t>(app, 0x38600001); // li r3,1
    cpu->m_memory.write<std::uint32_t>(app + 4, 0x48000004); // b +4
    ASSERT_EQ(cpu->getBlock(reader, 0).instrs[0].instr.raw, 0x38600001u);
    cpu->m_memory.write<std::uint32_t>(app, 0x38600002);
    const auto &updated = cpu->getBlock(reader, 0);
    ASSERT_EQ(updated.instrs[0].instr.raw, 0x38600002u);
    cpu->runBlock(updated);
    EXPECT_EQ(cpu->m_gpr[3], 2u);

    cpu->m_pc = 0;
    cpu->m_gpr[4] = app + 4;
    cpu->m_gpr[3] = 0x38A00007; // replacement: li r5,7
    cpu->m_memory.write<std::uint32_t>(app, 0x90640000); // stw r3,0(r4)
    cpu->m_memory.write<std::uint32_t>(app + 4, 0x38A00001);
    cpu->m_memory.write<std::uint32_t>(app + 8, 0x48000004);
    const auto &store = cpu->getBlock(reader, 0);
    ASSERT_EQ(store.instrs.size(), 3u); // blocks continue past stores...
    EXPECT_EQ(cpu->runBlock(store), 1u); // ...but leave right after one that hits decoded code
    EXPECT_EQ(cpu->m_pc, 4u);
    const auto &following = cpu->getBlock(reader, 4);
    ASSERT_EQ(following.instrs[0].instr.raw, 0x38A00007u);
    cpu->runBlock(following);
    EXPECT_EQ(cpu->m_gpr[5], 7u);
}

TEST_F(PpcFetchInterpreterTest, DataStoresDoNotEndDecodedBlocks)
{
    constexpr unsigned app = Core::Memory::MemoryMap::ApplicationCode;
    Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
    cpu->m_memory.write<std::uint32_t>(app, 0x90640000); // stw r3,0(r4)
    cpu->m_memory.write<std::uint32_t>(app + 4, 0x38A00001); // li r5,1
    cpu->m_memory.write<std::uint32_t>(app + 8, 0x48000004); // b +4
    cpu->m_pc = 0;
    cpu->m_gpr[3] = 0x12345678;
    cpu->m_gpr[4] = app + 0x10000; // a different page than the decoded code
    const auto &block = cpu->getBlock(reader, 0);
    ASSERT_EQ(block.instrs.size(), 3u);
    cpu->m_memory.clearCodeWritten(); // the setup writes above touched the code page
    EXPECT_EQ(cpu->runBlock(block), 3u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(app + 0x10000), 0x12345678u);
    EXPECT_EQ(cpu->m_gpr[5], 1u);
    EXPECT_EQ(cpu->m_pc, 12u);
    EXPECT_EQ(&cpu->getBlock(reader, 0), &block); // unchanged words reuse the cached version
}

TEST(PpcBlockTest, RejectsUnsupportedAndInvalidBlocks)
{
    EXPECT_FALSE(Block::decode(0, {}));
    const std::array<std::uint32_t, 1> add{0x38600001};
    EXPECT_FALSE(Block::decode(1, add));
    EXPECT_FALSE(Block::decode(0xFFFFFFFC, add));
    std::array<std::uint32_t, 65> tooLong{};
    tooLong.fill(add[0]);
    EXPECT_FALSE(Block::decode(0, tooLong));
    for (const auto unsupported: {0u, 0x4E800420u, 0xFC63282Au, 0x44000002u}) {
        const std::array words{add[0], unsupported};
        EXPECT_FALSE(Block::decode(0x02000000, words));
    }
    ASSERT_TRUE(Block::decode(0x02000000, add));
}

TEST(PpcBlockTest, SupportsRecordFormsButRejectsUnimplementedCarryAndOverflowForms)
{
    for (unsigned xo: {266u, 40u, 235u, 28u, 444u, 316u, 24u, 536u}) {
        const std::uint32_t word = (31u << 26) | (3u << 21) | (4u << 16) | (5u << 11) | (xo << 1);
        EXPECT_TRUE(Block::supports(word));
        EXPECT_TRUE(Block::supports(word | 1));
        if (xo == 266 || xo == 40 || xo == 235)
            EXPECT_FALSE(Block::supports(word | 0x400));
    }
    EXPECT_TRUE(Block::supports(0x5463083C)); // rlwinm
    EXPECT_TRUE(Block::supports(0x5463083D));
    for (unsigned opcode: {20u, 23u}) {
        const auto word = (opcode << 26) | (3u << 21) | (4u << 16) | (5u << 11) | (31u << 1);
        EXPECT_TRUE(Block::supports(word));
        EXPECT_TRUE(Block::supports(word | 1));
    }
    EXPECT_FALSE(Block::supports(0x7C642E30)); // sraw updates carry even with Rc=0
    EXPECT_FALSE(Block::supports(0x7C642814)); // addc updates carry even with Rc=0
    EXPECT_TRUE(Block::supports(0x706300FF)); // andi. records through the state ABI
}

#ifdef WEMU_HAS_LLVM
class PpcNativeTest : public InstructionTest {};

TEST_F(PpcFetchInterpreterTest, NativePrefixFollowsLiveCodeAndHookBoundaries)
{
    constexpr unsigned app = Core::Memory::MemoryMap::ApplicationCode;
    Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
    cpu->m_memory.write<std::uint32_t>(app, 0x38600001);
    cpu->m_memory.write<std::uint32_t>(app + 4, 0x38630002);
    cpu->m_memory.write<std::uint32_t>(app + 8, 0x48000004);
    EXPECT_EQ(cpu->getBlock(reader, 0).nativeWords, (std::vector<std::uint32_t>{0x38600001, 0x38630002, 0x48000004}));
    cpu->m_memory.write<std::uint32_t>(app + 4, 0x90640000); // a new decoded version, which continues past the store
    EXPECT_EQ(cpu->getBlock(reader, 0).nativeWords, (std::vector<std::uint32_t>{0x38600001, 0x90640000, 0x48000004}));
    cpu->m_memory.write<std::uint32_t>(app + 4, 0x38630003);
    EXPECT_EQ(cpu->getBlock(reader, 0).nativeWords, (std::vector<std::uint32_t>{0x38600001, 0x38630003, 0x48000004}));

    cpu->clearBlockCache();
    cpu->m_hooks_min = cpu->m_hooks_max = app + 4;
    cpu->m_hooks[app + 4] = [](Core::Interpreter &) {};
    const auto &hookBounded = cpu->getBlock(reader, 0);
    EXPECT_EQ(hookBounded.instrs.size(), 1u);
    EXPECT_TRUE(hookBounded.nativeWords.empty());
    cpu->m_hooks.erase(app + 4);
    cpu->m_hooks_min = 0xFFFFFFFFu;
    cpu->m_hooks_max = 0;
}

TEST(PpcDispatchTest, HotPrefixesRespectBudgetsChangesAndEviction)
{
    Dispatcher dispatcher(2, 2);
    std::array<std::uint32_t, 32> registers{};
    std::array<std::uint32_t, 3> words{0x38600001, 0x38630002, 0x4E800420};
    EXPECT_EQ(dispatcher.execute(0x02000000, words, registers, 64), 0u);
    EXPECT_EQ(registers[3], 0u);
    EXPECT_EQ(dispatcher.execute(0x02000000, words, registers, 1), 0u);
    EXPECT_EQ(registers[3], 0u);
    EXPECT_EQ(dispatcher.execute(0x02000000, words, registers, 2), 2u);
    EXPECT_EQ(registers[3], 3u);
    words[0] = 0x38600005;
    EXPECT_EQ(dispatcher.execute(0x02000000, words, registers, 64), 0u);
    EXPECT_EQ(registers[3], 3u);
    EXPECT_EQ(dispatcher.execute(0x02000000, words, registers, 64), 2u);
    EXPECT_EQ(registers[3], 7u);
    EXPECT_EQ(dispatcher.stats().invalidations, 1u);
    words[0] = 0x90640000; // Store cannot execute through the register-only ABI.
    EXPECT_EQ(dispatcher.execute(0x02000000, words, registers, 64), 0u);
    EXPECT_EQ(registers[3], 7u);
    EXPECT_EQ(dispatcher.size(), 1u);
    words[0] = 0x38600001;
    EXPECT_EQ(dispatcher.execute(0x02000004, words, registers, 64), 0u);
    EXPECT_EQ(dispatcher.execute(0x02000008, words, registers, 64), 0u);
    EXPECT_EQ(dispatcher.size(), 2u);
    EXPECT_EQ(dispatcher.stats().evictions, 1u);
    EXPECT_EQ(dispatcher.stats().compiled, 2u);
    EXPECT_EQ(dispatcher.stats().instructions, 4u);
    EXPECT_EQ(dispatcher.execute(1, words, registers, 64), 0u);
    EXPECT_EQ(dispatcher.execute(0xFFFFFFFC, words, registers, 64), 0u);
    EXPECT_EQ(dispatcher.execute(0x02000000, {}, registers, 64), 0u);
}

TEST_F(PpcNativeTest, ImmediateBlocksMatchInterpreterWithAliasesAndWraparound)
{
    NativeCompiler compiler;
    std::mt19937 random(0x505043);
    const std::array<unsigned, 6> opcodes{14, 15, 24, 25, 26, 27};
    for (unsigned blockIndex = 0; blockIndex < 24; ++blockIndex) {
        std::vector<std::uint32_t> words;
        for (unsigned i = 0; i < 64; ++i) {
            const auto opcode = opcodes[(i + blockIndex) % opcodes.size()];
            const unsigned rt = random() % 32;
            const unsigned ra = i % 3 == 0 ? 0 : i % 3 == 1 ? rt : random() % 32;
            const std::array<std::uint32_t, 8> immediates{0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x8001, 0x1234, std::uint32_t(random()) & 0xFFFF};
            words.push_back((opcode << 26) | (rt << 21) | (ra << 16) | immediates[(i / 6 + blockIndex) % 8]);
        }
        const auto block = Block::decode(0x02000000, words);
        ASSERT_TRUE(block);
        NativeBlock native(compiler, *block);
        const auto object = emitObject(*block);
        ASSERT_GT(object.size(), 4u);
        // Objects use the host format: Mach-O (MH_MAGIC_64) on macOS, ELF elsewhere.
    #ifdef __APPLE__
        const std::array<unsigned, 4> magic{0xCF, 0xFA, 0xED, 0xFE};
    #else
        const std::array<unsigned, 4> magic{0x7F, 'E', 'L', 'F'};
    #endif
        for (unsigned i = 0; i < magic.size(); ++i)
            EXPECT_EQ(object[i], magic[i]);
        NativeBlock aheadOfTime(compiler, object);
        for (unsigned sample = 0; sample < 64; ++sample) {
            std::array<std::uint32_t, 32> registers;
            for (unsigned i = 0; i < 32; ++i)
                registers[i] = cpu->m_gpr[i] = sample == 0 ? 0xFFFFFFFFu : sample == 1 ? 0 : random();
            cpu->m_cr.raw = 0xA5A55A5A;
            cpu->m_xer.raw = 0x5A5AA5A5;
            cpu->m_lr = 0x12345678;
            cpu->m_ctr = 0x87654321;
            auto aotRegisters = registers;
            for (const auto word: words)
                cpu->executeInstruction(EncodedInstruction(word));
            native.execute(registers);
            aheadOfTime.execute(aotRegisters);
            EXPECT_EQ(aotRegisters, registers);
            for (unsigned i = 0; i < 32; ++i)
                EXPECT_EQ(registers[i], cpu->m_gpr[i]);
            EXPECT_EQ(cpu->m_cr.raw, 0xA5A55A5Au);
            EXPECT_EQ(cpu->m_xer.raw, 0x5A5AA5A5u);
            EXPECT_EQ(cpu->m_lr, 0x12345678u);
            EXPECT_EQ(cpu->m_ctr, 0x87654321u);
        }
    }
}

TEST_F(PpcNativeTest, MixedIntegerBlocksMatchInterpreterAndPreserveFlags)
{
    NativeCompiler compiler;
    std::mt19937 random(0x494E5447);
    const std::array<unsigned, 6> xo{266, 40, 235, 28, 444, 316};
    for (unsigned blockIndex = 0; blockIndex < 32; ++blockIndex) {
        std::vector<std::uint32_t> words;
        for (unsigned i = 0; i < 64; ++i) {
            const unsigned d = random() % 32;
            const unsigned a = i % 3 == 0 ? d : i % 3 == 1 ? 0 : random() % 32;
            const unsigned b = i % 4 == 0 ? a : random() % 32;
            const unsigned kind = (i + blockIndex) % 8;
            if (kind < 6)
                words.push_back((31u << 26) | (d << 21) | (a << 16) | (b << 11) | (xo[kind] << 1));
            else if (kind == 6)
                words.push_back((7u << 26) | (d << 21) | (a << 16) | (random() & 0xFFFF));
            else
                words.push_back((21u << 26) | (d << 21) | (a << 16) | ((blockIndex & 31) << 11) | ((random() & 31) << 6) | ((random() & 31) << 1));
        }
        const auto block = Block::decode(0x02000000, words);
        ASSERT_TRUE(block);
        NativeBlock jit(compiler, *block);
        NativeBlock aot(compiler, emitObject(*block));
        for (unsigned sample = 0; sample < 64; ++sample) {
            std::array<std::uint32_t, 32> registers;
            for (unsigned i = 0; i < 32; ++i) {
                const std::array<std::uint32_t, 5> boundaries{0, 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF};
                registers[i] = cpu->m_gpr[i] = sample < 5 ? boundaries[(sample + i) % 5] : random();
            }
            cpu->m_cr.raw = 0xA5A55A5A;
            cpu->m_xer.raw = 0x5A5AA5A5;
            cpu->m_lr = 0x12345678;
            cpu->m_ctr = 0x87654321;
            auto linked = registers;
            for (auto word: words)
                cpu->executeInstruction(EncodedInstruction(word));
            jit.execute(registers);
            aot.execute(linked);
            EXPECT_EQ(linked, registers);
            for (unsigned i = 0; i < 32; ++i)
                EXPECT_EQ(registers[i], cpu->m_gpr[i]);
            EXPECT_EQ(cpu->m_cr.raw, 0xA5A55A5Au);
            EXPECT_EQ(cpu->m_xer.raw, 0x5A5AA5A5u);
            EXPECT_EQ(cpu->m_lr, 0x12345678u);
            EXPECT_EQ(cpu->m_ctr, 0x87654321u);
        }
    }
}

TEST_F(PpcNativeTest, LogicalShiftsMatchAllSixBitCountsAndAliases)
{
    NativeCompiler compiler;
    for (const unsigned xo: {24u, 536u}) {
        for (unsigned alias = 0; alias < 4; ++alias) {
            const unsigned source = alias == 3 ? 5 : 3;
            const unsigned destination = alias == 0 ? 4 : alias == 1 ? source : 5;
            const std::array words{(31u << 26) | (source << 21) | (destination << 16) | (5u << 11) | (xo << 1)};
            const auto block = Block::decode(0x02000000, words);
            ASSERT_TRUE(block);
            NativeBlock jit(compiler, *block);
            NativeBlock aot(compiler, emitObject(*block));
            for (unsigned count = 0; count < 128; ++count) {
                for (const std::uint32_t value: {0u, 1u, 0x80000000u, 0xFFFFFFFFu, 0xA5A51234u}) {
                    std::array<std::uint32_t, 32> registers{};
                    registers[source] = value;
                    registers[5] = 0xFFFF0000u | count;
                    for (unsigned i = 0; i < 32; ++i)
                        cpu->m_gpr[i] = registers[i];
                    const auto input = registers[source];
                    const auto shift = registers[5] & 63;
                    const auto expected = shift >= 32 ? 0u : xo == 24 ? input << shift : input >> shift;
                    auto linked = registers;
                    cpu->executeInstruction(EncodedInstruction(words[0]));
                    jit.execute(registers);
                    aot.execute(linked);
                    EXPECT_EQ(registers[destination], expected);
                    EXPECT_EQ(linked, registers);
                    for (unsigned i = 0; i < 32; ++i)
                        EXPECT_EQ(registers[i], cpu->m_gpr[i]);
                }
            }
        }
    }
}

TEST_F(PpcNativeTest, RotateInsertAndVariableRotateMatchEveryMaskWithDependencies)
{
    NativeCompiler compiler;
    std::mt19937 random(0x524F544C);
    for (unsigned mb = 0; mb < 32; ++mb) {
        std::vector<std::uint32_t> words;
        for (unsigned me = 0; me < 32; ++me) {
            const unsigned source = me % 4, destination = (me + 1) % 4;
            words.push_back((20u << 26) | (source << 21) | (destination << 16) | (((mb + me) % 32) << 11) | (mb << 6) | (me << 1));
            // Alias the variable count with the destination on alternate operations.
            words.push_back((23u << 26) | (destination << 21) | (source << 16) | ((me % 2 ? source : destination) << 11) | (mb << 6) | (me << 1));
        }
        const auto block = Block::decode(0x02000000, words);
        ASSERT_TRUE(block);
        NativeBlock jit(compiler, *block);
        NativeBlock aot(compiler, emitObject(*block));
        for (unsigned sample = 0; sample < 64; ++sample) {
            std::array<std::uint32_t, 32> registers;
            for (unsigned i = 0; i < 32; ++i)
                registers[i] = cpu->m_gpr[i] = sample == 0 ? 0 : sample == 1 ? 0xFFFFFFFFu : random();
            cpu->m_cr.raw = 0xA5A55A5A;
            cpu->m_xer.raw = 0x5A5AA5A5;
            auto linked = registers;
            for (auto word: words)
                cpu->executeInstruction(EncodedInstruction(word));
            jit.execute(registers);
            aot.execute(linked);
            EXPECT_EQ(linked, registers);
            for (unsigned i = 0; i < 32; ++i)
                EXPECT_EQ(registers[i], cpu->m_gpr[i]);
            EXPECT_EQ(cpu->m_cr.raw, 0xA5A55A5Au);
            EXPECT_EQ(cpu->m_xer.raw, 0x5A5AA5A5u);
        }
    }
}

TEST(PpcNativeLifetimeTest, SharedEngineIsolatesSymbolsAndRetainsSurvivingBlocks)
{
    std::unique_ptr<NativeBlock> survivor;
    const auto first = Block::decode(0x02000000, std::array<std::uint32_t, 1>{0x38600001});
    const auto second = Block::decode(0x02000000, std::array<std::uint32_t, 1>{0x38600002});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    std::array<std::uint32_t, 32> registers{};
    {
        NativeCompiler compiler;
        survivor = std::make_unique<NativeBlock>(compiler, *first);
        const auto object = emitObject(*second);
        for (unsigned i = 0; i < 32; ++i) {
            NativeBlock temporary(compiler, object);
            temporary.execute(registers);
            EXPECT_EQ(registers[3], 2u);
            survivor->execute(registers);
            EXPECT_EQ(registers[3], 1u);
        }
        survivor->execute(registers);
        EXPECT_EQ(registers[3], 1u);
    }
    registers[3] = 0;
    survivor->execute(registers);
    EXPECT_EQ(registers[3], 1u);
    survivor.reset();
}
#endif

TEST_F(PpcFetchInterpreterTest, NativePrefixSuffixKeepsBranchStoreAndFaultBoundaries)
{
    constexpr unsigned app = Core::Memory::MemoryMap::ApplicationCode;
    Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
    // Simulate a completed native add prefix, then execute the load/branch suffix.
    const std::array<std::uint32_t, 4> words{0x38600001, 0x38800002, 0x80A60000, 0x48000008};
    for (unsigned i = 0; i < words.size(); ++i)
        cpu->m_memory.write<std::uint32_t>(app + i * 4, words[i]);
    cpu->m_memory.write<std::uint32_t>(app + 0x100, 0xCAFEBABE);
    cpu->m_gpr[3] = 9; // Already produced by the native prefix; must not be executed again.
    cpu->m_gpr[4] = 10;
    cpu->m_gpr[6] = app + 0x100;
    cpu->m_pc = 8;
    const auto &block = cpu->getBlock(reader, 0);
    ASSERT_EQ(block.instrs.size(), 4u);
    EXPECT_EQ(cpu->runBlock(block, 2), 2u);
    EXPECT_EQ(cpu->m_gpr[3], 9u);
    EXPECT_EQ(cpu->m_gpr[4], 10u);
    EXPECT_EQ(cpu->m_gpr[5], 0xCAFEBABEu);
    EXPECT_EQ(cpu->m_pc, 20u);
    EXPECT_EQ(cpu->runBlock(block, block.instrs.size()), 0u);
    cpu->m_memory.write<std::uint32_t>(app + 8, 0x90660000); // A store into a decoded code page still ends execution.
    cpu->m_pc = 8;
    const auto &store = cpu->getBlock(reader, 0);
    ASSERT_EQ(store.instrs.size(), 4u);
    EXPECT_EQ(cpu->runBlock(store, 2), 1u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(app + 0x100), 9u);
    EXPECT_EQ(cpu->m_pc, 12u);
    cpu->m_memory.write<std::uint32_t>(app + 8, words[2]);
    cpu->m_scheduler.bootstrap(*cpu);
    cpu->m_gpr[6] = 0; // A suffix fault counts only the attempted suffix instruction.
    cpu->m_pc = 8;
    const auto &fault = cpu->getBlock(reader, 0);
    EXPECT_EQ(cpu->runBlock(fault, 2), 1u);
    EXPECT_TRUE(cpu->failed());
}

#ifdef WEMU_HAS_LLVM
TEST_F(PpcNativeTest, LockedCacheMemoryAccessFallsBackAtExactInstruction)
{
    constexpr auto app = Core::Memory::ApplicationCode;
    const std::array words{0x38630001u, 0x80A40000u}; // addi r3,r3,1; lwz r5,0(r4)
    const auto block = Block::decode(app, words);
    ASSERT_TRUE(block);
    NativeCompiler compiler;
    NativeBlock jit(compiler, *block), aot(compiler, emitObject(*block));
    for (const auto cache: Core::Memory::LOCKED_CACHE_BASES) {
        cpu->m_memory.write<std::uint32_t>(cache, 0xDEADBEEF);
        for (auto *native: {&jit, &aot}) {
            std::array<std::uint32_t, 32> registers{};
            registers[3] = 7;
            registers[4] = cache;
            const auto &ram = cpu->m_memory.getMemory();
            ExecutionState state{registers.data(), {{cpu->m_memory.hostPtr(app, ram.size()), ram.size(), app}, {}, {}}};
            EXPECT_EQ(native->execute(state), 1u);
            EXPECT_EQ(state.nextPc, app + 4);
            EXPECT_EQ(registers[3], 8u);
            EXPECT_EQ(registers[5], 0u);
            std::copy(registers.begin(), registers.end(), cpu->m_gpr);
            cpu->executeInstruction(EncodedInstruction(words[1]));
            EXPECT_EQ(cpu->m_gpr[3], 8u);
            EXPECT_EQ(cpu->m_gpr[5], 0xDEADBEEFu);
        }
    }
}

TEST_F(PpcNativeTest, CheckedMemoryJitAndAotMatchInterpreterForWidthsUpdatesAndAliases)
{
    NativeCompiler compiler;
    constexpr std::uint32_t app = Core::Memory::ApplicationCode;
    std::mt19937 random(0x4D454D32);
    const auto &ram = cpu->m_memory.getMemory();
    for (const unsigned opcode: {32u, 33u, 34u, 35u, 40u, 41u, 42u, 43u, 36u, 37u, 38u, 39u, 44u, 45u}) {
        const std::array words{0x38630001u, (opcode << 26) | (3u << 21) | (4u << 16) | 0xFFFFu};
        const auto block = Block::decode(app, words);
        ASSERT_TRUE(block);
        NativeBlock jit(compiler, *block), aot(compiler, emitObject(*block));
        for (unsigned sample = 0; sample < 32; ++sample) {
            const auto value = std::uint32_t(random());
            std::array<std::uint32_t, 32> initial{};
            initial[3] = value;
            initial[4] = app + 0x201; // Offset -1, including unaligned accesses in other samples.
            for (const auto address: {app + 0x200, app + 0x201}) {
                initial[4] = address + 1;
                cpu->m_memory.write<std::uint32_t>(address, value);
                for (unsigned i = 0; i < 32; ++i)
                    cpu->m_gpr[i] = initial[i];
                for (auto word: words)
                    cpu->executeInstruction(EncodedInstruction(word));
                const auto expectedMemory = cpu->m_memory.read<std::uint32_t>(address);
                for (auto *native: {&jit, &aot}) {
                    cpu->m_memory.write<std::uint32_t>(address, value);
                    auto registers = initial;
                    ExecutionState state{registers.data(), {{cpu->m_memory.hostPtr(app, ram.size()), ram.size(), app}, {}, {}}};
                    EXPECT_EQ(native->execute(state), words.size());
                    for (unsigned i = 0; i < 32; ++i)
                        EXPECT_EQ(registers[i], cpu->m_gpr[i]);
                    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(address), expectedMemory);
                }
            }
        }
    }
    EXPECT_TRUE(Block::decode(app, std::array{0x90640000u, 0x38600001u})); // Native stores use guarded code-write exits.
    EXPECT_FALSE(Block::supports(0x84630000u)); // lwzu r3,0(r3): invalid update alias.
    EXPECT_FALSE(Block::supports(0x84600000u)); // Update RA=0 is invalid.
}

TEST(PpcNativeMemoryTest, BoundsFallbackCommitsOnlyCompletedInstructionsAndNeverPartialStores)
{
    NativeCompiler compiler;
    std::array<std::uint8_t, 16> memory{};
    memory[12] = 0xCA;
    memory[13] = 0xFE;
    memory[14] = 0xBA;
    memory[15] = 0xBE;
    for (const auto memoryWord: {0x84A40000u, 0x94640000u}) { // lwzu / stwu.
        const std::array words{0x38630001u, memoryWord};
        const auto block = Block::decode(0x02000000, words);
        ASSERT_TRUE(block);
        NativeBlock jit(compiler, *block), aot(compiler, emitObject(*block));
        for (const auto address: {0u, 0xFFFFFFFCu, 0x02000000u - 1, 0x02000000u + 13, 0xC0FF0000u + 13}) {
            for (auto *native: {&jit, &aot}) {
                std::array<std::uint32_t, 32> registers{};
                registers[3] = 41;
                registers[4] = address;
                registers[5] = 99;
                const auto before = memory;
                ExecutionState state{registers.data(), {{memory.data(), memory.size(), 0x02000000}, {memory.data(), memory.size(), 0xC0FF0000}, {}}};
                EXPECT_EQ(native->execute(state), 1u);
                EXPECT_EQ(registers[3], 42u);
                EXPECT_EQ(registers[4], address);
                EXPECT_EQ(registers[5], 99u);
                EXPECT_EQ(memory, before);
            }
        }
    }
    const auto block = Block::decode(0x02000000, std::array{0x80A40000u});
    ASSERT_TRUE(block);
    NativeBlock jit(compiler, *block);
    for (const auto base: {0x02000000u, 0xC0FF0000u, 0xC0000000u}) {
        std::array<std::uint32_t, 32> registers{};
        registers[4] = base + 12;
        ExecutionState state{registers.data(), {{memory.data(), memory.size(), base}, {}, {}}};
        EXPECT_EQ(jit.execute(state), 1u);
        EXPECT_EQ(registers[5], 0xCAFEBABEu);
    }
}
#endif

#ifdef WEMU_HAS_LLVM
TEST_F(PpcNativeTest, ConditionsUnaryOperationsAndBranchesMatchReferenceState)
{
    NativeCompiler compiler;
    constexpr std::uint32_t app = Core::Memory::ApplicationCode;
    std::mt19937 random(0x43524252);
    const auto encode = [](unsigned xo, unsigned d, unsigned a, unsigned b, bool record) {
        return (31u << 26) | (d << 21) | (a << 16) | (b << 11) | (xo << 1) | unsigned(record);
    };
    std::vector<std::vector<std::uint32_t>> blocks;
    for (const unsigned xo: {104u, 922u, 954u, 26u, 266u, 40u, 235u, 28u, 444u, 316u, 24u, 536u})
        for (const bool record: {false, true})
            blocks.push_back({encode(xo, 3, 4, 5, record), 0x48000008u});
    for (const unsigned slot: {0u, 3u, 7u})
        for (const bool signedCompare: {false, true}) {
            blocks.push_back({((signedCompare ? 11u : 10u) << 26) | (slot << 23) | (4u << 16) | 0xFFFFu, 0x41820008u});
            blocks.push_back({encode(signedCompare ? 0 : 32, slot << 2, 4, 5, false), 0x40820008u});
        }
    for (const unsigned bo: {0u, 2u, 4u, 8u, 12u, 16u, 18u, 20u})
        for (const unsigned bi: {0u, 2u, 13u, 31u}) {
            blocks.push_back({0x706300FFu, (16u << 26) | (bo << 21) | (bi << 16) | 0xFFF8u});
            blocks.push_back({0x706300FFu, (19u << 26) | (bo << 21) | (bi << 16) | (16u << 1)});
        }
    blocks.push_back({0x38630001u, 0x48000102u}); // Absolute branch must match the interpreter's PPC address handling.
    blocks.push_back({0x38630001u, 0x48000103u}); // Absolute branch with link.
    blocks.push_back({0x38630001u, 0x4182FFFau}); // Absolute conditional branch.
    blocks.push_back({0x38630001u, 0x48000009u}); // BL updates LR after the native prefix.
    blocks.push_back({0x7C6803A6u, 0x7CA802A6u}); // MTLR then MFLR uses the interpreter's offset representation.
    blocks.push_back({0x7C6903A6u, 0x7CA902A6u}); // MTCTR then MFCTR.
    for (const auto &words: blocks) {
        const auto decoded = Block::decode(app, words);
        ASSERT_TRUE(decoded);
        NativeBlock jit(compiler, *decoded), aot(compiler, emitObject(*decoded));
        for (unsigned sample = 0; sample < 16; ++sample) {
            std::array<std::uint32_t, 32> initial;
            for (auto &r: initial)
                r = random();
            const std::array<std::uint32_t, 5> boundaries{0, 1, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu};
            if (sample < boundaries.size())
                initial[4] = boundaries[sample];
            const auto cr = std::uint32_t(random());
            const auto lr = std::uint32_t(random());
            const auto ctr = sample < 3 ? sample : std::uint32_t(random());
            for (unsigned i = 0; i < 32; ++i)
                cpu->m_gpr[i] = initial[i];
            cpu->m_cr.raw = cr;
            cpu->m_lr = lr;
            cpu->m_ctr = ctr;
            cpu->m_xer.so = sample & 1;
            cpu->m_pc = 0;
            for (auto word: words) {
                cpu->m_nextPc = cpu->m_pc + 4;
                cpu->executeInstruction(EncodedInstruction(word));
                cpu->m_pc = cpu->m_nextPc;
            }
            for (auto *native: {&jit, &aot}) {
                auto registers = initial;
                ExecutionState state{registers.data(), {}, cr, sample & 1, lr, ctr, app};
                ASSERT_EQ(native->execute(state), words.size());
                for (unsigned i = 0; i < 32; ++i)
                    EXPECT_EQ(registers[i], cpu->m_gpr[i]);
                EXPECT_EQ(state.condition, cpu->m_cr.raw);
                EXPECT_EQ(state.link, cpu->m_lr);
                EXPECT_EQ(state.counter, cpu->m_ctr);
                EXPECT_EQ(state.nextPc, cpu->m_pc + app);
            }
        }
    }
    EXPECT_FALSE(Block::supports(0x4E800420u)); // CTR branches may dispatch HLE.
    EXPECT_FALSE(Block::supports(0x41820009u)); // Conditional link policy stays in interpreter.
    EXPECT_FALSE(Block::decode(app, std::array{0x48000004u, 0x38600001u}));
}
#endif

#ifdef WEMU_HAS_LLVM
TEST_F(PpcNativeTest, IndexedMemoryMatchesInterpreterIncludingSourceAndDestinationAliases)
{
    NativeCompiler compiler;
    constexpr auto app = Core::Memory::ApplicationCode;
    const auto &ram = cpu->m_memory.getMemory();
    for (unsigned xo: {23u, 55u, 87u, 119u, 279u, 311u, 343u, 375u, 151u, 183u, 215u, 247u, 407u, 439u}) {
        const bool update = xo & 32;
        for (unsigned alias = 0; alias < 3; ++alias) {
            const unsigned destination = alias == 0 ? 3 : alias == 1 ? 5 : 4;
            const bool load = xo == 23 || xo == 55 || xo == 87 || xo == 119 || xo == 279 || xo == 311 || xo == 343 || xo == 375;
            if (load && update && destination == 4)
                continue;
            const std::array words{0x38630001u, (31u << 26) | (destination << 21) | (4u << 16) | (5u << 11) | (xo << 1)};
            const auto block = Block::decode(app, words);
            ASSERT_TRUE(block);
            NativeBlock jit(compiler, *block), aot(compiler, emitObject(*block));
            std::array<std::uint32_t, 32> initial{};
            initial[3] = 0xABCDEF00;
            initial[4] = app + 0x200;
            initial[5] = 1;
            constexpr auto address = app + 0x201;
            cpu->m_memory.write<std::uint32_t>(address, 0xFEDCBA98);
            for (unsigned i = 0; i < 32; ++i)
                cpu->m_gpr[i] = initial[i];
            for (auto word: words)
                cpu->executeInstruction(EncodedInstruction(word));
            const auto expectedMemory = cpu->m_memory.read<std::uint32_t>(address);
            for (auto *native: {&jit, &aot}) {
                cpu->m_memory.write<std::uint32_t>(address, 0xFEDCBA98);
                auto registers = initial;
                ExecutionState state{registers.data(), {{cpu->m_memory.hostPtr(app, ram.size()), ram.size(), app}, {}, {}}};
                EXPECT_EQ(native->execute(state), words.size());
                for (unsigned i = 0; i < 32; ++i)
                    EXPECT_EQ(registers[i], cpu->m_gpr[i]);
                EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(address), expectedMemory);
            }
        }
    }
}
#endif

#ifdef WEMU_HAS_LLVM
TEST(PpcDispatchTest, MinimumUsefulPrefixKeepsShortBlocksInterpreted)
{
    EXPECT_THROW(Dispatcher(8, 1, {}, 0), std::invalid_argument);
    EXPECT_THROW(Dispatcher(8, 1, {}, 65), std::invalid_argument);
    Dispatcher dispatcher(8, 1, {}, 4);
    std::array<std::uint32_t, 32> registers{};
    const std::array shortWords{0x38600001u, 0x38630002u, 0x48000004u};
    EXPECT_EQ(dispatcher.execute(0x02000000, shortWords, registers, 64), 0u);
    EXPECT_EQ(dispatcher.size(), 0u);
    EXPECT_EQ(registers[3], 0u);
    const std::array useful{0x38600001u, 0x38630002u, 0x38630003u, 0x48000004u};
    EXPECT_EQ(dispatcher.execute(0x02000000, useful, registers, 64), 4u);
    EXPECT_EQ(registers[3], 6u);
    EXPECT_EQ(dispatcher.stats().compiled, 1u);
    EXPECT_EQ(dispatcher.execute(0x02000000, useful, registers, 64), 4u);
    EXPECT_EQ(dispatcher.stats().compiled, 1u);
}
#endif

#ifdef WEMU_HAS_LLVM
TEST(PpcDispatchTest, BoundCodeExpiresWhenEvictedOrInvalidated)
{
    NativeBinding binding;
    std::array<std::uint32_t, 32> registers{};
    ExecutionState state{registers.data(), {}};
    const std::array words{0x38600001u, 0x38630002u};
    {
        Dispatcher dispatcher(1, 1);
        EXPECT_EQ(dispatcher.execute(0x02000000, words, state, 64, &binding), 2u);
        ASSERT_NE(dispatcher.bound(binding), nullptr);
        EXPECT_EQ(dispatcher.bound(binding)->execute(state), 2u);
        const std::array changed{0x38600005u, 0x38630002u};
        EXPECT_EQ(dispatcher.execute(0x02000000, changed, state, 64), 2u);
        EXPECT_EQ(dispatcher.bound(binding), nullptr);
        EXPECT_EQ(registers[3], 7u);
        EXPECT_EQ(dispatcher.execute(0x02000000, changed, state, 64, &binding), 2u);
        ASSERT_NE(dispatcher.bound(binding), nullptr);
        EXPECT_EQ(dispatcher.execute(0x02000040, words, state, 64), 2u);
        EXPECT_EQ(dispatcher.bound(binding), nullptr);
        EXPECT_EQ(dispatcher.execute(0x02000040, words, state, 64, &binding), 2u);
        ASSERT_NE(dispatcher.bound(binding), nullptr);
    }
    Dispatcher replacement(1, 1);
    EXPECT_EQ(replacement.bound(binding), nullptr); // A different lifetime never revives an old pointer.
}
#endif

TEST_F(PpcFetchInterpreterTest, AbsoluteBranchTargetsUsePpcAddressesAndPreserveLinkOffsets)
{
    constexpr auto app = Core::Memory::ApplicationCode;
    for (const auto word: {0x48000102u, 0x48000103u, 0x4BFFFFFEu, 0x4BFFFFFFu}) {
        cpu->m_pc = 0x40;
        cpu->m_lr = 0x84;
        cpu->m_nextPc = 0x44;
        cpu->executeInstruction(EncodedInstruction(word));
        const auto target = word & 0x02000000u ? 0xFFFFFFFCu : 0x100u;
        EXPECT_EQ(cpu->m_nextPc + app, target);
        EXPECT_EQ(cpu->m_lr, word & 1 ? 0x44u : 0x84u);
    }
}

#ifdef WEMU_HAS_LLVM
TEST_F(PpcNativeTest, BackwardLoadLoopMatchesReferenceAndRetiresOnlyItsInstructionBudget)
{
    NativeCompiler compiler;
    constexpr auto app = Core::Memory::ApplicationCode;
    const std::array words{0x80860000u, 0x38630001u, 0x7C032000u, 0x4180FFF4u}; // load limit; increment; cmp; blt root.
    const auto block = Block::decode(app, words);
    ASSERT_TRUE(block);
    NativeBlock jit(compiler, *block), aot(compiler, emitObject(*block));
    for (const unsigned budget: {0u, 1u, 3u, 4u, 8u, 11u, 64u, 0xFFFFFFFFu}) {
        std::array<std::uint32_t, 32> initial{};
        initial[6] = app + 0x100;
        cpu->m_memory.write<std::uint32_t>(initial[6], 5);
        for (unsigned i = 0; i < 32; ++i)
            cpu->m_gpr[i] = initial[i];
        cpu->m_cr.raw = 0x12345678;
        cpu->m_xer.so = 1;
        cpu->m_pc = 0;
        const auto iterations = budget ? std::min(5u, std::min(budget, 64u) / 4) : 1u;
        const auto expected = iterations * 4;
        for (unsigned i = 0; i < expected; ++i) {
            cpu->m_nextPc = cpu->m_pc + 4;
            cpu->executeInstruction(EncodedInstruction(words[cpu->m_pc / 4]));
            cpu->m_pc = cpu->m_nextPc;
        }
        for (auto *native: {&jit, &aot}) {
            auto registers = initial;
            ExecutionState state{registers.data(),
                                 {{cpu->m_memory.hostPtr(app, cpu->m_memory.getMemory().size()), cpu->m_memory.getMemory().size(), app}, {}, {}},
                                 0x12345678,
                                 1,
                                 0,
                                 0,
                                 app,
                                 budget};
            EXPECT_EQ(native->execute(state), expected);
            EXPECT_EQ(state.condition, cpu->m_cr.raw);
            EXPECT_EQ(state.nextPc, app + cpu->m_pc);
            for (unsigned i = 0; i < 32; ++i)
                EXPECT_EQ(registers[i], cpu->m_gpr[i]);
        }
    }
}

TEST_F(PpcNativeTest, BackwardLoopFaultCommitsPreviousIterationsAndCurrentPrefixPrecisely)
{
    NativeCompiler compiler;
    constexpr auto app = Core::Memory::ApplicationCode;
    constexpr auto base = Core::Memory::ApplicationMemoryEnd - 16;
    const std::array words{0x38A50001u, 0x84640004u, 0x2C05000Au, 0x4180FFF4u}; // increment; lwzu; cmp; blt root.
    const auto block = Block::decode(app, words);
    ASSERT_TRUE(block);
    NativeBlock jit(compiler, *block), aot(compiler, emitObject(*block));
    for (unsigned i = 0; i < 4; ++i)
        cpu->m_memory.write<std::uint32_t>(base + i * 4, 0xAABBCC00u + i);
    std::array<std::uint32_t, 32> initial{};
    initial[4] = base;
    for (unsigned i = 0; i < 32; ++i)
        cpu->m_gpr[i] = initial[i];
    cpu->m_cr.raw = 0x12345678;
    cpu->m_xer.so = 1;
    cpu->m_pc = 0;
    for (unsigned i = 0; i < 13; ++i) { // Three complete loops then the fourth iteration's first instruction.
        cpu->m_nextPc = cpu->m_pc + 4;
        cpu->executeInstruction(EncodedInstruction(words[cpu->m_pc / 4]));
        cpu->m_pc = cpu->m_nextPc;
    }
    for (auto *native: {&jit, &aot}) {
        auto registers = initial;
        ExecutionState state{registers.data(), {{cpu->m_memory.hostPtr(base, 16), 16, base}, {}, {}}, 0x12345678, 1, 0, 0, app, 64};
        EXPECT_EQ(native->execute(state), 13u);
        EXPECT_EQ(state.nextPc, app + 4);
        EXPECT_EQ(state.condition, cpu->m_cr.raw);
        for (unsigned i = 0; i < 32; ++i)
            EXPECT_EQ(registers[i], cpu->m_gpr[i]);
    }
}
#endif

#ifdef WEMU_HAS_LLVM
TEST(PpcDispatchTest, ShortSupportedLoopsCanAmortizeDispatchWithinTheBudget)
{
    Dispatcher dispatcher(8, 1, {}, 4);
    std::array<std::uint32_t, 32> registers{};
    const std::array words{0x38630001u, 0x4BFFFFFCu}; // addi; b root (relative -4).
    ASSERT_TRUE(Block::loopsToStart(0x02000000, words));
    ExecutionState state{registers.data(), {}};
    EXPECT_EQ(dispatcher.execute(0x02000000, words, state, 11), 10u);
    EXPECT_EQ(registers[3], 5u);
    EXPECT_EQ(state.nextPc, 0x02000000u);
    EXPECT_TRUE(Block::loopsToStart(0x02000000, std::array{0x90640000u, words[1]}));
    EXPECT_FALSE(Block::loopsToStart(0x02000000, std::array{0x48000004u, words[1]}));
}
#endif

#ifdef WEMU_HAS_LLVM
TEST_F(PpcFetchInterpreterTest, NativeLookaheadRetainsReferenceStoreBoundariesAndValidatesAllCode)
{
    constexpr auto app = Core::Memory::ApplicationCode;
    Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
    const std::array words{0x90640000u, 0x38630001u, 0x2C03000Au, 0x4180FFF4u};
    for (unsigned i = 0; i < words.size(); ++i)
        cpu->m_memory.write<std::uint32_t>(app + i * 4, words[i]);
    // Interpreted blocks continue past stores; only the lookahead reference stops at them.
    const auto &reference = cpu->getBlock(reader, 0);
    ASSERT_EQ(reference.instrs.size(), 4u);
    const auto &native = cpu->getBlock(reader, 0, true);
    EXPECT_EQ(native.instrs.size(), 1u);
    EXPECT_EQ(native.nativeWords, (std::vector<std::uint32_t>(words.begin(), words.end())));
    EXPECT_TRUE(native.nativeLoop);
    native.nativeVisits = 256;
    EXPECT_EQ(cpu->getBlock(reader, 0, true).nativeVisits, 256u);
    cpu->m_memory.write<std::uint32_t>(app + 4, 0x38630002u); // Change a word beyond the reference store boundary.
    EXPECT_EQ(cpu->getBlock(reader, 0, true).nativeWords[1], 0x38630002u);
    EXPECT_EQ(cpu->getBlock(reader, 0, true).nativeVisits, 0u); // A replacement version must become hot independently.
    cpu->m_hooks_min = cpu->m_hooks_max = app + 8;
    cpu->m_hooks[app + 8] = [](Core::Interpreter &) {};
    cpu->clearBlockCache();
    EXPECT_EQ(cpu->getBlock(reader, 0, true).nativeWords.size(), 2u);
    cpu->m_hooks.erase(app + 8);
    cpu->m_hooks_min = 0xFFFFFFFFu;
    cpu->m_hooks_max = 0;
}

TEST_F(PpcNativeTest, StoreLoopMatchesReferenceAndExitsBeforeExecutingRewrittenCode)
{
    NativeCompiler compiler;
    constexpr auto app = Core::Memory::ApplicationCode;
    const std::array words{0x90640000u, 0x38630001u, 0x2C03000Au, 0x4180FFF4u};
    const auto block = Block::decode(app, words);
    ASSERT_TRUE(block);
    NativeBlock jit(compiler, *block), aot(compiler, emitObject(*block));
    const auto &ram = cpu->m_memory.getMemory();
    for (const auto budget: {4u, 11u, 64u}) {
        std::array<std::uint32_t, 32> initial{};
        initial[4] = app + 0x100;
        for (unsigned i = 0; i < 32; ++i)
            cpu->m_gpr[i] = initial[i];
        cpu->m_cr.raw = 0x12345678;
        cpu->m_xer.so = 1;
        cpu->m_pc = 0;
        const unsigned retired = std::min(10u, budget / 4) * 4;
        for (unsigned i = 0; i < retired; ++i) {
            cpu->m_nextPc = cpu->m_pc + 4;
            cpu->executeInstruction(EncodedInstruction(words[cpu->m_pc / 4]));
            cpu->m_pc = cpu->m_nextPc;
        }
        const auto stored = cpu->m_memory.read<std::uint32_t>(initial[4]);
        for (auto *native: {&jit, &aot}) {
            cpu->m_memory.write<std::uint32_t>(initial[4], 0xFFFFFFFFu);
            auto registers = initial;
            ExecutionState state{
                    registers.data(), {{cpu->m_memory.hostPtr(app, ram.size()), ram.size(), app}, {}, {}}, 0x12345678, 1, 0, 0, app, budget};
            EXPECT_EQ(native->execute(state), retired);
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(initial[4]), stored);
            EXPECT_EQ(state.condition, cpu->m_cr.raw);
            EXPECT_EQ(state.nextPc, app + cpu->m_pc);
            for (unsigned i = 0; i < 32; ++i)
                EXPECT_EQ(registers[i], cpu->m_gpr[i]);
        }
    }
    for (auto *native: {&jit, &aot}) {
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<std::uint32_t>(app + i * 4, words[i]);
        std::array<std::uint32_t, 32> registers{};
        registers[3] = 0x38600007u;
        registers[4] = app + 4; // Patch the following instruction.
        ExecutionState state{registers.data(), {{cpu->m_memory.hostPtr(app, ram.size()), ram.size(), app}, {}, {}}, 0, 0, 0, 0, app, 64};
        EXPECT_EQ(native->execute(state), 1u);
        EXPECT_EQ(state.nextPc, app + 4);
        EXPECT_EQ(registers[3], 0x38600007u);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(app + 4), 0x38600007u);
    }
}
#endif

#ifdef WEMU_HAS_LLVM
TEST_F(PpcNativeTest, StoreLoopCodeWriteExitCommitsEarlierIterationsAndAddressUpdate)
{
    NativeCompiler compiler;
    constexpr auto app = Core::Memory::ApplicationCode;
    constexpr auto pc = app + 0x100;
    const std::array words{0x94640004u, 0x38630001u, 0x2C03000Au, 0x4180FFF4u}; // stwu; addi; cmp; blt root.
    const auto block = Block::decode(pc, words);
    ASSERT_TRUE(block);
    NativeBlock jit(compiler, *block), aot(compiler, emitObject(*block));
    const auto &ram = cpu->m_memory.getMemory();
    std::array<std::uint32_t, 32> initial{};
    initial[4] = pc - 16;
    for (auto *native: {&jit, &aot}) {
        for (unsigned i = 0; i < words.size(); ++i)
            cpu->m_memory.write<std::uint32_t>(pc + i * 4, words[i]);
        auto registers = initial;
        ExecutionState state{registers.data(), {{cpu->m_memory.hostPtr(app, ram.size()), ram.size(), app}, {}, {}}, 0, 1, 0, 0, pc, 64};
        EXPECT_EQ(native->execute(state), 13u); // The fourth iteration patches its own first word.
        EXPECT_EQ(state.nextPc, pc + 4);
        EXPECT_EQ(registers[3], 3u); // The addi after the code write must not execute.
        EXPECT_EQ(registers[4], pc);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(pc), 3u);
        EXPECT_EQ(state.condition >> 28, 9u); // LT + SO from the third complete iteration.
    }
}
#endif
