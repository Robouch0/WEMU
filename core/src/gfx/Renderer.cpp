//
// Created by nicolas on 5/21/26.
//

#include "Renderer.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "GpuQuadRasterizer.hpp"
#include "GpuRenderGraph.hpp"

// Wii U targets 60 fps. Cap flip_tv so the interpreter doesn't run faster than real hardware when the swapchain has spare images to return
// immediately.
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

Renderer::Renderer(VkInstance instance, VkSurfaceKHR surface, uint32_t w, uint32_t h) :
    m_instance(instance), m_surface(surface), m_surfaceWidth(w), m_surfaceHeight(h), m_embedded(true)
{
    initVulkanPipeline();
}

void Renderer::flip_tv(const std::uint8_t *rgbx, std::uint32_t w, std::uint32_t h)
{
    if (m_hostPresenter) {
        m_presentRate.record(Core::UI::FrameRate::Clock::now());
        if (!m_hostPresenter->present({rgbx, std::size_t(w) * h * 4}, w, h, hostStatistics()))
            m_open = false;
        pacePresentation();
        return;
    }
    presentFrame(rgbx, w, h, true);
}

void Renderer::presentFrame(const std::uint8_t *rgbx, std::uint32_t w, std::uint32_t h, bool guestFrame)
{
    // The slot's previous submission must release both its upload memory and
    // acquire semaphore before either is reused by the host.
    if (vkWaitForFences(m_logicalDevice, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        throw std::runtime_error("flip_tv: failed to wait for frame fence");
    const std::size_t cols = (w < WIDTH) ? w : WIDTH;
    const std::size_t rows = (h < HEIGHT) ? h : HEIGHT;

    auto *dst = static_cast<std::uint32_t *>(m_tvStagingMapped[m_currentFrame]);
    const auto *src = reinterpret_cast<const std::uint32_t *>(rgbx);
    if (cols != WIDTH || rows != HEIGHT)
        std::memset(dst, 0, WIDTH * HEIGHT * 4);

    for (std::uint32_t y = 0; y < rows; ++y) {
        const std::uint32_t *srow = src + static_cast<std::size_t>(y) * w;
        std::uint32_t *drow = dst + static_cast<std::size_t>(y) * WIDTH;
        std::memcpy(drow, srow, cols * 4);
    }
    const auto now = Core::UI::FrameRate::Clock::now();
    const auto pixels = std::span{static_cast<std::uint8_t *>(m_tvStagingMapped[m_currentFrame]), WIDTH * HEIGHT * 4};
    if (guestFrame) {
        m_presentRate.record(now);
        if (m_pauseMenu.showFps())
            Core::UI::PauseMenu::paintFps(pixels, WIDTH, HEIGHT, m_presentRate.value(now), m_movieRate.value(now));
    } else
        m_pauseMenu.paint(pixels, WIDTH, HEIGHT);

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
        throw std::runtime_error("flip_tv: failed to acquire swap chain image (VkResult " + std::to_string(result) + ")");
    }
    // end of image retrieval

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    // starts recording the command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("flip_tv: failed to begin command buffer");

    pipelineBarrier(m_tvImage, m_tvImageInitialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_tvImageInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    m_tvImageInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, cmd);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = WIDTH;
    region.bufferImageHeight = HEIGHT;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {WIDTH, HEIGHT, 1};
    vkCmdCopyBufferToImage(cmd, m_tvStagingBuffers[m_currentFrame], m_tvImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    pipelineBarrier(m_tvImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, cmd);

    pipelineBarrier(m_swapChainImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, cmd);


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
    submitInfo.pSignalSemaphores = &m_renderFinishedSemaphores[imageIndex];

    vkResetFences(m_logicalDevice, 1, &m_inFlightFences[m_currentFrame]);
    result = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (result != VK_SUCCESS)
        throw std::runtime_error("flip_tv: failed to submit command buffer (VkResult " + std::to_string(result) + ")");
    m_tvImageInitialized = true;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("flip_tv: failed to present (VkResult " + std::to_string(result) + ")");
    }

    if (guestFrame) {
        m_lastGameSlot = m_currentFrame;
        m_haveGameFrame = true;
    }
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    // Software frame cap / sleep the remaining time in this 1/60 s budget so
    // the game runs at Wii U native speed even when spare swapchain images
    // would otherwise let vkAcquireNextImageKHR return without blocking.
    pacePresentation();

    if (guestFrame && m_onFirstFrame) {
        auto cb = std::move(m_onFirstFrame);
        cb();
    }
}

void Renderer::pacePresentation()
{
    using Clock = std::chrono::steady_clock;
    const auto elapsed = Clock::now() - m_lastFlip;
    if (m_pacingReady && elapsed < kTargetFrameTime)
        std::this_thread::sleep_for(kTargetFrameTime - elapsed);
    m_lastFlip = Clock::now();
    m_pacingReady = true;
}

Core::Gfx::HostPresenter::Statistics Renderer::hostStatistics() const
{
    const auto now = Core::UI::FrameRate::Clock::now();
    return {m_presentRate.value(now), m_movieRate.value(now), m_presentRate.total(), m_movieRate.total()};
}

void Renderer::processHostCommands(std::chrono::milliseconds wait)
{
    using Action = Core::Gfx::HostPresenter::Action;
    for (const auto &command: m_hostPresenter->poll(wait)) {
        switch (command.action) {
            case Action::Pause:
                m_pauseMenu.requestPause();
                m_hostButtons = 0;
                break;
            case Action::Resume:
                m_pauseMenu.resume();
                break;
            case Action::Stop:
                m_pauseMenu.stop();
                break;
            case Action::Buttons:
                m_hostButtons = command.value;
                break;
        }
    }
    if (!m_hostPresenter->connected() || m_pauseMenu.state() == Core::UI::PauseMenu::State::Stopping)
        m_open = false;
}

void Renderer::gpuBegin()
{
    if (!m_gpuQuad)
        m_gpuQuad = new Core::Gfx::GpuQuadRasterizer(m_physicalDevice, m_logicalDevice, m_graphicsQueue, m_commandPool, WIDTH, HEIGHT);
    m_gpuQuad->begin();
}

void Renderer::gpuDrawTriangles(std::uint64_t key, const std::uint8_t *rgba, std::uint32_t tw, std::uint32_t th, const float *xyuv,
                                std::uint32_t count)
{
    if (m_gpuQuad)
        m_gpuQuad->drawTriangles(key, rgba, tw, th, reinterpret_cast<const Core::Gfx::GpuQuadRasterizer::Vertex *>(xyuv), count);
}

void Renderer::gpuEnd(std::uint8_t *outRgbx)
{
    if (m_gpuQuad)
        m_gpuQuad->end(outRgbx);
}

void Renderer::gpuBeginFrame()
{
    if (!m_gpuGraph)
        m_gpuGraph = new Core::Gfx::GpuRenderGraph(m_physicalDevice, m_logicalDevice, m_graphicsQueue, m_commandPool, WIDTH, HEIGHT);
    m_gpuGraph->beginFrame();
}

void Renderer::gpuBindTarget(std::uint32_t addr, std::uint32_t w, std::uint32_t h)
{
    if (m_gpuGraph)
        m_gpuGraph->bindTarget(addr, w, h);
}

void Renderer::gpuClearTarget(std::uint32_t addr, std::uint32_t w, std::uint32_t h, const float rgba[4])
{
    if (m_gpuGraph)
        m_gpuGraph->clearTarget(addr, w, h, rgba);
}

bool Renderer::gpuIsTarget(std::uint32_t addr) const { return m_gpuGraph && m_gpuGraph->isTarget(addr); }

void Renderer::gpuDrawTexture(std::uint64_t key, const std::uint8_t *rgba, std::uint32_t tw, std::uint32_t th, const float *xyuv, std::uint32_t count)
{
    if (m_gpuGraph)
        m_gpuGraph->draw(static_cast<std::uint32_t>(key), rgba, tw, th, reinterpret_cast<const Core::Gfx::GpuRenderGraph::Vertex *>(xyuv), count);
}

void Renderer::gpuDrawTarget(std::uint32_t srcAddr, const float *xyuv, std::uint32_t count)
{
    if (m_gpuGraph)
        m_gpuGraph->draw(srcAddr, nullptr, 0, 0, reinterpret_cast<const Core::Gfx::GpuRenderGraph::Vertex *>(xyuv), count);
}

void Renderer::gpuPresentTarget(std::uint32_t scanAddr, std::uint8_t *outRgbx)
{
    if (m_gpuGraph)
        m_gpuGraph->present(scanAddr, outRgbx);
}

bool Renderer::poll_events()
{
    if (m_hostPresenter) {
        processHostCommands();
        return m_open;
    }
    if (m_embedded)
        return true;
    SDL_Event event;
    while (SDL_PollEvent(&event))
        handleHostEvent(event);
    return m_open;
}

bool Renderer::handleHostEvent(const SDL_Event &event)
{
    int width{}, height{};
    SDL_GetWindowSize(m_window, &width, &height);
    const bool changed = m_pauseMenu.handleEvent(event, width, height);
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        m_framebufferResized = true;
    if (m_pauseMenu.state() == Core::UI::PauseMenu::State::Stopping)
        m_open = false;
    return changed;
}

void Renderer::service_pause(const std::function<void()> &onPaused)
{
    if (m_hostPresenter && pause_requested()) {
        m_pauseMenu.acknowledgePause({});
        m_hostPresenter->paused(true, hostStatistics());
        if (onPaused)
            onPaused();
        while (m_open && m_pauseMenu.state() == Core::UI::PauseMenu::State::Paused)
            processHostCommands(std::chrono::milliseconds(50));
        m_hostButtons = 0;
        m_presentRate.reset();
        m_movieRate.reset();
        m_pacingReady = false;
        if (m_open)
            m_hostPresenter->paused(false, hostStatistics());
        return;
    }
    if (m_embedded || !pause_requested())
        return;
    if (vkWaitForFences(m_logicalDevice, std::uint32_t(m_inFlightFences.size()), m_inFlightFences.data(), VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        throw std::runtime_error("pause: failed to wait for presentation uploads");
    m_pauseBackground.resize(WIDTH * HEIGHT * 4);
    if (m_haveGameFrame)
        std::memcpy(m_pauseBackground.data(), m_tvStagingMapped[m_lastGameSlot], m_pauseBackground.size());
    else
        std::fill(m_pauseBackground.begin(), m_pauseBackground.end(), 20);
    int count{};
    const auto *held = SDL_GetKeyboardState(&count);
    m_pauseMenu.acknowledgePause({held, std::size_t(count)});
    if (onPaused)
        onPaused();
    bool dirty = true;
    while (m_open && m_pauseMenu.state() == Core::UI::PauseMenu::State::Paused) {
        if (dirty && !(SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED)) {
            presentFrame(m_pauseBackground.data(), WIDTH, HEIGHT, false);
            dirty = false;
        }
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 50)) {
            dirty |= handleHostEvent(event);
            while (SDL_PollEvent(&event))
                dirty |= handleHostEvent(event);
        }
    }
    // Sampling/pacing use host time; emulated clocks and media timestamps stay untouched.
    m_presentRate.reset();
    m_movieRate.reset();
    m_pacingReady = false;
    m_pauseBackground.clear();
}

std::uint32_t Renderer::get_buttons() const
{
    if (m_hostPresenter)
        return m_pauseMenu.state() == Core::UI::PauseMenu::State::Running ? m_hostButtons : 0;
    if (m_embedded)
        return 0;
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const auto pressed = [&](SDL_Scancode key) { return keys[key] && !m_pauseMenu.suppress(key); };
    std::uint32_t btns = 0;
    if (pressed(SDL_SCANCODE_UP) || pressed(SDL_SCANCODE_W))
        btns |= BTN_UP;
    if (pressed(SDL_SCANCODE_DOWN) || pressed(SDL_SCANCODE_S))
        btns |= BTN_DOWN;
    if (pressed(SDL_SCANCODE_LEFT) || pressed(SDL_SCANCODE_A))
        btns |= BTN_LEFT;
    if (pressed(SDL_SCANCODE_RIGHT) || pressed(SDL_SCANCODE_D))
        btns |= BTN_RIGHT;
    if (pressed(SDL_SCANCODE_RETURN))
        btns |= BTN_A;
    if (pressed(SDL_SCANCODE_BACKSPACE))
        btns |= BTN_B;
    if (pressed(SDL_SCANCODE_P))
        btns |= BTN_PLUS;
    if (pressed(SDL_SCANCODE_M))
        btns |= BTN_MINUS;
    return btns;
}
