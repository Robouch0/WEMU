#pragma once

#include <memory>
#include <string>

#include "gfx/RasterBackend.hpp"

namespace Core::Gfx {
    // Initial synchronous native path. Unsupported state returns to software.
    // Owns a headless Vulkan device; no presentation or game-specific behavior.
    class VulkanRasterBackend final : public RasterBackend {
        public:
            explicit VulkanRasterBackend(bool retainTextures = true, bool lazyReadback = false);
            ~VulkanRasterBackend() override;
            std::optional<RasterResult> render(const RasterDraw &draw) override;
            std::optional<RasterResult> renderDeferred(const RasterDraw &draw) override;
            bool supportsRenderedTextures() const override { return true; }
            bool supportsRenderedTargets() const override { return true; }
            bool supportsDepth() const override { return true; }
            bool supportsPoints() const override { return true; }
            bool supportsFloatTargets() const override { return true; }
            bool supportsVertexStage() const override { return true; }
            const std::string &lastError() const noexcept;
            std::uint64_t completedDraws() const noexcept;
            std::uint64_t reusedDraws() const noexcept;
            std::uint64_t retainedTargetDraws() const noexcept;
            std::uint64_t retainedTextureUploads() const noexcept;
            std::uint64_t residentTextureCopies() const noexcept;
            std::uint64_t residentTargetCopies() const noexcept;
            std::uint64_t readbackTransfers() const noexcept;

        private:
            std::optional<RasterResult> renderImpl(const RasterDraw &draw, bool deferReadback);
            struct Impl;
            std::unique_ptr<Impl> m_impl;
    };
} // namespace Core::Gfx
