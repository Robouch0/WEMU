// Developer diagnostics: the reports a developer reads when a title breaks (crash context,
// symbol names, unknown imports) and the WEMU_WATCH_* / WEMU_PC_COUNT / WEMU_PATCH tools.
// The tools read their environment once per process, so those tests re-run this binary
// as a child with the variables set and check the log it prints.

#include <array>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <vector>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "cpu/types/Instruction.hpp"
#include "hle/Coreinit.hpp"
#include "utils/Diagnostics.hpp"

namespace {
    constexpr std::uint32_t App = Core::Memory::MemoryMap::ApplicationCode;
    constexpr std::uint32_t Result = 0x10001000, Text = 0x10002000;

    Core::Symbol symbol(const std::string &name, std::uint32_t address)
    {
        Core::Symbol s{};
        s.name = name;
        s.meta.virtAddress = address;
        return s;
    }

    struct ChildRun {
            std::string output;
            int exitCode = -1;
    };

    // Runs one DiagnosticsChildTest case in a fresh process with `environment` set.
    ChildRun runChild(const std::string &testName, const std::vector<std::string> &environment)
    {
        std::string command = "env WEMU_DIAG_CHILD=1";
        for (const auto &variable: environment)
            command += " '" + variable + "'";
        command += " '" + testing::internal::GetArgvs().front() + "' --gtest_filter=DiagnosticsChildTest." + testName + " 2>&1";
        ChildRun run;
        if (FILE *pipe = popen(command.c_str(), "r")) {
            std::array<char, 4096> buffer{};
            while (const auto read = std::fread(buffer.data(), 1, buffer.size(), pipe))
                run.output.append(buffer.data(), read);
            const int status = pclose(pipe);
            run.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        return run;
    }

    bool contains(const std::string &text, const std::string &needle) { return text.find(needle) != std::string::npos; }
} // namespace

class DiagnosticsTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_binary.symbols.clear();
            RegisterCoreinitFunctions();
        }

        void TearDown() override { cpu->m_binary.symbols.clear(); }
};

TEST_F(DiagnosticsTest, SymbolNamesFollowTheBinaryThatIsLoadedNow)
{
    cpu->m_binary.symbols = {symbol("main", App + 0x100), symbol("render", App + 0x400)};
    EXPECT_EQ(Core::Diag::symbolize(*cpu, App + 0x110), "0x02000110 <main+0x10>");
    EXPECT_EQ(Core::Diag::symbolize(*cpu, App + 0x400), "0x02000400 <render+0x0>");
    EXPECT_EQ(Core::Diag::symbolize(*cpu, App + 0x50), "0x02000050") << "before the first symbol";
    EXPECT_EQ(Core::Diag::symbolize(*cpu, App + 0x40000), "0x02040000") << "too far from any symbol to be meaningful";

    // A new title: names must come from its table, not from the previous one.
    cpu->m_binary.symbols = {symbol("titleMain", App + 0x100)};
    EXPECT_EQ(Core::Diag::symbolize(*cpu, App + 0x110), "0x02000110 <titleMain+0x10>");
}

TEST_F(DiagnosticsTest, CrashContextShowsRegistersObjectsBacktraceAndThreads)
{
    cpu->m_binary.symbols = {symbol("main", App + 0x100)};
    cpu->m_scheduler.bootstrap(*cpu);
    constexpr std::uint32_t Object = 0x10003000, Frame0 = 0x10004000, Frame1 = 0x10004100;
    cpu->m_memory.write<std::uint32_t>(Object, 0xCAFEBABE);
    cpu->m_memory.write<std::uint32_t>(Frame0, Frame1);
    cpu->m_memory.write<std::uint32_t>(Frame1, 0);
    cpu->m_memory.write<std::uint32_t>(Frame1 + 4, 0x120); // saved LR, code-relative
    cpu->m_gpr[1] = Frame0;
    cpu->m_gpr[3] = Object;
    cpu->m_gpr[31] = 0x00000010; // not a guest pointer: no peek
    cpu->m_lr = 0x108;

    testing::internal::CaptureStdout();
    Core::Diag::dumpCrashContext(*cpu, App + 0x104, "unit-test fault");
    const auto report = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(contains(report, "crash context: unit-test fault")) << report;
    EXPECT_TRUE(contains(report, "PC  = 0x02000104 <main+0x4>")) << report;
    EXPECT_TRUE(contains(report, "LR  = 0x02000108 <main+0x8>")) << report;
    EXPECT_TRUE(contains(report, "r3 =0x10003000")) << report;
    EXPECT_TRUE(contains(report, "r3  @ 0x10003000: +00=CAFEBABE")) << "the object r3 points to is dumped: " << report;
    EXPECT_FALSE(contains(report, "r31 @")) << "a small integer is not dereferenced";
    EXPECT_TRUE(contains(report, "#0  sp=0x10004100 ret=0x02000120 <main+0x20>")) << report;
    EXPECT_TRUE(contains(report, "Running pc=")) << "the thread list is part of the report: " << report;
    EXPECT_TRUE(contains(report, "==== end crash context ===="));
}

TEST_F(DiagnosticsTest, ImportsWithoutAHandlerReturnZeroAndAreListedMostFrequentFirst)
{
    cpu->m_binary.symbols = {Core::Symbol{}, symbol("OSGetCoreCount", 0), symbol("WemuTestMissingImport", 0)};
    cpu->m_lr = 0x400;

    // sc with the symbol index in BD: the loader writes this thunk for every imported function.
    cpu->m_gpr[3] = 99;
    Core::Instruction::SC(*cpu, EncodedInstruction(0x44000004));
    EXPECT_EQ(cpu->m_gpr[3], 3u) << "OSGetCoreCount ran";
    EXPECT_EQ(cpu->m_nextPc, 0x400u) << "and returned to the caller";

    for (int i = 0; i < 3; ++i) {
        cpu->m_gpr[3] = 99;
        Core::Instruction::SC(*cpu, EncodedInstruction(0x44000008));
        EXPECT_EQ(cpu->m_gpr[3], 0u) << "an import with no HLE handler returns 0";
        EXPECT_EQ(cpu->m_nextPc, 0x400u);
    }
    Core::Diag::noteUnknownImport("WemuTestRareImport", *cpu);

    testing::internal::CaptureStdout();
    Core::Diag::dumpUnknownImports();
    const auto summary = testing::internal::GetCapturedStdout();
    const auto frequent = summary.find("3  WemuTestMissingImport"), rare = summary.find("1  WemuTestRareImport");
    ASSERT_NE(frequent, std::string::npos) << summary;
    ASSERT_NE(rare, std::string::npos) << summary;
    EXPECT_LT(frequent, rare) << "most frequent first";
}

// ---- child processes -------------------------------------------------------------------------

class DiagnosticsChildTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            if (!std::getenv("WEMU_DIAG_CHILD"))
                GTEST_SKIP() << "runs only as a child of DiagnosticsToolsTest";
            InstructionTest::SetUp();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_hooks.clear();
            cpu->m_running = true;
            RegisterCoreinitFunctions();
        }

        // Sums 1..n (n = 10, or 100 once WEMU_PATCH rewrites `li r5,10`), storing the running sum at
        // Result+4, the total at Result and OSGetCoreCount() (called through an import thunk) at Result+8.
        void runProgram()
        {
            const std::vector<std::uint32_t> code{
                    0x7FE802A6, // 0x00 mflr  r31
                    0x3CC01000, // 0x04 lis   r6, 0x1000
                    0x60C62000, // 0x08 ori   r6, r6, 0x2000   (Text)
                    0x38600000, // 0x0C li    r3, 0
                    0x38800001, // 0x10 li    r4, 1
                    0x38A0000A, // 0x14 li    r5, 10
                    0x7CA903A6, // 0x18 mtctr r5
                    0x7C632214, // 0x1C loop: add  r3, r3, r4
                    0x38840001, // 0x20       addi r4, r4, 1
                    0x90690004, // 0x24       stw  r3, 4(r9)
                    0x4200FFF4, // 0x28       bdnz loop
                    0x90690000, // 0x2C stw   r3, 0(r9)
                    0x48000051, // 0x30 bl    0x80 (import thunk)
                    0x90690008, // 0x34 stw   r3, 8(r9)
                    0x7FE803A6, // 0x38 mtlr  r31
                    0x4E800020, // 0x3C blr
            };
            for (std::size_t i = 0; i < code.size(); ++i)
                cpu->m_memory.write<std::uint32_t>(App + static_cast<std::uint32_t>(i * 4), code[i]);
            cpu->m_memory.write<std::uint32_t>(App + 0x80, 0x44000004); // sc -> symbol 1
            cpu->m_binary.symbols = {Core::Symbol{}, symbol("OSGetCoreCount", 0)};
            const std::string text = "hello watch";
            for (std::size_t i = 0; i <= text.size(); ++i)
                cpu->m_memory.write<std::uint8_t>(Text + static_cast<std::uint32_t>(i), i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0);
            for (std::uint32_t off = 0; off < 12; off += 4)
                cpu->m_memory.write<std::uint32_t>(Result + off, 0);
            cpu->m_binary.header.e_entry = App;
            cpu->m_gpr[9] = Result;
            cpu->m_gpr[1] = 0xC0FFFFF0;
            cpu->m_lr = Core::RETURN_SENTINEL - App;
            cpu->run();
            ASSERT_FALSE(cpu->failed());
            EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result + 8), 3u);
            Core::Diag::dumpPcCounts();
            std::printf("[CHILD] sum=%u\n", cpu->m_memory.read<std::uint32_t>(Result));
        }
};

TEST_F(DiagnosticsChildTest, Program) { runProgram(); }

TEST_F(DiagnosticsChildTest, SignalDump)
{
    Core::Diag::installPcCountSignalDump();
    for (int i = 0; i < 3; ++i)
        Core::Diag::notePcCount(0x02000040);
    std::fflush(stdout);
    std::raise(SIGTERM);
}

TEST(DiagnosticsToolsTest, WatchToolsReportWhatTheGuestProgramDoes)
{
    const auto run = runChild("Program", {
                                                 "WEMU_PATCH=02000014:38A00064", // li r5,10 -> li r5,100
                                                 "WEMU_WATCH_PC=0200002C",
                                                 "WEMU_WATCH_DEREF=9,9+4",
                                                 "WEMU_WATCH_DEREF_WORDS=2",
                                                 "WEMU_WATCH_REG=02000020:r4=00000005,nonsense",
                                                 "WEMU_WATCH_CSTR=0200001C:r6,0200002C:@10002000,02000030:*r9",
                                                 "WEMU_WATCH_CSTR_MAX=2",
                                                 "WEMU_WATCH_WORD=10001004",
                                                 "WEMU_HEARTBEAT=100",
                                                 "WEMU_PC_COUNT=0200001C,0200003C",
                                                 "WEMU_TRACE_HLE_NAME=OSGetCoreCount",
                                         });
    const auto &log = run.output;
    ASSERT_EQ(run.exitCode, 0) << log;
    ASSERT_TRUE(contains(log, "[  PASSED  ] 1 test")) << log;
    EXPECT_TRUE(contains(log, "[DIAG] patched guest word 0x02000014 = 0x38A00064")) << log;
    EXPECT_TRUE(contains(log, "[CHILD] sum=5050")) << "the patched loop count was used: " << log;

    EXPECT_TRUE(contains(log, "[DIAG] watch PC 0x0200002C hit #1")) << log;
    EXPECT_TRUE(contains(log, "[r9+0x4=0x10001004]: 000013BA")) << "WEMU_WATCH_DEREF dumps words at rN+off: " << log;
    EXPECT_TRUE(contains(log, "[r9+0x0=0x10001000]: 00000000 000013BA")) << log;

    EXPECT_TRUE(contains(log, "[DIAG] watch reg PC=0x02000020 r4==0x00000005 hit #1")) << log;
    EXPECT_FALSE(contains(log, "hit #2 thread")) << "r4 is 5 only once";

    EXPECT_TRUE(contains(log, "watch cstr PC=0x0200001C r6 -> 0x10002000 hit #2 \"hello watch\"")) << log;
    EXPECT_FALSE(contains(log, "r6 -> 0x10002000 hit #3")) << "WEMU_WATCH_CSTR_MAX caps the hits";
    EXPECT_TRUE(contains(log, "watch cstr PC=0x0200002C @0x10002000 -> 0x10002000 hit #1 \"hello watch\"")) << log;
    EXPECT_TRUE(contains(log, "watch cstr PC=0x02000030 *r9 -> 0x000013BA hit #1 \"<unreadable>\""))
            << "a bad pointer is reported, not followed: " << log;

    EXPECT_TRUE(contains(log, "[DIAG] watch word 0x10001004: 0x00000000 -> 0x00000001")) << log;
    EXPECT_TRUE(contains(log, "[DIAG] watch word 0x10001004: 0x00001356 -> 0x000013BA")) << "the last change, sum 1..99 -> 1..100: " << log;
    EXPECT_TRUE(contains(log, "[DIAG] heartbeat 0M instr, PC=")) << log;
    EXPECT_TRUE(contains(log, "0x0200001C: 100")) << "WEMU_PC_COUNT tallies the loop head: " << log;
    EXPECT_TRUE(contains(log, "0x0200003C: 1")) << log;
    EXPECT_TRUE(contains(log, "[HLE] OSGetCoreCount(")) << "WEMU_TRACE_HLE_NAME traces the import call: " << log;
}

TEST(DiagnosticsToolsTest, MalformedWatchSpecsAreIgnoredAndTheTitleStillRuns)
{
    const auto run = runChild("Program", {
                                                 "WEMU_PATCH=nothex",
                                                 "WEMU_WATCH_CSTR=zz,0200001C:q5",
                                                 "WEMU_WATCH_REG=0200001C:r99=1",
                                                 "WEMU_WATCH_DEREF=x",
                                                 "WEMU_WATCH_PC=",
                                         });
    ASSERT_EQ(run.exitCode, 0) << run.output;
    EXPECT_TRUE(contains(run.output, "[CHILD] sum=55")) << run.output;
    EXPECT_FALSE(contains(run.output, "[DIAG] watch")) << run.output;
    EXPECT_FALSE(contains(run.output, "[DIAG] patched")) << run.output;
}

TEST(DiagnosticsToolsTest, PcCountsAreReportedEvenWhenTheProcessIsKilled)
{
    const auto run = runChild("SignalDump", {"WEMU_PC_COUNT=02000040,02000044"});
    EXPECT_EQ(run.exitCode, 128 + SIGTERM) << run.output;
    EXPECT_TRUE(contains(run.output, "[DIAG] WEMU_PC_COUNT tallies (killed by signal):")) << run.output;
    EXPECT_TRUE(contains(run.output, "0x02000040: 3")) << run.output;
    EXPECT_TRUE(contains(run.output, "0x02000044: 0")) << run.output;
}
