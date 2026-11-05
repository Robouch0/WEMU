#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>

#include "gfx/SpirvCompiler.hpp"

using namespace Core::Gfx;

namespace {
    struct TemporaryDirectory {
            std::filesystem::path path;
            TemporaryDirectory()
            {
                char name[] = "/tmp/wemu-spirv-test-XXXXXX";
                const auto *result = ::mkdtemp(name);
                if (!result)
                    throw std::runtime_error("Cannot create temporary directory");
                path = result;
            }
            ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    };

    constexpr auto fragmentSource = R"(#version 450
layout(location=0) in vec4 color;
layout(location=0) out vec4 target;
layout(set=0, binding=1) uniform sampler2D image;
void main() {
    vec4 sampled = texture(image, color.xy);
    target = sampled.a > 0.5 ? sampled * color : vec4(color.rgb, 1.0);
}
)";
} // namespace

TEST(SpirvCompilerTest, InProcessCompilationMatchesTheStandaloneCompiler)
{
#ifndef WEMU_HAS_GLSLANG
    GTEST_SKIP() << "Built without glslang's development package";
#else
    SpirvCompiler compiler;
    ASSERT_TRUE(SpirvCompiler::available());
    const auto module = compiler.compile(ShaderStage::Fragment, fragmentSource);
    ASSERT_TRUE(*module) << module->error;
    EXPECT_EQ(module->words[0], 0x07230203u);
    if (std::system("command -v glslangValidator > /dev/null 2>&1") != 0)
        GTEST_SKIP() << "glslangValidator is not installed";
    TemporaryDirectory directory;
    std::ofstream(directory.path / "shader.frag") << fragmentSource;
    const auto command = "glslangValidator -V -Os --target-env vulkan1.0 -o " + (directory.path / "shader.spv").string() + " " +
                         (directory.path / "shader.frag").string() + " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);
    std::ifstream file(directory.path / "shader.spv", std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    ASSERT_EQ(bytes.size(), module->words.size() * 4);
    EXPECT_EQ(std::memcmp(bytes.data(), module->words.data(), bytes.size()), 0);
#endif
}

TEST(SpirvCompilerTest, RejectedSourceReportsTheCompilerDiagnosticAndIsNotCachedOnDisk)
{
    if (!SpirvCompiler::available())
        GTEST_SKIP();
    TemporaryDirectory directory;
    SpirvCompiler compiler((directory.path / "cache").string());
    const auto module = compiler.compile(ShaderStage::Fragment, "#version 450\nvoid main() { undefinedCall(); }\n");
    EXPECT_FALSE(*module);
    EXPECT_NE(module->error.find("undefinedCall"), std::string::npos) << module->error;
    EXPECT_TRUE(std::filesystem::is_empty(directory.path / "cache"));
}

TEST(SpirvCompilerTest, CompilersOnSeveralThreadsProduceIdenticalModules)
{
    if (!SpirvCompiler::available())
        GTEST_SKIP();
    std::vector<std::vector<std::uint32_t>> results(6);
    std::vector<std::thread> threads;
    for (auto &result: results)
        threads.emplace_back([&result] {
            SpirvCompiler compiler;
            for (int i = 0; i < 4; ++i) {
                const auto source = std::string(fragmentSource) + "// variant " + std::to_string(i) + "\n";
                const auto module = compiler.compile(ShaderStage::Fragment, source);
                if (*module && i == 0)
                    result = module->words;
            }
        });
    for (auto &thread: threads)
        thread.join();
    ASSERT_FALSE(results[0].empty());
    for (const auto &result: results)
        EXPECT_EQ(result, results[0]);
}
