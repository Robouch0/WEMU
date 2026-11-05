//
// Created by nicolas on 2/18/26.
//

#include <stdexcept>

#include "../Renderer.hpp"

void Renderer::createSyncObjects()
{
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_logicalDevice, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_logicalDevice, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create semaphores!");
        }
    }
    createPresentSemaphores();
}

void Renderer::createPresentSemaphores()
{
    // Acquiring the same image again guarantees its previous present has consumed
    // this semaphore. A graphics fence alone does not prove presentation finished.
    m_renderFinishedSemaphores.assign(m_swapChainImages.size(), VK_NULL_HANDLE);
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto &semaphore: m_renderFinishedSemaphores)
        if (vkCreateSemaphore(m_logicalDevice, &info, nullptr, &semaphore) != VK_SUCCESS)
            throw std::runtime_error("failed to create presentation semaphore");
}
