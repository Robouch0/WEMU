#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx/LatteShaderLowering.hpp"
#include "utils/PrivateDirectory.hpp"

namespace Core::Gfx {
    // One native pipeline as recorded from its inputs: the Latte programs and lowering parameters of
    // both stages, plus backend-defined pipeline state. Recording inputs rather than GLSL or SPIR-V
    // keeps a manifest valid when shader translation changes.
    struct PipelineRecipe {
            std::shared_ptr<const Latte::ShaderOrigin> vertex; // null: the backend's built-in vertex stage
            std::shared_ptr<const Latte::ShaderOrigin> fragment;
            unsigned fragmentVariant{};
            std::vector<std::uint32_t> state;
            bool operator==(const PipelineRecipe &other) const;
    };

    // Pipelines a title created in earlier sessions, kept in a private per-executable file so the next
    // session can create them before the title starts. Only the replay thread calls into it; saves
    // serialize a snapshot on a background task.
    class PipelineManifest {
        public:
            static constexpr std::size_t maxRecipes = 16384;
            static constexpr std::size_t maxFileBytes = std::size_t{64} * 1024 * 1024;

            // WEMU_PIPELINE_MANIFEST_DIR (empty disables), else <cache root>/manifests.
            static std::string defaultDirectory();
            // "<executable stem>-<FNV-1a of its bytes>.manifest"; empty if the file cannot be read.
            static std::string filenameFor(const std::filesystem::path &executable);
            PipelineManifest(const std::string &directory, std::string filename);
            ~PipelineManifest();
            PipelineManifest(const PipelineManifest &) = delete;
            PipelineManifest &operator=(const PipelineManifest &) = delete;

            bool enabled() const { return m_directory.valid() && !m_filename.empty(); }
            const std::string &filename() const { return m_filename; }
            // Reads the file into the manifest and returns its recipes; identical origins share one object.
            std::vector<PipelineRecipe> load();
            // False when the recipe is already known, incomplete, or the manifest is full.
            bool add(PipelineRecipe recipe);
            std::size_t size() const { return m_recipes.size(); }
            // Starts a background save when recipes were added and the last save is at least
            // `interval` old; finish() waits for it and saves whatever is still unsaved.
            void saveInBackground(std::chrono::steady_clock::duration interval);
            bool finish();

            static std::vector<std::uint8_t> serialize(std::span<const PipelineRecipe> recipes);
            static std::optional<std::vector<PipelineRecipe>> parse(std::span<const std::uint8_t> bytes);

        private:
            std::uint64_t fingerprint(const PipelineRecipe &recipe) const;
            Utils::PrivateDirectory m_directory;
            std::string m_filename;
            std::vector<PipelineRecipe> m_recipes;
            std::unordered_multimap<std::uint64_t, std::size_t> m_index;
            std::size_t m_saved{};
            std::chrono::steady_clock::time_point m_lastSave{};
            std::future<bool> m_save;
    };
} // namespace Core::Gfx
