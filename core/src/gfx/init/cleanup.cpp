//
// Created by nicolas on 2/18/26.
//

#include "../Renderer.hpp"

void Renderer::recreateSwapChain()
{

    vkDeviceWaitIdle(m_logicalDevice);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
}

void Renderer::cleanupSwapChain() const
{
    for (const auto imageView: m_swapChainImageViews) {
        vkDestroyImageView(m_logicalDevice, imageView, nullptr);
    }
    vkDestroySwapchainKHR(m_logicalDevice, m_swapChain, nullptr);
}

void Renderer::cleanupFlipTv() const
{
    vkDestroyImage(m_logicalDevice, m_tvImage, nullptr);
    vkFreeMemory(m_logicalDevice, m_tvImageMemory, nullptr);
    vkDestroyBuffer(m_logicalDevice, m_tvStagingBuffer, nullptr);
    vkFreeMemory(m_logicalDevice, m_tvStagingMemory, nullptr);
}

void Renderer::cleanup() const
{
    cleanupFlipTv();
    cleanupSwapChain();

    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(m_logicalDevice, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_logicalDevice, m_renderFinishedSemaphores[i], nullptr);
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
