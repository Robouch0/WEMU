#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Core::Gfx {
    enum class ShaderStage { Vertex, Fragment, Compute };

    struct SpirvModule {
            std::vector<std::uint32_t> words;
            std::string error;
            explicit operator bool() const { return !words.empty() && error.empty(); }
    };

    // Single rendering-thread service. Compile/validate on a cache miss only;
    // no shell, and no guest filenames or code execute on the host.
    // Initial external-tool implementation can be replaced by an in-process
    // compiler without changing the renderer's SPIR-V contract.
    class SpirvCompiler {
        public:
            static bool available() noexcept;
            std::shared_ptr<const SpirvModule> compile(ShaderStage stage, std::string_view source);
            std::size_t cachedModules() const noexcept { return m_cache.size(); }

        private:
            std::map<std::pair<ShaderStage, std::string>, std::shared_ptr<const SpirvModule>> m_cache;
            std::size_t m_cacheBytes{};
    };
} // namespace Core::Gfx
