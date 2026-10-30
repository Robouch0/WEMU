#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Core::Gfx {
    enum class ShaderStage { Vertex, Fragment, Compute };

    struct SpirvModule {
            std::vector<std::uint32_t> words;
            std::string error;
            std::uint64_t id{}; // unique per successfully compiled module; stable while the module is cached
            explicit operator bool() const { return !words.empty() && error.empty(); }
    };

    // Single rendering-thread service. Compile/validate on a cache miss only;
    // no shell, and no guest filenames or code execute on the host.
    // Initial external-tool implementation can be replaced by an in-process
    // compiler without changing the renderer's SPIR-V contract.
    //
    // A non-empty cache directory additionally persists validated modules across sessions, keyed by
    // a hash of stage and source; each file also stores the full source, which must match exactly.
    // The directory must be private to the current user, like the PPC native object cache.
    class SpirvCompiler {
        public:
            SpirvCompiler() = default;
            explicit SpirvCompiler(const std::string &cacheDirectory);
            ~SpirvCompiler();
            SpirvCompiler(const SpirvCompiler &) = delete;
            SpirvCompiler &operator=(const SpirvCompiler &) = delete;
            static bool available() noexcept;
            // Default persistent location: WEMU_SHADER_CACHE_DIR (empty disables), else
            // $XDG_CACHE_HOME/wemu/spirv or ~/.cache/wemu/spirv.
            static std::string defaultCacheDirectory();
            std::shared_ptr<const SpirvModule> compile(ShaderStage stage, std::string_view source);
            std::size_t cachedModules() const noexcept
            {
                std::size_t count = 0;
                for (const auto &stage: m_cache)
                    count += stage.size();
                return count;
            }
            std::uint64_t diskHits() const noexcept { return m_diskHits; }

        private:
            struct SourceHash {
                    using is_transparent = void;
                    std::size_t operator()(std::string_view source) const noexcept { return std::hash<std::string_view>{}(source); }
            };
            using StageCache = std::unordered_map<std::string, std::shared_ptr<const SpirvModule>, SourceHash, std::equal_to<>>;
            std::array<StageCache, 3> m_cache;
            std::size_t m_cacheBytes{};
            int m_directory{-1};
            std::uint64_t m_diskHits{};
            std::shared_ptr<SpirvModule> loadCached(ShaderStage stage, std::string_view source) const;
            void storeCached(ShaderStage stage, std::string_view source, const SpirvModule &module) const;
    };
} // namespace Core::Gfx
