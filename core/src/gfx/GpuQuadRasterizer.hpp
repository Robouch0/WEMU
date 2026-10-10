#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace Core::Gfx {

    // GPU replacement for the software per-pixel fill in Gx2Replayer. It rasterises the frame's
    // textured triangles into an offscreen RGBA8 image on the GPU and reads the result back into
    // the CPU framebuffer, which the existing Renderer::flip_tv then presents. This keeps all the
    // hard-won geometry/placement logic (shader execution, tiling, UI fallback) on the CPU while
    // moving the expensive 17M-texels/frame fill onto the GPU.
    //
    // Lifetime: constructed lazily by the Renderer from its already-created Vulkan handles (device,
    // queue, command pool). One instance per Renderer. Textures are cached by guest address+content
    // hash so a static frame uploads nothing.
    class GpuQuadRasterizer {
        public:
            struct Vertex {
                    float x, y; // framebuffer pixel space [0,W]x[0,H]
                    float u, v; // texture coords [0,1]
            };

            GpuQuadRasterizer(VkPhysicalDevice phys, VkDevice device, VkQueue queue, VkCommandPool pool, std::uint32_t width, std::uint32_t height);
            ~GpuQuadRasterizer();

            GpuQuadRasterizer(const GpuQuadRasterizer &) = delete;
            GpuQuadRasterizer &operator=(const GpuQuadRasterizer &) = delete;

            // Start a new frame: clears the offscreen target and resets per-frame vertex/draw state.
            void begin();
            // Queue one draw: `key` identifies the guest texture (its image address), `rgba` is the
            // decoded tw*th RGBA8 image, `verts`/`count` are screen-space triangles (count % 3 == 0).
            void drawTriangles(std::uint64_t key, const std::uint8_t *rgba, std::uint32_t tw, std::uint32_t th, const Vertex *verts,
                               std::uint32_t count);
            // Submit the frame and read the result back into `outRgbx` (width*height*4 bytes, RGBX).
            void end(std::uint8_t *outRgbx);

        private:
            struct Texture {
                    VkImage image = VK_NULL_HANDLE;
                    VkDeviceMemory memory = VK_NULL_HANDLE;
                    VkImageView view = VK_NULL_HANDLE;
                    VkDescriptorSet descriptor = VK_NULL_HANDLE;
                    std::uint32_t w = 0, h = 0;
                    std::uint64_t hash = 0;
            };
            struct Draw {
                    VkDescriptorSet descriptor;
                    std::uint32_t firstVertex;
                    std::uint32_t vertexCount;
            };

            std::uint32_t findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags props) const;
            void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer &buf, VkDeviceMemory &mem) const;
            VkCommandBuffer beginOneTime() const;
            void endOneTime(VkCommandBuffer cmd) const;
            void createOffscreenTarget();
            void createRenderPass();
            void createPipeline();
            void createSampler();
            void createDescriptorPool();
            Texture &getOrCreateTexture(std::uint64_t key, const std::uint8_t *rgba, std::uint32_t tw, std::uint32_t th);

            VkPhysicalDevice m_phys;
            VkDevice m_device;
            VkQueue m_queue;
            VkCommandPool m_pool;
            std::uint32_t m_width, m_height;

            // Offscreen colour target.
            VkImage m_color = VK_NULL_HANDLE;
            VkDeviceMemory m_colorMem = VK_NULL_HANDLE;
            VkImageView m_colorView = VK_NULL_HANDLE;
            VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
            VkRenderPass m_renderPass = VK_NULL_HANDLE;

            // Pipeline.
            VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
            VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
            VkPipeline m_pipeline = VK_NULL_HANDLE;
            VkSampler m_sampler = VK_NULL_HANDLE;
            VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

            // Dynamic vertex buffer (host-visible, persistently mapped) + readback buffer.
            VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
            VkDeviceMemory m_vertexMem = VK_NULL_HANDLE;
            void *m_vertexMapped = nullptr;
            std::uint32_t m_vertexCapacity = 0;
            VkBuffer m_readback = VK_NULL_HANDLE;
            VkDeviceMemory m_readbackMem = VK_NULL_HANDLE;

            VkFence m_fence = VK_NULL_HANDLE;

            std::uint32_t m_vertexCount = 0;
            std::vector<Draw> m_draws;
            std::unordered_map<std::uint64_t, Texture> m_textures;
    };

} // namespace Core::Gfx
