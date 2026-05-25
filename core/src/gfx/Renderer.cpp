// /*
// ** Renderer.cpp — SDL2 + SDL_Texture framebuffer display.
// **
// ** PPC OSScreen pixels are stored RGBX (R=byte0, G=byte1, B=byte2, X=byte3).
// ** SDL_PIXELFORMAT_ABGR8888 on little-endian reads bytes from memory as
// ** [R, G, B, A] — identical layout. No bswap needed; copy pixels directly.
// **
// **   clear (0x00000000) → [0,0,0,0]     → black background   ✓
// **   draw_rect black    → [0,0,0,FF]    → black (center line) ✓
// **   draw_rect red      → [FF,0,0,FF]   → red (ball)          ✓
// **   draw_rect green    → [0,FF,0,FF]   → green (paddles)     ✓
// **   blit_char white    → [FF,FF,FF,FF] → white (text)        ✓
// */
//
// #include "Renderer.hpp"
//
// #include <cstring>
// #include <stdexcept>
//
// // VPAD button bitmasks (same as wut header vpad/input.h)
// static constexpr std::uint32_t BTN_UP = 0x0200;
// static constexpr std::uint32_t BTN_DOWN = 0x0100;
// static constexpr std::uint32_t BTN_LEFT = 0x0800;
// static constexpr std::uint32_t BTN_RIGHT = 0x0400;
// static constexpr std::uint32_t BTN_A = 0x8000;
// static constexpr std::uint32_t BTN_B = 0x4000;
// static constexpr std::uint32_t BTN_PLUS = 0x0008;
// static constexpr std::uint32_t BTN_MINUS = 0x0004;
//
// Renderer::Renderer()
// {
//     if (SDL_Init(SDL_INIT_VIDEO) != 0)
//         throw std::runtime_error(SDL_GetError());
//
//     m_window = SDL_CreateWindow("WEMU — pong", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, TV_W, TV_H, SDL_WINDOW_SHOWN);
//     if (!m_window)
//         throw std::runtime_error(SDL_GetError());
//
//     m_sdl_rend = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
//     if (!m_sdl_rend)
//         throw std::runtime_error(SDL_GetError());
//
//     // ABGR8888: on little-endian, memory order is [R, G, B, A] — matches PPC RGBX directly
//     m_texture = SDL_CreateTexture(m_sdl_rend, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, TV_W, TV_H);
//     if (!m_texture)
//         throw std::runtime_error(SDL_GetError());
// }
//
// Renderer::~Renderer()
// {
//     if (m_texture)
//         SDL_DestroyTexture(m_texture);
//     if (m_sdl_rend)
//         SDL_DestroyRenderer(m_sdl_rend);
//     if (m_window)
//         SDL_DestroyWindow(m_window);
//     SDL_Quit();
// }
//
// // Copy RGBX framebuffer to ABGR8888 texture (same memory layout) and present.
// void Renderer::flip_tv(const std::uint8_t *rgbx, std::uint32_t w, std::uint32_t h)
// {
//     void *pixels;
//     int pitch;
//     if (SDL_LockTexture(m_texture, nullptr, &pixels, &pitch) != 0)
//         return;
//
//     const std::uint32_t cols = (w < (std::uint32_t) TV_W) ? w : TV_W;
//     const std::uint32_t rows = (h < (std::uint32_t) TV_H) ? h : TV_H;
//
//     auto *dst32 = static_cast<std::uint32_t *>(pixels);
//     const auto *src32 = reinterpret_cast<const std::uint32_t *>(rgbx);
//     const std::uint32_t stride32 = pitch / 4;
//
//     for (std::uint32_t y = 0; y < rows; ++y) {
//         const std::uint32_t *srow = src32 + static_cast<std::size_t>(y) * w;
//         std::uint32_t *drow = dst32 + static_cast<std::size_t>(y) * stride32;
//         for (std::uint32_t x = 0; x < cols; ++x)
//             drow[x] = srow[x];
//     }
//
//     SDL_UnlockTexture(m_texture);
//     SDL_RenderClear(m_sdl_rend);
//     SDL_RenderCopy(m_sdl_rend, m_texture, nullptr, nullptr);
//
//     // Cap to 60fps (real WiiU framerate), regardless of monitor refresh rate
//     static std::uint64_t s_last_flip = 0;
//     std::uint64_t now = SDL_GetTicks64();
//     if (s_last_flip && now - s_last_flip < 16)
//         SDL_Delay(16 - (now - s_last_flip));
//     s_last_flip = SDL_GetTicks64();
//
//     SDL_RenderPresent(m_sdl_rend);
// }
//
// bool Renderer::poll_events()
// {
//     SDL_Event ev;
//     while (SDL_PollEvent(&ev)) {
//         if (ev.type == SDL_QUIT) {
//             m_open = false;
//             return false;
//         }
//         if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
//             m_open = false;
//             return false;
//         }
//     }
//     return m_open;
// }
//
// std::uint32_t Renderer::get_buttons()
// {
//     const std::uint8_t *keys = SDL_GetKeyboardState(nullptr);
//     std::uint32_t btns = 0;
//     if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])
//         btns |= BTN_UP;
//     if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])
//         btns |= BTN_DOWN;
//     if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])
//         btns |= BTN_LEFT;
//     if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D])
//         btns |= BTN_RIGHT;
//     if (keys[SDL_SCANCODE_RETURN])
//         btns |= BTN_A;
//     if (keys[SDL_SCANCODE_BACKSPACE])
//         btns |= BTN_B;
//     if (keys[SDL_SCANCODE_P])
//         btns |= BTN_PLUS;
//     if (keys[SDL_SCANCODE_M])
//         btns |= BTN_MINUS;
//     return btns;
// }

//
// Created by nicolas on 5/21/26.
//

#include "Renderer.hpp"
#include <cstring>
#include <stdexcept>
#include <thread>
#include <chrono>


// VPAD button bitmasks (same as wut header vpad/input.h)
static constexpr std::uint32_t BTN_UP = 0x0200;
static constexpr std::uint32_t BTN_DOWN = 0x0100;
static constexpr std::uint32_t BTN_LEFT = 0x0800;
static constexpr std::uint32_t BTN_RIGHT = 0x0400;
static constexpr std::uint32_t BTN_A = 0x8000;
static constexpr std::uint32_t BTN_B = 0x4000;
static constexpr std::uint32_t BTN_PLUS = 0x0008;
static constexpr std::uint32_t BTN_MINUS = 0x0004;

void Renderer::flip_tv(const std::uint8_t *rgbx, std::uint32_t w, std::uint32_t h)
{
        static std::chrono::steady_clock::time_point s_lastFlip{};
    {
        auto now = std::chrono::steady_clock::now();
        if (s_lastFlip != std::chrono::steady_clock::time_point{}) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - s_lastFlip).count();
            if (ms < 16)
                std::this_thread::sleep_for(std::chrono::milliseconds(16 - ms));
        }
        s_lastFlip = std::chrono::steady_clock::now();
    }

    // ── Step 1: CPU pixel copy into the staging buffer ────────────────────────
    // Mirrors: SDL_LockTexture  +  the row-copy loop  +  SDL_UnlockTexture.
    //
    // Your source is RGBX. Your swapchain (and m_tvImage) are BGRA.
    // We swap R and B channels here so colours come out correct.
    {
        const std::uint32_t cols = (w < WIDTH) ? w : WIDTH;
        const std::uint32_t rows = (h < HEIGHT) ? h : HEIGHT;

        void *mapped = nullptr;
        vkMapMemory(m_logicalDevice, m_tvStagingMemory, 0, WIDTH * HEIGHT * 4, 0, &mapped);

        auto       *dst = static_cast<std::uint32_t *>(mapped);
        const auto *src = reinterpret_cast<const std::uint32_t *>(rgbx);

        for (std::uint32_t y = 0; y < rows; ++y) {
            const std::uint32_t *srow = src + static_cast<std::size_t>(y) * w;
                  std::uint32_t *drow = dst + static_cast<std::size_t>(y) * WIDTH;
            std::memcpy(drow, srow, cols * 4);
        }
        // HOST_COHERENT memory — no explicit flush needed (same as SDL).
        vkUnmapMemory(m_logicalDevice, m_tvStagingMemory);
    }

    // ── Step 2: Acquire swapchain image ───────────────────────────────────────
    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        m_logicalDevice, m_swapChain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("flip_tv: failed to acquire swap chain image");

    // ── Step 3: Wait for this frame slot's fence, then reset it ───────────────
    vkWaitForFences(m_logicalDevice, 1, &m_inFlightFences[m_currentFrame],
                    VK_TRUE, UINT64_MAX);
    vkResetFences  (m_logicalDevice, 1, &m_inFlightFences[m_currentFrame]);

    // ── Step 4: Record GPU commands ───────────────────────────────────────────
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("flip_tv: failed to begin command buffer");

    // Helper: insert one image memory barrier
    const auto barrier = [&](VkImage image,
                              VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                              VkPipelineStageFlags srcStage,
                              VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = oldLayout;
        b.newLayout           = newLayout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = image;
        b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask       = srcAccess;
        b.dstAccessMask       = dstAccess;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // 4a. m_tvImage: UNDEFINED → TRANSFER_DST  (ready to receive the upload)
    barrier(m_tvImage,
        VK_IMAGE_LAYOUT_UNDEFINED,          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,                                  VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,  VK_PIPELINE_STAGE_TRANSFER_BIT);

    // 4b. Upload staging buffer → m_tvImage
    //     Mirrors: SDL_UnlockTexture (GPU DMA from CPU buffer to texture)
    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = WIDTH;   // tightly packed, no SDL pitch padding
    region.bufferImageHeight = HEIGHT;
    region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset       = {0, 0, 0};
    region.imageExtent       = {WIDTH, HEIGHT, 1};
    vkCmdCopyBufferToImage(cmd, m_tvStagingBuffer, m_tvImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // 4c. m_tvImage: TRANSFER_DST → TRANSFER_SRC  (ready to blit from)
    barrier(m_tvImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,          VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,        VK_PIPELINE_STAGE_TRANSFER_BIT);

    // 4d. Swapchain image: UNDEFINED → TRANSFER_DST  (ready to receive the blit)
    barrier(m_swapChainImages[imageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED,          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,                                  VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,  VK_PIPELINE_STAGE_TRANSFER_BIT);

    // 4e. Blit m_tvImage → swapchain image, scaled to fill the window
    //     Mirrors: SDL_RenderCopy(rend, texture, nullptr, nullptr)
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0]  = {0, 0, 0};
    blit.srcOffsets[1]  = {(int32_t)WIDTH, (int32_t)HEIGHT, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0]  = {0, 0, 0};
    blit.dstOffsets[1]  = {(int32_t)m_swapChainExtent.width,
                           (int32_t)m_swapChainExtent.height, 1};
    vkCmdBlitImage(cmd,
        m_tvImage,                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_LINEAR);

    // 4f. Swapchain image: TRANSFER_DST → PRESENT_SRC  (ready to present)
    //     Mirrors: handing the image back to the OS inside SDL_RenderPresent
    barrier(m_swapChainImages[imageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT,          0,
        VK_PIPELINE_STAGE_TRANSFER_BIT,        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("flip_tv: failed to record command buffer");

    // ── Step 5: Submit ────────────────────────────────────────────────────────
    // Wait at TRANSFER stage (not COLOR_ATTACHMENT_OUTPUT — we have no render pass)
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &m_imageAvailableSemaphores[m_currentFrame];
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &m_renderFinishedSemaphores[m_currentFrame];

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo,
                      m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("flip_tv: failed to submit command buffer");

    // ── Step 6: Present ───────────────────────────────────────────────────────
    // Mirrors: SDL_RenderPresent
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &m_renderFinishedSemaphores[m_currentFrame];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_swapChain;
    presentInfo.pImageIndices      = &imageIndex;

    result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR ||
        result == VK_SUBOPTIMAL_KHR        ||
        m_framebufferResized)
    {
        m_framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("flip_tv: failed to present");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

bool Renderer::poll_events()
{
    glfwPollEvents();
    if (glfwWindowShouldClose(m_window)) {
        m_open = false;
        return false;
    }
    return true;
}

std::uint32_t Renderer::get_buttons() {
    std::uint32_t btns = 0;
    if (glfwGetKey(m_window, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS)
        btns |= BTN_UP;
    if (glfwGetKey(m_window, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS)
        btns |= BTN_DOWN;
    if (glfwGetKey(m_window, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS)
        btns |= BTN_LEFT;
    if (glfwGetKey(m_window, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS)
        btns |= BTN_RIGHT;
    if (glfwGetKey(m_window, GLFW_KEY_ENTER) == GLFW_PRESS)
        btns |= BTN_A;
    if (glfwGetKey(m_window, GLFW_KEY_BACKSPACE) == GLFW_PRESS)
        btns |= BTN_B;
    if (glfwGetKey(m_window, GLFW_KEY_P) == GLFW_PRESS)
        btns |= BTN_PLUS;
    if (glfwGetKey(m_window, GLFW_KEY_M) == GLFW_PRESS)
        btns |= BTN_MINUS;
    return btns;
}


//     const std::uint8_t *keys = SDL_GetKeyboardState(nullptr);
//     std::uint32_t btns = 0;
//     if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])
//         btns |= BTN_UP;
//     if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])
//         btns |= BTN_DOWN;
//     if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])
//         btns |= BTN_LEFT;
//     if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D])
//         btns |= BTN_RIGHT;
//     if (keys[SDL_SCANCODE_RETURN])
//         btns |= BTN_A;
//     if (keys[SDL_SCANCODE_BACKSPACE])
//         btns |= BTN_B;
//     if (keys[SDL_SCANCODE_P])
//         btns |= BTN_PLUS;
//     if (keys[SDL_SCANCODE_M])
//         btns |= BTN_MINUS;