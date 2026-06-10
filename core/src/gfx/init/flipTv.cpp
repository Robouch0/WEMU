//
// Created by nicolas on 5/25/26.
//

#include <cstring>
#include "../Renderer.hpp"

void Renderer::initFlipTV()
{
    const VkDeviceSize bufSize = WIDTH * HEIGHT * 4;

    createBuffer(bufSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_tvStagingBuffer, m_tvStagingMemory);

    void *mapped = nullptr;
    vkMapMemory(m_logicalDevice, m_tvStagingMemory, 0, bufSize, 0, &mapped);
    std::memset(mapped, 0, bufSize);
    vkUnmapMemory(m_logicalDevice, m_tvStagingMemory);

    createImage(WIDTH, HEIGHT,
                VK_FORMAT_R8G8B8A8_UNORM, // matches your swapchain BGRA format
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_tvImage, m_tvImageMemory);
}
