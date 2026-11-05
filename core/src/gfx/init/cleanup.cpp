//
// Created by nicolas on 2/18/26.
//

#include "../GpuQuadRasterizer.hpp"
#include "../GpuRenderGraph.hpp"
#include "../Renderer.hpp"

void Renderer::recreateSwapChain()
{

    vkDeviceWaitIdle(m_logicalDevice);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createPresentSemaphores();
}

void Renderer::cleanupSwapChain() const
{
    for (const auto semaphore: m_renderFinishedSemaphores)
        vkDestroySemaphore(m_logicalDevice, semaphore, nullptr);
    for (const auto imageView: m_swapChainImageViews) {
        vkDestroyImageView(m_logicalDevice, imageView, nullptr);
    }
    vkDestroySwapchainKHR(m_logicalDevice, m_swapChain, nullptr);
}

void Renderer::cleanupFlipTv() const
{
    vkDestroyImage(m_logicalDevice, m_tvImage, nullptr);
    vkFreeMemory(m_logicalDevice, m_tvImageMemory, nullptr);
    for (std::size_t i = 0; i < m_tvStagingBuffers.size(); ++i) {
        vkUnmapMemory(m_logicalDevice, m_tvStagingMemories[i]);
        vkDestroyBuffer(m_logicalDevice, m_tvStagingBuffers[i], nullptr);
        vkFreeMemory(m_logicalDevice, m_tvStagingMemories[i], nullptr);
    }
}

void Renderer::cleanup() const
{
    vkDeviceWaitIdle(m_logicalDevice);

    // Destroy the GPU rasteriser / render graph (own Vulkan objects) before we tear down device/pool.
    delete m_gpuQuad;
    delete m_gpuGraph;

    cleanupFlipTv();
    cleanupSwapChain();

    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(m_logicalDevice, m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_logicalDevice, m_inFlightFences[i], nullptr);
    }
    vkDestroyCommandPool(m_logicalDevice, m_commandPool, nullptr);
    vkDestroyDevice(m_logicalDevice, nullptr);

    if (!m_embedded) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        vkDestroyInstance(m_instance, nullptr);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
    }
}
