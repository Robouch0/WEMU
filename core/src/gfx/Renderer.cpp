//
// Created by nicolas on 5/21/26.
//

#include "Renderer.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

// Wii U targets 60 fps. Cap flip_tv so the interpreter doesn't run faster than real hardware when the swapchain has spare images to return immediately.
static constexpr auto kTargetFrameTime = std::chrono::duration<double>(1.0 / 60.0);


// VPAD button bitmasks (same as wut header vpad/input.h)
static constexpr std::uint32_t BTN_UP = 0x0200;
static constexpr std::uint32_t BTN_DOWN = 0x0100;
static constexpr std::uint32_t BTN_LEFT = 0x0800;
static constexpr std::uint32_t BTN_RIGHT = 0x0400;
static constexpr std::uint32_t BTN_A = 0x8000;
static constexpr std::uint32_t BTN_B = 0x4000;
static constexpr std::uint32_t BTN_PLUS = 0x0008;
static constexpr std::uint32_t BTN_MINUS = 0x0004;

void pipelineBarrier(const VkImage &image, const VkImageLayout &oldLayout, const VkImageLayout &newLayout, const VkAccessFlags srcAccess,
                     const VkAccessFlags dstAccess, const VkPipelineStageFlags srcStage, const VkPipelineStageFlags dstStage,
                     const VkCommandBuffer &cmd)
{
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
};

Renderer::Renderer(VkInstance instance, VkSurfaceKHR surface, uint32_t w, uint32_t h)
    : m_instance(instance), m_surface(surface), m_surfaceWidth(w), m_surfaceHeight(h), m_embedded(true)
{
    initVulkanPipeline();
}

void Renderer::flip_tv(const std::uint8_t *rgbx, std::uint32_t w, std::uint32_t h)
{
    const std::size_t cols = (w < WIDTH) ? w : WIDTH;
    const std::size_t rows = (h < HEIGHT) ? h : HEIGHT;

    // map to cpu readable
    void *mapped = nullptr;
    vkMapMemory(m_logicalDevice, m_tvStagingMemory, 0, WIDTH * HEIGHT * 4, 0, &mapped);

    auto *dst = static_cast<std::uint32_t *>(mapped);
    const auto *src = reinterpret_cast<const std::uint32_t *>(rgbx);

    for (std::uint32_t y = 0; y < rows; ++y) {
        const std::uint32_t *srow = src + static_cast<std::size_t>(y) * w;
        std::uint32_t *drow = dst + static_cast<std::size_t>(y) * WIDTH;
        std::memcpy(drow, srow, cols * 4);
    }
    vkUnmapMemory(m_logicalDevice, m_tvStagingMemory);
    // endMapping

    // asks the swapchain the next available image to render into
    uint32_t imageIndex = 0;
    VkResult result;

    while (true) {
        result = vkAcquireNextImageKHR(m_logicalDevice, m_swapChain, UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE,
                                       &imageIndex);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
            break;
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            continue;
        }
        throw std::runtime_error("flip_tv: failed to acquire swap chain image");
    }
    // end of image retrieval

    vkWaitForFences(m_logicalDevice, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(m_logicalDevice, 1, &m_inFlightFences[m_currentFrame]);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    // starts recording the command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("flip_tv: failed to begin command buffer");

    pipelineBarrier(m_tvImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, cmd);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = WIDTH;
    region.bufferImageHeight = HEIGHT;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {WIDTH, HEIGHT, 1};
    vkCmdCopyBufferToImage(cmd, m_tvStagingBuffer, m_tvImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    pipelineBarrier(m_tvImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, cmd);

    pipelineBarrier(m_swapChainImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, cmd);


    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {WIDTH, HEIGHT, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<std::int32_t>(m_swapChainExtent.width), static_cast<std::int32_t>(m_swapChainExtent.height), 1};
    vkCmdBlitImage(cmd, m_tvImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, VK_FILTER_LINEAR);

    pipelineBarrier(m_swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("flip_tv: failed to record command buffer");
    // end image transfers

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_imageAvailableSemaphores[m_currentFrame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinishedSemaphores[m_currentFrame];

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("flip_tv: failed to submit command buffer");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("flip_tv: failed to present");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    // Software frame cap / sleep the remaining time in this 1/60 s budget so
    // the game runs at Wii U native speed even when spare swapchain images
    // would otherwise let vkAcquireNextImageKHR return without blocking.
    {
        using Clock = std::chrono::steady_clock;
        static thread_local Clock::time_point s_lastFlip = Clock::now();
        auto elapsed = Clock::now() - s_lastFlip;
        if (elapsed < kTargetFrameTime)
            std::this_thread::sleep_for(kTargetFrameTime - elapsed);
        s_lastFlip = Clock::now();
    }

    if (m_onFirstFrame) {
        auto cb = std::move(m_onFirstFrame);
        cb();
    }
}

bool Renderer::poll_events()
{
    if (m_embedded)
        return true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            m_open = false;
            return false;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            m_open = false;
            return false;
        }
    }
    return true;
}

std::uint32_t Renderer::get_buttons() const
{
    if (m_embedded)
        return 0;
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    std::uint32_t btns = 0;
    if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) btns |= BTN_UP;
    if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) btns |= BTN_DOWN;
    if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) btns |= BTN_LEFT;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) btns |= BTN_RIGHT;
    if (keys[SDL_SCANCODE_RETURN])                         btns |= BTN_A;
    if (keys[SDL_SCANCODE_BACKSPACE])                      btns |= BTN_B;
    if (keys[SDL_SCANCODE_P])                              btns |= BTN_PLUS;
    if (keys[SDL_SCANCODE_M])                              btns |= BTN_MINUS;
    return btns;
}
