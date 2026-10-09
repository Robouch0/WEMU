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

#ifdef WEMU_HAS_LLVM
TEST_F(PpcFetchInterpreterTest, RecordsUnsupportedPrefixBoundaryWithoutNativeExecution)
{
    constexpr unsigned app = Core::Memory::MemoryMap::ApplicationCode;
    Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
    const std::array<std::uint32_t, 4> words{0x38600001, 0x38800002, 0x80A30000, 0x48000004};
    for (unsigned i = 0; i < words.size(); ++i)
        cpu->m_memory.write<std::uint32_t>(app + i * 4, words[i]);
    const auto &two = cpu->getBlock(reader, 0);
    EXPECT_EQ(two.nativePrefixLength, 2u);
    EXPECT_EQ(two.nativeWords.size(), 2u);
    EXPECT_EQ(two.instrs[two.nativePrefixLength].instr.raw >> 26, 32u); // lwz
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

TEST_F(PpcFetchInterpreterTest, RevalidatesCachedCodeAndStopsAtStores)
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
    ASSERT_EQ(store.instrs.size(), 1u);
    cpu->runBlock(store);
    EXPECT_EQ(cpu->m_pc, 4u);
    const auto &following = cpu->getBlock(reader, 4);
    ASSERT_EQ(following.instrs[0].instr.raw, 0x38A00007u);
    cpu->runBlock(following);
    EXPECT_EQ(cpu->m_gpr[5], 7u);
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
    for (const auto unsupported: {0u, 0x48000004u, 0x80640000u, 0x44000002u}) {
        const std::array words{add[0], unsupported};
        EXPECT_FALSE(Block::decode(0x02000000, words));
    }
    ASSERT_TRUE(Block::decode(0x02000000, add));
}

TEST(PpcBlockTest, RejectsFlagUpdatesAndUnsupportedIntegerForms)
{
    for (unsigned xo: {266u, 40u, 235u, 28u, 444u, 316u, 24u, 536u}) {
        const std::uint32_t word = (31u << 26) | (3u << 21) | (4u << 16) | (5u << 11) | (xo << 1);
        EXPECT_TRUE(Block::supports(word));
        EXPECT_FALSE(Block::supports(word | 1));
        if (xo == 266 || xo == 40 || xo == 235)
            EXPECT_FALSE(Block::supports(word | 0x400));
    }
    EXPECT_TRUE(Block::supports(0x5463083C)); // rlwinm
    EXPECT_FALSE(Block::supports(0x5463083D));
    for (unsigned opcode: {20u, 23u}) {
        const auto word = (opcode << 26) | (3u << 21) | (4u << 16) | (5u << 11) | (31u << 1);
        EXPECT_TRUE(Block::supports(word));
        EXPECT_FALSE(Block::supports(word | 1));
    }
    EXPECT_FALSE(Block::supports(0x7C642E30)); // sraw updates carry even with Rc=0
    EXPECT_FALSE(Block::supports(0x7C642814)); // addc updates carry even with Rc=0
    EXPECT_FALSE(Block::supports(0x706300FF)); // andi. always records
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
    EXPECT_EQ(cpu->getBlock(reader, 0).nativeWords, (std::vector<std::uint32_t>{0x38600001, 0x38630002}));
    cpu->m_memory.write<std::uint32_t>(app + 4, 0x90640000); // store ends eligibility
    EXPECT_TRUE(cpu->getBlock(reader, 0).nativeWords.empty());
    cpu->m_memory.write<std::uint32_t>(app + 4, 0x38630003);
    EXPECT_EQ(cpu->getBlock(reader, 0).nativeWords, (std::vector<std::uint32_t>{0x38600001, 0x38630003}));

    cpu->m_blockCache.clear();
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
    std::array<std::uint32_t, 3> words{0x38600001, 0x38630002, 0x48000004};
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
        EXPECT_EQ(object[0], 0x7F);
        EXPECT_EQ(object[1], 'E');
        EXPECT_EQ(object[2], 'L');
        EXPECT_EQ(object[3], 'F');
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
