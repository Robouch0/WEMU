#pragma once

#include <map>
#include <memory>
#include <span>
#include <string>
#include <tuple>

#include "gfx/LatteProgram.hpp"

namespace Core::Gfx::Latte {

    enum class TextureType { TwoD, TwoDArray, ThreeD, Cube };

    struct TextureBinding {
            unsigned resource, sampler, binding;
            TextureType type{TextureType::TwoD};
            bool implicitLod{};
            bool explicitLod{};
    };

    struct FragmentShader {
            std::string source;
            // Hash of `source`, computed once by lowering (zero for hand-written shaders): lets the
            // backend find compiled modules without hashing the whole GLSL text on every draw.
            std::uint64_t sourceHash{};
            std::string error;
            std::vector<TextureBinding> textures;
            bool requiresBaseLevelOnly{};
            // Varying whole-quad masks need a runtime proof that implicit
            // sampling cannot observe derivatives (one mip, equal filters).
            bool requiresDerivativeFreeSampling{};
            bool usesGather{};
            bool exportsColor{true};
            bool writesDepth{};
            bool alwaysWritesColor{}; // Proven unconditional export, with no lane-mask changes, jumps or kills.
            bool kills{};             // KILL* instructions can discard the pixel.
            bool requiresOnlyColorZero{}; // Other direct color exports were proven disabled by draw state.
            struct UniformReference {
                    unsigned bank{}, index{};
                    bool operator==(const UniformReference &) const = default;
            };
            std::vector<UniformReference> uniforms;
            struct UniformBuffer {
                    unsigned bank{}, first{}, count{}; // contiguous slots in u.k for dynamic VTX_FETCH
            };
            std::vector<UniformBuffer> uniformBuffers;
            unsigned parameterMask{};
            explicit operator bool() const { return error.empty() && !source.empty(); }
    };

    // Initial reference-compatible subset, not a hardware-complete shader compiler.
    // ABI: locations 0..15 = interpolated inputs; set 0 binding 0 = 256 vec4 constants
    // (unused slots zero-filled); remaining bindings are normalized sampler2D or
    // sampler2DArray/samplerCubeArray pairs. Resource types specialize source and shader cache keys.
    // colorExport selects a direct guest color slot for an independent attachment
    // pass when onlyColorZero is true; it maps to native output location zero.
    // Unsupported programs return no source. The renderer must also validate resource
    // dimensions/formats, interpolation, and pipeline state before native dispatch.
    // Whole-quad ALU is accepted when helper writes stay inside their mask or are
    // safely overwritten. Forward edges merge helper-value provenance. Uniform
    // execute masks can retain live values across reconvergence. Varying escaping
    // values retain fallback; implicit consumers require derivative-free textures.
    FragmentShader lowerFragmentShader(const Program &program, std::span<const TextureType> resourceTypes = {}, bool onlyColorZero = false,
                                       unsigned colorExport = 0);
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
            // A scene can use several hundred programs; clearing below that working set
            // re-lowers (and recompiles) every shader each frame.
            static constexpr std::size_t maxEntries = 4096;
            std::shared_ptr<const FragmentShader> get(const std::vector<std::uint32_t> &words, std::span<const TextureType> resourceTypes = {},
                                                      bool onlyColorZero = false, unsigned colorExport = 0);
            // Coverage/depth-effect proof, without requiring usable texture results.
            bool hasUnconditionalColorOnlyEffects(const std::vector<std::uint32_t> &words);
            std::size_t size() const { return m_entries.size(); }
            std::size_t hits() const { return m_hits; }
            std::size_t misses() const { return m_misses; }

        private:
            using Key = std::tuple<std::vector<std::uint32_t>, std::vector<TextureType>, bool, unsigned>;
            std::map<Key, std::shared_ptr<const FragmentShader>> m_entries;
            std::map<std::vector<std::uint32_t>, bool> m_colorOnlyEffects;
            std::size_t m_bytes{}, m_hits{}, m_misses{};
    };

} // namespace Core::Gfx::Latte
