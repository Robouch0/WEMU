//
// Created by nicolas on 2/18/26.
//

#include "headers/wemuEngineVulkan.hpp"
#include <stdexcept>

void WemuEngineVulkan::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    const auto app = static_cast<WemuEngineVulkan*>(glfwGetWindowUserPointer(window));
    app->m_framebufferResized = true;
}

void WemuEngineVulkan::createFramebuffers() {
    swapChainFramebuffers.resize(m_swapChainImageViews.size());

    for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
        const VkImageView attachments[] = {
            m_swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_logicalDevice, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}
