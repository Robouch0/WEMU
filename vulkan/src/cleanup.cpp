//
// Created by nicolas on 2/18/26.
//

#include "headers/wemuEngineVulkan.hpp"

void WemuEngineVulkan::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_logicalDevice);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createFramebuffers();
}

void WemuEngineVulkan::cleanupSwapChain() const {
    for (const auto framebuffer: swapChainFramebuffers) {
        vkDestroyFramebuffer(m_logicalDevice, framebuffer, nullptr);
    }
    for (const auto imageView : m_swapChainImageViews) {
        vkDestroyImageView(m_logicalDevice, imageView, nullptr);
    }
    vkDestroySwapchainKHR(m_logicalDevice, m_swapChain, nullptr);
}

void WemuEngineVulkan::cleanup() const {
    cleanupSwapChain();

    vkDestroyBuffer(m_logicalDevice, m_vertexBuffer, nullptr);
    vkFreeMemory(m_logicalDevice, m_vertexBufferMemory, nullptr);
    vkDestroyBuffer(m_logicalDevice, m_indexBuffer, nullptr);
    vkFreeMemory(m_logicalDevice, m_indexBufferMemory, nullptr);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(m_logicalDevice, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_logicalDevice, m_renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(m_logicalDevice, m_inFlightFences[i], nullptr);
        vkDestroyBuffer(m_logicalDevice, m_uniformBuffers[i], nullptr);
        vkFreeMemory(m_logicalDevice, m_uniformBuffersMemory[i], nullptr);
    }
    vkDestroyDescriptorPool(m_logicalDevice, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_logicalDevice, m_descriptorSetLayout, nullptr);
    vkDestroyCommandPool(m_logicalDevice, m_commandPool, nullptr);
    vkDestroyPipeline(m_logicalDevice, m_graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(m_logicalDevice, m_pipelineLayout, nullptr);
    vkDestroyRenderPass(m_logicalDevice, m_renderPass, nullptr);
    vkDestroyDevice(m_logicalDevice, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
    glfwDestroyWindow(m_window);
    glfwTerminate();
}