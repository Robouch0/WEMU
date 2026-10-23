#pragma once

#include <memory>
#include <string>

#include "gfx/RasterBackend.hpp"

namespace Core::Gfx {
    // Initial synchronous native path. Unsupported state returns to software.
    // Owns a headless Vulkan device; no presentation or game-specific behavior.
    class VulkanRasterBackend final : public RasterBackend {
        public:
            // directDraws records lazily read deferred draws into shared command buffers (see
            // DirectRenderer); false keeps every draw on the per-shape bundle path.
            explicit VulkanRasterBackend(bool retainTextures = true, bool lazyReadback = false, bool directDraws = true);
            ~VulkanRasterBackend() override;
            std::optional<RasterResult> render(const RasterDraw &draw) override;
            std::optional<RasterResult> renderDeferred(const RasterDraw &draw) override;
            bool supportsRenderedTextures() const override { return true; }
            bool supportsRenderedTargets() const override { return true; }
            bool supportsDepth() const override { return true; }
            bool supportsStencil() const override { return true; }
            bool supportsPoints() const override { return true; }
            bool supportsFloatTargets() const override { return true; }
            bool supportsVertexStage() const override { return true; }
            bool supportsBlockTextures() const override { return true; }
            bool supportsStreamOut() const override { return true; }
            std::optional<StreamOutResult> streamOut(const StreamOutDraw &draw) override;
            bool supportsRenderedMipLevels() const override;
            const std::string &lastError() const noexcept;
            std::uint64_t completedDraws() const noexcept;
            std::uint64_t reusedDraws() const noexcept;
            std::uint64_t retainedTargetDraws() const noexcept;
            std::uint64_t retainedTextureUploads() const noexcept;
            std::uint64_t residentTextureCopies() const noexcept;
            // Draws that bound vertex arrays kept on the GPU by an earlier draw with the same owner.
            std::uint64_t residentVertexHits() const noexcept;
            // Draws that sampled a rendered array, cube or mip chain copied by an earlier draw.
            std::uint64_t assembledTextureHits() const noexcept;
            std::uint64_t directTextureBindings() const noexcept;
            std::uint64_t directTextureBytes() const noexcept;
            std::uint64_t targetUploadTransfers() const noexcept;
            // Direct draws whose target was a deferred clear (ClearedImage), cleared on the GPU.
            std::uint64_t targetClears() const noexcept;
            // Direct draws whose depth was a pending clear (RasterDraw::Depth::clear), cleared on the GPU.
            std::uint64_t depthClears() const noexcept;
            // Direct draws that continued the previous draw's render pass.
            std::uint64_t mergedRenderPasses() const noexcept;
            std::uint64_t targetCopyTransfers() const noexcept;
            std::uint64_t fullOverwriteDraws() const noexcept;
            std::uint64_t residentTargetCopies() const noexcept;
            std::uint64_t readbackTransfers() const noexcept;
            std::uint64_t pipelineCreations() const noexcept;
            std::uint64_t queueSubmissions() const noexcept;
            std::uint64_t submittedCommandBuffers() const noexcept;
            std::uint64_t submissionWaits() const noexcept;
            std::uint64_t sharedTextureUploads() const noexcept;
            std::uint64_t sharedTextureHits() const noexcept;
            std::uint64_t sharedTextureBytes() const noexcept;

        private:
            std::optional<RasterResult> renderImpl(const RasterDraw &draw, bool deferReadback);
            struct Impl;
            std::unique_ptr<Impl> m_impl;
    };
} // namespace Core::Gfx
