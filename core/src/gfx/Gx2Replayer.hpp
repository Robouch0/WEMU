#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gfx/Gx2CommandStream.hpp"
#include "gfx/LatteShaderLowering.hpp"
#include "gfx/RasterBackend.hpp"
#include "gfx/RasterWorkers.hpp"
#include "gfx/TextureSampler.hpp"

namespace Core {
    class Memory;
}

class Renderer; // global (core/src/gfx/Renderer.hpp)

// GX2 -> framebuffer replay (first stage of GX2->Vulkan presentation).
//
// MK8's menu frame is a compositing chain: draws render into offscreen colour buffers (each
// sampling UI textures or the previous buffer), and the final buffer is copied to the scan
// buffer. The replayer emulates the chain on the CPU: draws rasterize into per-surface-view
// backings for CPU-produced render targets, so MK8's same-address/different-view targets do not
// corrupt each other. CopyColorBufferToScanBuffer transfers the selected view into m_fb. Vertex
// shader execution handles placed UI where supported; unsupported draws fall back to captured
// position/uv heuristics. The bound texture is sampled with nearest-neighbour + alpha blending.
// Original tiled/BC assets are approximated but decoded by format where implemented.
// The framebuffer is handed to Renderer::flip_tv.
// Frame-building is pure (no Vulkan) so it is unit-testable.

namespace Core::Gfx {

    class Gx2Replayer {
            std::array<TextureSampler, 18> m_pixelSamplers{}, m_vertexSamplers{};

        public:
            static constexpr std::uint32_t kWidth = 1280;
            static constexpr std::uint32_t kHeight = 720;
            static constexpr std::uint32_t kDrcWidth = 854;
            static constexpr std::uint32_t kDrcHeight = 480;

            // A count of one retains the serial reference path. Zero selects the default.
            void setRasterWorkerCount(unsigned count, std::uint64_t minPixels = 32768)
            {
                m_rasterWorkers.reset();
                m_rasterWorkerCount = count;
                m_parallelMinPixels = minPixels;
            }

            // Diagnostic: compare every Nth eligible triangle with serial execution.
            // Zero disables comparison; unmatched output aborts replay.
            void setRasterVerificationInterval(std::uint64_t interval) { m_rasterVerifyEvery = interval; }
            [[nodiscard]] std::uint64_t verifiedRasterTriangles() const noexcept { return m_verifiedRasterTriangles; }

            // Processes all commands in the stream; rebuilds the framebuffer on each present
            // command. mem grants access to guest surfaces — without it only clears are replayed.
            void replay(const Gx2CommandStream &stream, Core::Memory *mem = nullptr);

            [[nodiscard]] const std::vector<std::uint8_t> &framebuffer() const noexcept { return m_fb; }
            // GamePad (DRC) scan-out, kDrcWidth*kDrcHeight RGBX. Built only while capture is enabled
            // (a remote GamePad is attached) and empty until the title scans a surface out to the DRC.
            void setDrcCapture(bool on) noexcept { m_drcCapture = on; }
            [[nodiscard]] const std::vector<std::uint8_t> &drcFramebuffer() const noexcept { return m_drcFb; }
            [[nodiscard]] std::uint32_t presentCount() const noexcept { return m_presentCount; }

            // GPU rasterisation (WEMU_GPU=1). When a renderer is supplied, each draw's screen-space
            // triangles are computed on the CPU (reusing all the shader/placement logic) and filled
            // on the GPU instead of by the software rasteriser; the result is read back into m_fb.
            void setGpuRenderer(::Renderer *r) noexcept { m_gpuRenderer = r; }
            void setRasterBackend(std::shared_ptr<RasterBackend> backend) { m_rasterBackend = std::move(backend); }
            // Diagnostic only: compare sampled native draws, then retain native output.
            void setNativeVerificationInterval(std::uint64_t interval) { m_nativeVerifyEvery = interval; }
            [[nodiscard]] std::uint64_t comparedNativeDraws() const noexcept { return m_comparedNativeDraws; }
            // Diagnostic: byte-for-byte reference comparison of every Nth native
            // stream-output pass. Zero disables comparison.
            void setStreamOutVerificationInterval(unsigned interval) { m_streamOutVerifyEvery = interval; }
            [[nodiscard]] std::uint64_t nativeStreamOutPasses() const noexcept { return m_nativeStreamOutPasses; }
            [[nodiscard]] std::uint64_t comparedStreamOutPasses() const noexcept { return m_comparedStreamOutPasses; }
            [[nodiscard]] std::uint64_t differingNativeDraws() const noexcept { return m_differingNativeDraws; }
            [[nodiscard]] std::uint64_t earlyDepthRejectedFragments() const noexcept { return m_earlyDepthRejectedFragments; }
            [[nodiscard]] std::uint64_t earlyDepthRejectedTriangles() const noexcept { return m_earlyDepthRejectedTriangles; }
            [[nodiscard]] std::uint64_t vertexShaderExecutions() const noexcept { return m_vertexShaderExecutions; }
            [[nodiscard]] std::uint64_t vertexShaderCacheHits() const noexcept { return m_vertexShaderCacheHits; }
            // Draws with a GPU vertex program that reused converted vertex arrays.
            [[nodiscard]] std::uint64_t directVertexReuses() const noexcept { return m_directVertexReuses; }
            void setVertexCacheEnabled(bool enabled) noexcept { m_vertexCacheEnabled = enabled; }

            // GPU render-target graph (the general multi-target compositor). When set, each guest
            // colour buffer becomes a persistent GPU image and draws render into their actual target
            // (offscreen buffers, bloom mips) rather than one flattened scan target; a draw whose
            // bound texture is itself a colour target samples that image directly (render-to-texture).
            void setGpuRenderGraph(bool on) noexcept { m_gpuGraph = on; }

        private:
            std::optional<std::uint64_t> m_nativeVerifyEvery;
            std::uint64_t m_nativeDraws{}, m_comparedNativeDraws{}, m_differingNativeDraws{};
            unsigned m_nativeWorstError{};
            std::uint64_t m_earlyDepthRejectedFragments{};
            std::uint64_t m_earlyDepthRejectedTriangles{};
            std::uint64_t m_vertexShaderExecutions{}, m_vertexShaderCacheHits{}, m_directVertexReuses{};
            bool m_vertexCacheEnabled{true};
            Latte::VertexShaderCache m_vertexShaderCache;
            std::unique_ptr<RasterWorkers> m_rasterWorkers;
            std::unique_ptr<RasterWorkers> m_vertexWorkers; // parallel CPU vertex shading (pre-shading pass)
            unsigned m_rasterWorkerCount{};
            std::uint64_t m_parallelMinPixels{32768};
            std::optional<std::uint64_t> m_rasterVerifyEvery;
            std::uint64_t m_parallelTriangles{}, m_verifiedRasterTriangles{};
            struct Surface {
                    std::uint32_t imagePtr{0};
                    std::uint32_t width{0};
                    std::uint32_t height{0};
                    std::uint32_t pitch{0}; // elements per row
                    std::uint32_t format{0};
                    std::uint32_t tileMode{0};
                    std::uint32_t swizzle{0};
                    std::uint32_t compMap{0x00010203}; // texture-view RGBA selectors, not storage layout
                    std::uint32_t dimension{1}, depth{1}, imageSize{0}, aa{0};
                    std::uint32_t firstMip{0}, firstSlice{0}, sliceCount{1};
                    std::uint32_t mipCount{0};
                    std::uint32_t mipImagePtr{}, mipImageSize{}, viewMipCount{};
                    std::array<std::uint32_t, 13> mipOffsets{};
                    bool operator==(const Surface &) const = default;
            };
            struct SurfaceKey {
                    std::uint32_t imagePtr{0};
                    std::uint32_t width{0};
                    std::uint32_t height{0};
                    std::uint32_t pitch{0};
                    std::uint32_t format{0};
                    std::uint32_t tileMode{0};
                    std::uint32_t swizzle{0};
                    bool operator==(const SurfaceKey &) const = default;
            };
            struct SurfaceKeyHash {
                    std::size_t operator()(const SurfaceKey &k) const noexcept;
            };
            struct Attrib {
                    std::uint32_t addr{0};
                    std::uint32_t stride{0};
                    std::uint32_t size{0};
                    bool operator==(const Attrib &) const = default;
            };
            struct BlendState {
                    bool useAlphaBlend{true};
                    std::uint32_t colorSrcBlend{4}; // SRC_ALPHA
                    std::uint32_t colorDstBlend{5}; // INV_SRC_ALPHA
                    std::uint32_t colorCombine{0}; // ADD
                    std::uint32_t alphaSrcBlend{1}; // ONE
                    std::uint32_t alphaDstBlend{5}; // INV_SRC_ALPHA
                    std::uint32_t alphaCombine{0}; // ADD
            };

            void buildClearFrame(unsigned coveredWidth = 0, unsigned coveredHeight = 0);
            Surface parseSurface(std::uint32_t addr);
            Surface parseSurface(const Gx2Command &cmd); // prefers the call-time struct snapshot
            void dumpSurface(const Surface &s, const char *kind, bool force = false);
            void clearSurface(const Surface &s, const float rgba[4]);
            struct DepthBacking {
                    std::vector<float> pixels;
                    std::shared_ptr<RasterReadback> pending;
                    // A clear not yet written to pixels: most cleared depth surfaces are only
                    // rendered on the GPU, so filling the host copy is deferred to its first use.
                    std::optional<float> clear;
                    // Every pixel holds this value; a repeated clear then needs no fill.
                    std::optional<float> uniform;
                    std::vector<std::uint8_t> stencil;
                    std::shared_ptr<RasterReadback> pendingStencil;
                    std::optional<std::uint8_t> stencilClear;
                    // Identity of the current pixels for the backend's upload cache (snapshotOwner).
                    // Dropped whenever pixels change, so an unchanged depth texture skips re-upload
                    // and byte comparison.
                    std::shared_ptr<const void> version;
                    void changed()
                    {
                        uniform.reset();
                        version.reset();
                    }
                    DepthFormat format{DepthFormat::Float32};
            };
            // applyClear = false leaves a pending clear in DepthBacking::clear (pixels are stale).
            DepthBacking *depthBacking(const Surface &s, bool resolve = true, bool applyClear = true) const;
            // Current versions of an array view's layers when at least one was rendered as its own
            // surface (a pending GPU version); layers never rendered come from guest memory.
            std::vector<std::shared_ptr<RasterReadback>> renderedArrayLayers(const Surface &surface);
            // Level-major pending GPU versions of every (level, slice) of a 2D, array or cube view,
            // or empty unless all of them are still on the GPU.
            std::vector<std::shared_ptr<RasterReadback>> renderedSubresources(const Surface &surface, unsigned levels) const;
            void clearDepthSurface(const Surface &s, float depth, unsigned flags, unsigned stencil = 0);
            void drawImage(const Gx2Command &cmd);
            void drawPrimitives(const Gx2Command &cmd, bool indexed, int selectedColor = -1);
            bool streamOut(const Gx2Command &cmd, bool indexed);
            // The bound fetch shader resolved against the attribute buffers once per draw, so
            // fetching a vertex no longer decodes formats, semantics or memory regions.
            struct AttributeFetch {
                    struct Element {
                            std::uint32_t out{}, mask{}, offset{}, stride{}, size{}, address{};
                            std::uint8_t components{}, bytes{};
                            bool fails{}, floating{}, packed1010102{}, integer{}, isSigned{}, scaled{};
                            std::array<std::uint8_t, 16> order{}; // source byte of each little-endian byte
                            // Each component is one word read as is or byte-reversed (every endian
                            // swap whose unit is the component size), so it loads without `order`.
                            bool wordLoad{}, wordSwap{};
                            std::array<std::uint8_t, 4> select{}; // mask: component 0-3, 4 zero, 5 one, else keep
                            const std::uint8_t *host{}; // set when every in-bounds element is resident
                            std::uint64_t count{}; // vertices whose element starts in the buffer
                    };
                    std::vector<Element> elements;
                    bool valid{};
            };
            AttributeFetch prepareAttributeFetch() const;
            // Compact inputs and triangle indices of a draw with a GPU vertex program.
            struct DirectVertices {
                    std::vector<std::array<float, 4>> attributes;
                    std::vector<std::uint32_t> indices;
            };
            // DirectVertices reused across draws and frames while the indices, fetch state and
            // guest vertex bytes they were converted from are unchanged.
            struct DirectVertexEntry {
                    std::vector<std::uint32_t> fetchLayout, sourceIndices;
                    std::array<std::uint32_t, 32> semantics{};
                    std::array<Attrib, 8> attribs{};
                    unsigned attributeStride{}, mode{};
                    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> sources; // guest ranges read
                    std::shared_ptr<const DirectVertices> vertices;
                    std::uint32_t validatedFrame{~0u};
                    std::uint64_t used{};
                    std::size_t bytes() const;
            };
            std::unordered_map<std::uint64_t, DirectVertexEntry> m_directVertexCache;
            std::unordered_map<std::uint64_t, std::uint8_t> m_directVertexSeen; // key -> sightings, then changes
            std::uint64_t m_directVertexBytes{}, m_directVertexClock{};
            // Guest byte ranges [address, address + size) the fetch plan reads for vertices
            // low..high, merged; false when a read is not resident in one host range.
            static bool directVertexSources(const AttributeFetch &fetch, unsigned outputs, std::uint32_t low, std::uint32_t high,
                                            std::vector<std::pair<std::uint32_t, std::uint32_t>> &ranges);
            bool fetchAttributes(const AttributeFetch &fetch, std::uint32_t vertex, std::span<std::array<float, 4>> out) const;
            bool fetchAttributes(std::uint32_t vertex, std::span<std::array<float, 4>> out) const
            {
                return fetchAttributes(prepareAttributeFetch(), vertex, out);
            }
            void copyToScanBuffer(const Surface &s);
            void copyToDrcBuffer(const Surface &s);
            void maybeDumpFrame();
            [[nodiscard]] std::array<std::uint8_t, 4> blendTexel(const std::array<std::uint8_t, 4> &src, const std::uint8_t *dst,
                                                                 const BlendState &blend) const;
            static bool useViewBacking();
            // WEMU_NATIVE_RESIDENT_TARGETS=1: native results stay on the GPU until read.
            static bool residentTargets();
            // The pending native version of a surface, for binding it on the GPU (null: CPU bytes).
            [[nodiscard]] const std::shared_ptr<RasterReadback> *renderedVersion(const Surface &s) const;
            static SurfaceKey makeSurfaceKey(const Surface &s);
            std::vector<std::uint8_t> &viewBacking(const Surface &s);
            void resolveViewBacking(const Surface &s) const;
            // Materializes pending targets; with `within`, only those stored inside these surfaces.
            void resolvePendingBackings(std::span<const Surface> within = {}) const;
            [[nodiscard]] const std::vector<std::uint8_t> *findViewBacking(const Surface &s) const;
            [[nodiscard]] std::array<std::uint8_t, 4> sampleViewBacking(const Surface &s, const std::vector<std::uint8_t> &backing, float u,
                                                                        float v) const;

            // Nearest-neighbour sample of a guest surface at normalized uv (RGBA out, 0..255).
            std::array<std::uint8_t, 4> sample(const Surface &s, float u, float v) const;
            std::array<float, 4> sampleTexture(const Surface &s, const TextureSampler &sampler, float u, float v, float layer = 0,
                                               const std::vector<std::uint8_t> *feedback = nullptr, bool gather = false,
                                               const std::vector<float> *depthFeedback = nullptr) const;
            static bool supportsGather(const Surface &surface, const TextureSampler &sampler);

            // Decode a whole guest surface to a linear RGBA8 image (reuses sample() per texel, so it
            // shares the untiling / BC / format handling) for upload to the GPU rasteriser.
            void decodeTextureToRgba(const Surface &s, std::vector<std::uint8_t> &out, std::uint32_t &tw, std::uint32_t &th) const;

            // WEMU_PROJ_FIX=1: every collapsing UI/menu draw reads one shared screen-projection whose
            // X/Y scale rows are uploaded as zero, so the shader maps every pane to a single point.
            // This detects that projection in the register file (by its z/w rows) and synthesises the
            // missing X/Y rows so the whole menu places at once. Scale is WEMU_PROJ_SCALE-tunable.
            void repairProjection();

            ::Renderer *m_gpuRenderer{nullptr}; // non-null => GPU rasterisation path
            std::shared_ptr<RasterBackend> m_rasterBackend;
            bool m_gpuGraph{false}; // true => route draws through the multi-target render graph
            std::uint32_t m_gpuScanAddr{0}; // colour buffer copied to the TV scan-out this frame (graph mode)
            std::vector<float> m_gpuTri; // current draw's triangles, interleaved x,y,u,v
            std::vector<std::uint8_t> m_texScratch; // decoded-texture scratch for GPU upload
            // Decoded-texture cache (GPU path), keyed by guest image address. Decoding a tiled/BC
            // surface to RGBA8 is as costly as sampling it per pixel, so without this the GPU path
            // pays a full CPU decode of every texture — including the 1280x720 render-target reads —
            // on every frame and ends up slower than the software rasteriser. Guest textures are
            // static for the menu (and in GPU mode the CPU never writes the colour buffers), so a
            // decode-once cache is safe here; a later multi-target design keeps them on the GPU.
            struct DecodedTex {
                    std::vector<std::uint8_t> rgba;
                    std::uint32_t w = 0, h = 0;
            };
            std::unordered_map<std::uint32_t, DecodedTex> m_texCache;
            // Guest bytes compared only when stores may have reached them: an entry is trusted
            // once two comparisons saw the same write generation, and re-compared periodically.
            struct WriteValidation {
                    std::optional<std::uint64_t> generation;
                    bool trusted{};
                    std::uint32_t comparedFrame{};
            };
            bool unchangedSinceValidation(WriteValidation &validation, std::uint32_t address, std::size_t bytes, std::uint64_t stagger) const;
            void validated(WriteValidation &validation, std::uint32_t address, std::size_t bytes, bool unchanged) const;
            struct ArraySnapshot {
                    Surface surface;
                    std::vector<std::uint8_t> source, rgba;
                    std::uint32_t validatedFrame{~0u}; // source compared against guest memory during this present
                    // Alias checks passed with this many view backings and CPU-written images; both
                    // only grow, so equal counts in the same present skip the checks.
                    std::size_t checkedBackings{}, checkedCpuWrites{};
                    WriteValidation writes;
            };
            std::vector<std::shared_ptr<ArraySnapshot>> m_arraySnapshots;
            // Raw canonical bytes of an array whose layers all live in view backings, packed layer
            // after layer without row padding. Rebuilt when any layer is written again.
            struct DecodedTexture;
            struct LayerSnapshot {
                    Surface surface;
                    std::uint64_t generation{};
                    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
                    // Decoded guest layers, kept alive so their addresses in `generation` stay unique.
                    std::vector<std::shared_ptr<DecodedTexture>> guestLayers;
            };
            std::vector<LayerSnapshot> m_layerSnapshots;
            // Evicted snapshots still owned by an upload keep their identity (and GPU image).
            struct RetainedLayerSnapshot {
                    Surface surface;
                    std::uint64_t generation{};
                    std::weak_ptr<const std::vector<std::uint8_t>> bytes;
            };
            std::vector<RetainedLayerSnapshot> m_retainedLayerSnapshots;
            std::shared_ptr<const std::vector<std::uint8_t>> layerSnapshot(const Surface &surface);
            std::optional<Surface> arraySlice(const Surface &surface, unsigned relative) const;
            Surface attachmentSurface(const Surface &surface) const;
            std::optional<Surface> mipSurface(const Surface &surface, unsigned level) const;
            struct MipSnapshot {
                    Surface surface;
                    unsigned levelCount{}, validatedFrame{};
                    std::uint64_t generation{};
                    std::vector<Surface> levels;
                    std::vector<std::vector<float>> texels;
                    std::vector<std::vector<std::uint8_t>> blockTexels;
                    bool blocksOnly{}, hasRenderedSources{};
                    std::size_t writtenSurfaceCount{};
                    WriteValidation baseWrites, mipWrites;
                    std::size_t bytes() const
                    {
                        auto total = source.size();
                        for (const auto &level: texels)
                            total += level.size() * sizeof(float);
                        for (const auto &level: blockTexels)
                            total += level.size();
                        return total;
                    }
                    std::vector<RasterDraw::Texture::MipLevel> mips;
                    std::vector<std::uint8_t> source;
                    // Image range of every (level, slice) view: lets a later lookup in the same frame
                    // recheck writes and feedback without recomputing the views.
                    std::vector<std::pair<std::uint32_t, std::uint32_t>> sliceRanges;
            };
            std::vector<std::shared_ptr<MipSnapshot>> m_mipSnapshots;
            std::unordered_multimap<unsigned, std::weak_ptr<MipSnapshot>> m_retainedMipSnapshots;
            std::shared_ptr<MipSnapshot> mipSnapshot(const Surface &surface, bool includeMips, bool blocksOnly = false);
            static bool sampleMipSnapshot(const MipSnapshot &snapshot, const TextureSampler &sampler, const std::array<float, 4> &coords,
                                          float cubeIndex, bool explicitLod, std::array<float, 4> &rgba);
            std::shared_ptr<ArraySnapshot> arraySnapshot(const Surface &surface);
            // Base-level sample() output for guest-memory textures (tiled/compressed), keyed by
            // view and validated against source snapshots so unchanged textures decode once.
            struct DecodedTexture {
                    std::uint64_t used{};
                    std::uint32_t validatedFrame{~0u};
                    WriteValidation writes;
                    std::vector<std::uint8_t> source;
                    std::vector<std::uint8_t> rgba;
                    std::vector<float> floating; // component-mapped signed normalized or float-format texels
                    std::vector<std::uint8_t> blocks;
                    std::size_t bytes() const { return source.size() + rgba.size() + floating.size() * sizeof(float) + blocks.size(); }
            };
            struct DecodedKey {
                    SurfaceKey surface;
                    std::uint32_t format{}, componentMap{}, imageSize{};
                    bool blocksOnly{}, floatTexels{};
                    bool operator==(const DecodedKey &) const = default;
            };
            struct DecodedKeyHash {
                    std::size_t operator()(const DecodedKey &key) const
                    {
                        return SurfaceKeyHash{}(key.surface) ^ (std::size_t(key.format) << 16) ^ std::size_t(key.componentMap) ^
                               std::size_t(key.imageSize) ^ std::size_t(key.blocksOnly) ^ (std::size_t(key.floatTexels) << 1);
                    }
            };
            std::unordered_map<DecodedKey, std::shared_ptr<DecodedTexture>, DecodedKeyHash> m_decodedTextures;
            std::unordered_map<DecodedKey, std::weak_ptr<DecodedTexture>, DecodedKeyHash> m_retainedDecodedTextures;
            std::uint64_t m_decodedBytes{}, m_decodedClock{};
            // floatTexels: component-mapped RGBA32F texels of a non-RGBA8 color format, in `floating`.
            std::shared_ptr<DecodedTexture> decodedTexture(const Surface &surface, bool blocksOnly = false, bool floatTexels = false);
            std::vector<float> volumeTexels(const Surface &surface) const;

            float m_clear[4]{0.0f, 0.0f, 0.0f, 1.0f}; // RGBA, 0..1
            std::vector<std::uint8_t> m_fb; // RGBX, kWidth*kHeight*4
            std::uint32_t m_presentCount{0};
            std::uint32_t m_richestTris{0}; // most triangles seen in one frame (WEMU_FB_DUMP_RICH)

            // Guest memory, read-only: reads through a writable view would count as stores (see
            // Memory::watchWrites). The few paths that render into guest memory use m_writableMem.
            const Core::Memory *m_mem{nullptr};
            Core::Memory *m_writableMem{nullptr};
            Surface m_color; // bound colour target
            Surface m_depth;
            std::array<Surface, 8> m_colorBuffers{};
            bool m_depthTest{}, m_depthWrite{}, m_stencilTest{};
            std::optional<std::uint32_t> m_stencilControl;
            std::optional<std::array<std::uint32_t, 2>> m_stencilMasks;
            unsigned m_depthCompare{7};
            mutable std::unordered_map<SurfaceKey, DepthBacking, SurfaceKeyHash> m_depthBackings;
            std::uint32_t m_colorTarget{0}; // GX2 render target slot for the bound colour target
            Surface m_texture; // texture selected by the current pixel shader
            std::array<Surface, 16> m_pixelTextures{}, m_vertexTextures{};
            Surface m_lastScan; // last surface scanned out to the TV (re-presented when a frame swaps without a copy)
            Surface m_lastDrcScan; // same for the GamePad
            std::vector<std::uint8_t> m_drcFb; // RGBX, kDrcWidth*kDrcHeight*4
            bool m_drcCapture{};
            Surface m_lastDrawColor; // colour target used by the most recent replayed draw in this frame
            std::array<BlendState, 8> m_blend{};
            std::array<std::uint8_t, 4> m_blendConstant{0, 0, 0, 0};
            std::array<std::uint32_t, 8> m_channelMask{0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF};
            std::uint32_t m_targetBlendEnable{0xFF};
            bool m_colorWriteEnable{true};
            bool m_multiWriteEnable{};
            unsigned m_polygonControl{};
            std::array<float, 5> m_polygonOffset{};
            bool m_rasterizerEnabled{true}, m_zClipEnabled{true}, m_halfZ{}; // GX2Init enables Z clipping
            // Vertex-shader uniform registers (GX2SetVertexUniformReg). MK8's UI places its unit
            // quads with a matrix in c0..c3; m_regsFresh marks regs written since the last draw.
            float m_vsRegs[256]{};
            bool m_regsFresh{false};
            // A shader's bytecode (empty = unsupported), interned per guest GX2VertexShader or
            // GX2PixelShader address, with the results of the lookups draws make for it: hashing
            // and comparing whole programs in each cache cost milliseconds per MK8 race frame.
            struct ShaderProgram {
                    std::vector<std::uint32_t> words;
                    std::shared_ptr<const Latte::Program> declaration;
                    std::shared_ptr<const Latte::VertexShader> vertexShader;
                    std::map<std::pair<std::array<unsigned, 4>, Latte::UniformBufferWindows>, std::shared_ptr<const Latte::FragmentShader>>
                            streamShaders;
                    std::map<std::pair<unsigned, Latte::UniformBufferWindows>, std::shared_ptr<const Latte::VertexShader>> windowedVertexShaders;
                    std::optional<bool> colorOnlyEffects, rejectsEveryPixel;
                    // Resource types (two bits each), onlyColorZero, colorExport.
                    std::map<std::tuple<std::uint64_t, bool, unsigned>, std::shared_ptr<const Latte::FragmentShader>> fragmentShaders;
            };
            [[nodiscard]] static const std::vector<std::uint32_t> &programWords(const std::shared_ptr<ShaderProgram> &program);
            [[nodiscard]] const std::vector<std::uint32_t> &vsProgram() const { return programWords(m_vsShader); }
            [[nodiscard]] const std::vector<std::uint32_t> &psProgram() const { return programWords(m_psShader); }
            [[nodiscard]] static std::shared_ptr<const Latte::Program> declaration(const std::shared_ptr<ShaderProgram> &program);
            [[nodiscard]] std::shared_ptr<const Latte::FragmentShader> fragmentShader(std::span<const Latte::TextureType> resourceTypes,
                                                                                      bool onlyColorZero, unsigned colorExport);
            [[nodiscard]] std::shared_ptr<const Latte::VertexShader> vertexShader(unsigned bufferWindow = 0,
                                                                                  const Latte::UniformBufferWindows &bufferWindows = {});
            [[nodiscard]] bool colorOnlyEffects();
            [[nodiscard]] bool rejectsEveryPixel();
            // Current vertex shader + cache keyed by the guest GX2VertexShader struct address.
            std::shared_ptr<ShaderProgram> m_vsShader;
            std::optional<unsigned> m_streamOutVerifyEvery;
            std::uint64_t m_nativeStreamOutPasses{}, m_comparedStreamOutPasses{};
            std::unordered_map<std::uint32_t, std::shared_ptr<ShaderProgram>> m_vsCache;
            // Vertex uniform BLOCKS (GX2SetVertexUniformBlock): payload snapshots per binding —
            // read through the shader's kcache windows in uniform-block mode.
            std::array<std::vector<std::uint32_t>, 16> m_vsBlocks{};
            // Pixel-shader state is captured separately for diagnostics and future shader replay.
            // The current software path still samples raw textures, but MK8 UI panes use PS
            // colour/mask composition, so the bytecode/uniform stream must be visible per draw.
            float m_psRegs[256]{};
            bool m_psRegsFresh{false};
            std::shared_ptr<ShaderProgram> m_psShader;
            std::unordered_map<std::uint32_t, std::shared_ptr<ShaderProgram>> m_psCache;
            // Opt-in translation audit only; no native dispatch authorization.
            std::map<std::pair<std::vector<std::uint32_t>, bool>, Latte::FragmentShader> m_nativeAuditCache;
            Latte::FragmentShaderCache m_fragmentShaderCache;
            std::array<std::vector<std::uint32_t>, 16> m_psBlocks{};
            std::array<Attrib, 8> m_attribs{};
            struct StreamOutBuffer {
                    std::uint32_t address{}, size{}, context{}, offset{};
            };
            std::array<StreamOutBuffer, 4> m_streamOutBuffers{};
            std::array<std::uint32_t, 4> m_streamOutStrides{};
            bool m_streamOutEnabled{};
            std::vector<std::pair<std::uint32_t, std::uint32_t>> m_streamOutWrites;
            std::vector<std::uint32_t> m_fetchLayout;
            std::array<std::uint32_t, 32> m_vertexSemantics{0, 1, 2, 3};
            std::uint32_t m_vertexInputCount{4};
            std::uint32_t m_pixelInputCount{4};
            std::array<std::uint32_t, 32> m_vertexOutputSemantics{0, 1, 2, 3};
            std::array<std::uint32_t, 32> m_pixelInputSemantics{0, 1, 2, 3};
            float m_vp[4]{0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight)}; // x,y,w,h
            float m_vpNear{}, m_vpFar{1.f};
            std::int32_t m_scissor[4]{0, 0, kWidth, kHeight}; // x,y,w,h

            // GX2ContextState shadowing: while a context is active the hardware mirrors every state
            // register into it, and GX2SetContextState reloads the whole register set. MK8 switches
            // between TV and DRC contexts mid-frame; without the reload, DRC depth/blend state leaks
            // into the TV passes that follow.
            struct ContextState {
                    Surface color, depth;
                    std::array<Surface, 8> colorBuffers{};
                    bool depthTest{}, depthWrite{}, stencilTest{};
                    std::optional<std::uint32_t> stencilControl;
                    std::optional<std::array<std::uint32_t, 2>> stencilMasks;
                    unsigned depthCompare{7};
                    std::uint32_t colorTarget{};
                    std::array<Surface, 16> pixelTextures{}, vertexTextures{};
                    std::array<TextureSampler, 18> pixelSamplers{}, vertexSamplers{};
                    std::array<BlendState, 8> blend{};
                    std::array<std::uint8_t, 4> blendConstant{};
                    std::array<std::uint32_t, 8> channelMask{};
                    std::uint32_t targetBlendEnable{};
                    bool colorWriteEnable{};
                    bool multiWriteEnable{};
                    unsigned polygonControl{};
                    std::array<float, 5> polygonOffset{};
                    bool rasterizerEnabled{true}, zClipEnabled{true}, halfZ{};
                    std::array<float, 256> vsRegs{}, psRegs{};
                    std::shared_ptr<ShaderProgram> vsShader, psShader;
                    std::vector<std::uint32_t> fetchLayout;
                    std::array<std::vector<std::uint32_t>, 16> vsBlocks{}, psBlocks{};
                    std::array<Attrib, 8> attribs{};
                    std::array<StreamOutBuffer, 4> streamOutBuffers{};
                    std::array<std::uint32_t, 4> streamOutStrides{};
                    bool streamOutEnabled{};
                    std::array<std::uint32_t, 32> vertexSemantics{};
                    std::uint32_t vertexInputCount{4}, pixelInputCount{4};
                    std::array<std::uint32_t, 32> vertexOutputSemantics{}, pixelInputSemantics{};
                    std::array<float, 4> viewport{};
                    float viewportNear{}, viewportFar{};
                    std::array<std::int32_t, 4> scissor{};
            };
            std::unordered_map<std::uint32_t, ContextState> m_contextStates;
            std::uint32_t m_activeContext{0};
            [[nodiscard]] ContextState captureContextState() const;
            void restoreContextState(const ContextState &state);
            void setContextState(std::uint32_t context);

            // Per-frame replay stats (logged around the WEMU_FB_DUMP frame to debug black output).
            struct Stats {
                    std::uint32_t draws{0};
                    std::uint32_t skipNoTarget{0};
                    std::uint32_t skipNoAttrib{0};
                    std::uint32_t skipBadPtr{0};
                    std::uint32_t tris{0};
                    std::uint32_t points{};
                    std::uint64_t pixels{0};
                    std::uint64_t alphaSum{0};
                    std::uint32_t scanPtr{0};
                    std::uint32_t scanNonZero{0};
            } m_stats;
            bool m_dumpSurfaces{false}; // dump every surface touched during the WEMU_FB_DUMP frame
            bool m_trace{false}; // per-draw logging during the WEMU_GX2_TRACE frame
            // Surfaces the CPU replay has rendered into (linear layout) — sampled without untiling
            // even when their GX2 header says tiled.
            std::unordered_set<std::uint32_t> m_cpuWritten;
            struct WrittenSurface {
                    Surface surface;
                    std::uint32_t frame{0};
                    std::uint32_t writes{0};
            };
            std::unordered_map<std::uint32_t, WrittenSurface> m_cpuWrittenSurfaces;
            std::unordered_set<std::uint64_t> m_surfaceAliasTraced;
            mutable std::unordered_map<SurfaceKey, std::vector<std::uint8_t>, SurfaceKeyHash> m_viewBackings;
            // Content version of each view backing, bumped by every write path (draws, clears,
            // resolved GPU versions). Textures read from a backing share one upload per version.
            struct BackingOwner {
                    std::uint64_t version{};
                    std::shared_ptr<const int> token;
            };
            mutable std::unordered_map<SurfaceKey, std::uint64_t, SurfaceKeyHash> m_backingVersions;
            mutable std::unordered_map<SurfaceKey, BackingOwner, SurfaceKeyHash> m_backingOwners;
            void touchBacking(const SurfaceKey &key) const { ++m_backingVersions[key]; }
            std::shared_ptr<const void> backingOwner(const SurfaceKey &key) const;
            // Shared zero texels for sized views of null images; replaced (never resized) when too small.
            std::shared_ptr<const std::vector<std::uint8_t>> m_zeroTexels;
            mutable std::unordered_map<SurfaceKey, std::shared_ptr<RasterReadback>, SurfaceKeyHash> m_pendingReadbacks;
    };

} // namespace Core::Gfx
