//
// Created by nicolas on 5/25/26.
//

#include <cstring>
#include <stdexcept>

#include "../Renderer.hpp"

void Renderer::initFlipTV()
{
    const VkDeviceSize bufSize = WIDTH * HEIGHT * 4;

    m_tvStagingBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_tvStagingMemories.resize(MAX_FRAMES_IN_FLIGHT);
    m_tvStagingMapped.resize(MAX_FRAMES_IN_FLIGHT);
    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createBuffer(bufSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_tvStagingBuffers[i], m_tvStagingMemories[i]);
        if (vkMapMemory(m_logicalDevice, m_tvStagingMemories[i], 0, bufSize, 0, &m_tvStagingMapped[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to map TV upload buffer");
        std::memset(m_tvStagingMapped[i], 0, bufSize);
    }

    createImage(WIDTH, HEIGHT,
                VK_FORMAT_R8G8B8A8_UNORM, // matches your swapchain BGRA format
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_tvImage, m_tvImageMemory);
}
