//
// Created by nicolas on 5/25/26.
//

#include "../Renderer.hpp"

void Renderer::initFlipTV() {
    const VkDeviceSize bufSize = WIDTH * HEIGHT * 4;

    // Staging buffer: CPU writes pixels here every frame.
    // Reuses your existing createBuffer() helper.
    createBuffer(
        bufSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_tvStagingBuffer,
        m_tvStagingMemory
    );

    // GPU image at TV resolution: upload destination, then blit source.
    // Reuses your existing createImage() helper.
    createImage(
        WIDTH, HEIGHT,
        VK_FORMAT_R8G8B8A8_UNORM,          // matches your swapchain BGRA format
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_tvImage,
        m_tvImageMemory
    );

}