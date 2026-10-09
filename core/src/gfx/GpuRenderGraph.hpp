#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>

namespace Core::Gfx {

    // GPU render-target graph: a general GX2->Vulkan compositor that keeps every guest colour buffer
    // as its own persistent GPU image. Draws render into the currently-bound target; when a later
    // draw samples a colour buffer as a texture (render-to-texture — MK8's menu renders offscreen
    // buffers, downsamples them for bloom, then composites), the graph samples that target's image
    // directly instead of a CPU-decoded copy. Non-target sources (loaded guest textures) are decoded
    // by the caller and uploaded/cached here.
    //
    // Execution is deferred: the frame's operations are recorded (targets bound, clears, draws with
    // their source) and replayed into a single command buffer at present time. Draws are grouped
    // into passes by target so the sampled-target layout transitions (COLOR_ATTACHMENT<->SHADER_READ)
    // can be inserted at pass boundaries, where Vulkan 1.0 render passes permit them.
    //
    // Coordinate space: vertices arrive in target pixel space (the GX2 viewport the game set for that
    // target); the vertex shader converts pixel->NDC with a per-pass invSize push constant, so one
    // pipeline (dynamic viewport/scissor) serves every target size.
    class GpuRenderGraph {
        public:
            struct Vertex {
                    float x, y; // target pixel space [0,W]x[0,H]
                    float u, v; // texture coords [0,1]
            };

            GpuRenderGraph(VkPhysicalDevice phys, VkDevice device, VkQueue queue, VkCommandPool pool, std::uint32_t maxWidth,
                           std::uint32_t maxHeight);
            ~GpuRenderGraph();

            GpuRenderGraph(const GpuRenderGraph &) = delete;
            GpuRenderGraph &operator=(const GpuRenderGraph &) = delete;

            // Start a fresh frame: resets the per-frame op/pass/vertex state.
            void beginFrame();
            // Bind the guest colour buffer at `addr` (w x h) as the current render target, creating
            // (or resizing) its persistent GPU image on demand.
            void bindTarget(std::uint32_t addr, std::uint32_t w, std::uint32_t h);
            // Clear the colour buffer at `addr` (w x h) to rgba (0..1), creating its image on demand.
            // Does not change the current render target.
            void clearTarget(std::uint32_t addr, std::uint32_t w, std::uint32_t h, const float rgba[4]);
            // True if `addr` has been bound as a colour target this run (=> it is a render-to-texture
            // source, not a loaded guest texture).
            [[nodiscard]] bool isTarget(std::uint32_t addr) const noexcept { return m_targets.count(addr) != 0; }
            // Queue a draw into the CURRENT target. If `srcRgba` is null, `srcAddr` names a colour
            // target whose image is sampled; otherwise srcRgba/tw/th is an RGBA8 texture to upload.
            void draw(std::uint32_t srcAddr, const std::uint8_t *srcRgba, std::uint32_t tw, std::uint32_t th, const Vertex *verts,
                      std::uint32_t count);
            // Execute the recorded frame and read the scan target (`scanAddr`) back into `outRgbx`
            // (maxWidth*maxHeight*4, RGBX). If scanAddr is unknown, outRgbx is left cleared.
            void present(std::uint32_t scanAddr, std::uint8_t *outRgbx);

        private:
            struct Target {
                    VkImage image = VK_NULL_HANDLE;
                    VkDeviceMemory memory = VK_NULL_HANDLE;
                    VkImageView view = VK_NULL_HANDLE;
                    VkFramebuffer framebuffer = VK_NULL_HANDLE;
                    VkDescriptorSet descriptor = VK_NULL_HANDLE; // for sampling this target
                    std::uint32_t w = 0, h = 0;
                    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            };
            struct Texture {
                    VkImage image = VK_NULL_HANDLE;
                    VkDeviceMemory memory = VK_NULL_HANDLE;
                    VkImageView view = VK_NULL_HANDLE;
                    VkDescriptorSet descriptor = VK_NULL_HANDLE;
                    std::uint32_t w = 0, h = 0;
                    std::uint64_t hash = 0;
            };
            struct DrawCmd {
                    VkDescriptorSet descriptor;
                    std::uint32_t srcTarget; // non-zero => sampled colour target that needs SHADER_READ
                    std::uint32_t firstVertex;
                    std::uint32_t vertexCount;
            };
            struct Pass {
                    std::uint32_t target;
                    bool clear = false;
                    float clearColor[4]{0, 0, 0, 1};
                    std::vector<DrawCmd> draws;
                    std::unordered_set<std::uint32_t> sources; // colour targets this pass samples
            };

            std::uint32_t findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags props) const;
            void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer &buf,
                              VkDeviceMemory &mem) const;
            VkCommandBuffer beginOneTime() const;
            void endOneTime(VkCommandBuffer cmd) const;
            void createRenderPass();
            void createSampler();
            void createDescriptorInfra();
            void createPipeline();
            Target &getOrCreateTarget(std::uint32_t addr, std::uint32_t w, std::uint32_t h);
            Texture &getOrCreateTexture(std::uint32_t key, const std::uint8_t *rgba, std::uint32_t tw, std::uint32_t th);
            void transitionTarget(VkCommandBuffer cmd, Target &t, VkImageLayout newLayout);
            Pass &currentPassFor(std::uint32_t target);

            VkPhysicalDevice m_phys;
            VkDevice m_device;
            VkQueue m_queue;
            VkCommandPool m_pool;
            std::uint32_t m_maxWidth, m_maxHeight;

            VkRenderPass m_renderPass = VK_NULL_HANDLE;
            VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
            VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
            VkPipeline m_pipeline = VK_NULL_HANDLE;
            VkSampler m_sampler = VK_NULL_HANDLE;
            VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

            VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
            VkDeviceMemory m_vertexMem = VK_NULL_HANDLE;
            void *m_vertexMapped = nullptr;
            std::uint32_t m_vertexCapacity = 0;
            VkBuffer m_readback = VK_NULL_HANDLE;
            VkDeviceMemory m_readbackMem = VK_NULL_HANDLE;
            VkFence m_fence = VK_NULL_HANDLE;

            std::unordered_map<std::uint32_t, Target> m_targets;
            std::unordered_map<std::uint32_t, Texture> m_textures;

            // Per-frame state.
            std::uint32_t m_curTarget = 0;
            std::uint32_t m_vertexCount = 0;
            std::vector<Pass> m_passes;
    };

} // namespace Core::Gfx
