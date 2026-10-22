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
    enum class TextureBlockFormat { None, BC1, BC4, BC4Signed, BC5Signed };
    class RasterReadback;
    // Backend-specific, versioned image identity. Never exposes a borrowed handle.
    struct RasterImage {
            virtual ~RasterImage() = default;
    };
    // A target cleared on the CPU side that nothing has drawn into since: a backend may clear its
    // image to `color` (the stored element, decoded) instead of uploading the resolved bytes.
    struct ClearedImage final : RasterImage {
            std::array<float, 4> color;
            explicit ClearedImage(const std::array<float, 4> &value) : color(value) {}
    };
    // Synchronous reference-raster ABI. All borrowed data expires when render returns.
    // Positions include window depth and reciprocal clip W. Varyings interpolate
    // perspectively for positive W; full clipping and multisampling are
    // outside this ABI's validated native subset.
    // Texture views expose only reference base-level texels. Shader output is
    // rounded to RGBA8 before integer blending on UNORM targets. RGBA32 float
    // and packed R11G11B10 targets preserve HDR values and use float blending. A GPU
    // implementation must validate its conversion/blend tolerances separately.
    struct RasterDraw {
            struct Vertex {
                    float x{}, y{};
                    std::array<std::array<float, 4>, 16> inputs{};
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
                    // Arrays require explicit CPU snapshots or renderedLayers.
                    unsigned layers{1};
                    // Optional rendered layers of a 2D array view, one current version per layer,
                    // laid out like `rendered` (unorm8Pitch, unorm8Map, renderedFormat/Depth).
                    // With renderedLevels > 1 (2D, array or cube views; backends advertising
                    // supportsRenderedMipLevels), entries are level-major: levels x layers, each
                    // level max(1, size >> level), and every entry must still be resident.
                    std::vector<std::shared_ptr<RasterReadback>> renderedLayers;
                    unsigned renderedLevels{1};
                    // Optional little-endian R32 texels, with pitch in texels. Avoids
                    // expanding guest GPU words into a temporary host float array.
                    std::span<const std::uint8_t> r32Bytes;
                    unsigned r32Pitch{};
                    ColorFormat renderedFormat{ColorFormat::RGBA8};
                    bool renderedDepth{};
                    std::span<const std::uint8_t> rgba32Bytes;
                    unsigned rgba32Pitch{};
                    // Original guest resource, independent of the base-only upload.
                    // Zero mip count means unknown, so it cannot prove LOD invariance.
                    unsigned sourceMipCount{}, sourceFirstMip{};
                    // Little-endian packed unsigned HDR texels, before component mapping.
                    std::span<const std::uint8_t> r11g11b10Bytes;
                    unsigned r11g11b10Pitch{};
                    struct MipLevel {
                            unsigned width{}, height{};
                            std::span<const float> rgba32; // tightly packed, layer-major, component-mapped
                    };
                    // Additional levels after the base snapshot. Multilevel uploads
                    // currently use component-mapped RGBA32F for every level.
                    std::span<const MipLevel> mips;
                    // Identity/lifetime of immutable snapshot bytes. A changed image
                    // must supply a new owner; permits sharing uploads across draws.
                    std::shared_ptr<const void> snapshotOwner;
                    // Tightly packed base-level 4x4 blocks, before view mapping.
                    std::span<const std::uint8_t> blocks;
                    TextureBlockFormat blockFormat{};
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
            // The caller replaces its only reference to renderedTarget with this draw's result,
            // so that version is never resolved afterwards: a backend may render into its image
            // in place instead of preserving it.
            bool supersedesTarget{};
            struct Depth {
                    using Format = DepthFormat;
                    bool test{}, write{};
                    unsigned compare{7}; // GX2/Vulkan: NEVER..ALWAYS
                    Format format{Format::Float32};
                    std::span<const float> target; // canonical depth, including padding
                    unsigned width{}, height{}, pitch{};
                    std::shared_ptr<RasterReadback> rendered;
                    // The surface was cleared to this stored value since `target` was written:
                    // every element of the width x height area holds it, padding keeps `target`.
                    std::optional<float> clear;
                    // The guest disabled Z clipping: window depth outside [0, 1] is clamped per
                    // fragment, as the reference rasterizer does, instead of clipping the primitive.
                    bool clamp{};
                    struct Stencil {
                            bool test{};
                            // GX2 DB_DEPTH_CONTROL and front/back DB_STENCILREFMASK.
                            std::uint32_t control{};
                            std::array<std::uint32_t, 2> masks{};
                            std::span<const std::uint8_t> target; // canonical byte plane, same pitch as depth
                            std::shared_ptr<RasterReadback> rendered;
                            std::optional<std::uint8_t> clear;
                    } stencil;
            } depth;
            enum class Topology { Triangles, Points };
            Topology topology{Topology::Triangles};
            // Triangle-list indices into vertices; empty draws vertices in order. Shared corners are
            // then built and uploaded once per draw.
            std::span<const std::uint32_t> indices;
            ColorFormat colorFormat{ColorFormat::RGBA8};
            struct VertexStage {
                    std::shared_ptr<const Latte::VertexShader> shader;
                    std::span<const float> constants;
                    std::span<const float> uniforms;
                    std::array<int, 16> inputParams = [] {
                        std::array<int, 16> inputs;
                        inputs.fill(-1);
                        return inputs;
                    }();
                    std::array<float, 6> viewport{};
                    unsigned inputCount{4};
                    // Optional compact program inputs replacing `vertices` (then empty): for each
                    // vertex, attributeStride consecutive attributes 0..stride-1. Attributes from
                    // attributeStride on read the reference defaults ({1,1,1,1}, three {0,0,0,1},
                    // then zeros). Positions come from the program, so none are needed.
                    std::span<const std::array<float, 4>> attributes;
                    unsigned attributeStride{};
                    // Identity/lifetime of immutable `attributes` and the draw's `indices`: a changed
                    // array must supply a new owner. Lets backends keep one upload across draws.
                    std::shared_ptr<const void> attributesOwner;
            } vertexStage;
            // Static kcache references in shader.uniforms, four floats each;
            // replayer snapshots the actual fragment uniform-block values.
            std::span<const float> uniforms;
            // Effective writable exports from current guest buffer bindings/masks.
            unsigned enabledColorTargets{1};
            bool cullFront{}, cullBack{}, frontFaceClockwise{};
            struct DepthBias {
                    bool enabled{};
                    float constant{}, slope{}, clamp{};
            } depthBias;
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
            // Byte size of a target the draw left exactly as passed (no color attachment): rgba
            // then stays empty and callers keep their bytes.
            std::size_t unchangedTarget{};
            std::vector<float> depth;
            std::shared_ptr<RasterReadback> depthReadback;
            std::shared_ptr<RasterReadback> stencilReadback;
            std::size_t size() const { return readback ? readback->size() : unchangedTarget ? unchangedTarget : rgba.size(); }
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
            // Fixed-function stencil on canonical UNORM24 depth/stencil storage.
            virtual bool supportsStencil() const { return false; }
            virtual bool supportsPoints() const { return false; }
            virtual bool supportsFloatTargets() const { return false; }
            virtual bool supportsVertexStage() const { return false; }
            virtual bool supportsBlockTextures() const { return false; }
            // Textures may carry rendered mip levels and cube faces (Texture::renderedLevels).
            virtual bool supportsRenderedMipLevels() const { return false; }
            // Same borrowed-input lifetime as render; only the owned readback may
            // outlive this call. Backends without deferred support remain synchronous.
            virtual std::optional<RasterResult> renderDeferred(const RasterDraw &draw) { return render(draw); }
    };
} // namespace Core::Gfx
