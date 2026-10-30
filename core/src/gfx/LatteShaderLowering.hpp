#pragma once

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>

#include "gfx/LatteProgram.hpp"

namespace Core::Gfx::Latte {

    enum class TextureType { TwoD, TwoDArray, ThreeD, Cube };

    struct TextureBinding {
            unsigned resource, sampler, binding;
            TextureType type{TextureType::TwoD};
            bool implicitLod{};
            bool explicitLod{};
            bool derivativeLod{}; // SAMPLE coordinates proven available across the whole quad.
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
            bool kills{}; // KILL* instructions can discard the pixel.
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
            // Compute stream-output metadata; zero for ordinary graphics stages.
            unsigned streamBufferMask{};
            std::array<unsigned, 4> streamStrides{};
            // Constant registers u.c[0, constantRegisters) the program may read; hand-written
            // shaders keep the whole file.
            unsigned constantRegisters{256};
            explicit operator bool() const { return error.empty() && !source.empty(); }
    };

    // u.k holds the uniform vectors a program reads after c[256], borders[16] and sampling[16]:
    // kcache references first (at most 512), then one window per dynamically indexed vertex
    // uniform block. The windows share what the references leave of 512 vectors unless the
    // replayer asks for windows sized for its blocks.
    constexpr unsigned kUniformVectors = 2048;
    constexpr std::size_t kStageConstantFloats = (256 + 16 + 16 + kUniformVectors) * 4;

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
                                       unsigned colorExport = 0, unsigned positionRegister = 32);
    struct VertexShader : FragmentShader {};
    using UniformBufferWindows = std::array<unsigned, 16>;
    // Compute form of an unmasked, uniform-buffer-only vertex stream-output pass.
    // Strides are bytes; partial component writes preserve the supplied output bytes.
    FragmentShader lowerStreamOutShader(const Program &program, const std::array<unsigned, 4> &strides,
                                        const UniformBufferWindows &bufferWindows = {});
    // bufferWindow: vectors per dynamically indexed uniform block (0: the shared default budget).
    VertexShader lowerVertexShader(const Program &program, std::span<const TextureType> resourceTypes = {}, unsigned bufferWindow = 0,
                                   const UniformBufferWindows &bufferWindows = {});
    class VertexShaderCache {
        public:
            std::shared_ptr<const VertexShader> get(const std::vector<std::uint32_t> &words, unsigned bufferWindow = 0,
                                                    const UniformBufferWindows &bufferWindows = {});

        private:
            std::unordered_map<std::vector<std::uint32_t>, std::shared_ptr<const VertexShader>, ProgramWordsHash, ProgramWordsEqual> m_entries;
            std::map<std::tuple<unsigned, UniformBufferWindows, std::vector<std::uint32_t>>, std::shared_ptr<const VertexShader>> m_windowed;
    };

    // Replayer-owned, single-threaded cache. Exact keys include resource specialization.
    class FragmentShaderCache {
        public:
            // A scene can use several hundred programs; clearing below that working set
            // re-lowers (and recompiles) every shader each frame.
            static constexpr std::size_t maxEntries = 4096;
            std::shared_ptr<const FragmentShader> get(const std::vector<std::uint32_t> &words, std::span<const TextureType> resourceTypes = {},
                                                      bool onlyColorZero = false, unsigned colorExport = 0, unsigned positionRegister = 32);
            // Coverage/depth-effect proof, without requiring usable texture results.
            bool hasUnconditionalColorOnlyEffects(const std::vector<std::uint32_t> &words);
            std::size_t size() const { return m_entries.size(); }
            std::size_t hits() const { return m_hits; }
            std::size_t misses() const { return m_misses; }

        private:
            struct KeyView {
                    std::span<const std::uint32_t> words;
                    std::span<const TextureType> resourceTypes;
                    bool onlyColorZero{};
                    unsigned colorExport{};
                    unsigned positionRegister{32};
            };
            struct Key {
                    std::vector<std::uint32_t> words;
                    std::vector<TextureType> resourceTypes;
                    bool onlyColorZero{};
                    unsigned colorExport{};
                    unsigned positionRegister{32};
                    operator KeyView() const { return {words, resourceTypes, onlyColorZero, colorExport, positionRegister}; }
            };
            struct KeyHash {
                    using is_transparent = void;
                    std::size_t operator()(const KeyView &key) const noexcept
                    {
                        auto hash = ProgramWordsHash{}(key.words);
                        for (const auto type: key.resourceTypes)
                            hash = hash * 31 + std::size_t(type);
                        return hash ^ (std::size_t(key.onlyColorZero) << 8) ^ (std::size_t(key.colorExport) << 16) ^
                               (std::size_t(key.positionRegister) << 24);
                    }
            };
            struct KeyEqual {
                    using is_transparent = void;
                    bool operator()(const KeyView &a, const KeyView &b) const noexcept
                    {
                        return a.onlyColorZero == b.onlyColorZero && a.colorExport == b.colorExport && a.positionRegister == b.positionRegister &&
                               std::ranges::equal(a.words, b.words) && std::ranges::equal(a.resourceTypes, b.resourceTypes);
                    }
            };
            std::unordered_map<Key, std::shared_ptr<const FragmentShader>, KeyHash, KeyEqual> m_entries;
            std::unordered_map<std::vector<std::uint32_t>, bool, ProgramWordsHash, ProgramWordsEqual> m_colorOnlyEffects;
            std::size_t m_bytes{}, m_hits{}, m_misses{};
    };

} // namespace Core::Gfx::Latte
