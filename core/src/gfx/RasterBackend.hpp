#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include "gfx/ColorTarget.hpp"
#include "gfx/Depth.hpp"
#include "gfx/LatteShaderLowering.hpp"
#include "gfx/TextureSampler.hpp"

namespace Core::Gfx {
    class RasterReadback;
    // Backend-specific, versioned image identity. Never exposes a borrowed handle.
    struct RasterImage {
            virtual ~RasterImage() = default;
    };
    // Synchronous reference-raster ABI. All borrowed data expires when render returns.
    // Positions include window depth and reciprocal clip W. Varyings interpolate
    // perspectively for positive W; full clipping, stencil and multisampling are
    // outside this ABI's validated native subset.
    // Texture views expose only reference base-level texels. Shader output is
    // rounded to RGBA8 before integer blending on UNORM targets. RGBA32 float
    // targets preserve shader floats and use float blending. A GPU
    // implementation must validate its conversion/blend tolerances separately.
    struct RasterDraw {
            struct Vertex {
                    float x{}, y{};
                    std::array<std::array<float, 4>, 4> inputs{};
                    float z{}, reciprocalW{1.f}; // window depth and perspective divisor
            };
            struct Texture {
                    Latte::TextureBinding binding;
                    unsigned width{}, height{};
                    TextureSampler sampler;
                    // Unfiltered, component-mapped reference texels. Exact target aliases
                    // read the pre-draw snapshot, never partially committed output.
                    std::function<std::array<float, 4>(unsigned, unsigned)> texel;
                    // Optional UNORM8 snapshot, with pitch in texels. One-channel
                    // input expands to R,R,R,1; two-channel to R,G,0,1 before mapping.
                    std::span<const std::uint8_t> unorm8;
                    unsigned unorm8Pitch{};
                    std::uint32_t unorm8Map{0x00010203};
                    // Optional tightly packed, component-mapped float texels.
                    std::span<const float> rgba32;
                    // Optional tightly packed R32 float values, before component mapping.
                    std::span<const float> r32;
                    std::uint32_t r32Map{0x00010203};
                    unsigned unorm8Channels{4};
                    // Optional rendered typed color/depth source. Overrides CPU snapshots;
                    // unorm8Pitch/Map describe its layout and component mapping.
                    std::shared_ptr<RasterReadback> rendered;
                    // Array snapshots are layer-major, with identical row pitches.
                    // Arrays require explicit CPU snapshots; rendered aliases remain 2D.
                    unsigned layers{1};
                    // Optional little-endian R32 texels, with pitch in texels. Avoids
                    // expanding guest GPU words into a temporary host float array.
                    std::span<const std::uint8_t> r32Bytes;
                    unsigned r32Pitch{};
                    ColorFormat renderedFormat{ColorFormat::RGBA8};
                    bool renderedDepth{};
                    std::span<const std::uint8_t> rgba32Bytes;
                    unsigned rgba32Pitch{};
            };
            struct Blend {
                    bool enabled{};
                    unsigned colorSource{}, colorDestination{}, colorOperation{};
                    unsigned alphaSource{}, alphaDestination{}, alphaOperation{};
                    std::array<std::uint8_t, 4> constant{};
            };
            const Latte::FragmentShader &shader;
            std::span<const Vertex> vertices; // ordered list, selected by topology
            std::span<const float> constants; // missing entries must read as zero
            std::span<const Texture> textures;
            std::span<const std::uint8_t> target; // canonical colorFormat bytes, including row padding
            unsigned width{}, height{}, pitch{};
            std::array<std::int32_t, 4> scissor{}; // x, y, width, height
            unsigned channelMask{};
            Blend blend;
            // Optional complete pre-draw target. Backends advertising this
            // capability may preserve active pixels on GPU; CPU fallback resolves it.
            std::shared_ptr<RasterReadback> renderedTarget;
            struct Depth {
                    using Format = DepthFormat;
                    bool test{}, write{};
                    unsigned compare{7}; // GX2/Vulkan: NEVER..ALWAYS
                    Format format{Format::Float32};
                    std::span<const float> target; // canonical depth, including padding
                    unsigned width{}, height{}, pitch{};
                    std::shared_ptr<RasterReadback> rendered;
            } depth;
            enum class Topology { Triangles, Points };
            Topology topology{Topology::Triangles};
            ColorFormat colorFormat{ColorFormat::RGBA8};
            struct VertexStage {
                    std::shared_ptr<const Latte::VertexShader> shader;
                    std::span<const float> constants;
                    std::span<const float> uniforms;
                    std::array<int, 4> inputParams{-1, -1, -1, -1};
                    std::array<float, 6> viewport{};
            } vertexStage;
    };

    // Owns all data/resources needed after renderDeferred returns. Materialization
    // is idempotent, and a failed/incomplete readback never becomes a valid surface.
    class RasterReadback {
        public:
            RasterReadback(std::size_t bytes, std::function<std::vector<std::uint8_t>()> read, std::shared_ptr<const RasterImage> image = {}) :
                m_size(bytes), m_read(std::move(read)), m_image(std::move(image))
            {
                if (!m_read)
                    throw std::invalid_argument("Missing raster readback operation");
            }
            std::size_t size() const { return m_size; }
            const std::shared_ptr<const RasterImage> &image() const { return m_image; }
            const std::vector<std::uint8_t> &resolve()
            {
                if (m_error)
                    std::rethrow_exception(m_error);
                try {
                    if (m_read) {
                        auto pixels = m_read();
                        if (pixels.size() != m_size)
                            throw std::runtime_error("Raster readback returned an incomplete target");
                        m_pixels = std::move(pixels);
                        m_read = {};
                    }
                } catch (...) {
                    m_error = std::current_exception();
                    m_read = {};
                    throw;
                }
                return m_pixels;
            }

        private:
            std::size_t m_size;
            std::function<std::vector<std::uint8_t>()> m_read;
            std::vector<std::uint8_t> m_pixels;
            std::exception_ptr m_error;
            std::shared_ptr<const RasterImage> m_image;
    };

    struct RasterResult {
            std::vector<std::uint8_t> rgba; // complete target, including preserved padding
            std::uint64_t pixels{}, alphaSum{};
            std::shared_ptr<RasterReadback> readback;
            std::vector<float> depth;
            std::shared_ptr<RasterReadback> depthReadback;
            std::size_t size() const { return readback ? readback->size() : rgba.size(); }
            void resolve()
            {
                if (readback) {
                    rgba = readback->resolve();
                    readback.reset();
                }
            }
            void resolveDepth()
            {
                if (depthReadback) {
                    const auto &bytes = depthReadback->resolve();
                    if (bytes.size() % sizeof(float))
                        throw std::runtime_error("Invalid depth readback size");
                    depth.resize(bytes.size() / sizeof(float));
                    std::memcpy(depth.data(), bytes.data(), bytes.size());
                    depthReadback.reset();
                }
            }
    };

    class RasterBackend {
        public:
            virtual ~RasterBackend() = default;
            // No retained views or modifications of borrowed inputs. nullopt means
            // unsupported: the caller executes the unchanged draw in software.
            // A result commits only after its size has been checked. Backend errors
            // propagate rather than silently discarding partially executed work.
            virtual std::optional<RasterResult> render(const RasterDraw &draw) = 0;
            virtual bool supportsRenderedTextures() const { return false; }
            virtual bool supportsRenderedTargets() const { return false; }
            virtual bool supportsDepth() const { return false; }
            virtual bool supportsPoints() const { return false; }
            virtual bool supportsFloatTargets() const { return false; }
            virtual bool supportsVertexStage() const { return false; }
            // Same borrowed-input lifetime as render; only the owned readback may
            // outlive this call. Backends without deferred support remain synchronous.
            virtual std::optional<RasterResult> renderDeferred(const RasterDraw &draw) { return render(draw); }
    };
} // namespace Core::Gfx
