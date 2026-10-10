#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
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
            [[nodiscard]] std::uint32_t presentCount() const noexcept { return m_presentCount; }

            // GPU rasterisation (WEMU_GPU=1). When a renderer is supplied, each draw's screen-space
            // triangles are computed on the CPU (reusing all the shader/placement logic) and filled
            // on the GPU instead of by the software rasteriser; the result is read back into m_fb.
            void setGpuRenderer(::Renderer *r) noexcept { m_gpuRenderer = r; }
            void setRasterBackend(std::shared_ptr<RasterBackend> backend) { m_rasterBackend = std::move(backend); }
            // Diagnostic only: compare sampled native draws, then retain native output.
            void setNativeVerificationInterval(std::uint64_t interval) { m_nativeVerifyEvery = interval; }
            [[nodiscard]] std::uint64_t comparedNativeDraws() const noexcept { return m_comparedNativeDraws; }
            [[nodiscard]] std::uint64_t differingNativeDraws() const noexcept { return m_differingNativeDraws; }

            // GPU render-target graph (the general multi-target compositor). When set, each guest
            // colour buffer becomes a persistent GPU image and draws render into their actual target
            // (offscreen buffers, bloom mips) rather than one flattened scan target; a draw whose
            // bound texture is itself a colour target samples that image directly (render-to-texture).
            void setGpuRenderGraph(bool on) noexcept { m_gpuGraph = on; }

        private:
            std::optional<std::uint64_t> m_nativeVerifyEvery;
            std::uint64_t m_nativeDraws{}, m_comparedNativeDraws{}, m_differingNativeDraws{};
            unsigned m_nativeWorstError{};
            std::unique_ptr<RasterWorkers> m_rasterWorkers;
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

            void buildClearFrame();
            Surface parseSurface(std::uint32_t addr);
            Surface parseSurface(const Gx2Command &cmd); // prefers the call-time struct snapshot
            void dumpSurface(const Surface &s, const char *kind, bool force = false);
            void clearSurface(const Surface &s, const float rgba[4]);
            struct DepthBacking {
                    std::vector<float> pixels;
                    std::shared_ptr<RasterReadback> pending;
                    DepthFormat format{DepthFormat::Float32};
            };
            DepthBacking *depthBacking(const Surface &s, bool resolve = true) const;
            void clearDepthSurface(const Surface &s, float depth, unsigned flags);
            void drawPrimitives(const Gx2Command &cmd, bool indexed);
            bool fetchAttributes(std::uint32_t vertex, std::array<std::array<float, 4>, 4> &out) const;
            void copyToScanBuffer(const Surface &s);
            void maybeDumpFrame();
            [[nodiscard]] std::array<std::uint8_t, 4> blendTexel(const std::array<std::uint8_t, 4> &src, const std::uint8_t *dst,
                                                                 const BlendState &blend) const;
            static bool useViewBacking();
            static SurfaceKey makeSurfaceKey(const Surface &s);
            std::vector<std::uint8_t> &viewBacking(const Surface &s);
            void resolveViewBacking(const Surface &s) const;
            void resolvePendingBackings() const;
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
            struct ArraySnapshot {
                    Surface surface;
                    std::vector<std::uint8_t> source, rgba;
            };
            std::vector<std::shared_ptr<ArraySnapshot>> m_arraySnapshots;
            std::optional<Surface> arraySlice(const Surface &surface, unsigned relative) const;
            std::shared_ptr<ArraySnapshot> arraySnapshot(const Surface &surface);

            float m_clear[4]{0.0f, 0.0f, 0.0f, 1.0f}; // RGBA, 0..1
            std::vector<std::uint8_t> m_fb; // RGBX, kWidth*kHeight*4
            std::uint32_t m_presentCount{0};
            std::uint32_t m_richestTris{0}; // most triangles seen in one frame (WEMU_FB_DUMP_RICH)

            Core::Memory *m_mem{nullptr};
            Surface m_color; // bound colour target
            Surface m_depth;
            bool m_depthTest{}, m_depthWrite{}, m_stencilTest{};
            unsigned m_depthCompare{7};
            mutable std::unordered_map<SurfaceKey, DepthBacking, SurfaceKeyHash> m_depthBackings;
            std::uint32_t m_colorTarget{0}; // GX2 render target slot for the bound colour target
            Surface m_texture; // texture selected by the current pixel shader
            std::array<Surface, 16> m_pixelTextures{}, m_vertexTextures{};
            Surface m_lastScan; // last surface scanned out to the TV (re-presented when a frame swaps without a copy)
            Surface m_lastDrawColor; // colour target used by the most recent replayed draw in this frame
            std::array<BlendState, 8> m_blend{};
            std::array<std::uint8_t, 4> m_blendConstant{0, 0, 0, 0};
            std::array<std::uint32_t, 8> m_channelMask{0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF};
            std::uint32_t m_targetBlendEnable{0xFF};
            bool m_colorWriteEnable{true};
            // Vertex-shader uniform registers (GX2SetVertexUniformReg). MK8's UI places its unit
            // quads with a matrix in c0..c3; m_regsFresh marks regs written since the last draw.
            float m_vsRegs[256]{};
            bool m_regsFresh{false};
            // Current vertex-shader bytecode (empty = none/unsupported) + cache keyed by the
            // guest GX2VertexShader struct address.
            std::vector<std::uint32_t> m_vsProgram;
            std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> m_vsCache;
            // Vertex uniform BLOCKS (GX2SetVertexUniformBlock): payload snapshots per binding —
            // read through the shader's kcache windows in uniform-block mode.
            std::array<std::vector<std::uint32_t>, 16> m_vsBlocks{};
            // Pixel-shader state is captured separately for diagnostics and future shader replay.
            // The current software path still samples raw textures, but MK8 UI panes use PS
            // colour/mask composition, so the bytecode/uniform stream must be visible per draw.
            float m_psRegs[256]{};
            bool m_psRegsFresh{false};
            std::vector<std::uint32_t> m_psProgram;
            std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> m_psCache;
            // Opt-in translation audit only; no native dispatch authorization.
            std::map<std::vector<std::uint32_t>, Latte::FragmentShader> m_nativeAuditCache;
            Latte::FragmentShaderCache m_fragmentShaderCache;
            std::array<std::vector<std::uint32_t>, 16> m_psBlocks{};
            std::array<Attrib, 8> m_attribs{};
            std::vector<std::uint32_t> m_fetchLayout;
            std::array<std::uint32_t, 4> m_vertexSemantics{0, 1, 2, 3};
            std::array<std::uint32_t, 4> m_vertexOutputSemantics{0, 1, 2, 3};
            std::array<std::uint32_t, 4> m_pixelInputSemantics{0, 1, 2, 3};
            float m_vp[4]{0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight)}; // x,y,w,h
            float m_vpNear{}, m_vpFar{1.f};
            std::int32_t m_scissor[4]{0, 0, kWidth, kHeight}; // x,y,w,h

            // Per-frame replay stats (logged around the WEMU_FB_DUMP frame to debug black output).
            struct Stats {
                    std::uint32_t draws{0};
                    std::uint32_t skipNoTarget{0};
                    std::uint32_t skipNoAttrib{0};
                    std::uint32_t skipBadPtr{0};
                    std::uint32_t tris{0};
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
            mutable std::unordered_map<SurfaceKey, std::shared_ptr<RasterReadback>, SurfaceKeyHash> m_pendingReadbacks;
    };

} // namespace Core::Gfx
