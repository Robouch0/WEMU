#include "gfx/VulkanRasterBackend.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

#include "gfx/SpirvCompiler.hpp"
#include "utils/FrameProfiler.hpp"
#include "utils/Logger.hpp"

namespace Core::Gfx {
    namespace {
        void check(VkResult result, const char *operation)
        {
            if (result != VK_SUCCESS)
                throw std::runtime_error(std::string(operation) + ": Vulkan result " + std::to_string(result));
        }
        bool depthOnly(const RasterDraw &draw) { return draw.depth.test && !draw.channelMask; }
        unsigned rasterWidth(const RasterDraw &draw) { return depthOnly(draw) ? draw.depth.width : draw.width; }
        unsigned rasterHeight(const RasterDraw &draw) { return depthOnly(draw) ? draw.depth.height : draw.height; }
        VkExtent2D attachmentExtent(const RasterDraw &draw)
        {
            return {draw.depth.test ? std::min(rasterWidth(draw), draw.depth.width) : draw.width,
                    draw.depth.test ? std::min(rasterHeight(draw), draw.depth.height) : draw.height};
        }
        VkRect2D clippedScissor(const RasterDraw &draw)
        {
            const auto left = std::clamp<std::int64_t>(draw.scissor[0], 0, rasterWidth(draw));
            const auto top = std::clamp<std::int64_t>(draw.scissor[1], 0, rasterHeight(draw));
            const auto right = std::clamp<std::int64_t>(std::int64_t(draw.scissor[0]) + draw.scissor[2], left, rasterWidth(draw));
            const auto bottom = std::clamp<std::int64_t>(std::int64_t(draw.scissor[1]) + draw.scissor[3], top, rasterHeight(draw));
            return {{int(left), int(top)}, {unsigned(right - left), unsigned(bottom - top)}};
        }

        VkRect2D readbackRegion(const RasterDraw &draw)
        {
            auto region = clippedScissor(draw);
            if (!draw.vertices.empty() && !draw.vertexStage.shader) {
                double minX = draw.vertices[0].x, maxX = minX;
                double minY = draw.vertices[0].y, maxY = minY;
                for (const auto &vertex: draw.vertices) {
                    minX = std::min(minX, double(vertex.x));
                    maxX = std::max(maxX, double(vertex.x));
                    minY = std::min(minY, double(vertex.y));
                    maxY = std::max(maxY, double(vertex.y));
                }
                // Pad one pixel for subpixel quantization. Clamp in floating point
                // before converting, so even very large offscreen vertices are safe.
                const double rightLimit = region.offset.x + region.extent.width;
                const double bottomLimit = region.offset.y + region.extent.height;
                const auto left = int(std::clamp(std::floor(minX) - 1, double(region.offset.x), rightLimit));
                const auto top = int(std::clamp(std::floor(minY) - 1, double(region.offset.y), bottomLimit));
                const auto right = int(std::clamp(std::ceil(maxX) + 1, double(left), rightLimit));
                const auto bottom = int(std::clamp(std::ceil(maxY) + 1, double(top), bottomLimit));
                region = {{left, top}, {unsigned(right - left), unsigned(bottom - top)}};
            }
            if (std::uint64_t(region.extent.width) * region.extent.height * 2 < std::uint64_t(draw.width) * draw.height)
                return region;
            return {{0, 0}, {draw.width, draw.height}};
        }
        bool overwritesTarget(const RasterDraw &draw)
        {
            static const bool enabled = [] {
                const auto *value = std::getenv("WEMU_NATIVE_TARGET_OVERWRITE");
                return !value || value[0] != '0';
            }();
            if (!enabled || !draw.shader.alwaysWritesColor || draw.channelMask != 15 || draw.blend.enabled || draw.depth.test ||
                draw.vertexStage.shader || draw.topology != RasterDraw::Topology::Triangles || draw.vertices.size() != 6 || !draw.indices.empty())
                return false;
            const auto scissor = clippedScissor(draw);
            if (scissor.offset.x || scissor.offset.y || scissor.extent.width != draw.width || scissor.extent.height != draw.height)
                return false;
            // A culled face cannot prove an overwrite, even when its rectangle
            // corners cover the target. Preserve previous pixels in that case.
            for (unsigned first: {0u, 3u}) {
                const auto &a = draw.vertices[first], &b = draw.vertices[first + 1], &c = draw.vertices[first + 2];
                const double area = (double(b.x) - a.x) * (double(c.y) - a.y) - (double(c.x) - a.x) * (double(b.y) - a.y);
                const bool front = (area > 0) == draw.frontFaceClockwise;
                if (front ? draw.cullFront : draw.cullBack)
                    return false;
            }
            unsigned masks[2]{};
            for (unsigned i = 0; i < 6; ++i) {
                const auto &v = draw.vertices[i];
                if (v.reciprocalW != 1.f || (v.x != 0.f && v.x != float(draw.width)) || (v.y != 0.f && v.y != float(draw.height)))
                    return false;
                const auto corner = v.x == 0.f ? (v.y == 0.f ? 0u : 3u) : (v.y == 0.f ? 1u : 2u);
                masks[i / 3] |= 1u << corner;
            }
            // Two nondegenerate triangles sharing a rectangle diagonal cover
            // every pixel center. Bounding boxes alone do not prove coverage.
            const auto diagonal = masks[0] & masks[1];
            return std::popcount(masks[0]) == 3 && std::popcount(masks[1]) == 3 && (masks[0] | masks[1]) == 15 && (diagonal == 5 || diagonal == 10);
        }
        std::optional<VkBlendFactor> blendFactor(unsigned mode)
        {
            switch (mode) {
                case 0:
                    return VK_BLEND_FACTOR_ZERO;
                case 1:
                    return VK_BLEND_FACTOR_ONE;
                case 2:
                    return VK_BLEND_FACTOR_SRC_COLOR;
                case 3:
                    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
                case 4:
                case 11:
                    return VK_BLEND_FACTOR_SRC_ALPHA;
                case 5:
                case 12:
                    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                case 6:
                    return VK_BLEND_FACTOR_DST_ALPHA;
                case 7:
                    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
                case 8:
                    return VK_BLEND_FACTOR_DST_COLOR;
                case 9:
                    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
                case 13:
                    return VK_BLEND_FACTOR_CONSTANT_COLOR;
                case 14:
                    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
                case 19:
                    return VK_BLEND_FACTOR_CONSTANT_ALPHA;
                case 20:
                    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
                default:
                    return std::nullopt;
            }
        }
        VkBlendOp blendOperation(unsigned mode)
        {
            constexpr VkBlendOp operations[]{VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX, VK_BLEND_OP_REVERSE_SUBTRACT};
            return operations[mode];
        }
        std::array<unsigned, 11> blendKey(const RasterDraw::Blend &blend)
        {
            return {unsigned(blend.enabled), blend.colorSource,      blend.colorDestination, blend.colorOperation,
                    blend.alphaSource,       blend.alphaDestination, blend.alphaOperation,   blend.constant[0],
                    blend.constant[1],       blend.constant[2],      blend.constant[3]};
        }
        VkFormat colorVkFormat(ColorFormat format)
        {
            return format == ColorFormat::RGBA32Float      ? VK_FORMAT_R32G32B32A32_SFLOAT
                   : format == ColorFormat::R11G11B10Float ? VK_FORMAT_B10G11R11_UFLOAT_PACK32
                                                           : VK_FORMAT_R8G8B8A8_UNORM;
        }
        unsigned imageElementBytes(VkFormat format) { return format == VK_FORMAT_R32G32B32A32_SFLOAT ? 16u : 4u; }
        VkFormat blockVkFormat(TextureBlockFormat format)
        {
            switch (format) {
                case TextureBlockFormat::BC1:
                    return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                case TextureBlockFormat::BC4:
                    return VK_FORMAT_BC4_UNORM_BLOCK;
                case TextureBlockFormat::BC4Signed:
                    return VK_FORMAT_BC4_SNORM_BLOCK;
                case TextureBlockFormat::BC5Signed:
                    return VK_FORMAT_BC5_SNORM_BLOCK;
                default:
                    return VK_FORMAT_UNDEFINED;
            }
        }
        unsigned blockElementBytes(VkFormat format)
        {
            if (format == VK_FORMAT_BC5_SNORM_BLOCK)
                return 16;
            return format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK || format == VK_FORMAT_BC4_UNORM_BLOCK || format == VK_FORMAT_BC4_SNORM_BLOCK ? 8 : 0;
        }
        VkFormat depthVkFormat(DepthFormat)
        {
            // Canonical floats avoid device-dependent fixed-point comparison precision.
            // UNORM writes are quantized in a GPU buffer pass after comparison.
            return VK_FORMAT_D32_SFLOAT;
        }
        std::array<unsigned, 4> depthKey(const RasterDraw::Depth &depth)
        {
            return {depth.test ? unsigned(depth.format) + 1 : 0, unsigned(depth.test), unsigned(depth.write), depth.compare};
        }
        struct TextureMemoryUsage {
                std::atomic<std::uint64_t> bytes{};
        };
        struct CachedTexture {
                VkDevice device{};
                VkImage image{};
                VkImageView view{};
                VkBuffer staging{};
                VkDeviceMemory imageMemory{}, stagingMemory{};
                std::shared_ptr<const void> owner;
                std::vector<std::uint64_t> key;
                std::weak_ptr<const void> producer;
                bool ready{};
                std::shared_ptr<TextureMemoryUsage> usage;
                std::uint64_t bytes{};
                ~CachedTexture()
                {
                    if (view)
                        vkDestroyImageView(device, view, nullptr);
                    if (image)
                        vkDestroyImage(device, image, nullptr);
                    if (staging)
                        vkDestroyBuffer(device, staging, nullptr);
                    if (imageMemory)
                        vkFreeMemory(device, imageMemory, nullptr);
                    if (stagingMemory)
                        vkFreeMemory(device, stagingMemory, nullptr);
                    if (usage)
                        usage->bytes.fetch_sub(bytes, std::memory_order_relaxed);
                }
        };
        struct Context {
                struct BatchFence {
                        VkDevice device{};
                        VkFence fence{};
                        bool submitted{}, completed{};
                        ~BatchFence()
                        {
                            if (fence)
                                vkDestroyFence(device, fence, nullptr);
                        }
                };
                // Command buffers per vkQueueSubmit. Waiting on a batch flushes it, so batching
                // only delays GPU start; 8 balances submit overhead against that delay in MK8's
                // menu. WEMU_NATIVE_BATCH_SUBMITS=N overrides it, 0 or 1 submits every draw.
                std::size_t batchLimit = [] {
                    const auto *value = std::getenv("WEMU_NATIVE_BATCH_SUBMITS");
                    return value ? std::size_t(std::strtoul(value, nullptr, 10)) : std::size_t{8};
                }();
                std::vector<VkCommandBuffer> pendingCommands;
                std::shared_ptr<BatchFence> pendingBatch;
                std::vector<std::shared_ptr<BatchFence>> batches;
                std::uint64_t queueSubmissions{}, submittedCommands{}, submissionWaits{};
                std::shared_ptr<BatchFence> enqueue(VkCommandBuffer command)
                {
                    if (!pendingBatch) {
                        for (const auto &batch: batches) {
                            if (batch.use_count() != 1 || !batch->completed)
                                continue;
                            pendingBatch = batch;
                            check(vkResetFences(device, 1, &batch->fence), "reset batch fence");
                            batch->submitted = batch->completed = false;
                            break;
                        }
                        if (!pendingBatch) {
                            pendingBatch = std::make_shared<BatchFence>();
                            pendingBatch->device = device;
                            VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                            check(vkCreateFence(device, &info, nullptr, &pendingBatch->fence), "create batch fence");
                            batches.push_back(pendingBatch);
                        }
                    }
                    pendingCommands.push_back(command);
                    auto result = pendingBatch;
                    if (pendingCommands.size() >= batchLimit)
                        flush();
                    return result;
                }
                void flush()
                {
                    if (pendingCommands.empty())
                        return;
                    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                    submit.commandBufferCount = static_cast<std::uint32_t>(pendingCommands.size());
                    submit.pCommandBuffers = pendingCommands.data();
                    check(vkQueueSubmit(queue, 1, &submit, pendingBatch->fence), "submit draw batch");
                    ++queueSubmissions;
                    submittedCommands += pendingCommands.size();
                    pendingBatch->submitted = true;
                    pendingCommands.clear();
                    pendingBatch.reset();
                }
                void wait(const std::shared_ptr<BatchFence> &batch, bool profile = true)
                {
                    if (!batch || batch->completed)
                        return;
                    Profile::Scope waiting(Profile::Stage::GpuWait, profile);
                    if (!batch->submitted)
                        flush();
                    check(vkWaitForFences(device, 1, &batch->fence, VK_TRUE, UINT64_MAX), "wait draw batch");
                    ++submissionWaits;
                    batch->completed = true;
                }
                struct PipelineKey {
                        // SpirvModule ids: equal sources share one cached module, so comparing ids
                        // replaces copying and comparing whole SPIR-V word vectors on every draw.
                        std::uint64_t vertex{}, fragment{};
                        std::vector<std::uint32_t> bindings;
                        unsigned channelMask{};
                        std::array<unsigned, 11> blend{};
                        std::array<unsigned, 4> depth{};
                        unsigned topology{}, colorFormat{}, cull{};
                        bool depthBias{};
                        auto operator<=>(const PipelineKey &) const = default;
                };
                struct PipelineBundle {
                        VkDescriptorSetLayout setLayout{};
                        VkPipelineLayout layout{};
                        VkPipeline pipeline{};
                };
                VkInstance instance{};
                VkPhysicalDevice physical{};
                VkDevice device{};
                VkQueue queue{};
                VkPipelineCache pipelineCache{};
                std::uint32_t family{};
                std::string deviceName;
                bool discrete{}; // separate VRAM: host-visible memory sits across PCIe
                std::uint64_t readbackTransfers{}, residentTextureCopies{}, directTextureBindings{}, directTextureBytes{};
                std::uint64_t targetUploadTransfers{}, targetCopyTransfers{}, fullOverwriteDraws{};
                std::uint64_t pipelineCreations{};
                VkDescriptorSetLayout depthQuantizeSetLayout{};
                VkPipelineLayout depthQuantizeLayout{};
                VkPipeline depthQuantizePipeline{};
                bool computeQueue{};
                bool cubeArrays{}, biasClamp{}, blockTextures{};
                unsigned timestampBits{};
                float timestampPeriod{};
                std::map<PipelineKey, PipelineBundle> pipelines;
                struct TextureEntry {
                        std::shared_ptr<CachedTexture> value;
                        std::uint64_t bytes{}, used{};
                };
                std::map<std::vector<std::uint64_t>, TextureEntry> textures;
                std::map<std::vector<std::uint64_t>, std::weak_ptr<CachedTexture>> retainedTextures;
                std::uint64_t textureBytes{}, textureClock{}, textureUploads{}, textureHits{};
                std::shared_ptr<TextureMemoryUsage> textureMemory = std::make_shared<TextureMemoryUsage>();
                // Shared texture LRU. A race frame samples hundreds of distinct textures; evicting them
                // every frame re-uploads each one. In-flight users retain their own references.
                static constexpr std::uint64_t textureBudget = 1536ull * 1024 * 1024;
                static constexpr std::size_t textureEntryLimit = 2048;
                void evictTextures(std::uint64_t incoming)
                {
                    while (!textures.empty() && (textureBytes + incoming > textureBudget || textures.size() >= textureEntryLimit)) {
                        const auto oldest =
                                std::min_element(textures.begin(), textures.end(), [](const auto &a, const auto &b) { return a.second.used < b.second.used; });
                        textureBytes -= oldest->second.bytes;
                        textures.erase(oldest);
                    }
                }
                void admitTexture(std::vector<std::uint64_t> key, const std::shared_ptr<CachedTexture> &texture)
                {
                    if (retainedTextures.size() >= 4096) {
                        std::erase_if(retainedTextures, [](const auto &entry) { return entry.second.expired(); });
                        if (retainedTextures.size() >= 4096)
                            retainedTextures.clear();
                    }
                    retainedTextures[key] = texture;
                    textures.emplace(std::move(key), TextureEntry{texture, texture->bytes, ++textureClock});
                    textureBytes += texture->bytes;
                    ++textureUploads;
                }
                void drainNoThrow() noexcept
                {
                    try {
                        flush();
                    } catch (...) {
                        // Failed submissions were never queued. Teardown still
                        // drains earlier work without propagating exceptions.
                        pendingCommands.clear();
                        pendingBatch.reset();
                    }
                    if (device)
                        vkDeviceWaitIdle(device);
                    for (const auto &batch: batches)
                        batch->completed = true;
                    pendingCommands.clear();
                    pendingBatch.reset();
                }
                // Pipelines depend on attachment formats only through render-pass compatibility, so any
                // pass with the draw's color/depth formats can create one. Modules are only needed
                // while compiling. Owners never destroy cached handles; clearPipelines() does.
                const PipelineBundle &pipelineFor(const PipelineKey &key, const SpirvModule &vertex, const SpirvModule &fragment, const RasterDraw &draw,
                                                  const std::vector<unsigned> &textureBindings, VkRenderPass pass)
                {
                    if (const auto it = pipelines.find(key); it != pipelines.end())
                        return it->second;
                    PipelineBundle created;
                    std::vector<VkDescriptorSetLayoutBinding> bindings{{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
                    if (draw.vertexStage.shader)
                        bindings.push_back({17, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
                    for (const auto binding: textureBindings)
                        bindings.push_back({binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                            binding > 17 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
                    VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                    si.bindingCount = bindings.size();
                    si.pBindings = bindings.data();
                    check(vkCreateDescriptorSetLayout(device, &si, nullptr, &created.setLayout), "create descriptor layout");
                    std::vector<VkShaderModule> modules;
                    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
                    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                    li.setLayoutCount = li.pushConstantRangeCount = 1;
                    li.pSetLayouts = &created.setLayout;
                    li.pPushConstantRanges = &push;
                    check(vkCreatePipelineLayout(device, &li, nullptr, &created.layout), "create pipeline layout");
                    VkPipelineShaderStageCreateInfo stages[2]{};
                    for (unsigned i = 0; i < 2; ++i) {
                        const auto &code = (i ? fragment : vertex).words;
                        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                        mi.codeSize = code.size() * sizeof(std::uint32_t);
                        mi.pCode = code.data();
                        VkShaderModule module{};
                        check(vkCreateShaderModule(device, &mi, nullptr, &module), "create shader");
                        modules.push_back(module);
                        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
                        stages[i].module = module;
                        stages[i].pName = "main";
                    }
                    VkVertexInputBindingDescription vertexBinding{0, sizeof(RasterDraw::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
                    VkVertexInputAttributeDescription attributes[18]{};
                    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
                    for (unsigned i = 0; i < 16; ++i)
                        attributes[i + 1] = {i + 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                                             unsigned(offsetof(RasterDraw::Vertex, inputs) + i * sizeof(float) * 4)};
                    attributes[17] = {17, 0, VK_FORMAT_R32G32_SFLOAT, unsigned(offsetof(RasterDraw::Vertex, z))};
                    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
                    input.vertexBindingDescriptionCount = 1;
                    input.pVertexBindingDescriptions = &vertexBinding;
                    input.vertexAttributeDescriptionCount = 18;
                    input.pVertexAttributeDescriptions = attributes;
                    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
                    assembly.topology = draw.topology == RasterDraw::Topology::Points ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                                                                                      : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    VkViewport viewport{0, 0, float(rasterWidth(draw)), float(rasterHeight(draw)), 0, 1};
                    const auto scissor = clippedScissor(draw);
                    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
                    viewportState.viewportCount = viewportState.scissorCount = 1;
                    viewportState.pViewports = &viewport;
                    viewportState.pScissors = &scissor;
                    constexpr VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
                    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
                    dynamic.dynamicStateCount = std::size(dynamicStates);
                    dynamic.pDynamicStates = dynamicStates;
                    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
                    raster.polygonMode = VK_POLYGON_MODE_FILL;
                    raster.cullMode = (draw.cullFront ? VK_CULL_MODE_FRONT_BIT : 0) | (draw.cullBack ? VK_CULL_MODE_BACK_BIT : 0);
                    raster.frontFace = draw.frontFaceClockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
                    raster.lineWidth = 1;
                    raster.depthBiasEnable = draw.depthBias.enabled && draw.depth.format == DepthFormat::Float32;
                    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
                    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                    VkPipelineColorBlendAttachmentState blend{};
                    blend.colorWriteMask = draw.channelMask;
                    if (draw.blend.enabled) {
                        blend.blendEnable = VK_TRUE;
                        const auto colorSource = blendFactor(draw.blend.colorSource);
                        const auto colorDestination = blendFactor(draw.blend.colorDestination);
                        const auto alphaSource = blendFactor(draw.blend.alphaSource);
                        const auto alphaDestination = blendFactor(draw.blend.alphaDestination);
                        if (!colorSource || !colorDestination || !alphaSource || !alphaDestination)
                            throw std::runtime_error("Unsupported blend factors");
                        blend.srcColorBlendFactor = *colorSource;
                        blend.dstColorBlendFactor = *colorDestination;
                        blend.colorBlendOp = blendOperation(draw.blend.colorOperation);
                        blend.srcAlphaBlendFactor = *alphaSource;
                        blend.dstAlphaBlendFactor = *alphaDestination;
                        blend.alphaBlendOp = blendOperation(draw.blend.alphaOperation);
                    }
                    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
                    blending.attachmentCount = depthOnly(draw) ? 0 : 1;
                    blending.pAttachments = &blend;
                    for (unsigned i = 0; i < 4; ++i)
                        blending.blendConstants[i] = float(draw.blend.constant[i]) / 255.0f;
                    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                    gp.stageCount = 2;
                    gp.pStages = stages;
                    gp.pVertexInputState = &input;
                    gp.pInputAssemblyState = &assembly;
                    gp.pViewportState = &viewportState;
                    gp.pDynamicState = &dynamic;
                    gp.pRasterizationState = &raster;
                    gp.pMultisampleState = &samples;
                    gp.pColorBlendState = &blending;
                    VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
                    depthState.depthTestEnable = draw.depth.test;
                    depthState.depthWriteEnable = draw.depth.test && draw.depth.write;
                    depthState.depthCompareOp = VkCompareOp(draw.depth.compare);
                    gp.pDepthStencilState = &depthState;
                    gp.layout = created.layout;
                    gp.renderPass = pass;
                    check(vkCreateGraphicsPipelines(device, pipelineCache, 1, &gp, nullptr, &created.pipeline), "create pipeline");
                    for (auto module: modules)
                        vkDestroyShaderModule(device, module, nullptr);
                    ++pipelineCreations;
                    return pipelines.emplace(key, created).first->second;
                }
                void destroyPipelines() noexcept
                {
                    for (const auto &[key, bundle]: pipelines) {
                        vkDestroyPipeline(device, bundle.pipeline, nullptr);
                        vkDestroyPipelineLayout(device, bundle.layout, nullptr);
                        vkDestroyDescriptorSetLayout(device, bundle.setLayout, nullptr);
                    }
                    pipelines.clear();
                }
                void clearPipelines()
                {
                    flush();
                    if (device)
                        vkDeviceWaitIdle(device);
                    destroyPipelines();
                }
                ~Context()
                {
                    drainNoThrow();
                    batches.clear();
                    textures.clear();
                    if (device)
                        destroyPipelines();
                    if (depthQuantizePipeline)
                        vkDestroyPipeline(device, depthQuantizePipeline, nullptr);
                    if (depthQuantizeLayout)
                        vkDestroyPipelineLayout(device, depthQuantizeLayout, nullptr);
                    if (depthQuantizeSetLayout)
                        vkDestroyDescriptorSetLayout(device, depthQuantizeSetLayout, nullptr);
                    if (pipelineCache)
                        vkDestroyPipelineCache(device, pipelineCache, nullptr);
                    if (device)
                        vkDestroyDevice(device, nullptr);
                    if (instance)
                        vkDestroyInstance(instance, nullptr);
                }
                void initialize()
                {
                    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
                    check(vkCreateInstance(&ii, nullptr, &instance), "create instance");
                    std::uint32_t count = 0;
                    check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate devices");
                    std::vector<VkPhysicalDevice> devices(count);
                    check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate devices");
                    int bestScore = -1;
                    for (auto candidate: devices) {
                        VkFormatProperties format{};
                        vkGetPhysicalDeviceFormatProperties(candidate, VK_FORMAT_R32G32B32A32_SFLOAT, &format);
                        if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
                            continue;
                        std::uint32_t n = 0;
                        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
                        std::vector<VkQueueFamilyProperties> queues(n);
                        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, queues.data());
                        VkPhysicalDeviceProperties candidateProps{};
                        vkGetPhysicalDeviceProperties(candidate, &candidateProps);
                        if (candidateProps.limits.maxVertexInputAttributes < 18 || candidateProps.limits.maxVertexOutputComponents < 72 ||
                            candidateProps.limits.maxFragmentInputComponents < 72 || candidateProps.limits.maxPushConstantsSize < 128)
                            continue;
                        const int score = candidateProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                                          : candidateProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                          : candidateProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 0
                                                                                                                : 1;
                        for (unsigned i = 0; i < n; ++i) {
                            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                                if (score > bestScore) {
                                    physical = candidate;
                                    family = i;
                                    timestampBits = queues[i].timestampValidBits;
                                    computeQueue = (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
                                    bestScore = score;
                                }
                                break;
                            }
                        }
                    }
                    if (!physical)
                        throw std::runtime_error("No Vulkan graphics device with float color target support");
                    VkPhysicalDeviceProperties props{};
                    vkGetPhysicalDeviceProperties(physical, &props);
                    deviceName = props.deviceName;
                    discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
                    timestampPeriod = props.limits.timestampPeriod;
                    Utils::Log::error("[NATIVE_VULKAN] device={}", deviceName);
                    const float priority = 1.0f;
                    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                    qi.queueFamilyIndex = family;
                    qi.queueCount = 1;
                    qi.pQueuePriorities = &priority;
                    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
                    di.queueCreateInfoCount = 1;
                    di.pQueueCreateInfos = &qi;
                    VkPhysicalDeviceFeatures features{};
                    vkGetPhysicalDeviceFeatures(physical, &features);
                    cubeArrays = features.imageCubeArray;
                    biasClamp = features.depthBiasClamp;
                    blockTextures = features.textureCompressionBC;
                    VkPhysicalDeviceFeatures enabled{};
                    enabled.imageCubeArray = features.imageCubeArray;
                    enabled.depthBiasClamp = features.depthBiasClamp;
                    enabled.textureCompressionBC = features.textureCompressionBC;
                    di.pEnabledFeatures = &enabled;
                    check(vkCreateDevice(physical, &di, nullptr, &device), "create device");
                    vkGetDeviceQueue(device, family, 0, &queue);

                    VkPipelineCacheCreateInfo cache{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
                    check(vkCreatePipelineCache(device, &cache, nullptr, &pipelineCache), "create pipeline cache");
                }
                void initializeDepthQuantizer()
                {
                    if (depthQuantizePipeline)
                        return;
                    SpirvCompiler compiler;
                    const auto module = compiler.compile(ShaderStage::Compute, R"(#version 450
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0,std430) buffer Depth { float values[]; } depth;
layout(push_constant) uniform Dimensions { uint width; uint height; float maximum; } dimensions;
float normalizedInteger(uint value,uint maximum) {
    if(value==0u) return 0.0;
    if(value==maximum) return 1.0;
    // For maximum=2^n-1, normalize value to a 24-bit significand t.
    // value/maximum has significand t+t/maximum: the correction can be
    // rounded with integer arithmetic instead of 23 serial division steps.
    // The 24-bit correction is always one; the 16-bit divisor is constant.
    uint leading=uint(findMSB(value));
    uint t=value<<(23u-leading);
    uint significand=t+(maximum==65535u ? (t+32767u)/65535u : 1u);
    uint exponent=leading+(maximum==65535u ? 111u : 103u);
    return uintBitsToFloat((exponent<<23)|(significand&0x7fffffu));
}
void main() {
    uvec2 p=gl_GlobalInvocationID.xy;
    if(p.x>=dimensions.width||p.y>=dimensions.height) return;
    uint i=p.y*dimensions.width+p.x;
    float value=clamp(depth.values[i],0.0,1.0);
    // Power-of-two multiplication is exact. Separate the fractional correction
    // so adding 0.5 cannot lose a bit at 24-bit integer magnitudes.
    float scaled=value*(dimensions.maximum+1.0);
    int integer=int(scaled)+int(floor(fract(scaled)-value+0.5));
    depth.values[i]=normalizedInteger(uint(integer),uint(dimensions.maximum));
}
)");
                    if (!*module)
                        throw std::runtime_error(module->error);
                    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
                    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                    setInfo.bindingCount = 1;
                    setInfo.pBindings = &binding;
                    check(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &depthQuantizeSetLayout), "create depth quantizer descriptors");
                    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 12};
                    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                    layoutInfo.setLayoutCount = 1;
                    layoutInfo.pSetLayouts = &depthQuantizeSetLayout;
                    layoutInfo.pushConstantRangeCount = 1;
                    layoutInfo.pPushConstantRanges = &range;
                    check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &depthQuantizeLayout), "create depth quantizer layout");
                    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                    shaderInfo.codeSize = module->words.size() * sizeof(std::uint32_t);
                    shaderInfo.pCode = module->words.data();
                    VkShaderModule shader{};
                    check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "create depth quantizer shader");
                    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                    pipelineInfo.layout = depthQuantizeLayout;
                    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                    pipelineInfo.stage.module = shader;
                    pipelineInfo.stage.pName = "main";
                    const auto created = vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &depthQuantizePipeline);
                    vkDestroyShaderModule(device, shader, nullptr);
                    check(created, "create depth quantizer pipeline");
                }
        };

        struct ColorImage {
                std::shared_ptr<Context> context;
                VkImage image{};
                VkDeviceMemory memory{};
                VkImageView attachmentView{}; // color attachment view shared by every bundle rendering into this image
                bool initialized{}; // rendered at least once: TRANSFER_SRC_OPTIMAL between draws, else UNDEFINED
                bool direct{};      // created by DirectRenderer and not adopted by a bundle since: rendered in place
                unsigned width{}, height{};
                std::uint64_t version{};
                VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
                DepthFormat depthFormat{DepthFormat::Float32};
                std::map<std::array<unsigned, 4>, VkImageView> samplingViews;
                VkImageView samplingView(VkComponentMapping components)
                {
                    const std::array<unsigned, 4> key{unsigned(components.r), unsigned(components.g), unsigned(components.b), unsigned(components.a)};
                    if (const auto found = samplingViews.find(key); found != samplingViews.end())
                        return found->second;
                    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    info.image = image;
                    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    info.format = format;
                    info.components = components;
                    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    VkImageView view{};
                    check(vkCreateImageView(context->device, &info, nullptr, &view), "create resident sampling view");
                    samplingViews.emplace(key, view);
                    return view;
                }
                ~ColorImage()
                {
                    if (attachmentView)
                        vkDestroyImageView(context->device, attachmentView, nullptr);
                    for (const auto &[key, view]: samplingViews)
                        vkDestroyImageView(context->device, view, nullptr);
                    if (image)
                        vkDestroyImage(context->device, image, nullptr);
                    if (memory)
                        vkFreeMemory(context->device, memory, nullptr);
                }
        };
        struct NativeImage final : RasterImage {
                std::shared_ptr<ColorImage> storage;
                std::uint64_t version;
                unsigned pitch;
                std::shared_ptr<const std::vector<std::uint8_t>> padding;
                NativeImage(std::shared_ptr<ColorImage> image, unsigned rowPitch, std::shared_ptr<const std::vector<std::uint8_t>> rowPadding) :
                    storage(std::move(image)), version(storage->version), pitch(rowPitch), padding(std::move(rowPadding))
                {
                }
        };

        class DrawResources : public std::enable_shared_from_this<DrawResources> {
            public:
                struct Texture {
                        std::uint32_t binding;
                        std::vector<float> rgba;
                        std::span<const std::byte> snapshot;
                        std::size_t sourcePitch{};
                        VkFormat format{VK_FORMAT_R32G32B32A32_SFLOAT};
                        VkComponentMapping components{};
                        unsigned width{}, height{};
                        unsigned layers{1}; // depth slices for volume textures
                        bool array{}, volume{};
                        bool cube{};
                        std::vector<std::span<const float>> mips;
                        VkSamplerAddressMode addressU{}, addressV{}, addressW{VK_SAMPLER_ADDRESS_MODE_REPEAT};
                        VkBorderColor borderColor{VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK};
                        VkFilter filter = VK_FILTER_NEAREST;
                        VkFilter minFilter = VK_FILTER_NEAREST;
                        VkSamplerMipmapMode mipFilter{VK_SAMPLER_MIPMAP_MODE_NEAREST};
                        float minLod{}, maxLod{};
                        std::shared_ptr<ColorImage> rendered;
                        std::shared_ptr<const void> snapshotOwner;

                        std::size_t rowBytes() const
                        {
                            if (const auto blockBytes = blockElementBytes(format))
                                return std::size_t((width + 3) / 4) * blockBytes;
                            const unsigned bytes = format == VK_FORMAT_R8_UNORM              ? 1
                                                   : format == VK_FORMAT_R8G8_UNORM          ? 2
                                                   : format == VK_FORMAT_R32G32B32A32_SFLOAT ? 16
                                                                                             : 4;
                            return std::size_t(width) * bytes;
                        }
                        unsigned rows() const { return blockElementBytes(format) ? (height + 3) / 4 : height; }
                        std::size_t bytes() const
                        {
                            auto size = rowBytes() * rows() * layers;
                            for (const auto mip: mips)
                                size += mip.size_bytes();
                            return size;
                        }
                        bool matches(const void *previous) const
                        {
                            const auto *source = snapshot.empty() ? reinterpret_cast<const std::byte *>(rgba.data()) : snapshot.data();
                            const auto pitch = snapshot.empty() ? rowBytes() : sourcePitch;
                            for (unsigned y = 0; y < rows() * layers; ++y)
                                if (std::memcmp(static_cast<const std::byte *>(previous) + y * rowBytes(), source + y * pitch, rowBytes()) != 0)
                                    return false;
                            auto offset = rowBytes() * rows() * layers;
                            for (const auto mip: mips) {
                                if (std::memcmp(static_cast<const std::byte *>(previous) + offset, mip.data(), mip.size_bytes()) != 0)
                                    return false;
                                offset += mip.size_bytes();
                            }
                            return true;
                        }
                        void copyTo(void *destination) const
                        {
                            if (snapshot.empty()) {
                                std::memcpy(destination, rgba.data(), rowBytes() * rows() * layers);
                            } else if (sourcePitch == rowBytes()) {
                                std::memcpy(destination, snapshot.data(), rowBytes() * rows() * layers);
                            } else {
                                for (unsigned y = 0; y < rows() * layers; ++y)
                                    std::memcpy(static_cast<std::byte *>(destination) + y * rowBytes(), snapshot.data() + y * sourcePitch,
                                                rowBytes());
                            }
                            auto offset = rowBytes() * rows() * layers;
                            for (const auto mip: mips) {
                                std::memcpy(static_cast<std::byte *>(destination) + offset, mip.data(), mip.size_bytes());
                                offset += mip.size_bytes();
                            }
                        }
                };
                explicit DrawResources(std::shared_ptr<Context> ctx) :
                    contextOwner(std::move(ctx)), context(*contextOwner), physical(context.physical), device(context.device), queue(context.queue),
                    pipelineCache(context.pipelineCache), family(context.family)
                {
                }
                DrawResources(const DrawResources &) = delete;
                DrawResources &operator=(const DrawResources &) = delete;
                ~DrawResources()
                {
                    try {
                        context.wait(batch, false); // Static teardown can follow profiler TLS destruction.
                    } catch (...) {
                        context.drainNoThrow();
                    }
                    batch.reset();
                    if (gpuQueries)
                        vkDestroyQueryPool(device, gpuQueries, nullptr);
                    if (pipeline && !cachedPipeline)
                        vkDestroyPipeline(device, pipeline, nullptr);
                    if (layout && !cachedPipeline)
                        vkDestroyPipelineLayout(device, layout, nullptr);
                    for (auto module: modules)
                        vkDestroyShaderModule(device, module, nullptr);
                    if (descriptorPool)
                        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
                    if (setLayout && !cachedPipeline)
                        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
                    if (framebuffer && !colorImage)
                        vkDestroyFramebuffer(device, framebuffer, nullptr);
                    for (const auto &entry: framebuffers)
                        vkDestroyFramebuffer(device, entry.second, nullptr);
                    if (pass)
                        vkDestroyRenderPass(device, pass, nullptr);
                    if (depthView)
                        vkDestroyImageView(device, depthView, nullptr);
                    if (depthQuantizePool)
                        vkDestroyDescriptorPool(device, depthQuantizePool, nullptr);
                    for (auto sampler: samplers)
                        vkDestroySampler(device, sampler, nullptr);
                    for (std::size_t i = 0; i < textureViews.size(); ++i)
                        if (!cachedTextures[i])
                            vkDestroyImageView(device, textureViews[i], nullptr);
                    for (std::size_t i = 0; i < textureImages.size(); ++i)
                        if (!cachedTextures[i])
                            vkDestroyImage(device, textureImages[i], nullptr);
                    for (auto buffer: buffers)
                        vkDestroyBuffer(device, buffer, nullptr);
                    for (auto memory: memories)
                        vkFreeMemory(device, memory, nullptr);
                    if (pool)
                        vkDestroyCommandPool(device, pool, nullptr);
                    if (readbackPool)
                        vkDestroyCommandPool(device, readbackPool, nullptr);
                }

                // Only the first constantFloats/vertexConstantFloats values are defined and read by
                // the bundle's shaders; the rest of each uniform buffer is never accessed.
                void initialize(const SpirvModule &vertex, const SpirvModule &fragment, const std::array<float, 3200> &constants,
                                const std::array<float, 3200> &vertexConstants, std::size_t constantFloats, std::size_t vertexConstantFloats,
                                const std::vector<Texture> &textures, const RasterDraw &draw, const std::shared_ptr<ColorImage> &targetSource,
                                const std::shared_ptr<ColorImage> &depthSource, bool overwriteTarget)
                {
                    fullOverwrite = overwriteTarget;
                    const auto *profile = std::getenv("WEMU_NATIVE_GPU_TIMING");
                    if (profile && profile[0] == '1' && context.timestampBits) {
                        VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                        query.queryType = VK_QUERY_TYPE_TIMESTAMP;
                        query.queryCount = 2;
                        check(vkCreateQueryPool(device, &query, nullptr, &gpuQueries), "create GPU timing queries");
                    }
                    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                    pi.queueFamilyIndex = family;
                    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                    check(vkCreateCommandPool(device, &pi, nullptr, &pool), "create command pool");
                    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                    ai.commandPool = pool;
                    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                    ai.commandBufferCount = 1;
                    check(vkAllocateCommandBuffers(device, &ai, &command), "allocate commands");
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    check(vkBeginCommandBuffer(command, &begin), "begin commands");

                    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                    ci.imageType = VK_IMAGE_TYPE_2D;
                    ci.mipLevels = ci.arrayLayers = 1;
                    ci.samples = VK_SAMPLE_COUNT_1_BIT;
                    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
                    VkMemoryRequirements req{};
                    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    if (!depthOnly(draw)) {
                        ci.imageType = VK_IMAGE_TYPE_2D;
                        ci.format = colorVkFormat(draw.colorFormat);
                        ci.extent = {draw.width, draw.height, 1};
                        ci.mipLevels = ci.arrayLayers = 1;
                        ci.samples = VK_SAMPLE_COUNT_1_BIT;
                        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
                        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT;
                        colorImage = std::make_shared<ColorImage>();
                        colorImage->context = contextOwner;
                        colorImage->width = draw.width;
                        colorImage->height = draw.height;
                        colorImage->format = ci.format;
                        check(vkCreateImage(device, &ci, nullptr, &colorImage->image), "create image");
                        image = colorImage->image;

                        vkGetImageMemoryRequirements(device, image, &req);
                        colorImage->memory = allocate(req, 0, 0, false);
                        check(vkBindImageMemory(device, image, colorImage->memory, 0), "bind image");

                        vi.image = image;
                        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
                        vi.format = ci.format;
                        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                        check(vkCreateImageView(device, &vi, nullptr, &view), "create view");
                        colorImage->attachmentView = view;
                    }
                    if (draw.depth.test) {
                        depthImage = std::make_shared<ColorImage>();
                        depthImage->context = contextOwner;
                        depthImage->width = draw.depth.width;
                        depthImage->height = draw.depth.height;
                        depthImage->format = depthVkFormat(draw.depth.format);
                        depthImage->depthFormat = draw.depth.format;
                        ci.format = depthImage->format;
                        ci.extent = {draw.depth.width, draw.depth.height, 1};
                        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                        check(vkCreateImage(device, &ci, nullptr, &depthImage->image), "create depth image");
                        vkGetImageMemoryRequirements(device, depthImage->image, &req);
                        depthImage->memory = allocate(req, 0, 0, false);
                        check(vkBindImageMemory(device, depthImage->image, depthImage->memory, 0), "bind depth image");
                        vi.image = depthImage->image;
                        vi.format = ci.format;
                        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                        check(vkCreateImageView(device, &vi, nullptr, &depthView), "create depth view");
                        const auto bytes = std::size_t(draw.depth.pitch) * draw.depth.height * sizeof(float);
                        depthUpload = buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, depthUploadMemory);
                        depthReadback = buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, depthReadbackMemory, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                        if (!depthSource)
                            uploadDepth(draw.depth);
                        if (draw.depth.format != DepthFormat::Float32 && draw.depth.write)
                            initializeDepthQuantization(draw.depth);
                    }
                    VkAttachmentDescription attachment{};
                    attachment.format = colorVkFormat(draw.colorFormat);
                    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                    attachment.loadOp = fullOverwrite ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD;
                    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                    auto depthAttachment = attachment;
                    depthAttachment.format = draw.depth.test ? depthImage->format : VK_FORMAT_UNDEFINED;
                    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    VkAttachmentDescription attachments[]{attachment, depthAttachment};
                    VkAttachmentReference depthReference{depthOnly(draw) ? 0u : 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                    VkSubpassDescription subpass{};
                    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                    subpass.colorAttachmentCount = depthOnly(draw) ? 0 : 1;
                    subpass.pColorAttachments = &color;
                    subpass.pDepthStencilAttachment = draw.depth.test ? &depthReference : nullptr;
                    VkSubpassDependency dependency{};
                    dependency.srcSubpass = 0;
                    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
                    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
                    ri.attachmentCount = depthOnly(draw) ? 1 : draw.depth.test ? 2 : 1;
                    ri.pAttachments = attachments + unsigned(depthOnly(draw));
                    ri.subpassCount = 1;
                    ri.pSubpasses = &subpass;
                    ri.dependencyCount = 1;
                    ri.pDependencies = &dependency;
                    check(vkCreateRenderPass(device, &ri, nullptr, &pass), "create render pass");
                    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                    fi.renderPass = pass;
                    const VkImageView views[]{view, depthView};
                    fi.attachmentCount = ri.attachmentCount;
                    fi.pAttachments = views + unsigned(depthOnly(draw));
                    fi.width = attachmentExtent(draw).width;
                    fi.height = attachmentExtent(draw).height;
                    fi.layers = 1;
                    check(vkCreateFramebuffer(device, &fi, nullptr, &framebuffer), "create framebuffer");
                    framebufferExtent = {fi.width, fi.height};
                    if (colorImage)
                        framebuffers.emplace_back(colorImage, framebuffer);

                    uniform = buffer(sizeof(constants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniformMemory);
                    void *mapped = nullptr;
                    check(vkMapMemory(device, uniformMemory, 0, sizeof(constants), 0, &mapped), "map constants");
                    std::memcpy(mapped, constants.data(), constantFloats * sizeof(float));
                    vkUnmapMemory(device, uniformMemory);
                    if (draw.vertexStage.shader) {
                        vertexUniform = buffer(sizeof(vertexConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, vertexUniformMemory);
                        upload(vertexUniformMemory, vertexConstants.data(), vertexConstantFloats * sizeof(float));
                    }
                    if (!depthOnly(draw))
                        readback = buffer(draw.target.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, readbackMemory, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                    // Clipping changes the submitted count as a model rotates. Reuse
                    // a capacity bucket instead of allocating every attachment again
                    // for each small count change. Only live vertices are uploaded/drawn.
                    vertexBuffer =
                            buffer(std::bit_ceil(draw.vertices.size()) * sizeof(RasterDraw::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexMemory);
                    upload(vertexMemory, draw.vertices.data(), draw.vertices.size_bytes());
                    if (!draw.indices.empty()) {
                        indexBuffer = buffer(std::bit_ceil(draw.indices.size()) * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexMemory);
                        upload(indexMemory, draw.indices.data(), draw.indices.size_bytes());
                    }
                    if (!depthOnly(draw)) {
                        targetBuffer = buffer(draw.target.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, targetMemory);
                        if (!fullOverwrite && !targetSource)
                            upload(targetMemory, draw.target.data(), draw.target.size());
                    }
                    // Dimensions/scissors are dynamic, and attachment load ops
                    // do not affect render-pass compatibility. Share one pipeline
                    // across these draw variants instead of compiling each size.
                    Context::PipelineKey key{vertex.id, fragment.id, {}, draw.channelMask};
                    key.blend = blendKey(draw.blend);
                    key.depth = depthKey(draw.depth);
                    key.depthBias = draw.depthBias.enabled;
                    key.topology = unsigned(draw.topology);
                    key.colorFormat = unsigned(draw.colorFormat);
                    key.cull = unsigned(draw.cullFront) | (unsigned(draw.cullBack) << 1) | (unsigned(draw.frontFaceClockwise) << 2);
                    std::vector<unsigned> textureBindings;
                    for (const auto &texture: textures) {
                        key.bindings.push_back(texture.binding);
                        textureBindings.push_back(texture.binding);
                    }
                    const auto &cached = context.pipelineFor(key, vertex, fragment, draw, textureBindings, pass);
                    setLayout = cached.setLayout;
                    layout = cached.layout;
                    pipeline = cached.pipeline;
                    cachedPipeline = true;
                    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, draw.vertexStage.shader ? 2u : 1u},
                                                 {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<std::uint32_t>(textures.size())}};
                    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                    dpi.maxSets = 1;
                    dpi.poolSizeCount = textures.empty() ? 1 : 2;
                    dpi.pPoolSizes = sizes;
                    check(vkCreateDescriptorPool(device, &dpi, nullptr, &descriptorPool), "create descriptors");
                    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                    dai.descriptorPool = descriptorPool;
                    dai.descriptorSetCount = 1;
                    dai.pSetLayouts = &setLayout;
                    check(vkAllocateDescriptorSets(device, &dai, &descriptor), "allocate descriptor");
                    VkDescriptorBufferInfo bi{uniform, 0, sizeof(constants)};
                    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet = descriptor;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    write.pBufferInfo = &bi;
                    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                    if (draw.vertexStage.shader) {
                        VkDescriptorBufferInfo info{vertexUniform, 0, sizeof(vertexConstants)};
                        write.dstBinding = 17;
                        write.pBufferInfo = &info;
                        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                    }
                    for (const auto &texture: textures)
                        uploadTexture(texture);
                }

                bool ownsImage(const std::shared_ptr<ColorImage> &candidate) const { return colorImage == candidate || depthImage == candidate; }
                const std::shared_ptr<ColorImage> &color() const { return colorImage; }

                // Render the next draw directly into `target`, the current version of its color
                // surface, instead of copying it into this bundle's own image. The images are
                // exchanged with `owner` (the bundle that produced `target`, if still cached), so
                // every image keeps exactly one owner. The caller guarantees the draw supersedes
                // the readback ticket of `target`'s current version (the replayer replaces it
                // with this draw's result), so nothing reads that version afterwards.
                void adopt(const std::shared_ptr<ColorImage> &target, DrawResources *owner)
                {
                    auto previous = std::exchange(colorImage, target);
                    target->direct = false; // this bundle may overwrite it when reused
                    image = target->image;
                    retainedTarget = true;
                    if (owner) {
                        owner->colorImage = std::move(previous);
                        owner->image = owner->colorImage->image;
                    }
                }

                VkFramebuffer framebufferFor(const std::shared_ptr<ColorImage> &target)
                {
                    // Called after this bundle's previous submission completed, so framebuffers of
                    // released images are no longer in use.
                    std::erase_if(framebuffers, [&](const auto &entry) {
                        if (!entry.first.expired())
                            return false;
                        vkDestroyFramebuffer(device, entry.second, nullptr);
                        return true;
                    });
                    for (const auto &[owner, cached]: framebuffers)
                        if (owner.lock() == target)
                            return cached;
                    if (!target->attachmentView) {
                        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                        vi.image = target->image;
                        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
                        vi.format = target->format;
                        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                        check(vkCreateImageView(device, &vi, nullptr, &target->attachmentView), "create adopted view");
                    }
                    const VkImageView views[]{target->attachmentView, depthView};
                    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                    fi.renderPass = pass;
                    fi.attachmentCount = depthView ? 2 : 1;
                    fi.pAttachments = views;
                    fi.width = framebufferExtent.width;
                    fi.height = framebufferExtent.height;
                    fi.layers = 1;
                    VkFramebuffer created{};
                    check(vkCreateFramebuffer(device, &fi, nullptr, &created), "create adopted framebuffer");
                    framebuffers.emplace_back(target, created);
                    return created;
                }
                // True when refreshing this bundle cannot block on the GPU (non-blocking fence query).
                bool idle() const
                {
                    if (!batch || batch->completed)
                        return true;
                    if (!batch->submitted)
                        return false;
                    if (vkGetFenceStatus(device, batch->fence) != VK_SUCCESS)
                        return false;
                    batch->completed = true;
                    return true;
                }
                bool needsReadbackPreservation() const
                {
                    return (!readbackValid && !pendingReadback.expired()) || (!depthReadbackValid && !pendingDepthReadback.expired());
                }


                RasterResult execute(const RasterDraw &draw, bool deferReadback, const std::shared_ptr<ColorImage> &targetSource,
                                     bool allowLazyReadback, const std::shared_ptr<const NativeImage> &targetSnapshot,
                                     const std::shared_ptr<ColorImage> &depthSource)
                {
                    const bool hasColor = !depthOnly(draw);
                    const bool lazyReadback = allowLazyReadback && deferReadback;
                    // Padding is not represented by VkImage. Preserve it independently
                    // of active pixels, and share it unchanged through target chains.
                    std::shared_ptr<const std::vector<std::uint8_t>> padding;
                    if (targetSnapshot) {
                        padding = targetSnapshot->padding;
                    } else if (draw.pitch > draw.width) {
                        const auto rowBytes = std::size_t(draw.pitch - draw.width) * colorElementBytes(draw.colorFormat);
                        auto snapshot = std::make_shared<std::vector<std::uint8_t>>(rowBytes * draw.height);
                        for (unsigned y = 0; y < draw.height; ++y)
                            std::memcpy(snapshot->data() + y * rowBytes,
                                        draw.target.data() + (std::size_t(y) * draw.pitch + draw.width) * colorElementBytes(draw.colorFormat),
                                        rowBytes);
                        padding = std::move(snapshot);
                    }
                    const auto region = !hasColor ? VkRect2D{} : targetSource ? VkRect2D{{0, 0}, {draw.width, draw.height}} : readbackRegion(draw);
                    const bool partial = region.extent.width != draw.width || region.extent.height != draw.height;
                    if (hasColor && partial && !lazyReadback) {
                        // Keep a complete CPU snapshot for result assembly and exact target
                        // retention checks. Only the bounded draw region can change on GPU.
                        upload(readbackMemory, draw.target.data(), draw.target.size());
                        VkMemoryBarrier hostWrites{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        hostWrites.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                        hostWrites.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &hostWrites, 0, nullptr, 0,
                                             nullptr);
                    }
                    if (hasColor) {
                        VkImageMemoryBarrier targetBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                        targetBarrier.srcQueueFamilyIndex = targetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        targetBarrier.image = image;
                        targetBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                        // Layout state belongs to the image: bundles can exchange images (adopt()).
                        const bool initialized = colorImage->initialized;
                        targetBarrier.oldLayout = initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
                        if (fullOverwrite) {
                            targetBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                            targetBarrier.srcAccessMask = initialized ? VK_ACCESS_TRANSFER_READ_BIT : 0;
                            targetBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                            vkCmdPipelineBarrier(command, initialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &targetBarrier);
                            ++context.fullOverwriteDraws;
                        } else {
                            if (!retainedTarget) {
                                targetBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                                targetBarrier.srcAccessMask = initialized ? VK_ACCESS_TRANSFER_READ_BIT : 0;
                                targetBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                                vkCmdPipelineBarrier(command, initialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &targetBarrier);
                                if (targetSource) {
                                    VkImageCopy copy{};
                                    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                                    copy.extent = {draw.width, draw.height, 1};
                                    vkCmdCopyImage(command, targetSource->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                                    sampledImages.push_back(targetSource);
                                    ++context.targetCopyTransfers;
                                } else {
                                    VkBufferImageCopy targetCopy{};
                                    targetCopy.bufferRowLength = draw.pitch;
                                    targetCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                                    targetCopy.imageExtent = {draw.width, draw.height, 1};
                                    vkCmdCopyBufferToImage(command, targetBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &targetCopy);
                                    ++context.targetUploadTransfers;
                                }
                                targetBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                            }
                            targetBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                            targetBarrier.srcAccessMask = retainedTarget ? VK_ACCESS_MEMORY_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
                            targetBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                            vkCmdPipelineBarrier(command, retainedTarget ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &targetBarrier);
                        }
                    }
                    if (draw.depth.test)
                        prepareDepth(draw.depth, depthSource);
                    if (gpuQueries) {
                        vkCmdResetQueryPool(command, gpuQueries, 0, 2);
                        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, gpuQueries, 0);
                        gpuTimingPending = true;
                        gpuTimingWidth = draw.width;
                        gpuTimingHeight = draw.height;
                        gpuTimingVertices = draw.vertices.size();
                        gpuTimingVertexShader = bool(draw.vertexStage.shader);
                        gpuTimingFragmentBytes = draw.shader.source.size();
                    }
                    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                    rp.renderPass = pass;
                    rp.framebuffer = colorImage ? framebufferFor(colorImage) : framebuffer;
                    rp.renderArea.extent = attachmentExtent(draw);
                    transitionDirectTextures(true);
                    vkCmdBeginRenderPass(command, &rp, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    const VkViewport viewport{0, 0, float(rasterWidth(draw)), float(rasterHeight(draw)), 0, 1};
                    const auto scissor = clippedScissor(draw);
                    vkCmdSetViewport(command, 0, 1, &viewport);
                    vkCmdSetScissor(command, 0, 1, &scissor);
                    vkCmdSetDepthBias(command, draw.depthBias.enabled && draw.depth.format == DepthFormat::Float32 ? draw.depthBias.constant : 0.f,
                                      draw.depthBias.enabled && draw.depth.format == DepthFormat::Float32 ? draw.depthBias.clamp : 0.f,
                                      draw.depthBias.enabled && draw.depth.format == DepthFormat::Float32 ? draw.depthBias.slope : 0.f);
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0, nullptr);
                    const float scale[]{2.0f / float(rasterWidth(draw)), 2.0f / float(rasterHeight(draw)), draw.depth.test ? 1.f : 0.f};
                    struct VertexPush {
                            float scale[3], unused;
                            float window[4], depth[2], padding[2];
                            std::int32_t inputs[16];
                            float bias[4];
                    } push{};
                    std::copy(std::begin(scale), std::end(scale), push.scale);
                    push.window[0] = draw.vertexStage.viewport[0];
                    push.window[1] = draw.vertexStage.viewport[1];
                    push.window[2] = draw.vertexStage.viewport[2] * .5f;
                    push.window[3] = draw.vertexStage.viewport[3] * .5f;
                    push.depth[0] = draw.vertexStage.viewport[4];
                    push.depth[1] = draw.vertexStage.viewport[5];
                    std::fill(std::begin(push.inputs), std::end(push.inputs), -1);
                    std::copy_n(draw.vertexStage.inputParams.begin(), draw.vertexStage.inputCount, push.inputs);
                    if (draw.depthBias.enabled && draw.depth.format != DepthFormat::Float32) {
                        push.bias[0] = draw.depthBias.constant / (draw.depth.format == DepthFormat::UNorm16 ? 65535.f : 16777215.f);
                        push.bias[1] = draw.depthBias.slope;
                        push.bias[2] = draw.depthBias.clamp;
                    }
                    static_assert(sizeof(push) == 128);
                    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
                    const VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer, &offset);
                    if (draw.indices.empty()) {
                        vkCmdDraw(command, draw.vertices.size(), 1, 0, 0);
                    } else {
                        vkCmdBindIndexBuffer(command, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(command, draw.indices.size(), 1, 0, 0, 0);
                    }
                    vkCmdEndRenderPass(command);
                    transitionDirectTextures(false);
                    if (gpuQueries)
                        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpuQueries, 1);
                    if (draw.depth.test && draw.depth.write && draw.depth.format != DepthFormat::Float32)
                        quantizeDepthImage(draw.depth);
                    if (draw.depth.test && draw.depth.write && !lazyReadback)
                        copyDepth(draw.depth.width, draw.depth.height, draw.depth.pitch);
                    VkBufferImageCopy copy{};
                    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    copy.imageOffset = {region.offset.x, region.offset.y, 0};
                    copy.imageExtent = {region.extent.width, region.extent.height, 1};
                    copy.bufferOffset = (VkDeviceSize(region.offset.y) * draw.pitch + region.offset.x) * colorElementBytes(draw.colorFormat);
                    copy.bufferRowLength = draw.pitch;
                    if (!lazyReadback && region.extent.width && region.extent.height) {
                        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
                        ++context.readbackTransfers;
                    }
                    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    if (!lazyReadback)
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0,
                                             nullptr);
                    check(vkEndCommandBuffer(command), "end commands");
                    batch = context.enqueue(command);
                    for (const auto &texture: cachedTextures)
                        if (texture && texture->producer.lock().get() == this)
                            texture->ready = true;
                    submitted = true;
                    if (colorImage)
                        colorImage->initialized = true;
                    readbackValid = hasColor && !lazyReadback;
                    depthReadbackValid = !lazyReadback;
                    if (colorImage)
                        ++colorImage->version;
                    if (depthImage)
                        ++depthImage->version;
                    RasterResult result;
                    if (draw.depth.test && draw.depth.write) {
                        const auto padding = depthPadding(draw.depth);
                        result.depthReadback = std::make_shared<RasterReadback>(
                                draw.depth.target.size_bytes(),
                                [self = shared_from_this(), width = draw.depth.width, height = draw.depth.height, pitch = draw.depth.pitch, padding] {
                                    return self->readDepth(width, height, pitch, padding);
                                },
                                std::make_shared<NativeImage>(depthImage, draw.depth.pitch, padding));
                        pendingDepthReadback = result.depthReadback;
                    }
                    if (!hasColor) {
                        // The guest color binding is unused. Preserve its exact version
                        // without allocating, uploading or copying a color attachment.
                        result.readback = draw.renderedTarget;
                        if (!result.readback)
                            result.rgba.assign(draw.target.begin(), draw.target.end());
                        if (!deferReadback) {
                            result.resolve();
                            result.resolveDepth();
                        }
                        return result;
                    }
                    if (!deferReadback) {
                        pendingReadback.reset();
                        result.rgba = readPixels(colorImage, draw.width, draw.height, draw.pitch, padding);
                        result.resolveDepth();
                        return result;
                    }
                    auto ticket = std::make_shared<RasterReadback>(
                            draw.target.size(),
                            [self = shared_from_this(), target = colorImage, padding, width = draw.width, height = draw.height, pitch = draw.pitch] {
                                return self->readPixels(target, width, height, pitch, padding);
                            },
                            std::make_shared<NativeImage>(colorImage, draw.pitch, padding));
                    pendingReadback = ticket;
                    result.readback = std::move(ticket);
                    return result;
                }

                void finishPending()
                {
                    if (auto pending = pendingReadback.lock())
                        pending->resolve();
                    if (auto pending = pendingDepthReadback.lock())
                        pending->resolve();
                    wait();
                }

                void uploadDepth(const RasterDraw::Depth &depth)
                {
                    void *mapped = nullptr;
                    check(vkMapMemory(device, depthUploadMemory, 0, depth.target.size_bytes(), 0, &mapped), "map depth upload");
                    std::memcpy(mapped, depth.target.data(), depth.target.size_bytes());
                    vkUnmapMemory(device, depthUploadMemory);
                }

                void initializeDepthQuantization(const RasterDraw::Depth &depth)
                {
                    context.initializeDepthQuantizer();
                    const auto bytes = std::size_t(depth.width) * depth.height * sizeof(float);
                    depthQuantizeBuffer =
                            buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   depthQuantizeMemory, 0, true);
                    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
                    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                    poolInfo.maxSets = poolInfo.poolSizeCount = 1;
                    poolInfo.pPoolSizes = &size;
                    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &depthQuantizePool), "create depth quantizer pool");
                    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                    setInfo.descriptorPool = depthQuantizePool;
                    setInfo.descriptorSetCount = 1;
                    setInfo.pSetLayouts = &context.depthQuantizeSetLayout;
                    check(vkAllocateDescriptorSets(device, &setInfo, &depthQuantizeSet), "allocate depth quantizer descriptor");
                    VkDescriptorBufferInfo info{depthQuantizeBuffer, 0, bytes};
                    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet = depthQuantizeSet;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.pBufferInfo = &info;
                    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                }

                void quantizeDepthImage(const RasterDraw::Depth &depth)
                {
                    VkBufferImageCopy copy{};
                    copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
                    copy.imageExtent = {depth.width, depth.height, 1};
                    vkCmdCopyImageToBuffer(command, depthImage->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, depthQuantizeBuffer, 1, &copy);
                    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    memory.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memory, 0, nullptr, 0,
                                         nullptr);
                    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, context.depthQuantizePipeline);
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, context.depthQuantizeLayout, 0, 1, &depthQuantizeSet, 0,
                                            nullptr);
                    struct Dimensions {
                            unsigned width, height;
                            float maximum;
                    } dimensions{depth.width, depth.height, depth.format == DepthFormat::UNorm16 ? 65535.f : 16777215.f};
                    vkCmdPushConstants(command, context.depthQuantizeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dimensions), &dimensions);
                    vkCmdDispatch(command, (depth.width + 7) / 8, (depth.height + 7) / 8, 1);
                    memory.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    memory.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory, 0, nullptr, 0,
                                         nullptr);
                    VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    image.srcQueueFamilyIndex = image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    image.image = depthImage->image;
                    image.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                    image.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    image.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    image.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                         &image);
                    vkCmdCopyBufferToImage(command, depthQuantizeBuffer, depthImage->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                    image.oldLayout = image.newLayout;
                    image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    image.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    image.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                         &image);
                }

                void prepareDepth(const RasterDraw::Depth &depth, const std::shared_ptr<ColorImage> &source)
                {
                    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = depthImage->image;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                    barrier.oldLayout = submitted ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcAccessMask = submitted ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : 0;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    vkCmdPipelineBarrier(command, submitted ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                    if (source) {
                        VkImageCopy copy{};
                        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
                        copy.extent = {depth.width, depth.height, 1};
                        vkCmdCopyImage(command, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, depthImage->image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                        sampledImages.push_back(source);
                    } else {
                        VkBufferImageCopy copy{};
                        copy.bufferRowLength = depth.pitch;
                        copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
                        copy.imageExtent = {depth.width, depth.height, 1};
                        vkCmdCopyBufferToImage(command, depthUpload, depthImage->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                    }
                    barrier.oldLayout = barrier.newLayout;
                    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0,
                                         nullptr, 1, &barrier);
                }

                void copyDepth(unsigned width, unsigned height, unsigned pitch, VkCommandBuffer destination = VK_NULL_HANDLE)
                {
                    VkBufferImageCopy copy{};
                    copy.bufferRowLength = pitch;
                    copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
                    copy.imageExtent = {width, height, 1};
                    vkCmdCopyImageToBuffer(destination ? destination : command, depthImage->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           depthReadback, 1, &copy);
                    ++context.readbackTransfers;
                }

                std::shared_ptr<const std::vector<std::uint8_t>> depthPadding(const RasterDraw::Depth &depth)
                {
                    if (depth.rendered) {
                        const auto native = std::dynamic_pointer_cast<const NativeImage>(depth.rendered->image());
                        if (native && native->storage->format == depthImage->format)
                            return native->padding;
                    }
                    if (depth.pitch == depth.width)
                        return {};
                    const auto rowBytes = std::size_t(depth.pitch - depth.width) * sizeof(float);
                    auto padding = std::make_shared<std::vector<std::uint8_t>>(rowBytes * depth.height);
                    for (unsigned y = 0; y < depth.height; ++y)
                        std::memcpy(padding->data() + y * rowBytes, depth.target.data() + std::size_t(y) * depth.pitch + depth.width, rowBytes);
                    return padding;
                }

                VkCommandBuffer beginReadback()
                {
                    if (!readbackPool) {
                        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                        poolInfo.queueFamilyIndex = family;
                        check(vkCreateCommandPool(device, &poolInfo, nullptr, &readbackPool), "create readback pool");
                        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                        allocate.commandPool = readbackPool;
                        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                        allocate.commandBufferCount = 1;
                        check(vkAllocateCommandBuffers(device, &allocate, &readbackCommand), "allocate readback command");
                    } else {
                        // Every copy waits before returning, so this pool is idle.
                        check(vkResetCommandPool(device, readbackPool, 0), "reset readback pool");
                    }
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    check(vkBeginCommandBuffer(readbackCommand, &begin), "begin readback command");
                    return readbackCommand;
                }
                void completeReadback(VkCommandBuffer copyCommand)
                {
                    const auto producer = batch;
                    check(vkEndCommandBuffer(copyCommand), "end readback command");
                    batch = context.enqueue(copyCommand);
                    wait();
                    // This queue's copy fence covers earlier submissions too. Mark
                    // the producer token complete without another host fence wait.
                    if (producer)
                        producer->completed = true;
                }

                std::vector<std::uint8_t> readDepth(unsigned width, unsigned height, unsigned pitch,
                                                    const std::shared_ptr<const std::vector<std::uint8_t>> &padding)
                {
                    Profile::Scope reading(Profile::Stage::Readback);
                    if (!depthReadbackValid) {
                        const auto copyCommand = beginReadback();
                        copyDepth(width, height, pitch, copyCommand);
                        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                        vkCmdPipelineBarrier(copyCommand, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0,
                                             nullptr);
                        completeReadback(copyCommand);
                        depthReadbackValid = true;
                    } else {
                        wait();
                    }
                    const auto elementBytes = sizeof(float);
                    std::vector<std::uint8_t> result(std::size_t(pitch) * height * sizeof(float));
                    void *mapped = nullptr;
                    check(vkMapMemory(device, depthReadbackMemory, 0, std::size_t(pitch) * height * elementBytes, 0, &mapped), "map depth readback");
                    const auto *source = static_cast<const std::uint8_t *>(mapped);
                    for (unsigned y = 0; y < height; ++y)
                        if (std::endian::native == std::endian::little)
                            std::memcpy(result.data() + std::size_t(y) * pitch * sizeof(float), source + std::size_t(y) * pitch * sizeof(float),
                                        std::size_t(width) * sizeof(float));
                        else
                            for (unsigned x = 0; x < width; ++x) {
                                const auto index = std::size_t(y) * pitch + x;
                                std::uint32_t word{};
                                for (unsigned b = 0; b < elementBytes; ++b)
                                    word |= std::uint32_t(source[index * elementBytes + b]) << (b * 8);
                                const auto value = unpackDepth(word, DepthFormat::Float32);
                                std::memcpy(result.data() + index * sizeof(float), &value, sizeof(value));
                            }
                    vkUnmapMemory(device, depthReadbackMemory);
                    if (padding) {
                        const auto rowBytes = std::size_t(pitch - width) * sizeof(float);
                        for (unsigned y = 0; y < height; ++y)
                            std::memcpy(result.data() + (std::size_t(y) * pitch + width) * sizeof(float), padding->data() + y * rowBytes, rowBytes);
                    }
                    return result;
                }

                std::vector<std::uint8_t> readPixels(const std::shared_ptr<ColorImage> &target, unsigned width, unsigned height, unsigned pitch,
                                                     const std::shared_ptr<const std::vector<std::uint8_t>> &padding)
                {
                    Profile::Scope reading(Profile::Stage::Readback);
                    if (!readbackValid) {
                        // The image is still this ticket's version: reuse resolves live
                        // tickets before overwriting it. Discarded tickets need no transfer.
                        const auto copyCommand = beginReadback();
                        VkBufferImageCopy copy{};
                        copy.bufferRowLength = pitch;
                        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        copy.imageExtent = {width, height, 1};
                        vkCmdCopyImageToBuffer(copyCommand, target->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
                        ++context.readbackTransfers;
                        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                        vkCmdPipelineBarrier(copyCommand, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0,
                                             nullptr);
                        completeReadback(copyCommand);
                        readbackValid = true;
                    } else {
                        wait();
                    }
                    std::vector<std::uint8_t> result(std::size_t(pitch) * height * imageElementBytes(target->format));
                    void *mapped = nullptr;
                    check(vkMapMemory(device, readbackMemory, 0, result.size(), 0, &mapped), "map readback");
                    for (unsigned y = 0; y < height; ++y)
                        std::memcpy(result.data() + std::size_t(y) * pitch * imageElementBytes(target->format),
                                    static_cast<const std::uint8_t *>(mapped) + std::size_t(y) * pitch * imageElementBytes(target->format),
                                    std::size_t(width) * imageElementBytes(target->format));
                    vkUnmapMemory(device, readbackMemory);
                    if (padding) {
                        const auto rowBytes = std::size_t(pitch - width) * imageElementBytes(target->format);
                        for (unsigned y = 0; y < height; ++y)
                            std::memcpy(result.data() + (std::size_t(y) * pitch + width) * imageElementBytes(target->format),
                                        padding->data() + y * rowBytes, rowBytes);
                    }
                    return result;
                }

                bool refresh(const std::array<float, 3200> &constants, const std::array<float, 3200> &vertexConstants, std::size_t constantFloats,
                             std::size_t vertexConstantFloats, const std::vector<Texture> &textures, const RasterDraw &draw, bool retainTextures,
                             const std::shared_ptr<ColorImage> &targetSource, const std::shared_ptr<ColorImage> &depthSource)
                {
                    finishPending();
                    directImages.clear();
                    check(vkResetCommandPool(device, pool, 0), "reset commands");
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    check(vkBeginCommandBuffer(command, &begin), "begin commands");
                    upload(uniformMemory, constants.data(), constantFloats * sizeof(float));
                    if (draw.vertexStage.shader)
                        upload(vertexUniformMemory, vertexConstants.data(), vertexConstantFloats * sizeof(float));
                    upload(vertexMemory, draw.vertices.data(), draw.vertices.size_bytes());
                    if (!draw.indices.empty())
                        upload(indexMemory, draw.indices.data(), draw.indices.size_bytes());
                    retainedTarget = !depthOnly(draw) && !fullOverwrite && (targetSource ? targetSource == colorImage : targetMatchesReadback(draw));
                    if (!depthOnly(draw) && !fullOverwrite && !targetSource && !retainedTarget)
                        upload(targetMemory, draw.target.data(), draw.target.size());
                    if (draw.depth.test && !depthSource)
                        uploadDepth(draw.depth);
                    for (std::size_t i = 0; i < textures.size(); ++i) {
                        const auto &texture = textures[i];
                        if (cachedTextures[i]) {
                            bindCachedTexture(i, texture);
                            continue;
                        }
                        if (bindDirectTexture(i, texture)) {
                            textureStagingValid[i] = false;
                            continue;
                        }
                        bindTextureDescriptor(texture.binding, samplers[i], textureViews[i]);
                        if (texture.rendered) {
                            recordTextureUpload(textureImages[i], textureStagingBuffers[i], texture.width, texture.height,
                                                !textureImageInitialized[i], texture.rendered, texture.layers);
                            textureImageInitialized[i] = true;
                            textureStagingValid[i] = false;
                            continue;
                        }
                        if (retainTextures && textureStagingValid[i]) {
                            void *mapped = nullptr;
                            check(vkMapMemory(device, textureStagingMemories[i], 0, texture.bytes(), 0, &mapped), "map texture comparison");
                            const bool unchanged = texture.matches(mapped);
                            vkUnmapMemory(device, textureStagingMemories[i]);
                            if (unchanged) {
                                ++retainedTextures;
                                retainedTextureBytes += texture.bytes();
                                continue;
                            }
                        }
                        uploadTextureData(textureStagingMemories[i], texture);
                        textureStagingValid[i] = true;
                        recordTextureUpload(textureImages[i], textureStagingBuffers[i], texture.width, texture.height, !textureImageInitialized[i],
                                            {}, texture.layers, texture.volume, unsigned(texture.mips.size() + 1));
                        textureImageInitialized[i] = true;
                    }
                    return retainedTarget;
                }

            private:
                void wait()
                {
                    context.wait(batch);
                    batch.reset();
                    if (gpuTimingPending) {
                        std::array<std::uint64_t, 2> stamps{};
                        check(vkGetQueryPoolResults(device, gpuQueries, 0, 2, sizeof(stamps), stamps.data(), sizeof(std::uint64_t),
                                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                              "read GPU timestamps");
                        const auto mask = context.timestampBits == 64 ? ~std::uint64_t{} : (std::uint64_t{1} << context.timestampBits) - 1;
                        const auto us = double((stamps[1] - stamps[0]) & mask) * context.timestampPeriod / 1000.0;
                        Utils::Log::error("[NATIVE_GPU_TIME] target={}x{} vertices={} vertex_shader={} fragment_bytes={} gpu_us={}", gpuTimingWidth,
                                          gpuTimingHeight, gpuTimingVertices, gpuTimingVertexShader, gpuTimingFragmentBytes, us);
                        gpuTimingPending = false;
                    }
                    sampledImages.clear();
                    directImages.clear();
                }
                bool targetMatchesReadback(const RasterDraw &draw)
                {
                    if (!submitted || !readbackValid)
                        return false;
                    void *mapped = nullptr;
                    check(vkMapMemory(device, readbackMemory, 0, draw.target.size(), 0, &mapped), "map target comparison");
                    bool equal = true;
                    for (unsigned y = 0; y < draw.height; ++y) {
                        const auto offset = std::size_t(y) * draw.pitch * colorElementBytes(draw.colorFormat);
                        if (std::memcmp(draw.target.data() + offset, static_cast<const std::uint8_t *>(mapped) + offset,
                                        std::size_t(draw.width) * colorElementBytes(draw.colorFormat)) != 0) {
                            equal = false;
                            break;
                        }
                    }
                    vkUnmapMemory(device, readbackMemory);
                    return equal;
                }
                void uploadTextureData(VkDeviceMemory memory, const Texture &texture)
                {
                    void *mapped = nullptr;
                    check(vkMapMemory(device, memory, 0, texture.bytes(), 0, &mapped), "map texture staging");
                    texture.copyTo(mapped);
                    vkUnmapMemory(device, memory);
                }
                void upload(VkDeviceMemory memory, const void *data, std::size_t size)
                {
                    void *mapped = nullptr;
                    check(vkMapMemory(device, memory, 0, size, 0, &mapped), "map upload");
                    std::memcpy(mapped, data, size);
                    vkUnmapMemory(device, memory);
                }
                VkSampler makeSampler(const Texture &texture)
                {
                    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                    si.magFilter = texture.filter;
                    si.minFilter = texture.minFilter;
                    si.mipmapMode = texture.mipFilter;
                    si.minLod = texture.minLod;
                    si.maxLod = texture.maxLod;
                    si.addressModeU = texture.addressU;
                    si.addressModeV = texture.addressV;
                    si.addressModeW = texture.addressW;
                    si.borderColor = texture.borderColor;
                    VkSampler sampler{};
                    check(vkCreateSampler(device, &si, nullptr, &sampler), "create sampler");
                    samplers.push_back(sampler);
                    return sampler;
                }

                std::shared_ptr<CachedTexture> cachedTexture(const Texture &texture, const std::shared_ptr<CachedTexture> &previous)
                {
                    std::vector<std::uint64_t> key{reinterpret_cast<std::uintptr_t>(texture.snapshotOwner.get()),
                                                   reinterpret_cast<std::uintptr_t>(texture.snapshot.data()),
                                                   texture.width,
                                                   texture.height,
                                                   texture.layers,
                                                   unsigned(texture.format),
                                                   texture.sourcePitch,
                                                   unsigned(texture.components.r),
                                                   unsigned(texture.components.g),
                                                   unsigned(texture.components.b),
                                                   unsigned(texture.components.a),
                                                   unsigned(texture.array),
                                                   unsigned(texture.volume),
                                                   unsigned(texture.cube)};
                    for (const auto mip: texture.mips) {
                        key.push_back(reinterpret_cast<std::uintptr_t>(mip.data()));
                        key.push_back(mip.size());
                    }
                    if (previous && previous->key == key && previous->ready) {
                        ++context.textureHits;
                        return previous;
                    }
                    if (auto found = context.textures.find(key); found != context.textures.end()) {
                        if (found->second.value->ready || found->second.value->producer.lock().get() == this) {
                            found->second.used = ++context.textureClock;
                            ++context.textureHits;
                            return found->second.value;
                        }
                        context.textureBytes -= found->second.bytes;
                        context.textures.erase(found);
                    }
                    if (const auto retained = context.retainedTextures.find(key); retained != context.retainedTextures.end())
                        if (auto value = retained->second.lock(); value && (value->ready || value->producer.lock().get() == this)) {
                            ++context.textureHits;
                            return value;
                        }
                    const auto bytes = texture.bytes() * 2;
                    context.evictTextures(bytes);
                    auto result = std::make_shared<CachedTexture>();
                    result->device = device;
                    result->owner = texture.snapshotOwner;
                    result->key = key;
                    result->producer = shared_from_this();
                    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                    ci.imageType = texture.volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
                    ci.flags = texture.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
                    ci.format = texture.format;
                    ci.extent = {texture.width, texture.height, texture.volume ? texture.layers : 1};
                    ci.mipLevels = unsigned(texture.mips.size() + 1);
                    ci.arrayLayers = texture.volume ? 1 : texture.layers;
                    ci.samples = VK_SAMPLE_COUNT_1_BIT;
                    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
                    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    check(vkCreateImage(device, &ci, nullptr, &result->image), "create shared texture");
                    VkMemoryRequirements req{};
                    vkGetImageMemoryRequirements(device, result->image, &req);
                    // Immutable sampled images never need host mapping or CPU
                    // readback. Prefer VRAM instead of the first compatible heap,
                    // which can be system memory on a discrete GPU.
                    result->imageMemory = allocate(req, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
                    check(vkBindImageMemory(device, result->image, result->imageMemory, 0), "bind shared texture");
                    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    vi.image = result->image;
                    vi.viewType = texture.cube     ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
                                  : texture.volume ? VK_IMAGE_VIEW_TYPE_3D
                                  : texture.array  ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                   : VK_IMAGE_VIEW_TYPE_2D;
                    vi.format = ci.format;
                    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, ci.mipLevels, 0, ci.arrayLayers};
                    vi.components = texture.components;
                    check(vkCreateImageView(device, &vi, nullptr, &result->view), "create shared texture view");
                    result->staging = buffer(texture.bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, result->stagingMemory,
                                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT, false, false);
                    uploadTextureData(result->stagingMemory, texture);
                    recordTextureUpload(result->image, result->staging, texture.width, texture.height, true, {}, texture.layers, texture.volume,
                                        ci.mipLevels);
                    result->bytes = bytes;
                    result->usage = context.textureMemory;
                    result->usage->bytes.fetch_add(bytes, std::memory_order_relaxed);
                    context.admitTexture(std::move(key), result);
                    return result;
                }

                void bindCachedTexture(std::size_t index, const Texture &texture)
                {
                    auto shared = cachedTexture(texture, cachedTextures[index]);
                    textureImages[index] = shared->image;
                    textureViews[index] = shared->view;
                    textureStagingBuffers[index] = shared->staging;
                    textureStagingMemories[index] = shared->stagingMemory;
                    cachedTextures[index] = std::move(shared);
                    bindTextureDescriptor(texture.binding, samplers[index], textureViews[index]);
                }

                void uploadTexture(const Texture &texture)
                {
                    cachedTextures.push_back({});
                    if (texture.snapshotOwner && !texture.snapshot.empty() && !texture.rendered) {
                        textureImages.push_back({});
                        textureViews.push_back({});
                        textureStagingBuffers.push_back({});
                        textureStagingMemories.push_back({});
                        textureStagingValid.push_back(false);
                        textureImageInitialized.push_back(true);
                        makeSampler(texture);
                        bindCachedTexture(textureImages.size() - 1, texture);
                        return;
                    }
                    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                    ci.imageType = texture.volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
                    ci.flags = texture.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
                    ci.format = texture.format;
                    ci.extent = {texture.width, texture.height, texture.volume ? texture.layers : 1};
                    ci.mipLevels = unsigned(texture.mips.size() + 1);
                    ci.arrayLayers = texture.volume ? 1 : texture.layers;
                    ci.samples = VK_SAMPLE_COUNT_1_BIT;
                    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
                    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    VkImage textureImage{};
                    check(vkCreateImage(device, &ci, nullptr, &textureImage), "create texture");
                    textureImages.push_back(textureImage);
                    VkMemoryRequirements req{};
                    vkGetImageMemoryRequirements(device, textureImage, &req);
                    check(vkBindImageMemory(device, textureImage, allocate(req, 0), 0), "bind texture");
                    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    vi.image = textureImage;
                    vi.viewType = texture.cube     ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
                                  : texture.volume ? VK_IMAGE_VIEW_TYPE_3D
                                  : texture.array  ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                   : VK_IMAGE_VIEW_TYPE_2D;
                    vi.format = ci.format;
                    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, ci.mipLevels, 0, ci.arrayLayers};
                    vi.components = texture.components;
                    VkImageView textureView{};
                    check(vkCreateImageView(device, &vi, nullptr, &textureView), "create texture view");
                    textureViews.push_back(textureView);
                    const auto sampler = makeSampler(texture);
                    VkDeviceMemory stagingMemory{};
                    const auto staging = buffer(texture.bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, stagingMemory,
                                                VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                    textureStagingBuffers.push_back(staging);
                    textureStagingMemories.push_back(stagingMemory);
                    textureStagingValid.push_back(false);
                    textureImageInitialized.push_back(false);
                    const auto index = textureImages.size() - 1;
                    if (bindDirectTexture(index, texture))
                        return;
                    if (!texture.rendered) {
                        uploadTextureData(stagingMemory, texture);
                        textureStagingValid[index] = true;
                    }
                    recordTextureUpload(textureImage, staging, texture.width, texture.height, true, texture.rendered, texture.layers, texture.volume,
                                        ci.mipLevels);
                    textureImageInitialized[index] = true;
                    bindTextureDescriptor(texture.binding, sampler, textureView);
                }
                void bindTextureDescriptor(unsigned binding, VkSampler sampler, VkImageView textureView)
                {
                    VkDescriptorImageInfo info{sampler, textureView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet = descriptor;
                    write.dstBinding = binding;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.pImageInfo = &info;
                    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                }
                bool bindDirectTexture(std::size_t index, const Texture &texture)
                {
                    static const bool enabled = [] {
                        const auto *value = std::getenv("WEMU_NATIVE_DIRECT_TEXTURES");
                        return !value || value[0] != '0';
                    }();
                    const auto &source = texture.rendered;
                    // Self-feedback must snapshot before the attachment is modified.
                    // Depth keeps the established D32-to-R32 conversion path.
                    if (!enabled || !source || texture.array || texture.cube || !texture.mips.empty() || source == colorImage ||
                        source == depthImage || source->format != texture.format)
                        return false;
                    bindTextureDescriptor(texture.binding, samplers[index], source->samplingView(texture.components));
                    if (std::find(directImages.begin(), directImages.end(), source) == directImages.end()) {
                        directImages.push_back(source);
                        sampledImages.push_back(source);
                    }
                    ++context.directTextureBindings;
                    context.directTextureBytes += texture.bytes();
                    return true;
                }
                void transitionDirectTextures(bool sampling)
                {
                    // Copy target/depth inputs first, then sample each source once
                    // regardless of how many stage bindings alias it. Restore the
                    // canonical transfer-source layout for future consumers.
                    for (const auto &source: directImages) {
                        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.image = source->image;
                        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                        barrier.oldLayout = sampling ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        barrier.newLayout = sampling ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                        barrier.srcAccessMask = sampling ? VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT;
                        barrier.dstAccessMask = sampling ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_TRANSFER_READ_BIT;
                        vkCmdPipelineBarrier(command,
                                             sampling ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
                                                      : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                             sampling ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                      : VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             0, 0, nullptr, 0, nullptr, 1, &barrier);
                    }
                }
                void recordTextureUpload(VkImage textureImage, VkBuffer staging, unsigned width, unsigned height, bool initial,
                                         const std::shared_ptr<ColorImage> &source = {}, unsigned layers = 1, bool volume = false,
                                         unsigned levels = 1)
                {
                    const unsigned depth = volume ? layers : 1;
                    if (volume)
                        layers = 1;
                    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = textureImage;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, layers};
                    barrier.oldLayout = initial ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcAccessMask = initial ? 0 : VK_ACCESS_SHADER_READ_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    vkCmdPipelineBarrier(command,
                                         initial ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                 : (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                    if (source)
                        ++context.residentTextureCopies;
                    if (source && source->format == VK_FORMAT_D32_SFLOAT) {
                        auto found = depthTextureTransfers.find(staging);
                        if (found == depthTextureTransfers.end()) {
                            VkDeviceMemory memory{};
                            const auto scratch = buffer(VkDeviceSize(width) * height * sizeof(float),
                                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memory, 0, true);
                            found = depthTextureTransfers.emplace(staging, scratch).first;
                        }
                        const auto scratch = found->second;
                        VkBufferMemoryBarrier reuse{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                        reuse.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                        reuse.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        reuse.srcQueueFamilyIndex = reuse.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        reuse.buffer = scratch;
                        reuse.size = VK_WHOLE_SIZE;
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &reuse, 0,
                                             nullptr);
                        VkBufferImageCopy copy{};
                        copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
                        copy.imageExtent = {width, height, 1};
                        vkCmdCopyImageToBuffer(command, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scratch, 1, &copy);
                        VkBufferMemoryBarrier dependency{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                        dependency.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                        dependency.srcQueueFamilyIndex = dependency.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        dependency.buffer = scratch;
                        dependency.size = VK_WHOLE_SIZE;
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &dependency,
                                             0, nullptr);
                        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        vkCmdCopyBufferToImage(command, scratch, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                        sampledImages.push_back(source);
                    } else if (source) {
                        VkImageCopy copy{};
                        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        copy.extent = {width, height, 1};
                        vkCmdCopyImage(command, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, textureImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                        sampledImages.push_back(source);
                    } else {
                        std::vector<VkBufferImageCopy> copies;
                        VkDeviceSize offset = 0;
                        for (unsigned level = 0; level < levels; ++level) {
                            VkBufferImageCopy copy{};
                            copy.bufferOffset = offset;
                            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, layers};
                            copy.imageExtent = {std::max(1u, width >> level), std::max(1u, height >> level), depth};
                            copies.push_back(copy);
                            // Multilevel snapshots are RGBA32F; existing single-level
                            // formats do not need a following staging offset.
                            offset += VkDeviceSize(copy.imageExtent.width) * copy.imageExtent.height * depth * layers * 16;
                        }
                        vkCmdCopyBufferToImage(command, staging, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, unsigned(copies.size()),
                                               copies.data());
                    }
                    barrier.oldLayout = barrier.newLayout;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                         &barrier);
                }

                static void check(VkResult result, const char *operation)
                {
                    if (result != VK_SUCCESS)
                        throw std::runtime_error(std::string(operation) + ": Vulkan result " + std::to_string(result));
                }
                VkDeviceMemory allocate(const VkMemoryRequirements &req, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags preferred = 0,
                                        bool track = true)
                {
                    // Explicit device-only buffers prefer the local heap. On a discrete GPU, images in
                    // the first compatible (system memory) type are rendered and sampled across PCIe:
                    // MK8's character select spent ~75 ms per frame in fence waits. Integrated GPUs keep
                    // the established placement, where local images regressed total frame delivery.
                    // WEMU_NATIVE_DEVICE_LOCAL_IMAGES=0/1 overrides the choice.
                    if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && !preferred) {
                        static const int localImages = [] {
                            const auto *value = std::getenv("WEMU_NATIVE_DEVICE_LOCAL_IMAGES");
                            return value ? int(value[0] == '1') : -1;
                        }();
                        if (localImages == 1 || (localImages < 0 && context.discrete))
                            preferred = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                    }
                    VkPhysicalDeviceMemoryProperties props{};
                    vkGetPhysicalDeviceMemoryProperties(physical, &props);
                    for (unsigned pass = 0; pass < 2; ++pass) {
                        const auto required = flags | (pass ? 0 : preferred);
                        for (unsigned i = 0; i < props.memoryTypeCount; ++i) {
                            if (!(req.memoryTypeBits & (1u << i)) || (props.memoryTypes[i].propertyFlags & required) != required)
                                continue;
                            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                            ai.allocationSize = req.size;
                            ai.memoryTypeIndex = i;
                            VkDeviceMemory memory{};
                            check(vkAllocateMemory(device, &ai, nullptr, &memory), "allocate memory");
                            if (track)
                                memories.push_back(memory);
                            return memory;
                        }
                    }
                    throw std::runtime_error("No suitable Vulkan memory type");
                }
                VkBuffer buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkDeviceMemory &memory, VkMemoryPropertyFlags preferred = 0,
                                bool deviceOnly = false, bool track = true)
                {
                    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                    bi.size = bytes;
                    bi.usage = usage;
                    VkBuffer result{};
                    check(vkCreateBuffer(device, &bi, nullptr, &result), "create buffer");
                    if (track)
                        buffers.push_back(result);
                    VkMemoryRequirements req{};
                    vkGetBufferMemoryRequirements(device, result, &req);
                    try {
                        memory = allocate(req, deviceOnly ? 0 : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                          deviceOnly ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : preferred, track);
                        check(vkBindBufferMemory(device, result, memory, 0), "bind buffer");
                    } catch (...) {
                        if (!track)
                            vkDestroyBuffer(device, result, nullptr);
                        throw;
                    }
                    return result;
                }

                std::shared_ptr<Context> contextOwner;
                std::shared_ptr<ColorImage> colorImage;
                std::shared_ptr<ColorImage> depthImage;
                std::vector<std::shared_ptr<ColorImage>> sampledImages, directImages;
                std::map<VkBuffer, VkBuffer> depthTextureTransfers;
                Context &context;
                std::weak_ptr<RasterReadback> pendingReadback;
                std::weak_ptr<RasterReadback> pendingDepthReadback;
                std::shared_ptr<Context::BatchFence> batch;
                bool readbackValid{};
                bool depthReadbackValid{};
                VkQueryPool gpuQueries{};
                bool gpuTimingPending{}, gpuTimingVertexShader{};
                unsigned gpuTimingWidth{}, gpuTimingHeight{};
                std::size_t gpuTimingVertices{}, gpuTimingFragmentBytes{};
                bool cachedPipeline{};
                VkPhysicalDevice physical{};
                VkDevice device{};
                VkQueue queue{};
                VkCommandPool pool{}, readbackPool{};
                VkCommandBuffer command{}, readbackCommand{};
                VkImage image{};
                VkImageView view{};
                VkImageView depthView{};
                VkRenderPass pass{};
                VkFramebuffer framebuffer{};
                // Framebuffers for every color image this bundle has rendered into (see adopt()).
                std::vector<std::pair<std::weak_ptr<ColorImage>, VkFramebuffer>> framebuffers;
                VkExtent2D framebufferExtent{};
                VkDescriptorSetLayout setLayout{};
                VkDescriptorPool descriptorPool{};
                VkDescriptorSet descriptor{};
                VkPipelineLayout layout{};
                VkPipeline pipeline{};
                VkBuffer uniform{}, readback{}, vertexBuffer{}, targetBuffer{}, indexBuffer{};
                VkDeviceMemory indexMemory{};
                VkDeviceMemory uniformMemory{}, vertexUniformMemory{}, vertexMemory{}, targetMemory{};
                VkBuffer vertexUniform{};
                VkBuffer depthUpload{}, depthReadback{};
                VkDeviceMemory depthUploadMemory{}, depthReadbackMemory{};
                VkBuffer depthQuantizeBuffer{};
                VkDeviceMemory depthQuantizeMemory{};
                VkDescriptorPool depthQuantizePool{};
                VkDescriptorSet depthQuantizeSet{};
                VkPipelineCache pipelineCache{};
                std::uint32_t family{};
                VkDeviceMemory readbackMemory{};
                std::vector<VkBuffer> buffers;
                std::vector<VkDeviceMemory> memories;
                std::vector<VkShaderModule> modules;
                std::vector<VkImage> textureImages;
                std::vector<VkBuffer> textureStagingBuffers;
                std::vector<VkDeviceMemory> textureStagingMemories;
                std::vector<bool> textureStagingValid, textureImageInitialized;
                std::vector<VkImageView> textureViews;
                std::vector<std::shared_ptr<CachedTexture>> cachedTextures;
                std::vector<VkSampler> samplers;
                bool submitted{};
                bool retainedTarget{}, fullOverwrite{};

            public:
                std::uint64_t retainedTextures{};
                std::uint64_t retainedTextureBytes{};
        };


        // Records draws whose attachments can stay resident into shared command buffers, instead of
        // a cached bundle per draw shape (each owning target-sized images and buffers). Images this
        // path creates (ColorImage::direct) are rendered in place: the replayer supersedes the
        // previous version's ticket. Other resident images are copied once into a direct image.
        // Constants, vertices, indices and private texels are suballocated from the open segment's
        // upload buffer and descriptor sets from its pools. A segment is submitted every few dozen
        // draws, and always before bundle work or readbacks, so queue order follows replay order.
        class DirectRenderer : public std::enable_shared_from_this<DirectRenderer> {
            public:
                struct Inputs {
                        const RasterDraw &draw;
                        const SpirvModule &vertex, &fragment;
                        const std::array<float, 3200> &constants, &vertexConstants;
                        std::size_t constantFloats{}, vertexConstantFloats{};
                        const std::vector<DrawResources::Texture> &textures;
                        std::shared_ptr<ColorImage> targetSource;
                        std::shared_ptr<const NativeImage> targetSnapshot;
                        std::shared_ptr<ColorImage> depthSource;
                        bool overwriteTarget{};
                        // The replayer drops the previous versions' tickets, so their images can be
                        // rendered in place. Verification keeps them and gets copies instead.
                        bool supersedes{};
                };

                explicit DirectRenderer(std::shared_ptr<Context> owner) : contextOwner(std::move(owner)), context(*contextOwner), device(context.device)
                {
                    VkPhysicalDeviceProperties properties{};
                    vkGetPhysicalDeviceProperties(context.physical, &properties);
                    alignment = std::max<VkDeviceSize>({256, properties.limits.minUniformBufferOffsetAlignment,
                                                        properties.limits.optimalBufferCopyOffsetAlignment});
                    static const unsigned configured = [] {
                        const auto *value = std::getenv("WEMU_NATIVE_SEGMENT_DRAWS");
                        return value ? std::max(1u, unsigned(std::strtoul(value, nullptr, 10))) : 48u;
                    }();
                    drawsPerSegment = configured;
                }
                DirectRenderer(const DirectRenderer &) = delete;
                DirectRenderer &operator=(const DirectRenderer &) = delete;
                ~DirectRenderer()
                {
                    try {
                        submit();
                    } catch (...) {
                    }
                    context.drainNoThrow();
                    for (auto &segment: segments) {
                        segment.retained.clear();
                        for (auto pool: segment.descriptorPools)
                            vkDestroyDescriptorPool(device, pool, nullptr);
                        if (segment.pool)
                            vkDestroyCommandPool(device, segment.pool, nullptr);
                        destroyBuffer(segment.buffer, segment.memory);
                    }
                    if (readbackPool)
                        vkDestroyCommandPool(device, readbackPool, nullptr);
                    destroyBuffer(readbackBuffer, readbackMemory);
                    destroyBuffer(scratchBuffer, scratchMemory);
                    for (const auto &[key, entry]: framebuffers)
                        vkDestroyFramebuffer(device, entry.framebuffer, nullptr);
                    for (const auto &[key, pass]: passes)
                        vkDestroyRenderPass(device, pass, nullptr);
                    for (const auto &[key, sampler]: samplers)
                        vkDestroySampler(device, sampler, nullptr);
                    scratchTextures.clear();
                }

                // Queues everything recorded so far. Bundle submissions and readbacks call this first.
                void submit()
                {
                    auto &segment = segments[current];
                    if (!segment.recording)
                        return;
                    check(vkEndCommandBuffer(segment.command), "end direct commands");
                    segment.fence = context.enqueue(segment.command);
                    context.flush();
                    segment.recording = false;
                    current = (current + 1) % segments.size();
                }

                std::uint64_t draws() const { return renderedDraws; }

                RasterResult render(const Inputs &in)
                {
                    const auto &draw = in.draw;
                    const bool hasColor = !depthOnly(draw);
                    const bool colorInPlace = hasColor && in.targetSource && in.targetSource->direct && in.supersedes;
                    const bool colorCopy = hasColor && in.targetSource && !colorInPlace;
                    const bool colorUpload = hasColor && !in.targetSource && !in.overwriteTarget;
                    const bool depthInPlace = draw.depth.test && in.depthSource && in.depthSource->direct && (in.supersedes || !draw.depth.write);
                    const bool depthCopy = draw.depth.test && in.depthSource && !depthInPlace;
                    const bool depthUpload = draw.depth.test && !in.depthSource;

                    // Classify textures before recording, so the whole draw fits in one segment.
                    enum class Source { Shared, Direct, Copy, Private };
                    std::vector<Source> sources(in.textures.size());
                    std::vector<std::shared_ptr<CachedTexture>> shared(in.textures.size());
                    VkDeviceSize need = 0;
                    const auto reserve = [&](VkDeviceSize bytes) { need += (bytes + alignment - 1) / alignment * alignment; };
                    reserve(sizeof(in.constants));
                    if (draw.vertexStage.shader)
                        reserve(sizeof(in.vertexConstants));
                    reserve(draw.vertices.size_bytes());
                    reserve(draw.indices.size_bytes());
                    if (colorUpload)
                        reserve(draw.target.size());
                    if (depthUpload)
                        reserve(draw.depth.target.size_bytes());
                    static const bool directTextures = [] {
                        const auto *value = std::getenv("WEMU_NATIVE_DIRECT_TEXTURES");
                        return !value || value[0] != '0';
                    }();
                    for (std::size_t i = 0; i < in.textures.size(); ++i) {
                        const auto &texture = in.textures[i];
                        if (texture.rendered) {
                            // Sampling an attachment of this draw would be a feedback loop: copy it first.
                            const bool attached = (hasColor && texture.rendered == in.targetSource) || texture.rendered == in.depthSource;
                            sources[i] = directTextures && !attached && !texture.array && !texture.cube && texture.mips.empty() &&
                                                         texture.rendered->format == texture.format
                                                 ? Source::Direct
                                                 : Source::Copy;
                            continue;
                        }
                        if (texture.snapshotOwner && !texture.snapshot.empty()) {
                            sources[i] = Source::Shared;
                            shared[i] = findSharedTexture(texture);
                            if (!shared[i])
                                reserve(texture.bytes());
                            continue;
                        }
                        sources[i] = Source::Private;
                        reserve(texture.bytes());
                    }

                    auto &segment = open(need);
                    const auto command = segment.command;
                    const auto pushUpload = [&](const void *data, std::size_t bytes, std::size_t reserved) {
                        const auto offset = segment.used;
                        if (bytes)
                            std::memcpy(segment.mapped + offset, data, bytes);
                        segment.used += (std::max(bytes, reserved) + alignment - 1) / alignment * alignment;
                        return offset;
                    };
                    const auto constantsOffset = pushUpload(in.constants.data(), in.constantFloats * sizeof(float), sizeof(in.constants));
                    const auto vertexConstantsOffset = draw.vertexStage.shader ? pushUpload(in.vertexConstants.data(), in.vertexConstantFloats * sizeof(float),
                                                                                            sizeof(in.vertexConstants))
                                                                               : VkDeviceSize{};
                    const auto verticesOffset = pushUpload(draw.vertices.data(), draw.vertices.size_bytes(), 0);
                    const auto indicesOffset = draw.indices.empty() ? VkDeviceSize{} : pushUpload(draw.indices.data(), draw.indices.size_bytes(), 0);

                    // Textures first: their transfers must precede the render pass.
                    std::vector<VkDescriptorImageInfo> imageInfos(in.textures.size());
                    std::vector<std::shared_ptr<ColorImage>> sampled;
                    for (std::size_t i = 0; i < in.textures.size(); ++i) {
                        const auto &texture = in.textures[i];
                        imageInfos[i].sampler = sampler(texture);
                        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        switch (sources[i]) {
                            case Source::Shared:
                                if (!shared[i])
                                    shared[i] = createSharedTexture(texture, segment, pushUpload);
                                imageInfos[i].imageView = shared[i]->view;
                                segment.retained.push_back(shared[i]);
                                break;
                            case Source::Direct:
                                imageInfos[i].imageView = texture.rendered->samplingView(texture.components);
                                if (std::find(sampled.begin(), sampled.end(), texture.rendered) == sampled.end())
                                    sampled.push_back(texture.rendered);
                                segment.retained.push_back(texture.rendered);
                                ++context.directTextureBindings;
                                context.directTextureBytes += texture.bytes();
                                break;
                            case Source::Copy:
                            case Source::Private: {
                                auto scratch = scratchTexture(texture);
                                if (sources[i] == Source::Copy)
                                    copyRendered(command, texture, *scratch);
                                else {
                                    const auto offset = pushUpload(nullptr, 0, texture.bytes());
                                    texture.copyTo(segment.mapped + offset);
                                    uploadTexels(command, texture, scratch->image, segment.buffer, offset, true);
                                }
                                imageInfos[i].imageView = scratch->view;
                                segment.retained.push_back(std::move(scratch));
                                if (sources[i] == Source::Copy) {
                                    segment.retained.push_back(texture.rendered);
                                    ++context.residentTextureCopies;
                                }
                                break;
                            }
                        }
                    }

                    // Attachments: render in place, or start a direct image from a copy or CPU bytes.
                    std::shared_ptr<ColorImage> color, depth;
                    VkImageLayout colorLayout = VK_IMAGE_LAYOUT_UNDEFINED, depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    if (hasColor) {
                        if (colorInPlace) {
                            color = in.targetSource;
                            colorLayout = color->initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
                        } else {
                            color = createImage(draw.width, draw.height, colorVkFormat(draw.colorFormat), false);
                            if (colorCopy) {
                                copyImage(command, *in.targetSource, *color, false);
                                segment.retained.push_back(in.targetSource);
                                ++context.targetCopyTransfers;
                                colorLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                            } else if (colorUpload) {
                                const auto offset = pushUpload(draw.target.data(), draw.target.size(), 0);
                                uploadImage(command, *color, segment.buffer, offset, draw.pitch, false);
                                ++context.targetUploadTransfers;
                                colorLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                            } else
                                ++context.fullOverwriteDraws;
                        }
                    }
                    if (draw.depth.test) {
                        if (depthInPlace) {
                            depth = in.depthSource;
                            depthLayout = depth->initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
                        } else {
                            depth = createImage(draw.depth.width, draw.depth.height, depthVkFormat(draw.depth.format), true);
                            depth->depthFormat = draw.depth.format;
                            if (depthCopy) {
                                copyImage(command, *in.depthSource, *depth, true);
                                segment.retained.push_back(in.depthSource);
                            } else {
                                const auto offset = pushUpload(draw.depth.target.data(), draw.depth.target.size_bytes(), 0);
                                uploadImage(command, *depth, segment.buffer, offset, draw.depth.pitch, true);
                            }
                            depthLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        }
                    }
                    std::vector<VkImageMemoryBarrier> barriers;
                    const auto transition = [&](const ColorImage &image, bool depthAspect, VkImageLayout from, VkImageLayout to) {
                        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.image = image.image;
                        barrier.subresourceRange = {VkImageAspectFlags(depthAspect ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
                        barrier.oldLayout = from;
                        barrier.newLayout = to;
                        barrier.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
                        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                        barriers.push_back(barrier);
                    };
                    if (color)
                        transition(*color, false, colorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                    if (depth)
                        transition(*depth, true, depthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                    for (const auto &image: sampled)
                        transition(*image, false, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                                         unsigned(barriers.size()), barriers.data());

                    const auto pass = renderPass(hasColor ? colorVkFormat(draw.colorFormat) : VK_FORMAT_UNDEFINED,
                                                 draw.depth.test ? depthVkFormat(draw.depth.format) : VK_FORMAT_UNDEFINED);
                    Context::PipelineKey key{in.vertex.id, in.fragment.id, {}, draw.channelMask};
                    key.blend = blendKey(draw.blend);
                    key.depth = depthKey(draw.depth);
                    key.depthBias = draw.depthBias.enabled;
                    key.topology = unsigned(draw.topology);
                    key.colorFormat = unsigned(draw.colorFormat);
                    key.cull = unsigned(draw.cullFront) | (unsigned(draw.cullBack) << 1) | (unsigned(draw.frontFaceClockwise) << 2);
                    std::vector<unsigned> textureBindings;
                    for (const auto &texture: in.textures) {
                        key.bindings.push_back(texture.binding);
                        textureBindings.push_back(texture.binding);
                    }
                    const auto &pipeline = context.pipelineFor(key, in.vertex, in.fragment, draw, textureBindings, pass);

                    const auto descriptor = allocateDescriptor(segment, pipeline.setLayout);
                    std::vector<VkWriteDescriptorSet> writes;
                    const VkDescriptorBufferInfo constantsInfo{segment.buffer, constantsOffset, sizeof(in.constants)};
                    const VkDescriptorBufferInfo vertexConstantsInfo{segment.buffer, vertexConstantsOffset, sizeof(in.vertexConstants)};
                    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet = descriptor;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    write.pBufferInfo = &constantsInfo;
                    writes.push_back(write);
                    if (draw.vertexStage.shader) {
                        write.dstBinding = 17;
                        write.pBufferInfo = &vertexConstantsInfo;
                        writes.push_back(write);
                    }
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.pBufferInfo = nullptr;
                    for (std::size_t i = 0; i < in.textures.size(); ++i) {
                        write.dstBinding = in.textures[i].binding;
                        write.pImageInfo = &imageInfos[i];
                        writes.push_back(write);
                    }
                    vkUpdateDescriptorSets(device, unsigned(writes.size()), writes.data(), 0, nullptr);

                    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                    begin.renderPass = pass;
                    begin.renderArea.extent = attachmentExtent(draw);
                    begin.framebuffer = framebuffer(pass, color, depth, begin.renderArea.extent);
                    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
                    const VkViewport viewport{0, 0, float(rasterWidth(draw)), float(rasterHeight(draw)), 0, 1};
                    const auto scissor = clippedScissor(draw);
                    vkCmdSetViewport(command, 0, 1, &viewport);
                    vkCmdSetScissor(command, 0, 1, &scissor);
                    const bool floatBias = draw.depthBias.enabled && draw.depth.format == DepthFormat::Float32;
                    vkCmdSetDepthBias(command, floatBias ? draw.depthBias.constant : 0.f, floatBias ? draw.depthBias.clamp : 0.f,
                                      floatBias ? draw.depthBias.slope : 0.f);
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &descriptor, 0, nullptr);
                    struct VertexPush {
                            float scale[3], unused;
                            float window[4], depth[2], padding[2];
                            std::int32_t inputs[16];
                            float bias[4];
                    } push{};
                    static_assert(sizeof(push) == 128);
                    push.scale[0] = 2.0f / float(rasterWidth(draw));
                    push.scale[1] = 2.0f / float(rasterHeight(draw));
                    push.scale[2] = draw.depth.test ? 1.f : 0.f;
                    push.window[0] = draw.vertexStage.viewport[0];
                    push.window[1] = draw.vertexStage.viewport[1];
                    push.window[2] = draw.vertexStage.viewport[2] * .5f;
                    push.window[3] = draw.vertexStage.viewport[3] * .5f;
                    push.depth[0] = draw.vertexStage.viewport[4];
                    push.depth[1] = draw.vertexStage.viewport[5];
                    std::fill(std::begin(push.inputs), std::end(push.inputs), -1);
                    std::copy_n(draw.vertexStage.inputParams.begin(), draw.vertexStage.inputCount, push.inputs);
                    if (draw.depthBias.enabled && draw.depth.format != DepthFormat::Float32) {
                        push.bias[0] = draw.depthBias.constant / (draw.depth.format == DepthFormat::UNorm16 ? 65535.f : 16777215.f);
                        push.bias[1] = draw.depthBias.slope;
                        push.bias[2] = draw.depthBias.clamp;
                    }
                    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
                    vkCmdBindVertexBuffers(command, 0, 1, &segment.buffer, &verticesOffset);
                    if (draw.indices.empty())
                        vkCmdDraw(command, unsigned(draw.vertices.size()), 1, 0, 0);
                    else {
                        vkCmdBindIndexBuffer(command, segment.buffer, indicesOffset, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(command, unsigned(draw.indices.size()), 1, 0, 0, 0);
                    }
                    vkCmdEndRenderPass(command);
                    barriers.clear();
                    for (const auto &image: sampled)
                        transition(*image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                    if (!barriers.empty())
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                                             unsigned(barriers.size()), barriers.data());

                    RasterResult result;
                    if (depth) {
                        depth->initialized = true;
                        segment.retained.push_back(depth);
                        // A read-only draw that had to upload or copy its depth still returns the new
                        // image, so later draws find the surface resident instead of uploading again.
                        if (draw.depth.write || !depthInPlace) {
                            if (draw.depth.write)
                                ++depth->version;
                            const auto padding = depthPadding(draw.depth);
                            result.depthReadback = std::make_shared<RasterReadback>(
                                    draw.depth.target.size_bytes(),
                                    [self = shared_from_this(), image = depth, width = draw.depth.width, height = draw.depth.height, pitch = draw.depth.pitch,
                                     padding] { return self->read(image, true, width, height, pitch, padding); },
                                    std::make_shared<NativeImage>(depth, draw.depth.pitch, padding));
                        }
                    }
                    if (++segment.draws >= drawsPerSegment)
                        submit();
                    ++renderedDraws;
                    if (!color) {
                        // The guest color binding is unused: preserve its exact version.
                        result.readback = draw.renderedTarget;
                        if (!result.readback)
                            result.rgba.assign(draw.target.begin(), draw.target.end());
                        return result;
                    }
                    color->initialized = true;
                    ++color->version;
                    segment.retained.push_back(color);
                    std::shared_ptr<const std::vector<std::uint8_t>> padding;
                    if (in.targetSnapshot)
                        padding = in.targetSnapshot->padding;
                    else if (draw.pitch > draw.width) {
                        const auto elementBytes = colorElementBytes(draw.colorFormat);
                        const auto rowBytes = std::size_t(draw.pitch - draw.width) * elementBytes;
                        auto snapshot = std::make_shared<std::vector<std::uint8_t>>(rowBytes * draw.height);
                        for (unsigned y = 0; y < draw.height; ++y)
                            std::memcpy(snapshot->data() + y * rowBytes, draw.target.data() + (std::size_t(y) * draw.pitch + draw.width) * elementBytes,
                                        rowBytes);
                        padding = std::move(snapshot);
                    }
                    result.readback = std::make_shared<RasterReadback>(
                            draw.target.size(),
                            [self = shared_from_this(), image = color, width = draw.width, height = draw.height, pitch = draw.pitch, padding] {
                                return self->read(image, false, width, height, pitch, padding);
                            },
                            std::make_shared<NativeImage>(color, draw.pitch, padding));
                    return result;
                }

            private:
                struct Segment {
                        VkCommandPool pool{};
                        VkCommandBuffer command{};
                        std::shared_ptr<Context::BatchFence> fence;
                        std::vector<VkDescriptorPool> descriptorPools;
                        std::size_t descriptorPool{};
                        VkBuffer buffer{};
                        VkDeviceMemory memory{};
                        std::uint8_t *mapped{};
                        VkDeviceSize capacity{}, used{};
                        std::vector<std::shared_ptr<const void>> retained; // released once the fence completes
                        unsigned draws{};
                        bool recording{};
                };
                struct ScratchTexture {
                        VkDevice device{};
                        VkImage image{};
                        VkImageView view{};
                        VkDeviceMemory memory{};
                        ~ScratchTexture()
                        {
                            if (view)
                                vkDestroyImageView(device, view, nullptr);
                            if (image)
                                vkDestroyImage(device, image, nullptr);
                            if (memory)
                                vkFreeMemory(device, memory, nullptr);
                        }
                };
                struct FramebufferEntry {
                        std::weak_ptr<ColorImage> color, depth;
                        VkFramebuffer framebuffer{};
                };

                Segment &open(VkDeviceSize need)
                {
                    if (segments[current].recording && segments[current].used + need <= segments[current].capacity)
                        return segments[current];
                    submit();
                    auto &segment = segments[current];
                    context.wait(segment.fence);
                    segment.fence.reset();
                    segment.retained.clear();
                    for (auto pool: segment.descriptorPools)
                        check(vkResetDescriptorPool(device, pool, 0), "reset direct descriptors");
                    segment.descriptorPool = 0;
                    if (!segment.pool) {
                        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                        info.queueFamilyIndex = context.family;
                        check(vkCreateCommandPool(device, &info, nullptr, &segment.pool), "create direct command pool");
                        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                        allocate.commandPool = segment.pool;
                        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                        allocate.commandBufferCount = 1;
                        check(vkAllocateCommandBuffers(device, &allocate, &segment.command), "allocate direct commands");
                    } else
                        check(vkResetCommandPool(device, segment.pool, 0), "reset direct commands");
                    if (segment.capacity < need) {
                        destroyBuffer(segment.buffer, segment.memory);
                        segment.capacity = std::max<VkDeviceSize>(need, VkDeviceSize{32} << 20);
                        segment.buffer = createBuffer(segment.capacity,
                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                      segment.memory, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
                        void *mapped = nullptr;
                        check(vkMapMemory(device, segment.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map direct uploads");
                        segment.mapped = static_cast<std::uint8_t *>(mapped);
                    }
                    segment.used = 0;
                    segment.draws = 0;
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    check(vkBeginCommandBuffer(segment.command, &begin), "begin direct commands");
                    segment.recording = true;
                    return segment;
                }

                VkDescriptorSet allocateDescriptor(Segment &segment, VkDescriptorSetLayout layout)
                {
                    for (;; ++segment.descriptorPool) {
                        if (segment.descriptorPool == segment.descriptorPools.size()) {
                            const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048}};
                            VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                            info.maxSets = 256;
                            info.poolSizeCount = 2;
                            info.pPoolSizes = sizes;
                            VkDescriptorPool pool{};
                            check(vkCreateDescriptorPool(device, &info, nullptr, &pool), "create direct descriptors");
                            segment.descriptorPools.push_back(pool);
                        }
                        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                        info.descriptorPool = segment.descriptorPools[segment.descriptorPool];
                        info.descriptorSetCount = 1;
                        info.pSetLayouts = &layout;
                        VkDescriptorSet set{};
                        const auto result = vkAllocateDescriptorSets(device, &info, &set);
                        if (result == VK_SUCCESS)
                            return set;
                        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
                            check(result, "allocate direct descriptor");
                    }
                }

                VkDeviceMemory allocate(const VkMemoryRequirements &requirements, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
                {
                    VkPhysicalDeviceMemoryProperties properties{};
                    vkGetPhysicalDeviceMemoryProperties(context.physical, &properties);
                    for (unsigned pass = 0; pass < 2; ++pass) {
                        const auto flags = required | (pass ? 0 : preferred);
                        for (unsigned i = 0; i < properties.memoryTypeCount; ++i) {
                            if (!(requirements.memoryTypeBits & (1u << i)) || (properties.memoryTypes[i].propertyFlags & flags) != flags)
                                continue;
                            VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                            info.allocationSize = requirements.size;
                            info.memoryTypeIndex = i;
                            VkDeviceMemory memory{};
                            check(vkAllocateMemory(device, &info, nullptr, &memory), "allocate direct memory");
                            return memory;
                        }
                    }
                    throw std::runtime_error("No suitable Vulkan memory type");
                }
                VkMemoryPropertyFlags imagePlacement() const
                {
                    // Same placement policy as bundle images (see DrawResources::allocate).
                    static const int localImages = [] {
                        const auto *value = std::getenv("WEMU_NATIVE_DEVICE_LOCAL_IMAGES");
                        return value ? int(value[0] == '1') : -1;
                    }();
                    return localImages == 1 || (localImages < 0 && context.discrete) ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0;
                }
                VkBuffer createBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkDeviceMemory &memory, VkMemoryPropertyFlags required,
                                      VkMemoryPropertyFlags preferred)
                {
                    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                    info.size = bytes;
                    info.usage = usage;
                    VkBuffer buffer{};
                    check(vkCreateBuffer(device, &info, nullptr, &buffer), "create direct buffer");
                    VkMemoryRequirements requirements{};
                    vkGetBufferMemoryRequirements(device, buffer, &requirements);
                    try {
                        memory = allocate(requirements, required, preferred);
                        check(vkBindBufferMemory(device, buffer, memory, 0), "bind direct buffer");
                    } catch (...) {
                        vkDestroyBuffer(device, buffer, nullptr);
                        if (memory)
                            vkFreeMemory(device, memory, nullptr);
                        memory = {};
                        throw;
                    }
                    return buffer;
                }
                void destroyBuffer(VkBuffer &buffer, VkDeviceMemory &memory)
                {
                    if (buffer)
                        vkDestroyBuffer(device, buffer, nullptr);
                    if (memory)
                        vkFreeMemory(device, memory, nullptr);
                    buffer = {};
                    memory = {};
                }

                std::shared_ptr<ColorImage> createImage(unsigned width, unsigned height, VkFormat format, bool depthAspect)
                {
                    auto image = std::make_shared<ColorImage>();
                    image->context = contextOwner;
                    image->width = width;
                    image->height = height;
                    image->format = format;
                    image->direct = true;
                    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                    info.imageType = VK_IMAGE_TYPE_2D;
                    info.format = format;
                    info.extent = {width, height, 1};
                    info.mipLevels = info.arrayLayers = 1;
                    info.samples = VK_SAMPLE_COUNT_1_BIT;
                    info.tiling = VK_IMAGE_TILING_OPTIMAL;
                    info.usage = depthAspect ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                             : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                       VK_IMAGE_USAGE_SAMPLED_BIT;
                    check(vkCreateImage(device, &info, nullptr, &image->image), "create direct image");
                    VkMemoryRequirements requirements{};
                    vkGetImageMemoryRequirements(device, image->image, &requirements);
                    image->memory = allocate(requirements, 0, imagePlacement());
                    check(vkBindImageMemory(device, image->image, image->memory, 0), "bind direct image");
                    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    view.image = image->image;
                    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    view.format = format;
                    view.subresourceRange = {VkImageAspectFlags(depthAspect ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
                    check(vkCreateImageView(device, &view, nullptr, &image->attachmentView), "create direct attachment view");
                    return image;
                }
                void imageBarrier(VkCommandBuffer command, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                                  unsigned levels = 1, unsigned layers = 1)
                {
                    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = image;
                    barrier.subresourceRange = {aspect, 0, levels, 0, layers};
                    barrier.oldLayout = from;
                    barrier.newLayout = to;
                    barrier.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
                    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                         &barrier);
                }
                void copyImage(VkCommandBuffer command, const ColorImage &source, const ColorImage &destination, bool depthAspect)
                {
                    const VkImageAspectFlags aspect = depthAspect ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                    imageBarrier(command, destination.image, aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    VkImageCopy copy{};
                    copy.srcSubresource = copy.dstSubresource = {aspect, 0, 0, 1};
                    copy.extent = {std::min(source.width, destination.width), std::min(source.height, destination.height), 1};
                    vkCmdCopyImage(command, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &copy);
                }
                void uploadImage(VkCommandBuffer command, const ColorImage &destination, VkBuffer buffer, VkDeviceSize offset, unsigned pitch,
                                 bool depthAspect)
                {
                    const VkImageAspectFlags aspect = depthAspect ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                    imageBarrier(command, destination.image, aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    VkBufferImageCopy copy{};
                    copy.bufferOffset = offset;
                    copy.bufferRowLength = pitch;
                    copy.imageSubresource = {aspect, 0, 0, 1};
                    copy.imageExtent = {destination.width, destination.height, 1};
                    vkCmdCopyBufferToImage(command, buffer, destination.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                }

                // Tightly packed texels (DrawResources::Texture::copyTo) into every level and layer.
                void uploadTexels(VkCommandBuffer command, const DrawResources::Texture &texture, VkImage image, VkBuffer buffer, VkDeviceSize offset,
                                  bool initial)
                {
                    const unsigned levels = unsigned(texture.mips.size() + 1);
                    const unsigned depth = texture.volume ? texture.layers : 1;
                    const unsigned layers = texture.volume ? 1 : texture.layers;
                    imageBarrier(command, image, VK_IMAGE_ASPECT_COLOR_BIT, initial ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, layers);
                    std::vector<VkBufferImageCopy> copies;
                    for (unsigned level = 0; level < levels; ++level) {
                        VkBufferImageCopy copy{};
                        copy.bufferOffset = offset;
                        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, layers};
                        copy.imageExtent = {std::max(1u, texture.width >> level), std::max(1u, texture.height >> level), depth};
                        copies.push_back(copy);
                        // Multilevel snapshots are RGBA32F; single-level formats need no following offset.
                        offset += VkDeviceSize(copy.imageExtent.width) * copy.imageExtent.height * depth * layers * 16;
                    }
                    vkCmdCopyBufferToImage(command, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, unsigned(copies.size()), copies.data());
                    imageBarrier(command, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 levels, layers);
                }
                // Copies a resident image into a sampled scratch texture; D32 depth goes through a
                // device buffer to become R32 float texels.
                void copyRendered(VkCommandBuffer command, const DrawResources::Texture &texture, const ScratchTexture &scratch)
                {
                    const auto &source = *texture.rendered;
                    imageBarrier(command, scratch.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    if (source.format == VK_FORMAT_D32_SFLOAT) {
                        const VkDeviceSize bytes = VkDeviceSize(texture.width) * texture.height * sizeof(float);
                        if (scratchCapacity < bytes) {
                            destroyBuffer(scratchBuffer, scratchMemory);
                            scratchCapacity = std::max(bytes, VkDeviceSize{16} << 20);
                            scratchBuffer = createBuffer(scratchCapacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, scratchMemory, 0,
                                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                        }
                        VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        reuse.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                        reuse.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &reuse, 0, nullptr, 0, nullptr);
                        VkBufferImageCopy copy{};
                        copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
                        copy.imageExtent = {texture.width, texture.height, 1};
                        vkCmdCopyImageToBuffer(command, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scratchBuffer, 1, &copy);
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &reuse, 0, nullptr, 0, nullptr);
                        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        vkCmdCopyBufferToImage(command, scratchBuffer, scratch.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                    } else {
                        VkImageCopy copy{};
                        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        copy.extent = {texture.width, texture.height, 1};
                        vkCmdCopyImage(command, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scratch.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                       &copy);
                    }
                    imageBarrier(command, scratch.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }

                std::vector<std::uint64_t> sharedKey(const DrawResources::Texture &texture) const
                {
                    // Same identity as DrawResources::cachedTexture, so both paths share uploads.
                    std::vector<std::uint64_t> key{reinterpret_cast<std::uintptr_t>(texture.snapshotOwner.get()),
                                                   reinterpret_cast<std::uintptr_t>(texture.snapshot.data()),
                                                   texture.width,
                                                   texture.height,
                                                   texture.layers,
                                                   unsigned(texture.format),
                                                   texture.sourcePitch,
                                                   unsigned(texture.components.r),
                                                   unsigned(texture.components.g),
                                                   unsigned(texture.components.b),
                                                   unsigned(texture.components.a),
                                                   unsigned(texture.array),
                                                   unsigned(texture.volume),
                                                   unsigned(texture.cube)};
                    for (const auto mip: texture.mips) {
                        key.push_back(reinterpret_cast<std::uintptr_t>(mip.data()));
                        key.push_back(mip.size());
                    }
                    return key;
                }
                std::shared_ptr<CachedTexture> findSharedTexture(const DrawResources::Texture &texture)
                {
                    const auto key = sharedKey(texture);
                    if (auto found = context.textures.find(key); found != context.textures.end() && found->second.value->ready) {
                        found->second.used = ++context.textureClock;
                        ++context.textureHits;
                        return found->second.value;
                    }
                    if (const auto retained = context.retainedTextures.find(key); retained != context.retainedTextures.end())
                        if (auto value = retained->second.lock(); value && value->ready) {
                            ++context.textureHits;
                            return value;
                        }
                    return {};
                }
                template <typename Upload> std::shared_ptr<CachedTexture> createSharedTexture(const DrawResources::Texture &texture, Segment &segment, Upload &upload)
                {
                    auto key = sharedKey(texture);
                    if (const auto found = context.textures.find(key); found != context.textures.end()) {
                        context.textureBytes -= found->second.bytes;
                        context.textures.erase(found);
                    }
                    const auto bytes = texture.bytes();
                    context.evictTextures(bytes);
                    auto result = std::make_shared<CachedTexture>();
                    result->device = device;
                    result->owner = texture.snapshotOwner;
                    result->key = key;
                    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                    info.imageType = texture.volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
                    info.flags = texture.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
                    info.format = texture.format;
                    info.extent = {texture.width, texture.height, texture.volume ? texture.layers : 1};
                    info.mipLevels = unsigned(texture.mips.size() + 1);
                    info.arrayLayers = texture.volume ? 1 : texture.layers;
                    info.samples = VK_SAMPLE_COUNT_1_BIT;
                    info.tiling = VK_IMAGE_TILING_OPTIMAL;
                    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    check(vkCreateImage(device, &info, nullptr, &result->image), "create shared texture");
                    VkMemoryRequirements requirements{};
                    vkGetImageMemoryRequirements(device, result->image, &requirements);
                    result->imageMemory = allocate(requirements, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    check(vkBindImageMemory(device, result->image, result->imageMemory, 0), "bind shared texture");
                    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    view.image = result->image;
                    view.viewType = texture.cube     ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
                                    : texture.volume ? VK_IMAGE_VIEW_TYPE_3D
                                    : texture.array  ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                     : VK_IMAGE_VIEW_TYPE_2D;
                    view.format = info.format;
                    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, info.mipLevels, 0, info.arrayLayers};
                    view.components = texture.components;
                    check(vkCreateImageView(device, &view, nullptr, &result->view), "create shared texture view");
                    const auto offset = upload(nullptr, 0, bytes);
                    texture.copyTo(segment.mapped + offset);
                    uploadTexels(segment.command, texture, result->image, segment.buffer, offset, true);
                    // Bundles close the open segment before recording, so the upload is already queued
                    // ahead of any other consumer.
                    result->ready = true;
                    result->bytes = bytes; // no staging copy is kept
                    result->usage = context.textureMemory;
                    result->usage->bytes.fetch_add(result->bytes, std::memory_order_relaxed);
                    context.admitTexture(std::move(key), result);
                    return result;
                }

                std::shared_ptr<ScratchTexture> scratchTexture(const DrawResources::Texture &texture)
                {
                    const std::array<std::uint64_t, 10> key{texture.width,
                                                             texture.height,
                                                             texture.layers,
                                                             unsigned(texture.format),
                                                             texture.mips.size(),
                                                             unsigned(texture.array) | unsigned(texture.volume) << 1 | unsigned(texture.cube) << 2,
                                                             unsigned(texture.components.r),
                                                             unsigned(texture.components.g),
                                                             unsigned(texture.components.b),
                                                             unsigned(texture.components.a)};
                    auto &pool = scratchTextures[key];
                    // Retained by in-flight segments until their fences complete.
                    for (const auto &entry: pool)
                        if (entry.use_count() == 1)
                            return entry;
                    auto result = std::make_shared<ScratchTexture>();
                    result->device = device;
                    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                    info.imageType = texture.volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
                    info.flags = texture.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
                    info.format = texture.format;
                    info.extent = {texture.width, texture.height, texture.volume ? texture.layers : 1};
                    info.mipLevels = unsigned(texture.mips.size() + 1);
                    info.arrayLayers = texture.volume ? 1 : texture.layers;
                    info.samples = VK_SAMPLE_COUNT_1_BIT;
                    info.tiling = VK_IMAGE_TILING_OPTIMAL;
                    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    check(vkCreateImage(device, &info, nullptr, &result->image), "create scratch texture");
                    VkMemoryRequirements requirements{};
                    vkGetImageMemoryRequirements(device, result->image, &requirements);
                    result->memory = allocate(requirements, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    check(vkBindImageMemory(device, result->image, result->memory, 0), "bind scratch texture");
                    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    view.image = result->image;
                    view.viewType = texture.cube     ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
                                    : texture.volume ? VK_IMAGE_VIEW_TYPE_3D
                                    : texture.array  ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                     : VK_IMAGE_VIEW_TYPE_2D;
                    view.format = info.format;
                    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, info.mipLevels, 0, info.arrayLayers};
                    view.components = texture.components;
                    check(vkCreateImageView(device, &view, nullptr, &result->view), "create scratch texture view");
                    if (pool.size() >= 16)
                        std::erase_if(pool, [](const auto &entry) { return entry.use_count() == 1; });
                    pool.push_back(result);
                    return result;
                }

                VkSampler sampler(const DrawResources::Texture &texture)
                {
                    const std::array<std::uint32_t, 9> key{unsigned(texture.filter),
                                                           unsigned(texture.minFilter),
                                                           unsigned(texture.mipFilter),
                                                           std::bit_cast<std::uint32_t>(texture.minLod),
                                                           std::bit_cast<std::uint32_t>(texture.maxLod),
                                                           unsigned(texture.addressU),
                                                           unsigned(texture.addressV),
                                                           unsigned(texture.addressW),
                                                           unsigned(texture.borderColor)};
                    if (const auto found = samplers.find(key); found != samplers.end())
                        return found->second;
                    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                    info.magFilter = texture.filter;
                    info.minFilter = texture.minFilter;
                    info.mipmapMode = texture.mipFilter;
                    info.minLod = texture.minLod;
                    info.maxLod = texture.maxLod;
                    info.addressModeU = texture.addressU;
                    info.addressModeV = texture.addressV;
                    info.addressModeW = texture.addressW;
                    info.borderColor = texture.borderColor;
                    VkSampler created{};
                    check(vkCreateSampler(device, &info, nullptr, &created), "create direct sampler");
                    return samplers.emplace(key, created).first->second;
                }

                VkRenderPass renderPass(VkFormat colorFormat, VkFormat depthFormat)
                {
                    const auto key = std::make_pair(colorFormat, depthFormat);
                    if (const auto found = passes.find(key); found != passes.end())
                        return found->second;
                    // Matches the bundle passes' attachment formats (render-pass compatibility), but
                    // always loads: images are rendered in place and stay TRANSFER_SRC between draws.
                    std::vector<VkAttachmentDescription> attachments;
                    VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                    VkAttachmentReference depthReference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                    VkAttachmentDescription attachment{};
                    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    if (colorFormat != VK_FORMAT_UNDEFINED) {
                        attachment.format = colorFormat;
                        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        attachments.push_back(attachment);
                    }
                    if (depthFormat != VK_FORMAT_UNDEFINED) {
                        depthReference.attachment = unsigned(attachments.size());
                        attachment.format = depthFormat;
                        attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        attachments.push_back(attachment);
                    }
                    VkSubpassDescription subpass{};
                    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                    subpass.colorAttachmentCount = colorFormat != VK_FORMAT_UNDEFINED ? 1 : 0;
                    subpass.pColorAttachments = &colorReference;
                    subpass.pDepthStencilAttachment = depthFormat != VK_FORMAT_UNDEFINED ? &depthReference : nullptr;
                    VkSubpassDependency dependency{};
                    dependency.srcSubpass = 0;
                    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
                    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
                    info.attachmentCount = unsigned(attachments.size());
                    info.pAttachments = attachments.data();
                    info.subpassCount = 1;
                    info.pSubpasses = &subpass;
                    info.dependencyCount = 1;
                    info.pDependencies = &dependency;
                    VkRenderPass pass{};
                    check(vkCreateRenderPass(device, &info, nullptr, &pass), "create direct render pass");
                    return passes.emplace(key, pass).first->second;
                }

                VkFramebuffer framebuffer(VkRenderPass pass, const std::shared_ptr<ColorImage> &color, const std::shared_ptr<ColorImage> &depth,
                                          VkExtent2D extent)
                {
                    const std::tuple key{pass, color ? color->attachmentView : VkImageView{}, depth ? depth->attachmentView : VkImageView{},
                                         extent.width, extent.height};
                    if (const auto found = framebuffers.find(key); found != framebuffers.end()) {
                        // Views of released images can be recycled with the same handle.
                        if (found->second.color.lock() == color && found->second.depth.lock() == depth)
                            return found->second.framebuffer;
                        vkDestroyFramebuffer(device, found->second.framebuffer, nullptr);
                        framebuffers.erase(found);
                    }
                    if (framebuffers.size() >= 1024)
                        // In-flight segments retain their images, so a framebuffer whose images are
                        // released is idle.
                        std::erase_if(framebuffers, [&](const auto &entry) {
                            if (!entry.second.color.expired() || !entry.second.depth.expired())
                                return false;
                            vkDestroyFramebuffer(device, entry.second.framebuffer, nullptr);
                            return true;
                        });
                    VkImageView views[2]{};
                    unsigned count = 0;
                    if (color)
                        views[count++] = color->attachmentView;
                    if (depth)
                        views[count++] = depth->attachmentView;
                    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                    info.renderPass = pass;
                    info.attachmentCount = count;
                    info.pAttachments = views;
                    info.width = extent.width;
                    info.height = extent.height;
                    info.layers = 1;
                    VkFramebuffer created{};
                    check(vkCreateFramebuffer(device, &info, nullptr, &created), "create direct framebuffer");
                    framebuffers.emplace(key, FramebufferEntry{color, depth, created});
                    return created;
                }

                std::shared_ptr<const std::vector<std::uint8_t>> depthPadding(const RasterDraw::Depth &depth) const
                {
                    if (depth.rendered)
                        if (const auto native = std::dynamic_pointer_cast<const NativeImage>(depth.rendered->image());
                            native && native->storage->format == depthVkFormat(depth.format))
                            return native->padding;
                    if (depth.pitch == depth.width)
                        return {};
                    const auto rowBytes = std::size_t(depth.pitch - depth.width) * sizeof(float);
                    auto padding = std::make_shared<std::vector<std::uint8_t>>(rowBytes * depth.height);
                    for (unsigned y = 0; y < depth.height; ++y)
                        std::memcpy(padding->data() + y * rowBytes, depth.target.data() + std::size_t(y) * depth.pitch + depth.width, rowBytes);
                    return padding;
                }

                std::vector<std::uint8_t> read(const std::shared_ptr<ColorImage> &image, bool depthAspect, unsigned width, unsigned height, unsigned pitch,
                                               const std::shared_ptr<const std::vector<std::uint8_t>> &padding)
                {
                    Profile::Scope reading(Profile::Stage::Readback);
                    submit();
                    const auto elementBytes = depthAspect ? sizeof(float) : imageElementBytes(image->format);
                    const VkDeviceSize bytes = VkDeviceSize(pitch) * height * elementBytes;
                    context.wait(readbackFence);
                    if (readbackCapacity < bytes) {
                        destroyBuffer(readbackBuffer, readbackMemory);
                        readbackCapacity = bytes;
                        readbackBuffer = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readbackMemory,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                        void *mapped = nullptr;
                        check(vkMapMemory(device, readbackMemory, 0, VK_WHOLE_SIZE, 0, &mapped), "map direct readback");
                        readbackMapped = static_cast<const std::uint8_t *>(mapped);
                    }
                    if (!readbackPool) {
                        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                        info.queueFamilyIndex = context.family;
                        check(vkCreateCommandPool(device, &info, nullptr, &readbackPool), "create direct readback pool");
                        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                        allocate.commandPool = readbackPool;
                        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                        allocate.commandBufferCount = 1;
                        check(vkAllocateCommandBuffers(device, &allocate, &readbackCommand), "allocate direct readback");
                    } else
                        check(vkResetCommandPool(device, readbackPool, 0), "reset direct readback pool");
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    check(vkBeginCommandBuffer(readbackCommand, &begin), "begin direct readback");
                    VkBufferImageCopy copy{};
                    copy.bufferRowLength = pitch;
                    copy.imageSubresource = {VkImageAspectFlags(depthAspect ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 0, 1};
                    copy.imageExtent = {width, height, 1};
                    vkCmdCopyImageToBuffer(readbackCommand, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1, &copy);
                    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    vkCmdPipelineBarrier(readbackCommand, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
                    check(vkEndCommandBuffer(readbackCommand), "end direct readback");
                    readbackFence = context.enqueue(readbackCommand);
                    context.wait(readbackFence);
                    ++context.readbackTransfers;
                    std::vector<std::uint8_t> result(bytes);
                    const auto rowBytes = std::size_t(pitch) * elementBytes;
                    for (unsigned y = 0; y < height; ++y)
                        std::memcpy(result.data() + y * rowBytes, readbackMapped + y * rowBytes, std::size_t(width) * elementBytes);
                    if (padding) {
                        const auto paddingBytes = std::size_t(pitch - width) * elementBytes;
                        for (unsigned y = 0; y < height; ++y)
                            std::memcpy(result.data() + y * rowBytes + std::size_t(width) * elementBytes, padding->data() + y * paddingBytes, paddingBytes);
                    }
                    return result;
                }

                std::shared_ptr<Context> contextOwner;
                Context &context;
                VkDevice device{};
                VkDeviceSize alignment{256};
                unsigned drawsPerSegment{48};
                std::array<Segment, 4> segments;
                std::size_t current{};
                std::uint64_t renderedDraws{};
                VkCommandPool readbackPool{};
                VkCommandBuffer readbackCommand{};
                std::shared_ptr<Context::BatchFence> readbackFence;
                VkBuffer readbackBuffer{};
                VkDeviceMemory readbackMemory{};
                VkDeviceSize readbackCapacity{};
                const std::uint8_t *readbackMapped{};
                VkBuffer scratchBuffer{};
                VkDeviceMemory scratchMemory{};
                VkDeviceSize scratchCapacity{};
                std::map<std::array<std::uint32_t, 9>, VkSampler> samplers;
                std::map<std::pair<VkFormat, VkFormat>, VkRenderPass> passes;
                std::map<std::tuple<VkRenderPass, VkImageView, VkImageView, unsigned, unsigned>, FramebufferEntry> framebuffers;
                std::map<std::array<std::uint64_t, 10>, std::vector<std::shared_ptr<ScratchTexture>>> scratchTextures;
        };
    } // namespace

    struct VulkanRasterBackend::Impl {
            struct ResourceKey {
                    Context::PipelineKey pipeline;
                    unsigned width{}, height{}, pitch{}, vertexCapacity{}, indexCapacity{};
                    bool overwriteTarget{};
                    unsigned depthWidth{}, depthHeight{}, depthPitch{};
                    std::vector<std::uint32_t> textureShape;
                    unsigned chainSlot{};
                    bool sameDraw(const ResourceKey &other) const
                    {
                        return pipeline == other.pipeline && width == other.width && height == other.height && pitch == other.pitch &&
                               vertexCapacity == other.vertexCapacity && indexCapacity == other.indexCapacity && overwriteTarget == other.overwriteTarget && depthWidth == other.depthWidth &&
                               depthHeight == other.depthHeight && depthPitch == other.depthPitch && textureShape == other.textureShape;
                    }
                    auto operator<=>(const ResourceKey &) const = default;
            };
            SpirvCompiler compiler{SpirvCompiler::defaultCacheDirectory()};
            std::shared_ptr<Context> context;
            std::shared_ptr<DirectRenderer> direct; // destroyed before the context it shares
            struct ResourceEntry {
                    std::shared_ptr<DrawResources> value;
                    std::uint64_t bytes{}, used{};
            };
            std::map<ResourceKey, ResourceEntry> resources;
            std::uint64_t resourceClock{};
            std::uint64_t resourceHits{};
            std::uint64_t residentTargetCopies{};
            std::uint64_t retainedTargets{};
            std::uint64_t adoptedTargets{};
            std::uint64_t retainedTextures{};
            std::uint64_t retainedTextureBytes{};
            std::uint64_t cachedBytes{};
            std::string error;
            std::uint64_t completed{};
            bool retainTextures = true;
            bool lazyReadback = false;
            bool directDraws = true;
    };

    VulkanRasterBackend::VulkanRasterBackend(bool retainTextures, bool lazyReadback, bool directDraws) : m_impl(std::make_unique<Impl>())
    {
        m_impl->retainTextures = retainTextures;
        m_impl->lazyReadback = lazyReadback;
        m_impl->directDraws = directDraws;
    }
    VulkanRasterBackend::~VulkanRasterBackend() = default;
    const std::string &VulkanRasterBackend::lastError() const noexcept { return m_impl->error; }
    std::uint64_t VulkanRasterBackend::completedDraws() const noexcept { return m_impl->completed; }
    std::uint64_t VulkanRasterBackend::reusedDraws() const noexcept { return m_impl->resourceHits; }
    std::uint64_t VulkanRasterBackend::retainedTargetDraws() const noexcept { return m_impl->retainedTargets; }
    std::uint64_t VulkanRasterBackend::retainedTextureUploads() const noexcept { return m_impl->retainedTextures; }
    std::uint64_t VulkanRasterBackend::residentTextureCopies() const noexcept { return m_impl->context ? m_impl->context->residentTextureCopies : 0; }
    std::uint64_t VulkanRasterBackend::directTextureBindings() const noexcept { return m_impl->context ? m_impl->context->directTextureBindings : 0; }
    std::uint64_t VulkanRasterBackend::directTextureBytes() const noexcept { return m_impl->context ? m_impl->context->directTextureBytes : 0; }
    std::uint64_t VulkanRasterBackend::targetUploadTransfers() const noexcept { return m_impl->context ? m_impl->context->targetUploadTransfers : 0; }
    std::uint64_t VulkanRasterBackend::targetCopyTransfers() const noexcept { return m_impl->context ? m_impl->context->targetCopyTransfers : 0; }
    std::uint64_t VulkanRasterBackend::fullOverwriteDraws() const noexcept { return m_impl->context ? m_impl->context->fullOverwriteDraws : 0; }
    std::uint64_t VulkanRasterBackend::residentTargetCopies() const noexcept { return m_impl->residentTargetCopies; }
    std::uint64_t VulkanRasterBackend::readbackTransfers() const noexcept { return m_impl->context ? m_impl->context->readbackTransfers : 0; }
    std::uint64_t VulkanRasterBackend::pipelineCreations() const noexcept { return m_impl->context ? m_impl->context->pipelineCreations : 0; }

    std::optional<RasterResult> VulkanRasterBackend::render(const RasterDraw &draw) { return renderImpl(draw, false); }

    std::optional<RasterResult> VulkanRasterBackend::renderDeferred(const RasterDraw &draw) { return renderImpl(draw, true); }

    std::uint64_t VulkanRasterBackend::queueSubmissions() const noexcept { return m_impl->context ? m_impl->context->queueSubmissions : 0; }
    std::uint64_t VulkanRasterBackend::submittedCommandBuffers() const noexcept { return m_impl->context ? m_impl->context->submittedCommands : 0; }
    std::uint64_t VulkanRasterBackend::submissionWaits() const noexcept { return m_impl->context ? m_impl->context->submissionWaits : 0; }
    std::uint64_t VulkanRasterBackend::sharedTextureUploads() const noexcept { return m_impl->context ? m_impl->context->textureUploads : 0; }
    std::uint64_t VulkanRasterBackend::sharedTextureHits() const noexcept { return m_impl->context ? m_impl->context->textureHits : 0; }
    std::uint64_t VulkanRasterBackend::sharedTextureBytes() const noexcept
    {
        return m_impl->context ? m_impl->context->textureMemory->bytes.load(std::memory_order_relaxed) : 0;
    }

    std::optional<RasterResult> VulkanRasterBackend::renderImpl(const RasterDraw &inputDraw, bool deferReadback)
    {
        Profile::Scope drawTime(Profile::Stage::NativeDraw);
        auto draw = inputDraw;
        m_impl->error.clear();
        const auto reject = [&](std::string_view reason) -> std::optional<RasterResult> {
            m_impl->error = reason;
            return std::nullopt;
        };
        if (!draw.shader || draw.channelMask > 15)
            return reject("Unsupported shader or blend state");
        if (draw.shader.requiresOnlyColorZero && draw.enabledColorTargets != 1)
            return reject("Specialized color exports require only writable target zero");
        if (!draw.shader.exportsColor)
            draw.channelMask = 0;
        if (draw.depth.compare > 7)
            return reject("Unsupported depth comparison");
        if (draw.depth.format != DepthFormat::Float32 && draw.depth.format != DepthFormat::UNorm16 && draw.depth.format != DepthFormat::UNorm24)
            return reject("Unsupported depth storage format");
        if (draw.depth.test &&
            (!draw.depth.width || !draw.depth.height || draw.depth.width > 4096 || draw.depth.height > 4096 || draw.depth.pitch < draw.depth.width ||
             std::uint64_t(draw.depth.pitch) * draw.depth.height != draw.depth.target.size() ||
             draw.depth.target.size_bytes() > 64ull * 1024 * 1024 ||
             (draw.depth.rendered && draw.depth.rendered->size() != draw.depth.target.size_bytes())))
            return reject("Unsupported depth dimensions or snapshot");
        // UNORM targets saturate subtract results to [0, 1], as the reference blender does.
        if (draw.blend.enabled && (draw.blend.colorOperation > 4 || draw.blend.alphaOperation > 4 || !blendFactor(draw.blend.colorSource) || !blendFactor(draw.blend.colorDestination) ||
                                   !blendFactor(draw.blend.alphaSource) || !blendFactor(draw.blend.alphaDestination)))
            return reject("Unsupported blend factors or equation");
        if (!draw.width || !draw.height || draw.width > 4096 || draw.height > 4096 || draw.pitch < draw.width ||
            std::uint64_t(draw.pitch) * draw.height * colorElementBytes(draw.colorFormat) != draw.target.size() ||
            draw.target.size() > std::size_t{64} * 1024 * 1024)
            return reject("Unsupported target dimensions");
        const auto drawnVertices = draw.indices.empty() ? draw.vertices.size() : draw.indices.size();
        if ((!draw.indices.empty() && (draw.topology != RasterDraw::Topology::Triangles || draw.indices.size() > 262144 * 3 ||
                                       std::any_of(draw.indices.begin(), draw.indices.end(), [&](auto i) { return i >= draw.vertices.size(); }))) ||
            (draw.topology == RasterDraw::Topology::Triangles && drawnVertices % 3) || draw.vertices.size() > 262144 ||
            draw.constants.size() > 1024 || draw.uniforms.size() != draw.shader.uniforms.size() * 4 || draw.uniforms.size() > 2048 ||
            draw.textures.size() != draw.shader.textures.size() + (draw.vertexStage.shader ? draw.vertexStage.shader->textures.size() : 0) ||
            draw.textures.size() > 32)
            return reject("Unsupported draw inputs");
        if (draw.vertexStage.shader && (!*draw.vertexStage.shader || draw.vertexStage.constants.size() > 1024 ||
                                        draw.vertexStage.uniforms.size() != draw.vertexStage.shader->uniforms.size() * 4))
            return reject("Invalid translated vertex stage or uniform snapshot");
        if (draw.vertexStage.shader &&
            (draw.vertexStage.inputCount > 16 ||
             std::any_of(draw.vertexStage.inputParams.begin(), draw.vertexStage.inputParams.end(), [](int p) { return p < -1 || p > 31; })))
            return reject("Invalid vertex parameter mapping");
        for (const auto &vertex: draw.vertices)
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.reciprocalW) || vertex.reciprocalW <= 0.f ||
                (draw.depth.test && (!std::isfinite(vertex.z) || vertex.z < 0.f || vertex.z > 1.f)))
                return reject("Nonfinite vertex position");
        if (draw.renderedTarget && draw.renderedTarget->size() != draw.target.size())
            return reject("Invalid rendered target size");
        if (draw.vertices.empty() || (!draw.channelMask && !(draw.depth.test && draw.depth.write)) || draw.scissor[2] <= 0 || draw.scissor[3] <= 0) {
            if (draw.renderedTarget)
                draw.target = draw.renderedTarget->resolve();
            RasterResult result{std::vector<std::uint8_t>(draw.target.begin(), draw.target.end()), 0, 0};
            if (draw.depth.test && draw.depth.write) {
                if (draw.depth.rendered)
                    result.depthReadback = draw.depth.rendered;
                else
                    result.depth.assign(draw.depth.target.begin(), draw.depth.target.end());
            }
            return result;
        }
        std::uint64_t textureBytes = 0;
        std::set<unsigned> bindings;
        for (const auto &texture: draw.textures) {
            const auto &binding = texture.binding;
            const auto &expectedTextures = binding.binding > 17 && draw.vertexStage.shader ? draw.vertexStage.shader->textures : draw.shader.textures;
            const auto found = std::find_if(expectedTextures.begin(), expectedTextures.end(), [&](const auto &expected) {
                return expected.binding + (binding.binding > 17 ? 17u : 0u) == binding.binding && expected.resource == binding.resource &&
                       expected.sampler == binding.sampler && expected.type == binding.type;
            });
            if (!binding.binding || (binding.binding > 33 || binding.binding == 17 || (binding.binding > 16 && !draw.vertexStage.shader)) ||
                !bindings.insert(binding.binding).second || found == expectedTextures.end() || !texture.texel || !texture.width || !texture.height ||
                texture.width > 4096 || texture.height > 4096)
                return reject("Unsupported texture binding");
            const bool cube = binding.type == Latte::TextureType::Cube;
            const bool array = binding.type == Latte::TextureType::TwoDArray || binding.type == Latte::TextureType::ThreeD || cube;
            const auto compressedFormat = blockVkFormat(texture.blockFormat);
            const bool compressed = texture.blockFormat != TextureBlockFormat::None;
            if ((!compressed && !texture.blocks.empty()) ||
                (compressed && (compressedFormat == VK_FORMAT_UNDEFINED || array || !texture.mips.empty() || texture.rendered ||
                                !texture.unorm8.empty() || !texture.rgba32.empty() || !texture.r32.empty() || !texture.r32Bytes.empty() ||
                                !texture.rgba32Bytes.empty() || !texture.r11g11b10Bytes.empty() ||
                                texture.blocks.size() !=
                                        std::uint64_t((texture.width + 3) / 4) * ((texture.height + 3) / 4) * blockElementBytes(compressedFormat))))
                return reject("Invalid compressed base texture snapshot");
            if (cube && (texture.width != texture.height || texture.layers % 6))
                return reject("Invalid cube texture dimensions or face count");
            if (!texture.layers || texture.layers > 256 || (!array && texture.layers != 1) ||
                (array && (texture.rendered || (texture.unorm8.empty() && texture.rgba32.empty() && texture.r32.empty() && texture.r32Bytes.empty() &&
                                                texture.r11g11b10Bytes.empty()))))
                return reject("Unsupported array texture snapshot");
            if (texture.rendered &&
                (texture.unorm8Pitch < texture.width ||
                 std::uint64_t(texture.unorm8Pitch) * texture.height * (texture.renderedDepth ? 4u : colorElementBytes(texture.renderedFormat)) !=
                         texture.rendered->size()))
                return reject("Invalid rendered texture snapshot");
            if (!texture.unorm8.empty() &&
                ((texture.unorm8Channels != 1 && texture.unorm8Channels != 2 && texture.unorm8Channels != 4) || texture.unorm8Pitch < texture.width ||
                 std::uint64_t(texture.unorm8Pitch) * texture.height * texture.layers * texture.unorm8Channels > texture.unorm8.size()))
                return reject("Incomplete or unsupported UNORM8 texture snapshot");
            if (!texture.rgba32.empty() &&
                (!texture.unorm8.empty() || texture.rgba32.size() != std::uint64_t(texture.width) * texture.height * texture.layers * 4))
                return reject("Invalid float texture snapshot");
            if (!texture.r32.empty() && (!texture.unorm8.empty() || !texture.rgba32.empty() ||
                                         texture.r32.size() != std::uint64_t(texture.width) * texture.height * texture.layers))
                return reject("Invalid R32 texture snapshot");
            if (!texture.rgba32Bytes.empty() &&
                (!texture.unorm8.empty() || !texture.rgba32.empty() || !texture.r32.empty() || !texture.r32Bytes.empty() ||
                 texture.rgba32Pitch < texture.width || std::uint64_t(texture.rgba32Pitch) * texture.height * 16 > texture.rgba32Bytes.size()))
                return reject("Invalid RGBA32 float texture snapshot");
            if (!texture.r11g11b10Bytes.empty() &&
                (std::endian::native != std::endian::little || !texture.unorm8.empty() || !texture.rgba32.empty() || !texture.r32.empty() ||
                 !texture.r32Bytes.empty() || !texture.rgba32Bytes.empty() || texture.r11g11b10Pitch < texture.width ||
                 std::uint64_t(texture.r11g11b10Pitch) * texture.height * texture.layers * 4 > texture.r11g11b10Bytes.size()))
                return reject("Invalid little-endian R11G11B10 texture snapshot");
            if (!texture.r32Bytes.empty() &&
                (std::endian::native != std::endian::little || !texture.unorm8.empty() || !texture.rgba32.empty() || !texture.r32.empty() ||
                 texture.r32Pitch < texture.width || std::uint64_t(texture.r32Pitch) * texture.height * texture.layers * 4 > texture.r32Bytes.size()))
                return reject("Invalid little-endian R32 texture snapshot");
            const auto r = texture.sampler.regs[0];
            if (!texture.mips.empty() && (texture.rgba32.empty() || texture.rendered || binding.type == Latte::TextureType::ThreeD ||
                                          texture.mips.size() >= std::bit_width(std::max(texture.width, texture.height)) || texture.mips.size() > 13))
                return reject("Unsupported mip chain snapshot");
            for (unsigned level = 0; level < texture.mips.size(); ++level) {
                const auto &mip = texture.mips[level];
                if (mip.width != std::max(1u, texture.width >> (level + 1)) || mip.height != std::max(1u, texture.height >> (level + 1)) ||
                    mip.rgba32.size() != std::uint64_t(mip.width) * mip.height * texture.layers * 4)
                    return reject("Incomplete mip chain snapshot");
                textureBytes += mip.rgba32.size_bytes();
            }
            if (found->explicitLod && (((r >> 12) & 7) > 1 || ((r >> 17) & 3) > 2 ||
                                       (((r >> 17) & 3) && (!texture.sourceMipCount || texture.mips.size() + 1 < texture.sourceMipCount))))
                return reject("Explicit LOD requires the complete texture view and supported filters");
            if (binding.binding <= 16 && draw.shader.requiresDerivativeFreeSampling && found->implicitLod &&
                (texture.sourceMipCount != 1 || texture.sourceFirstMip || ((r >> 9) & 7) != ((r >> 12) & 7) || ((r >> 19) & 7)))
                return reject("Whole-quad implicit sampling requires one mip, equal filters and no anisotropy");
            // Match the reference's base-level magnification filter. Other addressing
            // modes need separate border/mirror-clamp validation before acceleration.
            // CLAMP_HALF_BORDER/CLAMP_BORDER read the sampler border like the reference,
            // which returns the unmapped border; only accept borders the view swizzle preserves.
            const bool borderSupported = [&] {
                if (((r >> 22) & 3) == 3)
                    return false;
                const auto border = texture.sampler.border();
                const auto map = texture.renderedDepth || !texture.r32.empty() || !texture.r32Bytes.empty() ? texture.r32Map : texture.unorm8Map;
                // Constant (ZERO/ONE) selectors are applied after border substitution, so only
                // channels that read a component must agree with the unmapped border.
                for (unsigned c = 0; c < 4; ++c) {
                    auto selector = (map >> (24 - c * 8)) & 7;
                    if (texture.blockFormat == TextureBlockFormat::BC4 && selector < 4)
                        selector = 0; // Preserve the reference's luminance/alpha expansion.
                    if (selector < 4 && border[selector] != border[c])
                        return false;
                }
                return true;
            }();
            const auto addressSupported = [&](unsigned mode) { return mode <= 2 || ((mode == 4 || mode == 6) && borderSupported); };
            if (!addressSupported(r & 7) || !addressSupported((r >> 3) & 7) || ((r >> 9) & 7) > 1 ||
                (binding.type == Latte::TextureType::ThreeD && !addressSupported((r >> 6) & 7)))
                return reject(std::format("Unsupported sampler mode {:08X}", r));
            textureBytes += compressed ? texture.blocks.size()
                                       : std::uint64_t(texture.width) * texture.height * texture.layers *
                                                 (texture.rendered          ? colorElementBytes(texture.renderedFormat)
                                                  : !texture.unorm8.empty() ? texture.unorm8Channels
                                                  : !texture.r32.empty() || !texture.r32Bytes.empty() || !texture.r11g11b10Bytes.empty() ? 4
                                                                                                                                         : 16);
            if (textureBytes > std::uint64_t{128} * 1024 * 1024)
                return reject("Texture upload budget exceeded");
        }
        constexpr auto vertexSource = R"(#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec4 attributes[16];
layout(location=17) in vec2 depthAndW;
layout(location=0) out vec4 inputs[16];
layout(location=16) noperspective out float windowDepth;
invariant gl_Position; precise gl_Position;
layout(push_constant) uniform Viewport { vec2 scale; float depthEnabled; } viewport;
void main() {
    gl_PointSize=1.0;
    float w=1.0/depthAndW.y;
    gl_Position=vec4((position*viewport.scale-vec2(1.0))*w,viewport.depthEnabled!=0.0?depthAndW.x*w:0.0,w);
    for(int i=0;i<16;++i) inputs[i]=attributes[i];
    windowDepth=depthAndW.x;
}
)";
        static const bool timing = [] {
            const auto *value = std::getenv("WEMU_NATIVE_TIMING");
            return value && value[0] == '1';
        }();
        const auto t0 = std::chrono::steady_clock::now();
        const bool overwriteTarget = overwritesTarget(draw);
        // Sources are only copied when they must be patched; the compiler looks them up by view.
        std::string_view nativeVertexSource = draw.vertexStage.shader ? std::string_view(draw.vertexStage.shader->source) : vertexSource;
        std::string patchedVertexSource;
        if (draw.vertexStage.shader && !draw.vertexStage.shader->textures.empty()) {
            patchedVertexSource = draw.vertexStage.shader->source;
            for (const auto &binding: draw.vertexStage.shader->textures) {
                const auto old = "binding=" + std::to_string(binding.binding) + ")";
                const auto pos = patchedVertexSource.find(old);
                if (pos == std::string::npos)
                    return reject("Invalid vertex sampler interface");
                patchedVertexSource.replace(pos, old.size(), "binding=" + std::to_string(binding.binding + 17) + ")");
            }
            nativeVertexSource = patchedVertexSource;
        }
        static const bool directDraws = [] {
            const auto *value = std::getenv("WEMU_NATIVE_DIRECT_DRAWS");
            return !value || value[0] != '0';
        }();
        static const bool gpuTiming = [] {
            const auto *value = std::getenv("WEMU_NATIVE_GPU_TIMING");
            return value && value[0] == '1';
        }();
        // GPU timing uses bundle queries.
        const bool direct = directDraws && m_impl->directDraws && !gpuTiming && deferReadback && m_impl->lazyReadback;
        const auto vertex = m_impl->compiler.compile(ShaderStage::Vertex, nativeVertexSource);
        std::string_view fragmentView = draw.shader.source;
        std::string fragmentSource;
        if (draw.depthBias.enabled && draw.depth.format != DepthFormat::Float32) {
            fragmentSource = draw.shader.source;
            fragmentSource.insert(fragmentSource.find('\n') + 1,
                                  "layout(push_constant) uniform Bias { layout(offset=112) vec4 value; } bias;\n"
                                  "float biasedDepth() { float slope=max(abs(dFdx(gl_FragCoord.z)),abs(dFdy(gl_FragCoord.z))); "
                                  "float b=bias.value.x+bias.value.y*slope; "
                                  "if(bias.value.z>0.0) b=min(b,bias.value.z); else if(bias.value.z<0.0) b=max(b,bias.value.z); "
                                  "return clamp(gl_FragCoord.z+b,0.0,1.0); }\n");
            const auto initial = fragmentSource.find("gl_FragDepth=gl_FragCoord.z;");
            if (initial != std::string::npos)
                fragmentSource.replace(initial, std::string("gl_FragDepth=gl_FragCoord.z;").size(), "gl_FragDepth=biasedDepth();");
            else
                fragmentSource.insert(fragmentSource.find("void main() {") + std::string("void main() {").size(), "\ngl_FragDepth=biasedDepth();\n");
            fragmentView = fragmentSource;
        }
        if (direct && draw.depth.test && draw.depth.write && draw.depth.format != DepthFormat::Float32) {
            // Direct draws store fixed-point depth from the fragment shader instead of the bundle
            // quantization pass (three full-surface copies per draw). Rounding matches that pass;
            // the depth test then compares quantized incoming values, as fixed-point hardware does.
            if (fragmentSource.empty())
                fragmentSource = draw.shader.source;
            const bool writesDepth = fragmentSource.find("gl_FragDepth=") != std::string::npos;
            const auto maximum = draw.depth.format == DepthFormat::UNorm16 ? "65535u" : "16777215u";
            fragmentSource.insert(fragmentSource.find('\n') + 1, std::format(R"(float wemuQuantizedDepth(float depth) {{
    const uint maximum={0};
    float value=clamp(depth,0.0,1.0);
    float scaled=value*float(maximum+1u);
    uint integer=uint(int(scaled)+int(floor(fract(scaled)-value+0.5)));
    if(integer==0u) return 0.0;
    if(integer>=maximum) return 1.0;
    uint leading=uint(findMSB(integer));
    uint t=integer<<(23u-leading);
    uint significand=t+(maximum==65535u ? (t+32767u)/65535u : 1u);
    uint exponent=leading+(maximum==65535u ? 111u : 103u);
    return uintBitsToFloat((exponent<<23)|(significand&0x7fffffu));
}}
)",
                                                                                              maximum));
            fragmentSource.insert(fragmentSource.rfind('}'),
                                  std::format("gl_FragDepth=wemuQuantizedDepth({});\n", writesDepth ? "gl_FragDepth" : "gl_FragCoord.z"));
            fragmentView = fragmentSource;
        }
        const auto fragment = m_impl->compiler.compile(ShaderStage::Fragment, fragmentView);
        if (!*vertex || !*fragment) {
            m_impl->error = !*vertex ? vertex->error : fragment->error;
            return std::nullopt;
        }
        const auto t1 = std::chrono::steady_clock::now();
        if (!m_impl->context) {
            auto context = std::make_shared<Context>();
            context->initialize();
            m_impl->context = std::move(context);
        }
        const auto t2 = std::chrono::steady_clock::now();
        if (draw.depthBias.enabled &&
            (!std::isfinite(draw.depthBias.constant) || !std::isfinite(draw.depthBias.slope) || !std::isfinite(draw.depthBias.clamp) ||
             (draw.depth.format == DepthFormat::Float32 && draw.depthBias.clamp != 0 && !m_impl->context->biasClamp)))
            return reject("Unsupported polygon depth bias");
        if (!m_impl->context->cubeArrays && std::any_of(draw.textures.begin(), draw.textures.end(),
                                                        [](const auto &texture) { return texture.binding.type == Latte::TextureType::Cube; }))
            return reject("Device lacks cube array sampling support");
        {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(m_impl->context->physical, colorVkFormat(draw.colorFormat), &properties);
            const auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | (draw.blend.enabled ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT : 0);
            if ((properties.optimalTilingFeatures & required) != required)
                return reject("Device lacks target attachment/blending support");
        }
        if (draw.depth.test) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(m_impl->context->physical, depthVkFormat(draw.depth.format), &properties);
            if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
                return reject("Device lacks the requested depth attachment format");
            if (draw.depth.format != DepthFormat::Float32 && draw.depth.write && !m_impl->context->computeQueue)
                return reject("Device lacks compute support for fixed-point depth storage");
        }
        std::shared_ptr<ColorImage> depthSource;
        std::vector<float> resolvedDepth;
        if (draw.depth.test && draw.depth.rendered) {
            const auto native = std::dynamic_pointer_cast<const NativeImage>(draw.depth.rendered->image());
            if (native && native->storage->context == m_impl->context && native->storage->version == native->version &&
                native->storage->width == draw.depth.width && native->storage->height == draw.depth.height &&
                native->storage->format == depthVkFormat(draw.depth.format) && native->storage->depthFormat == draw.depth.format)
                depthSource = native->storage;
            else {
                const auto &bytes = draw.depth.rendered->resolve();
                resolvedDepth.resize(draw.depth.target.size());
                std::memcpy(resolvedDepth.data(), bytes.data(), bytes.size());
                draw.depth.target = resolvedDepth;
            }
        }
        std::shared_ptr<ColorImage> targetSource;
        std::shared_ptr<const NativeImage> targetSnapshot;
        if (draw.renderedTarget) {
            const auto native = std::dynamic_pointer_cast<const NativeImage>(draw.renderedTarget->image());
            if (native && native->pitch == draw.pitch && native->storage->context == m_impl->context && native->storage->version == native->version &&
                native->storage->width == draw.width && native->storage->height == draw.height &&
                native->storage->format == colorVkFormat(draw.colorFormat)) {
                targetSource = native->storage;
                targetSnapshot = native;
            } else
                draw.target = draw.renderedTarget->resolve();
        }
        std::vector<DrawResources::Texture> textures;
        for (const auto &input: draw.textures) {
            auto texture = input;
            std::shared_ptr<ColorImage> resident;
            if (texture.rendered) {
                const auto native = std::dynamic_pointer_cast<const NativeImage>(texture.rendered->image());
                if (native && native->storage->context == m_impl->context && native->storage->version == native->version &&
                    native->storage->width == texture.width && native->storage->height == texture.height &&
                    native->storage->format == (texture.renderedDepth ? VK_FORMAT_D32_SFLOAT : colorVkFormat(texture.renderedFormat)))
                    resident = native->storage;
                if (texture.renderedDepth) {
                    texture.r32Bytes = resident ? std::span<const std::uint8_t>{} : texture.rendered->resolve();
                    texture.r32Pitch = texture.unorm8Pitch;
                    texture.r32Map = texture.unorm8Map;
                    texture.unorm8 = {};
                } else if (texture.renderedFormat == ColorFormat::RGBA32Float) {
                    texture.rgba32Bytes = resident ? std::span<const std::uint8_t>{} : texture.rendered->resolve();
                    texture.rgba32Pitch = texture.unorm8Pitch;
                    texture.unorm8 = {};
                } else if (texture.renderedFormat == ColorFormat::R11G11B10Float) {
                    texture.r11g11b10Bytes = resident ? std::span<const std::uint8_t>{} : texture.rendered->resolve();
                    texture.r11g11b10Pitch = texture.unorm8Pitch;
                    texture.unorm8 = {};
                } else
                    texture.unorm8 = resident ? std::span<const std::uint8_t>{} : texture.rendered->resolve();
                texture.unorm8Channels = 4;
                texture.r32 = {};
                if (!texture.renderedDepth)
                    texture.r32Bytes = {};
                texture.rgba32 = {};
            }
            const auto compressedFormat = blockVkFormat(texture.blockFormat);
            if (compressedFormat != VK_FORMAT_UNDEFINED && !m_impl->context->blockTextures)
                return reject("Device lacks BC texture compression support");
            const auto textureFormat = compressedFormat != VK_FORMAT_UNDEFINED ? compressedFormat
                                       : resident                              ? (texture.renderedDepth ? VK_FORMAT_R32_SFLOAT : resident->format)
                                       : !texture.r32.empty() || !texture.r32Bytes.empty() ? VK_FORMAT_R32_SFLOAT
                                       : !texture.r11g11b10Bytes.empty()                   ? VK_FORMAT_B10G11R11_UFLOAT_PACK32
                                       : texture.unorm8.empty()                            ? VK_FORMAT_R32G32B32A32_SFLOAT
                                       : texture.unorm8Channels == 1                       ? VK_FORMAT_R8_UNORM
                                       : texture.unorm8Channels == 2                       ? VK_FORMAT_R8G8_UNORM
                                                                                           : VK_FORMAT_R8G8B8A8_UNORM;
            VkFormatProperties format{};
            vkGetPhysicalDeviceFormatProperties(m_impl->context->physical, textureFormat, &format);
            const auto r = texture.sampler.regs[0];
            const bool linear = ((r >> 9) & 7) == 1 || (texture.binding.explicitLod && ((r >> 12) & 7) == 1);
            if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
                (linear && !(format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)))
                return reject("Host cannot sample float texture with requested filter");
            constexpr VkSamplerAddressMode address[]{VK_SAMPLER_ADDRESS_MODE_REPEAT,          VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
                                                     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                                     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER};
            constexpr VkBorderColor borderColors[]{VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
                                                   VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK};
            DrawResources::Texture uploaded;
            uploaded.snapshotOwner = m_impl->retainTextures ? texture.snapshotOwner : nullptr;
            uploaded.format = textureFormat;
            uploaded.binding = texture.binding.binding;
            uploaded.width = texture.width;
            uploaded.height = texture.height;
            uploaded.layers = texture.layers;
            uploaded.array = texture.binding.type == Latte::TextureType::TwoDArray;
            uploaded.volume = texture.binding.type == Latte::TextureType::ThreeD;
            uploaded.cube = texture.binding.type == Latte::TextureType::Cube;
            uploaded.addressW = address[(r >> 6) & 7];
            uploaded.addressU = address[r & 7];
            uploaded.addressV = address[(r >> 3) & 7];
            uploaded.borderColor = borderColors[(r >> 22) & 3];
            uploaded.filter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            uploaded.minFilter = uploaded.filter;
            for (const auto &mip: texture.mips)
                uploaded.mips.push_back(mip.rgba32);
            if (texture.binding.explicitLod) {
                uploaded.minFilter = ((r >> 12) & 7) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                uploaded.mipFilter = ((r >> 17) & 3) == 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
                if ((r >> 17) & 3) {
                    uploaded.minLod = std::min(float(texture.mips.size()), float(texture.sampler.regs[1] & 1023) / 64.f);
                    uploaded.maxLod = std::clamp(float((texture.sampler.regs[1] >> 10) & 1023) / 64.f, uploaded.minLod, float(texture.mips.size()));
                }
            }
            uploaded.rendered = resident;
            if (compressedFormat != VK_FORMAT_UNDEFINED) {
                const auto channel = [&](unsigned c) {
                    constexpr VkComponentSwizzle channels[]{VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,    VK_COMPONENT_SWIZZLE_B,
                                                            VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE};
                    auto selector = (texture.unorm8Map >> (24 - c * 8)) & 7;
                    if (texture.blockFormat == TextureBlockFormat::BC4 && selector < 4)
                        selector = 0;
                    return channels[selector < 6 ? selector : 4];
                };
                uploaded.components = {channel(0), channel(1), channel(2), channel(3)};
                uploaded.snapshot = std::as_bytes(texture.blocks);
                uploaded.sourcePitch = std::size_t((texture.width + 3) / 4) * blockElementBytes(compressedFormat);
                textures.push_back(std::move(uploaded));
                continue;
            }
            if ((resident && !texture.renderedDepth && texture.renderedFormat != ColorFormat::RGBA8) || !texture.rgba32Bytes.empty() ||
                !texture.r11g11b10Bytes.empty()) {
                const auto channel = [&](unsigned c) {
                    constexpr VkComponentSwizzle channels[]{VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,    VK_COMPONENT_SWIZZLE_B,
                                                            VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE};
                    const auto selected = (texture.unorm8Map >> (24 - c * 8)) & 7;
                    return channels[selected < 6 ? selected : 4];
                };
                uploaded.components = {channel(0), channel(1), channel(2), channel(3)};
                const bool packed = textureFormat == VK_FORMAT_B10G11R11_UFLOAT_PACK32;
                uploaded.snapshot = packed ? std::as_bytes(texture.r11g11b10Bytes) : std::as_bytes(texture.rgba32Bytes);
                uploaded.sourcePitch = packed ? std::size_t(texture.r11g11b10Pitch) * 4 : std::size_t(texture.rgba32Pitch) * 16;
                textures.push_back(std::move(uploaded));
                continue;
            }
            if (texture.renderedDepth || !texture.r32.empty() || !texture.r32Bytes.empty()) {
                const auto channel = [&](unsigned c) {
                    constexpr VkComponentSwizzle channels[]{VK_COMPONENT_SWIZZLE_R,    VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO,
                                                            VK_COMPONENT_SWIZZLE_ONE,  VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE,
                                                            VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO};
                    return channels[(texture.r32Map >> (24 - c * 8)) & 7];
                };
                uploaded.components = {channel(0), channel(1), channel(2), channel(3)};
                uploaded.snapshot = texture.r32Bytes.empty() ? std::as_bytes(texture.r32) : std::as_bytes(texture.r32Bytes);
                uploaded.sourcePitch = std::size_t(texture.r32Bytes.empty() ? texture.width : texture.r32Pitch) * sizeof(float);
                textures.push_back(std::move(uploaded));
                continue;
            }
            if (!texture.unorm8.empty() || resident) {
                const auto channel = [&](unsigned c) {
                    constexpr VkComponentSwizzle channels[]{VK_COMPONENT_SWIZZLE_R,    VK_COMPONENT_SWIZZLE_G,    VK_COMPONENT_SWIZZLE_B,
                                                            VK_COMPONENT_SWIZZLE_A,    VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE,
                                                            VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO};
                    const auto selector = (texture.unorm8Map >> (24 - c * 8)) & 7;
                    if (texture.unorm8Channels == 1 && selector < 3)
                        return VK_COMPONENT_SWIZZLE_R;
                    if (texture.unorm8Channels == 2 && selector == 2)
                        return VK_COMPONENT_SWIZZLE_ZERO;
                    if (texture.unorm8Channels < 4 && selector == 3)
                        return VK_COMPONENT_SWIZZLE_ONE;
                    return channels[selector];
                };
                uploaded.components = {channel(0), channel(1), channel(2), channel(3)};
                uploaded.snapshot = std::as_bytes(texture.unorm8);
                uploaded.sourcePitch = std::size_t(texture.unorm8Pitch) * texture.unorm8Channels;
                textures.push_back(std::move(uploaded));
                continue;
            }
            if (!texture.rgba32.empty()) {
                uploaded.snapshot = std::as_bytes(texture.rgba32);
                uploaded.sourcePitch = std::size_t(texture.width) * 4 * sizeof(float);
                textures.push_back(std::move(uploaded));
                continue;
            }
            uploaded.rgba.resize(std::size_t(texture.width) * texture.height * 4);
            for (unsigned y = 0; y < texture.height; ++y)
                for (unsigned x = 0; x < texture.width; ++x) {
                    const auto color = texture.texel(x, y);
                    std::copy(color.begin(), color.end(), uploaded.rgba.data() + (std::size_t(y) * texture.width + x) * 4);
                }
            textures.push_back(std::move(uploaded));
        }
        // Layout: registers [0, 1024), sampler borders and modes [1024, 1152), static uniform-block
        // references from 1152. Values past the references are never read, so they stay unset.
        std::array<float, 3200> constants, vertexConstants;
        const auto constantFloats = std::min<std::size_t>(constants.size(), std::max(draw.constants.size(), 1152 + draw.uniforms.size()));
        const auto vertexConstantFloats =
                draw.vertexStage.shader ? std::min<std::size_t>(vertexConstants.size(),
                                                                std::max(draw.vertexStage.constants.size(), 1152 + draw.vertexStage.uniforms.size()))
                                        : 0;
        std::fill_n(constants.begin(), constantFloats, 0.f);
        std::fill_n(vertexConstants.begin(), vertexConstantFloats, 0.f);
        if (draw.vertexStage.shader) {
            std::copy(draw.vertexStage.constants.begin(), draw.vertexStage.constants.end(), vertexConstants.begin());
            std::copy(draw.vertexStage.uniforms.begin(), draw.vertexStage.uniforms.end(), vertexConstants.begin() + 1152);
        }
        std::copy(draw.constants.begin(), draw.constants.end(), constants.begin());
        std::copy(draw.uniforms.begin(), draw.uniforms.end(), constants.begin() + 1152);
        for (const auto &texture: draw.textures) {
            const auto border = texture.sampler.border();
            const auto slot = texture.binding.binding - (texture.binding.binding > 17 ? 18 : 1);
            auto *stageConstants = texture.binding.binding > 17 ? vertexConstants.data() : constants.data();
            std::copy(border.begin(), border.end(), stageConstants + 1024 + std::size_t(slot) * 4);
            const auto offset = 1088 + slot * 4;
            const auto sampler = texture.sampler.regs[0];
            stageConstants[offset] = float(sampler & 7);
            stageConstants[offset + 1] = float((sampler >> 3) & 7);
            stageConstants[offset + 2] = float((sampler >> 9) & 7);
            stageConstants[offset + 3] = float((sampler >> 26) & 7); // DEPTH_COMPARE_FUNCTION
        }
        if (direct) {
            if (!m_impl->direct)
                m_impl->direct = std::make_shared<DirectRenderer>(m_impl->context);
            auto result = m_impl->direct->render({draw, *vertex, *fragment, constants, vertexConstants, constantFloats, vertexConstantFloats, textures,
                                                  targetSource, targetSnapshot, depthSource, overwriteTarget, draw.supersedesTarget});
            ++m_impl->completed;
            return result;
        }
        if (m_impl->direct)
            m_impl->direct->submit(); // keep queue order: bundle commands follow earlier direct draws
        Impl::ResourceKey key;
        key.pipeline = {vertex->id, fragment->id, {}, draw.channelMask};
        key.pipeline.blend = blendKey(draw.blend);
        key.pipeline.depth = depthKey(draw.depth);
        key.pipeline.depthBias = draw.depthBias.enabled;
        key.pipeline.topology = unsigned(draw.topology);
        key.pipeline.colorFormat = unsigned(draw.colorFormat);
        key.pipeline.cull = unsigned(draw.cullFront) | (unsigned(draw.cullBack) << 1) | (unsigned(draw.frontFaceClockwise) << 2);
        key.width = draw.width;
        key.height = draw.height;
        key.overwriteTarget = overwriteTarget;
        key.depthWidth = draw.depth.test ? draw.depth.width : 0;
        key.depthHeight = draw.depth.test ? draw.depth.height : 0;
        key.depthPitch = draw.depth.test ? draw.depth.pitch : 0;
        key.pitch = draw.pitch;
        key.vertexCapacity = std::bit_ceil(draw.vertices.size());
        key.indexCapacity = draw.indices.empty() ? 0 : std::bit_ceil(draw.indices.size());
        std::uint64_t depthTransferBytes{}, privateTextureBytes{}, incomingSharedBytes{};
        for (const auto &texture: textures) {
            if (texture.snapshotOwner && !texture.snapshot.empty() && !texture.rendered)
                incomingSharedBytes += texture.bytes() * 2;
            else {
                privateTextureBytes += texture.bytes();
                if (texture.format == VK_FORMAT_R32_SFLOAT)
                    depthTransferBytes += texture.bytes();
            }
        }
        const auto estimatedBytes = (depthOnly(draw) ? 0 : std::uint64_t(draw.target.size()) * 3) + privateTextureBytes * 2 + depthTransferBytes +
                                    std::uint64_t(key.vertexCapacity) * sizeof(RasterDraw::Vertex) +
                                    std::uint64_t(key.indexCapacity) * sizeof(std::uint32_t) + sizeof(constants) +
                                    (draw.vertexStage.shader ? sizeof(vertexConstants) : 0) +
                                    (draw.depth.test ? draw.depth.target.size_bytes() * 3 : 0);
        for (const auto &texture: textures) {
            key.pipeline.bindings.push_back(texture.binding);
            key.textureShape.insert(key.textureShape.end(), {texture.width,
                                                             texture.height,
                                                             texture.layers,
                                                             unsigned(texture.array),
                                                             unsigned(texture.format),
                                                             unsigned(texture.components.r),
                                                             unsigned(texture.components.g),
                                                             unsigned(texture.components.b),
                                                             unsigned(texture.components.a),
                                                             unsigned(texture.addressU),
                                                             unsigned(texture.addressV),
                                                             unsigned(texture.filter),
                                                             unsigned(texture.volume),
                                                             unsigned(texture.addressW),
                                                             unsigned(texture.borderColor),
                                                             unsigned(texture.cube),
                                                             unsigned(texture.mips.size()),
                                                             unsigned(texture.minFilter),
                                                             unsigned(texture.mipFilter),
                                                             std::bit_cast<unsigned>(texture.minLod),
                                                             std::bit_cast<unsigned>(texture.maxLod),
                                                             unsigned(texture.snapshotOwner && !texture.snapshot.empty() && !texture.rendered)});
        }
        const auto t3 = std::chrono::steady_clock::now();
        RasterResult result;
        std::chrono::steady_clock::time_point t4, t5;
        bool reused = false;
        {
            auto it = m_impl->resources.lower_bound(key);
            // Reuse any free matching bundle before forcing a live surface onto
            // the CPU. Search past holes left by eviction before allocating.
            // chainSlot is last in the key so matching bundles are contiguous.
            static const bool preserve = [] {
                const auto *preserveImages = std::getenv("WEMU_NATIVE_PRESERVE_IMAGES");
                return !preserveImages || preserveImages[0] != '0';
            }();
            // Keeping every live output resident can allocate thousands of identical
            // bundles for small lighting passes. Retain a short version chain, then
            // materialize its oldest output and reuse the existing allocations.
            static const std::size_t maxLiveVersions = [] {
                const auto *value = std::getenv("WEMU_NATIVE_LIVE_VERSIONS");
                return value ? std::max<std::size_t>(1, std::strtoull(value, nullptr, 10)) : std::size_t{8};
            }();
            const auto ownsInput = [&](const auto &resources) {
                // Sampled aliases already copy before attachment writes. The
                // attachment inputs themselves need a separate destination.
                return (targetSource && resources.ownsImage(targetSource)) || (depthSource && resources.ownsImage(depthSource));
            };
            auto recyclable = m_impl->resources.end(), busy = m_impl->resources.end();
            unsigned freeSlot{}, matching{};
            for (; it != m_impl->resources.end(); ++it) {
                if (!it->first.sameDraw(key)) {
                    it = m_impl->resources.end();
                    break;
                }
                ++matching;
                if (!ownsInput(*it->second.value)) {
                    if (!preserve || !it->second.value->needsReadbackPreservation()) {
                        // Refreshing a bundle the GPU still executes would stall the CPU on its
                        // fence; keep looking for an idle one and grow the chain instead.
                        if (it->second.value->idle())
                            break;
                        if (busy == m_impl->resources.end())
                            busy = it;
                    } else if (recyclable == m_impl->resources.end() || it->second.used < recyclable->second.used)
                        recyclable = it;
                }
                if (it->first.chainSlot == freeSlot)
                    ++freeSlot;
            }
            if (it == m_impl->resources.end() && busy != m_impl->resources.end() && matching >= maxLiveVersions)
                it = busy;
            if (it == m_impl->resources.end() && matching >= maxLiveVersions && recyclable != m_impl->resources.end())
                it = recyclable; // refresh() saves outstanding versions before overwriting.
            key.chainSlot = freeSlot;
            static const std::size_t maxEntries = [] {
                const auto *value = std::getenv("WEMU_NATIVE_CACHE_ENTRIES");
                return value ? std::max<std::size_t>(16, std::strtoull(value, nullptr, 10)) : std::size_t{384};
            }();
            static const std::uint64_t budget = [] {
                const auto *value = std::getenv("WEMU_NATIVE_CACHE_MB");
                return (value ? std::max<std::uint64_t>(128, std::strtoull(value, nullptr, 10)) : 2560ull) * 1024 * 1024;
            }();
            // Count each shared image once, including images evicted from the
            // texture cache but still held by draw bundles. Reserve a worst-case
            // upload for the incoming snapshots on both allocation and refresh.
            while (!m_impl->resources.empty() && ((it == m_impl->resources.end() && m_impl->resources.size() >= maxEntries) ||
                                                  m_impl->cachedBytes + (it == m_impl->resources.end() ? estimatedBytes : 0) + incomingSharedBytes +
                                                                  m_impl->context->textureMemory->bytes.load(std::memory_order_relaxed) >
                                                          budget)) {
                auto oldest = m_impl->resources.end();
                for (auto candidate = m_impl->resources.begin(); candidate != m_impl->resources.end(); ++candidate)
                    if (candidate != it && (oldest == m_impl->resources.end() || candidate->second.used < oldest->second.used))
                        oldest = candidate;
                if (oldest == m_impl->resources.end())
                    break;
                if (timing)
                    Utils::Log::error("[NATIVE_CACHE_EVICT] entries={} bytes={} incoming={} shared_bytes={} victim_bytes={}",
                                      m_impl->resources.size(), m_impl->cachedBytes, estimatedBytes,
                                      m_impl->context->textureMemory->bytes.load(std::memory_order_relaxed), oldest->second.bytes);
                oldest->second.value->finishPending();
                m_impl->cachedBytes -= oldest->second.bytes;
                m_impl->resources.erase(oldest);
            }
            if (it == m_impl->resources.end()) {
                auto resources = std::make_shared<DrawResources>(m_impl->context);
                resources->initialize(*vertex, *fragment, constants, vertexConstants, constantFloats, vertexConstantFloats, textures, draw, targetSource,
                                      depthSource, overwriteTarget);
                it = m_impl->resources.emplace(std::move(key), Impl::ResourceEntry{std::move(resources), estimatedBytes, ++m_impl->resourceClock})
                             .first;
                m_impl->cachedBytes += estimatedBytes;
            } else {
                reused = true;
                ++m_impl->resourceHits;
                it->second.used = ++m_impl->resourceClock;
                const auto oldTextures = it->second.value->retainedTextures;
                const auto oldBytes = it->second.value->retainedTextureBytes;
                if (it->second.value->refresh(constants, vertexConstants, constantFloats, vertexConstantFloats, textures, draw, m_impl->retainTextures,
                                              targetSource, depthSource))
                    ++m_impl->retainedTargets;
                m_impl->retainedTextures += it->second.value->retainedTextures - oldTextures;
                m_impl->retainedTextureBytes += it->second.value->retainedTextureBytes - oldBytes;
            }
            // Render into the surface's current image instead of copying it into this bundle's
            // image first (a full-target copy per draw otherwise). Excluded when the draw also
            // samples that image, which would turn the attachment into a feedback loop.
            static const bool adoptTargets = [] {
                const auto *value = std::getenv("WEMU_NATIVE_ADOPT_TARGETS");
                return !value || value[0] != '0';
            }();
            auto &bundle = *it->second.value;
            if (adoptTargets && draw.supersedesTarget && targetSource && targetSnapshot && !overwriteTarget && !depthOnly(draw) && bundle.color() &&
                bundle.color() != targetSource &&
                std::none_of(textures.begin(), textures.end(), [&](const auto &texture) { return texture.rendered == targetSource; })) {
                DrawResources *owner = nullptr;
                for (auto &[ownerKey, entry]: m_impl->resources)
                    if (entry.value->color() == targetSource) {
                        owner = entry.value.get();
                        break;
                    }
                bundle.adopt(targetSource, owner);
                ++m_impl->adoptedTargets;
            }
            t4 = std::chrono::steady_clock::now();
            // Fragment/alpha counters are not measured by this backend yet.
            result = it->second.value->execute(draw, deferReadback, targetSource, m_impl->lazyReadback, targetSnapshot, depthSource);
            t5 = std::chrono::steady_clock::now();
        }
        if (targetSource)
            ++m_impl->residentTargetCopies;
        if (timing) {
            const auto micros = [](auto begin, auto end) { return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count(); };
            const auto region = targetSource ? VkRect2D{{0, 0}, {draw.width, draw.height}} : readbackRegion(draw);
            Utils::Log::error("[NATIVE_TIMING] target={}x{} textures={} vertices={} reused={} cached={} hits={} retained_targets={} "
                              "shader_us={} context_us={} texture_us={} setup_us={} execute_us={} teardown_us={} readback_bytes={} "
                              "retained_textures={} retained_texture_bytes={} deferred={} resident_texture_copies={} resident_target_copies={} "
                              "readback_transfers={} direct_texture_bindings={} direct_texture_bytes={} target_upload_transfers={} "
                              "target_copy_transfers={} full_overwrite_draws={} queue_submissions={} submitted_commands={} submission_waits={} "
                              "shared_texture_uploads={} shared_texture_hits={} shared_texture_bytes={}",
                              draw.width, draw.height, textures.size(), draw.vertices.size(), reused, m_impl->resources.size(), m_impl->resourceHits,
                              m_impl->retainedTargets, micros(t0, t1), micros(t1, t2), micros(t2, t3), micros(t3, t4), micros(t4, t5),
                              micros(t5, std::chrono::steady_clock::now()),
                              m_impl->lazyReadback && deferReadback
                                      ? 0
                                      : std::uint64_t(region.extent.width) * region.extent.height * colorElementBytes(draw.colorFormat),
                              m_impl->retainedTextures, m_impl->retainedTextureBytes, deferReadback, residentTextureCopies(),
                              m_impl->residentTargetCopies, readbackTransfers(), directTextureBindings(), directTextureBytes(),
                              m_impl->context->targetUploadTransfers, m_impl->context->targetCopyTransfers, m_impl->context->fullOverwriteDraws,
                              queueSubmissions(), submittedCommandBuffers(), submissionWaits(), sharedTextureUploads(), sharedTextureHits(),
                              sharedTextureBytes());
        }
        ++m_impl->completed;
        return result;
    }
} // namespace Core::Gfx
