#pragma once

#include <cstdint> // Necessary for uint32_t
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

class Renderer {
    public:
        Renderer()
        {
            initWindow();
            initVulkan();
        }

        ~Renderer() { cleanup(); }

        void flip_tv(const std::uint8_t *rgbx, std::uint32_t w, std::uint32_t h);

        bool poll_events();

        std::uint32_t get_buttons();

        bool is_open() const { return m_open; }

        struct QueueFamilyIndices {
                std::optional<uint32_t> graphicsFamily;
                std::optional<uint32_t> presentFamily;

                [[nodiscard]] bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
        };

        struct SwapChainSupportDetails {
                VkSurfaceCapabilitiesKHR capabilities{};
                std::vector<VkSurfaceFormatKHR> formats;
                std::vector<VkPresentModeKHR> presentModes;
        };

    private:
        void createInstance();

        void initWindow();

        static void framebufferResizeCallback(GLFWwindow *window, int width, int height);

        void initVulkan()
        {
            createInstance();
            createSurface();
            pickPhysicalDevice();
            createLogicalDevice();
            createSwapChain();
            createImageViews();
            createCommandPool();
            createCommandBuffers();
            createSyncObjects();
            initFlipTV();
        }

        void initFlipTV();

        void createRenderPass();

        [[nodiscard]] VkShaderModule createShaderModule(const std::vector<char> &code) const;

        void createDescriptorSetLayout();

        void createGraphicsPipeline();

        void createFramebuffers();

        void createSwapChain();

        void createSurface();

        void pickPhysicalDevice();

        void createLogicalDevice();

        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;

        static bool checkDeviceExtensionSupport(VkPhysicalDevice device);

        bool isDeviceSuitable(VkPhysicalDevice device) const;

        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;

        static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);

        static VkPresentModeKHR chooseSwapPresentMode();

        [[nodiscard]] VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) const;

        void createSyncObjects();

        void createCommandPool();

        void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory &imageMemory) const;

        VkImageView createImageView(VkImage image, VkFormat format);

        void createImageViews();

        void createTextureImage();

        void createTextureImageView();

        void createTextureSampler();

        void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;

        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory) const;

        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const;

        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;

        void createVertexBuffer();

        void createIndexBuffer();

        void createUniformBuffers();

        void createDescriptorPool();

        void createDescriptorSets();

        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        void createCommandBuffers();

        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;

        void mainLoop();

        void drawFrame();

        void updateUniformBuffer(uint32_t currentImage) const;

        void recreateSwapChain();

        void cleanupSwapChain() const;

        void cleanupFlipTv() const;

        void cleanup() const;

        GLFWwindow *m_window;
        VkInstance m_instance;
        VkPhysicalDevice m_physicalDevice;
        VkDevice m_logicalDevice;
        VkSurfaceKHR m_surface;

        VkQueue m_graphicsQueue;
        VkQueue m_presentQueue;

        VkSwapchainKHR m_swapChain;
        std::vector<VkImage> m_swapChainImages;
        VkFormat m_swapChainImageFormat;
        VkExtent2D m_swapChainExtent;
        std::vector<VkImageView> m_swapChainImageViews;
        std::vector<VkFramebuffer> swapChainFramebuffers;
        VkRenderPass m_renderPass;

        VkDescriptorSetLayout m_descriptorSetLayout;
        VkDescriptorPool m_descriptorPool;
        std::vector<VkDescriptorSet> m_descriptorSets;

        VkPipelineLayout m_pipelineLayout;
        VkPipeline m_graphicsPipeline;

        VkImage m_textureImage;
        VkImageView m_textureImageView;
        VkSampler m_textureSampler;
        VkDeviceMemory m_textureImageMemory;

        VkCommandPool m_commandPool;
        std::vector<VkCommandBuffer> m_commandBuffers;

        std::vector<VkSemaphore> m_imageAvailableSemaphores;
        std::vector<VkSemaphore> m_renderFinishedSemaphores;
        std::vector<VkFence> m_inFlightFences;

        VkBuffer m_vertexBuffer;
        VkDeviceMemory m_vertexBufferMemory;

        VkBuffer m_indexBuffer;
        VkDeviceMemory m_indexBufferMemory;

        std::vector<VkBuffer> m_uniformBuffers;
        std::vector<VkDeviceMemory> m_uniformBuffersMemory;
        std::vector<void *> m_uniformBuffersMapped;

        uint32_t m_currentFrame = 0;

        VkBuffer m_tvStagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_tvStagingMemory = VK_NULL_HANDLE;
        VkImage m_tvImage = VK_NULL_HANDLE;
        VkDeviceMemory m_tvImageMemory = VK_NULL_HANDLE;

        bool m_framebufferResized = false;

        bool m_open = true;
};
