#pragma once

#include <map>
#include <memory>
#include <span>
#include <string>

#include "gfx/LatteProgram.hpp"

namespace Core::Gfx::Latte {

    enum class TextureType { TwoD, TwoDArray };

    struct TextureBinding {
            unsigned resource, sampler, binding;
            TextureType type{TextureType::TwoD};
    };

    struct FragmentShader {
            std::string source;
            std::string error;
            std::vector<TextureBinding> textures;
            bool requiresBaseLevelOnly{};
            bool usesGather{};
            bool exportsColor{true};
            bool writesDepth{};
            struct UniformReference {
                    unsigned bank{}, index{};
                    bool operator==(const UniformReference &) const = default;
            };
            std::vector<UniformReference> uniforms;
            unsigned parameterMask{};
            explicit operator bool() const { return error.empty() && !source.empty(); }
    };

    // Initial reference-compatible subset, not a hardware-complete shader compiler.
    // ABI: locations 0..3 = interpolated inputs; set 0 binding 0 = 256 vec4 constants
    // (unused slots zero-filled); remaining bindings are normalized sampler2D or
    // sampler2DArray pairs. Resource types specialize source and shader cache keys.
    // Unsupported programs return no source. The renderer must also validate resource
    // dimensions/formats, interpolation, and pipeline state before native dispatch.
    // Whole-quad ALU is accepted only when helper writes cannot escape an enclosing
    // mask and no implicit derivatives consume them; general quad execution falls back.
    FragmentShader lowerFragmentShader(const Program &program, std::span<const TextureType> resourceTypes = {});
    struct VertexShader : FragmentShader {};
    VertexShader lowerVertexShader(const Program &program, std::span<const TextureType> resourceTypes = {});
    class VertexShaderCache {
        public:
            std::shared_ptr<const VertexShader> get(const std::vector<std::uint32_t> &words);

        private:
            std::map<std::vector<std::uint32_t>, std::shared_ptr<const VertexShader>> m_entries;
    };

    // Replayer-owned, single-threaded cache. Exact keys include resource specialization.
    class FragmentShaderCache {
        public:
            std::shared_ptr<const FragmentShader> get(const std::vector<std::uint32_t> &words, std::span<const TextureType> resourceTypes = {});
            std::size_t size() const { return m_entries.size(); }
            std::size_t hits() const { return m_hits; }
            std::size_t misses() const { return m_misses; }

        private:
            using Key = std::pair<std::vector<std::uint32_t>, std::vector<TextureType>>;
            std::map<Key, std::shared_ptr<const FragmentShader>> m_entries;
            std::size_t m_bytes{}, m_hits{}, m_misses{};
    };

} // namespace Core::Gfx::Latte
