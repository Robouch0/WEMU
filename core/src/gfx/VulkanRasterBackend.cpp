#include "gfx/VulkanRasterBackend.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "gfx/SpirvCompiler.hpp"
#include "utils/Logger.hpp"


namespace Core::Gfx {
    namespace {
        void check(VkResult result, const char *operation)
        {
            if (result != VK_SUCCESS)
                throw std::runtime_error(std::string(operation) + ": Vulkan result " + std::to_string(result));
        }
        VkRect2D clippedScissor(const RasterDraw &draw)
        {
            const auto left = std::clamp<std::int64_t>(draw.scissor[0], 0, draw.width);
            const auto top = std::clamp<std::int64_t>(draw.scissor[1], 0, draw.height);
            const auto right = std::clamp<std::int64_t>(std::int64_t(draw.scissor[0]) + draw.scissor[2], left, draw.width);
            const auto bottom = std::clamp<std::int64_t>(std::int64_t(draw.scissor[1]) + draw.scissor[3], top, draw.height);
            return {{int(left), int(top)}, {unsigned(right - left), unsigned(bottom - top)}};
        }

        VkRect2D readbackRegion(const RasterDraw &draw)
        {
            auto region = clippedScissor(draw);
            if (!draw.vertices.empty()) {
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
        std::array<unsigned, 11> blendKey(const RasterDraw::Blend &blend)
        {
            return {unsigned(blend.enabled), blend.colorSource,      blend.colorDestination, blend.colorOperation,
                    blend.alphaSource,       blend.alphaDestination, blend.alphaOperation,   blend.constant[0],
                    blend.constant[1],       blend.constant[2],      blend.constant[3]};
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
        struct Context {
                struct PipelineKey {
                        std::vector<std::uint32_t> vertex, fragment, bindings;
                        unsigned width{}, height{}, channelMask{};
                        std::array<std::int32_t, 4> scissor{};
                        std::array<unsigned, 11> blend{};
                        std::array<unsigned, 4> depth{};
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
                std::uint64_t readbackTransfers{};
                VkDescriptorSetLayout depthQuantizeSetLayout{};
                VkPipelineLayout depthQuantizeLayout{};
                VkPipeline depthQuantizePipeline{};
                bool computeQueue{};
                std::map<PipelineKey, PipelineBundle> pipelines;
                void clearPipelines()
                {
                    if (device)
                        vkDeviceWaitIdle(device);
                    for (const auto &[key, bundle]: pipelines) {
                        vkDestroyPipeline(device, bundle.pipeline, nullptr);
                        vkDestroyPipelineLayout(device, bundle.layout, nullptr);
                        vkDestroyDescriptorSetLayout(device, bundle.setLayout, nullptr);
                    }
                    pipelines.clear();
                }
                ~Context()
                {
                    if (device)
                        vkDeviceWaitIdle(device);
                    if (device)
                        clearPipelines();
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
                        const int score = candidateProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                                          : candidateProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                          : candidateProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 0
                                                                                                                : 1;
                        for (unsigned i = 0; i < n; ++i) {
                            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                                if (score > bestScore) {
                                    physical = candidate;
                                    family = i;
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
                    Utils::Log::error("[NATIVE_VULKAN] device={}", deviceName);
                    const float priority = 1.0f;
                    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                    qi.queueFamilyIndex = family;
                    qi.queueCount = 1;
                    qi.pQueuePriorities = &priority;
                    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
                    di.queueCreateInfoCount = 1;
                    di.pQueueCreateInfos = &qi;
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
    int exponent=0;
    uint remainder=value;
    while(remainder<maximum) { remainder<<=1; --exponent; }
    remainder-=maximum;
    uint fraction=0u;
    for(int bit=22;bit>=0;--bit) {
        remainder<<=1;
        if(remainder>=maximum) { remainder-=maximum; fraction|=1u<<bit; }
    }
    if((remainder<<1)>maximum) ++fraction;
    if(fraction==0x800000u) { fraction=0u; ++exponent; }
    return uintBitsToFloat((uint(exponent+127)<<23)|fraction);
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
                unsigned width{}, height{};
                std::uint64_t version{};
                VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
                DepthFormat depthFormat{DepthFormat::Float32};
                ~ColorImage()
                {
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
                        unsigned layers{1};
                        bool array{};
                        VkSamplerAddressMode addressU{}, addressV{};
                        VkFilter filter = VK_FILTER_NEAREST;
                        std::shared_ptr<ColorImage> rendered;

                        std::size_t rowBytes() const
                        {
                            const unsigned bytes = format == VK_FORMAT_R8_UNORM              ? 1
                                                   : format == VK_FORMAT_R8G8_UNORM          ? 2
                                                   : format == VK_FORMAT_R32G32B32A32_SFLOAT ? 16
                                                                                             : 4;
                            return std::size_t(width) * bytes;
                        }
                        std::size_t bytes() const { return rowBytes() * height * layers; }
                        bool matches(const void *previous) const
                        {
                            const auto *source = snapshot.empty() ? reinterpret_cast<const std::byte *>(rgba.data()) : snapshot.data();
                            const auto pitch = snapshot.empty() ? rowBytes() : sourcePitch;
                            for (unsigned y = 0; y < height * layers; ++y)
                                if (std::memcmp(static_cast<const std::byte *>(previous) + y * rowBytes(), source + y * pitch, rowBytes()) != 0)
                                    return false;
                            return true;
                        }
                        void copyTo(void *destination) const
                        {
                            if (snapshot.empty()) {
                                std::memcpy(destination, rgba.data(), bytes());
                            } else if (sourcePitch == rowBytes()) {
                                std::memcpy(destination, snapshot.data(), bytes());
                            } else {
                                for (unsigned y = 0; y < height * layers; ++y)
                                    std::memcpy(static_cast<std::byte *>(destination) + y * rowBytes(), snapshot.data() + y * sourcePitch,
                                                rowBytes());
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
                    if (inFlight)
                        vkWaitForFences(device, 1, &completion, VK_TRUE, UINT64_MAX);
                    if (completion)
                        vkDestroyFence(device, completion, nullptr);
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
                    if (framebuffer)
                        vkDestroyFramebuffer(device, framebuffer, nullptr);
                    if (pass)
                        vkDestroyRenderPass(device, pass, nullptr);
                    if (view)
                        vkDestroyImageView(device, view, nullptr);
                    if (depthView)
                        vkDestroyImageView(device, depthView, nullptr);
                    if (depthQuantizePool)
                        vkDestroyDescriptorPool(device, depthQuantizePool, nullptr);
                    for (auto sampler: samplers)
                        vkDestroySampler(device, sampler, nullptr);
                    for (auto textureView: textureViews)
                        vkDestroyImageView(device, textureView, nullptr);
                    for (auto textureImage: textureImages)
                        vkDestroyImage(device, textureImage, nullptr);
                    for (auto buffer: buffers)
                        vkDestroyBuffer(device, buffer, nullptr);
                    for (auto memory: memories)
                        vkFreeMemory(device, memory, nullptr);
                    if (pool)
                        vkDestroyCommandPool(device, pool, nullptr);
                }

                void initialize(const std::vector<std::uint32_t> &vertex, const std::vector<std::uint32_t> &fragment,
                                const std::array<float, 1152> &constants, const std::vector<Texture> &textures, const RasterDraw &draw,
                                const std::shared_ptr<ColorImage> &targetSource, const std::shared_ptr<ColorImage> &depthSource)
                {
                    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                    check(vkCreateFence(device, &fenceInfo, nullptr, &completion), "create submission fence");
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
                    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
                    ci.extent = {draw.width, draw.height, 1};
                    ci.mipLevels = ci.arrayLayers = 1;
                    ci.samples = VK_SAMPLE_COUNT_1_BIT;
                    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
                    ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                    colorImage = std::make_shared<ColorImage>();
                    colorImage->context = contextOwner;
                    colorImage->width = draw.width;
                    colorImage->height = draw.height;
                    check(vkCreateImage(device, &ci, nullptr, &colorImage->image), "create image");
                    image = colorImage->image;
                    VkMemoryRequirements req{};
                    vkGetImageMemoryRequirements(device, image, &req);
                    colorImage->memory = allocate(req, 0, 0, false);
                    check(vkBindImageMemory(device, image, colorImage->memory, 0), "bind image");
                    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    vi.image = image;
                    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    vi.format = ci.format;
                    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    check(vkCreateImageView(device, &vi, nullptr, &view), "create view");
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
                    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
                    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
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
                    VkAttachmentReference depthReference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                    VkSubpassDescription subpass{};
                    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                    subpass.colorAttachmentCount = 1;
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
                    ri.attachmentCount = draw.depth.test ? 2 : 1;
                    ri.pAttachments = attachments;
                    ri.subpassCount = 1;
                    ri.pSubpasses = &subpass;
                    ri.dependencyCount = 1;
                    ri.pDependencies = &dependency;
                    check(vkCreateRenderPass(device, &ri, nullptr, &pass), "create render pass");
                    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                    fi.renderPass = pass;
                    const VkImageView views[]{view, depthView};
                    fi.attachmentCount = ri.attachmentCount;
                    fi.pAttachments = views;
                    fi.width = draw.depth.test ? std::min(draw.width, draw.depth.width) : draw.width;
                    fi.height = draw.depth.test ? std::min(draw.height, draw.depth.height) : draw.height;
                    fi.layers = 1;
                    check(vkCreateFramebuffer(device, &fi, nullptr, &framebuffer), "create framebuffer");

                    uniform = buffer(sizeof(constants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniformMemory);
                    void *mapped = nullptr;
                    check(vkMapMemory(device, uniformMemory, 0, sizeof(constants), 0, &mapped), "map constants");
                    std::memcpy(mapped, constants.data(), sizeof(constants));
                    vkUnmapMemory(device, uniformMemory);
                    readback = buffer(draw.target.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, readbackMemory, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                    vertexBuffer = buffer(draw.vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexMemory);
                    upload(vertexMemory, draw.vertices.data(), draw.vertices.size_bytes());
                    targetBuffer = buffer(draw.target.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, targetMemory);
                    if (!targetSource)
                        upload(targetMemory, draw.target.data(), draw.target.size());
                    Context::PipelineKey key{vertex, fragment, {}, draw.width, draw.height, draw.channelMask, draw.scissor};
                    key.blend = blendKey(draw.blend);
                    key.depth = depthKey(draw.depth);
                    for (const auto &texture: textures)
                        key.bindings.push_back(texture.binding);
                    if (const auto it = context.pipelines.find(key); it != context.pipelines.end()) {
                        setLayout = it->second.setLayout;
                        layout = it->second.layout;
                        pipeline = it->second.pipeline;
                        cachedPipeline = true;
                    }
                    if (!cachedPipeline) {
                        std::vector<VkDescriptorSetLayoutBinding> bindings{
                                {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
                        for (const auto &texture: textures)
                            bindings.push_back(
                                    {texture.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
                        VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                        si.bindingCount = bindings.size();
                        si.pBindings = bindings.data();
                        check(vkCreateDescriptorSetLayout(device, &si, nullptr, &setLayout), "create descriptor layout");
                    }
                    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
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
                    for (const auto &texture: textures)
                        uploadTexture(texture);

                    if (!cachedPipeline) {
                        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 3};
                        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                        li.setLayoutCount = li.pushConstantRangeCount = 1;
                        li.pSetLayouts = &setLayout;
                        li.pPushConstantRanges = &push;
                        check(vkCreatePipelineLayout(device, &li, nullptr, &layout), "create pipeline layout");
                        VkPipelineShaderStageCreateInfo stages[2]{};
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto &code = i ? fragment : vertex;
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
                        VkVertexInputAttributeDescription attributes[6]{};
                        attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
                        for (unsigned i = 0; i < 4; ++i)
                            attributes[i + 1] = {i + 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 unsigned(offsetof(RasterDraw::Vertex, inputs) + i * sizeof(float) * 4)};
                        attributes[5] = {5, 0, VK_FORMAT_R32G32_SFLOAT, unsigned(offsetof(RasterDraw::Vertex, z))};
                        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
                        input.vertexBindingDescriptionCount = 1;
                        input.pVertexBindingDescriptions = &vertexBinding;
                        input.vertexAttributeDescriptionCount = 6;
                        input.pVertexAttributeDescriptions = attributes;
                        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
                        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                        VkViewport viewport{0, 0, float(draw.width), float(draw.height), 0, 1};
                        const auto scissor = clippedScissor(draw);
                        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
                        viewportState.viewportCount = viewportState.scissorCount = 1;
                        viewportState.pViewports = &viewport;
                        viewportState.pScissors = &scissor;
                        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
                        raster.polygonMode = VK_POLYGON_MODE_FILL;
                        raster.lineWidth = 1;
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
                            blend.colorBlendOp = VK_BLEND_OP_ADD;
                            blend.srcAlphaBlendFactor = *alphaSource;
                            blend.dstAlphaBlendFactor = *alphaDestination;
                            blend.alphaBlendOp = VK_BLEND_OP_ADD;
                        }
                        VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
                        blending.attachmentCount = 1;
                        blending.pAttachments = &blend;
                        for (unsigned i = 0; i < 4; ++i)
                            blending.blendConstants[i] = float(draw.blend.constant[i]) / 255.0f;
                        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                        gp.stageCount = 2;
                        gp.pStages = stages;
                        gp.pVertexInputState = &input;
                        gp.pInputAssemblyState = &assembly;
                        gp.pViewportState = &viewportState;
                        gp.pRasterizationState = &raster;
                        gp.pMultisampleState = &samples;
                        gp.pColorBlendState = &blending;
                        VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
                        depthState.depthTestEnable = draw.depth.test;
                        depthState.depthWriteEnable = draw.depth.test && draw.depth.write;
                        depthState.depthCompareOp = VkCompareOp(draw.depth.compare);
                        gp.pDepthStencilState = &depthState;
                        gp.layout = layout;
                        gp.renderPass = pass;
                        check(vkCreateGraphicsPipelines(device, pipelineCache, 1, &gp, nullptr, &pipeline), "create pipeline");
                        if (context.pipelines.size() >= 128)
                            context.clearPipelines();
                        context.pipelines.emplace(std::move(key), Context::PipelineBundle{setLayout, layout, pipeline});
                        cachedPipeline = true;
                    }
                }

                bool ownsImage(const std::shared_ptr<ColorImage> &candidate) const { return colorImage == candidate || depthImage == candidate; }

                RasterResult execute(const RasterDraw &draw, bool deferReadback, const std::shared_ptr<ColorImage> &targetSource,
                                     bool allowLazyReadback, const std::shared_ptr<const NativeImage> &targetSnapshot,
                                     const std::shared_ptr<ColorImage> &depthSource)
                {
                    const bool lazyReadback = allowLazyReadback && deferReadback;
                    // Padding is not represented by VkImage. Preserve it independently
                    // of active pixels, and share it unchanged through target chains.
                    std::shared_ptr<const std::vector<std::uint8_t>> padding;
                    if (targetSnapshot) {
                        padding = targetSnapshot->padding;
                    } else if (draw.pitch > draw.width) {
                        const auto rowBytes = std::size_t(draw.pitch - draw.width) * 4;
                        auto snapshot = std::make_shared<std::vector<std::uint8_t>>(rowBytes * draw.height);
                        for (unsigned y = 0; y < draw.height; ++y)
                            std::memcpy(snapshot->data() + y * rowBytes, draw.target.data() + (std::size_t(y) * draw.pitch + draw.width) * 4,
                                        rowBytes);
                        padding = std::move(snapshot);
                    }
                    const auto region = targetSource ? VkRect2D{{0, 0}, {draw.width, draw.height}} : readbackRegion(draw);
                    const bool partial = region.extent.width != draw.width || region.extent.height != draw.height;
                    if (partial && !lazyReadback) {
                        // Keep a complete CPU snapshot for result assembly and exact target
                        // retention checks. Only the bounded draw region can change on GPU.
                        upload(readbackMemory, draw.target.data(), draw.target.size());
                        VkMemoryBarrier hostWrites{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        hostWrites.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                        hostWrites.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &hostWrites, 0, nullptr, 0,
                                             nullptr);
                    }
                    VkImageMemoryBarrier targetBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    targetBarrier.srcQueueFamilyIndex = targetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    targetBarrier.image = image;
                    targetBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    targetBarrier.oldLayout = submitted ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
                    if (!retainedTarget) {
                        targetBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        targetBarrier.srcAccessMask = submitted ? VK_ACCESS_TRANSFER_READ_BIT : 0;
                        targetBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        vkCmdPipelineBarrier(command, submitted ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &targetBarrier);
                        if (targetSource) {
                            VkImageCopy copy{};
                            copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                            copy.extent = {draw.width, draw.height, 1};
                            vkCmdCopyImage(command, targetSource->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                            sampledImages.push_back(targetSource);
                        } else {
                            VkBufferImageCopy targetCopy{};
                            targetCopy.bufferRowLength = draw.pitch;
                            targetCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                            targetCopy.imageExtent = {draw.width, draw.height, 1};
                            vkCmdCopyBufferToImage(command, targetBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &targetCopy);
                        }
                        targetBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    }
                    targetBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    targetBarrier.srcAccessMask = retainedTarget ? VK_ACCESS_MEMORY_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
                    targetBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    vkCmdPipelineBarrier(command, retainedTarget ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &targetBarrier);
                    if (draw.depth.test)
                        prepareDepth(draw.depth, depthSource);
                    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                    rp.renderPass = pass;
                    rp.framebuffer = framebuffer;
                    rp.renderArea.extent = {draw.depth.test ? std::min(draw.width, draw.depth.width) : draw.width,
                                            draw.depth.test ? std::min(draw.height, draw.depth.height) : draw.height};
                    vkCmdBeginRenderPass(command, &rp, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0, nullptr);
                    const float scale[]{2.0f / float(draw.width), 2.0f / float(draw.height), draw.depth.test ? 1.f : 0.f};
                    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(scale), scale);
                    const VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer, &offset);
                    vkCmdDraw(command, draw.vertices.size(), 1, 0, 0);
                    vkCmdEndRenderPass(command);
                    if (draw.depth.test && draw.depth.write && draw.depth.format != DepthFormat::Float32)
                        quantizeDepthImage(draw.depth);
                    if (draw.depth.test && draw.depth.write && !lazyReadback)
                        copyDepth(draw.depth.width, draw.depth.height, draw.depth.pitch);
                    VkBufferImageCopy copy{};
                    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    copy.imageOffset = {region.offset.x, region.offset.y, 0};
                    copy.imageExtent = {region.extent.width, region.extent.height, 1};
                    copy.bufferOffset = (VkDeviceSize(region.offset.y) * draw.pitch + region.offset.x) * 4;
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
                    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                    submit.commandBufferCount = 1;
                    submit.pCommandBuffers = &command;
                    check(vkResetFences(device, 1, &completion), "reset submission fence");
                    check(vkQueueSubmit(queue, 1, &submit, completion), "submit");
                    inFlight = true;
                    submitted = true;
                    readbackValid = !lazyReadback;
                    depthReadbackValid = !lazyReadback;
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
                    if (!deferReadback) {
                        pendingReadback.reset();
                        result.rgba = readPixels(draw.width, draw.height, draw.pitch, padding);
                        result.resolveDepth();
                        return result;
                    }
                    auto ticket = std::make_shared<RasterReadback>(
                            draw.target.size(),
                            [self = shared_from_this(), padding, width = draw.width, height = draw.height, pitch = draw.pitch] {
                                return self->readPixels(width, height, pitch, padding);
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

                void copyDepth(unsigned width, unsigned height, unsigned pitch)
                {
                    VkBufferImageCopy copy{};
                    copy.bufferRowLength = pitch;
                    copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
                    copy.imageExtent = {width, height, 1};
                    vkCmdCopyImageToBuffer(command, depthImage->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, depthReadback, 1, &copy);
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

                std::vector<std::uint8_t> readDepth(unsigned width, unsigned height, unsigned pitch,
                                                    const std::shared_ptr<const std::vector<std::uint8_t>> &padding)
                {
                    wait();
                    if (!depthReadbackValid) {
                        check(vkResetCommandPool(device, pool, 0), "reset depth readback commands");
                        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                        check(vkBeginCommandBuffer(command, &begin), "begin depth readback commands");
                        copyDepth(width, height, pitch);
                        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0,
                                             nullptr);
                        check(vkEndCommandBuffer(command), "end depth readback commands");
                        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                        submit.commandBufferCount = 1;
                        submit.pCommandBuffers = &command;
                        check(vkResetFences(device, 1, &completion), "reset depth readback fence");
                        check(vkQueueSubmit(queue, 1, &submit, completion), "submit depth readback");
                        inFlight = true;
                        wait();
                        depthReadbackValid = true;
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

                std::vector<std::uint8_t> readPixels(unsigned width, unsigned height, unsigned pitch,
                                                     const std::shared_ptr<const std::vector<std::uint8_t>> &padding)
                {
                    wait();
                    if (!readbackValid) {
                        // The image is still this ticket's version: reuse resolves live
                        // tickets before overwriting it. Discarded tickets need no transfer.
                        check(vkResetCommandPool(device, pool, 0), "reset readback commands");
                        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                        check(vkBeginCommandBuffer(command, &begin), "begin readback commands");
                        VkBufferImageCopy copy{};
                        copy.bufferRowLength = pitch;
                        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        copy.imageExtent = {width, height, 1};
                        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
                        ++context.readbackTransfers;
                        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0,
                                             nullptr);
                        check(vkEndCommandBuffer(command), "end readback commands");
                        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                        submit.commandBufferCount = 1;
                        submit.pCommandBuffers = &command;
                        check(vkResetFences(device, 1, &completion), "reset readback fence");
                        check(vkQueueSubmit(queue, 1, &submit, completion), "submit readback");
                        inFlight = true;
                        wait();
                        readbackValid = true;
                    }
                    std::vector<std::uint8_t> result(std::size_t(pitch) * height * 4);
                    void *mapped = nullptr;
                    check(vkMapMemory(device, readbackMemory, 0, result.size(), 0, &mapped), "map readback");
                    for (unsigned y = 0; y < height; ++y)
                        std::memcpy(result.data() + std::size_t(y) * pitch * 4,
                                    static_cast<const std::uint8_t *>(mapped) + std::size_t(y) * pitch * 4, std::size_t(width) * 4);
                    vkUnmapMemory(device, readbackMemory);
                    if (padding) {
                        const auto rowBytes = std::size_t(pitch - width) * 4;
                        for (unsigned y = 0; y < height; ++y)
                            std::memcpy(result.data() + (std::size_t(y) * pitch + width) * 4, padding->data() + y * rowBytes, rowBytes);
                    }
                    return result;
                }

                bool refresh(const std::array<float, 1152> &constants, const std::vector<Texture> &textures, const RasterDraw &draw,
                             bool retainTextures, const std::shared_ptr<ColorImage> &targetSource, const std::shared_ptr<ColorImage> &depthSource)
                {
                    finishPending();
                    check(vkResetCommandPool(device, pool, 0), "reset commands");
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    check(vkBeginCommandBuffer(command, &begin), "begin commands");
                    upload(uniformMemory, constants.data(), sizeof(constants));
                    upload(vertexMemory, draw.vertices.data(), draw.vertices.size_bytes());
                    retainedTarget = targetSource ? targetSource == colorImage : targetMatchesReadback(draw);
                    if (!targetSource && !retainedTarget)
                        upload(targetMemory, draw.target.data(), draw.target.size());
                    if (draw.depth.test && !depthSource)
                        uploadDepth(draw.depth);
                    for (std::size_t i = 0; i < textures.size(); ++i) {
                        const auto &texture = textures[i];
                        if (texture.rendered) {
                            recordTextureUpload(textureImages[i], textureStagingBuffers[i], texture.width, texture.height, false, texture.rendered,
                                                texture.layers);
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
                        recordTextureUpload(textureImages[i], textureStagingBuffers[i], texture.width, texture.height, false, {}, texture.layers);
                    }
                    return retainedTarget;
                }

            private:
                void wait()
                {
                    if (inFlight) {
                        check(vkWaitForFences(device, 1, &completion, VK_TRUE, UINT64_MAX), "wait readback submission");
                        inFlight = false;
                    }
                    sampledImages.clear();
                }
                bool targetMatchesReadback(const RasterDraw &draw)
                {
                    if (!submitted || !readbackValid)
                        return false;
                    void *mapped = nullptr;
                    check(vkMapMemory(device, readbackMemory, 0, draw.target.size(), 0, &mapped), "map target comparison");
                    bool equal = true;
                    for (unsigned y = 0; y < draw.height; ++y) {
                        const auto offset = std::size_t(y) * draw.pitch * 4;
                        if (std::memcmp(draw.target.data() + offset, static_cast<const std::uint8_t *>(mapped) + offset,
                                        std::size_t(draw.width) * 4) != 0) {
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
                void uploadTexture(const Texture &texture)
                {
                    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                    ci.imageType = VK_IMAGE_TYPE_2D;
                    ci.format = texture.format;
                    ci.extent = {texture.width, texture.height, 1};
                    ci.mipLevels = 1;
                    ci.arrayLayers = texture.layers;
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
                    vi.viewType = texture.array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
                    vi.format = ci.format;
                    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, texture.layers};
                    vi.components = texture.components;
                    VkImageView textureView{};
                    check(vkCreateImageView(device, &vi, nullptr, &textureView), "create texture view");
                    textureViews.push_back(textureView);
                    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                    si.magFilter = si.minFilter = texture.filter;
                    si.addressModeU = texture.addressU;
                    si.addressModeV = texture.addressV;
                    VkSampler sampler{};
                    check(vkCreateSampler(device, &si, nullptr, &sampler), "create sampler");
                    samplers.push_back(sampler);
                    VkDeviceMemory stagingMemory{};
                    const auto staging = buffer(texture.bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stagingMemory, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                    textureStagingBuffers.push_back(staging);
                    textureStagingMemories.push_back(stagingMemory);
                    textureStagingValid.push_back(!texture.rendered);
                    if (!texture.rendered)
                        uploadTextureData(stagingMemory, texture);
                    recordTextureUpload(textureImage, staging, texture.width, texture.height, true, texture.rendered, texture.layers);
                    VkDescriptorImageInfo info{sampler, textureView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet = descriptor;
                    write.dstBinding = texture.binding;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.pImageInfo = &info;
                    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                }
                void recordTextureUpload(VkImage textureImage, VkBuffer staging, unsigned width, unsigned height, bool initial,
                                         const std::shared_ptr<ColorImage> &source = {}, unsigned layers = 1)
                {
                    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = textureImage;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
                    barrier.oldLayout = initial ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcAccessMask = initial ? 0 : VK_ACCESS_SHADER_READ_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    vkCmdPipelineBarrier(command, initial ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                    if (source) {
                        VkImageCopy copy{};
                        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        copy.extent = {width, height, 1};
                        vkCmdCopyImage(command, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, textureImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                        sampledImages.push_back(source);
                    } else {
                        VkBufferImageCopy copy{};
                        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
                        copy.imageExtent = {width, height, 1};
                        vkCmdCopyBufferToImage(command, staging, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                    }
                    barrier.oldLayout = barrier.newLayout;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
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
                                bool deviceOnly = false)
                {
                    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                    bi.size = bytes;
                    bi.usage = usage;
                    VkBuffer result{};
                    check(vkCreateBuffer(device, &bi, nullptr, &result), "create buffer");
                    buffers.push_back(result);
                    VkMemoryRequirements req{};
                    vkGetBufferMemoryRequirements(device, result, &req);
                    memory = allocate(req, deviceOnly ? 0 : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      deviceOnly ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : preferred);
                    check(vkBindBufferMemory(device, result, memory, 0), "bind buffer");
                    return result;
                }

                std::shared_ptr<Context> contextOwner;
                std::shared_ptr<ColorImage> colorImage;
                std::shared_ptr<ColorImage> depthImage;
                std::vector<std::shared_ptr<ColorImage>> sampledImages;
                Context &context;
                std::weak_ptr<RasterReadback> pendingReadback;
                std::weak_ptr<RasterReadback> pendingDepthReadback;
                bool inFlight{};
                bool readbackValid{};
                bool depthReadbackValid{};
                VkFence completion{};
                bool cachedPipeline{};
                VkPhysicalDevice physical{};
                VkDevice device{};
                VkQueue queue{};
                VkCommandPool pool{};
                VkCommandBuffer command{};
                VkImage image{};
                VkImageView view{};
                VkImageView depthView{};
                VkRenderPass pass{};
                VkFramebuffer framebuffer{};
                VkDescriptorSetLayout setLayout{};
                VkDescriptorPool descriptorPool{};
                VkDescriptorSet descriptor{};
                VkPipelineLayout layout{};
                VkPipeline pipeline{};
                VkBuffer uniform{}, readback{}, vertexBuffer{}, targetBuffer{};
                VkDeviceMemory uniformMemory{}, vertexMemory{}, targetMemory{};
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
                std::vector<bool> textureStagingValid;
                std::vector<VkImageView> textureViews;
                std::vector<VkSampler> samplers;
                bool submitted{};
                bool retainedTarget{};

            public:
                std::uint64_t retainedTextures{};
                std::uint64_t retainedTextureBytes{};
        };

    } // namespace

    struct VulkanRasterBackend::Impl {
            struct ResourceKey {
                    Context::PipelineKey pipeline;
                    unsigned pitch{}, vertexCount{}, chainSlot{};
                    unsigned depthWidth{}, depthHeight{}, depthPitch{};
                    std::vector<std::uint32_t> textureShape;
                    auto operator<=>(const ResourceKey &) const = default;
            };
            SpirvCompiler compiler;
            std::shared_ptr<Context> context;
            std::map<ResourceKey, std::shared_ptr<DrawResources>> resources;
            std::uint64_t resourceHits{};
            std::uint64_t residentTextureCopies{};
            std::uint64_t residentTargetCopies{};
            std::uint64_t retainedTargets{};
            std::uint64_t retainedTextures{};
            std::uint64_t retainedTextureBytes{};
            std::uint64_t cachedBytes{};
            std::string error;
            std::uint64_t completed{};
            bool retainTextures = true;
            bool lazyReadback = false;
    };

    VulkanRasterBackend::VulkanRasterBackend(bool retainTextures, bool lazyReadback) : m_impl(std::make_unique<Impl>())
    {
        m_impl->retainTextures = retainTextures;
        m_impl->lazyReadback = lazyReadback;
    }
    VulkanRasterBackend::~VulkanRasterBackend() = default;
    const std::string &VulkanRasterBackend::lastError() const noexcept { return m_impl->error; }
    std::uint64_t VulkanRasterBackend::completedDraws() const noexcept { return m_impl->completed; }
    std::uint64_t VulkanRasterBackend::reusedDraws() const noexcept { return m_impl->resourceHits; }
    std::uint64_t VulkanRasterBackend::retainedTargetDraws() const noexcept { return m_impl->retainedTargets; }
    std::uint64_t VulkanRasterBackend::retainedTextureUploads() const noexcept { return m_impl->retainedTextures; }
    std::uint64_t VulkanRasterBackend::residentTextureCopies() const noexcept { return m_impl->residentTextureCopies; }
    std::uint64_t VulkanRasterBackend::residentTargetCopies() const noexcept { return m_impl->residentTargetCopies; }
    std::uint64_t VulkanRasterBackend::readbackTransfers() const noexcept { return m_impl->context ? m_impl->context->readbackTransfers : 0; }

    std::optional<RasterResult> VulkanRasterBackend::render(const RasterDraw &draw) { return renderImpl(draw, false); }

    std::optional<RasterResult> VulkanRasterBackend::renderDeferred(const RasterDraw &draw) { return renderImpl(draw, true); }

    std::optional<RasterResult> VulkanRasterBackend::renderImpl(const RasterDraw &inputDraw, bool deferReadback)
    {
        auto draw = inputDraw;
        m_impl->error.clear();
        const auto reject = [&](const char *reason) -> std::optional<RasterResult> {
            m_impl->error = reason;
            return std::nullopt;
        };
        if (!draw.shader || draw.channelMask > 15)
            return reject("Unsupported shader or blend state");
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
        if (draw.blend.enabled &&
            (draw.blend.colorOperation || draw.blend.alphaOperation || !blendFactor(draw.blend.colorSource) ||
             !blendFactor(draw.blend.colorDestination) || !blendFactor(draw.blend.alphaSource) || !blendFactor(draw.blend.alphaDestination)))
            return reject("Unsupported blend factors or equation");
        if (!draw.width || !draw.height || draw.width > 4096 || draw.height > 4096 || draw.pitch < draw.width ||
            std::uint64_t(draw.pitch) * draw.height * 4 != draw.target.size() || draw.target.size() > std::size_t{64} * 1024 * 1024)
            return reject("Unsupported target dimensions");
        if (draw.vertices.size() % 3 || draw.vertices.size() > 262144 || draw.constants.size() > 1024 ||
            draw.textures.size() != draw.shader.textures.size() || draw.textures.size() > 16)
            return reject("Unsupported draw inputs");
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
            const auto found = std::find_if(draw.shader.textures.begin(), draw.shader.textures.end(), [&](const auto &expected) {
                return expected.binding == binding.binding && expected.resource == binding.resource && expected.sampler == binding.sampler &&
                       expected.type == binding.type;
            });
            if (!binding.binding || binding.binding > 16 || !bindings.insert(binding.binding).second || found == draw.shader.textures.end() ||
                !texture.texel || !texture.width || !texture.height || texture.width > 4096 || texture.height > 4096)
                return reject("Unsupported texture binding");
            const bool array = binding.type == Latte::TextureType::TwoDArray;
            if (!texture.layers || texture.layers > 256 || (!array && texture.layers != 1) ||
                (array &&
                 (texture.rendered || (texture.unorm8.empty() && texture.rgba32.empty() && texture.r32.empty() && texture.r32Bytes.empty()))))
                return reject("Unsupported array texture snapshot");
            if (texture.rendered &&
                (texture.unorm8Pitch < texture.width || std::uint64_t(texture.unorm8Pitch) * texture.height * 4 != texture.rendered->size()))
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
            if (!texture.r32Bytes.empty() &&
                (std::endian::native != std::endian::little || !texture.unorm8.empty() || !texture.rgba32.empty() || !texture.r32.empty() ||
                 texture.r32Pitch < texture.width || std::uint64_t(texture.r32Pitch) * texture.height * texture.layers * 4 > texture.r32Bytes.size()))
                return reject("Invalid little-endian R32 texture snapshot");
            const auto r = texture.sampler.regs[0];
            // Match the reference's base-level magnification filter. Other addressing
            // modes need separate border/mirror-clamp validation before acceleration.
            if ((r & 7) > 2 || ((r >> 3) & 7) > 2 || ((r >> 9) & 7) > 1)
                return reject("Unsupported sampler mode");
            textureBytes += std::uint64_t(texture.width) * texture.height * texture.layers *
                            (texture.rendered                                    ? 4
                             : !texture.unorm8.empty()                           ? texture.unorm8Channels
                             : !texture.r32.empty() || !texture.r32Bytes.empty() ? 4
                                                                                 : 16);
            if (textureBytes > std::uint64_t{128} * 1024 * 1024)
                return reject("Texture upload budget exceeded");
        }
        constexpr auto vertexSource = R"(#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec4 attributes[4];
layout(location=5) in vec2 depthAndW;
layout(location=0) out vec4 inputs[4];
layout(location=4) noperspective out float windowDepth;
layout(push_constant) uniform Viewport { vec2 scale; float depthEnabled; } viewport;
void main() {
    float w=1.0/depthAndW.y;
    gl_Position=vec4((position*viewport.scale-vec2(1.0))*w,viewport.depthEnabled!=0.0?depthAndW.x*w:0.0,w);
    for(int i=0;i<4;++i) inputs[i]=attributes[i];
    windowDepth=depthAndW.x;
}
)";
        static const bool timing = [] {
            const auto *value = std::getenv("WEMU_NATIVE_TIMING");
            return value && value[0] == '1';
        }();
        const auto t0 = std::chrono::steady_clock::now();
        const auto vertex = m_impl->compiler.compile(ShaderStage::Vertex, vertexSource);
        auto fragmentSource = draw.shader.source;
        if (draw.depth.test) {
            fragmentSource.insert(fragmentSource.find('\n') + 1, "layout(location=4) noperspective in float windowDepth;\n");
            const auto main = fragmentSource.find("void main() {");
            if (main == std::string::npos)
                return reject("Depth specialization requires the shared fragment ABI");
            if (draw.shader.writesDepth) {
                const auto initial = fragmentSource.find("gl_FragDepth=gl_FragCoord.z;");
                if (initial != std::string::npos)
                    fragmentSource.replace(initial, std::string("gl_FragDepth=gl_FragCoord.z;").size(), "gl_FragDepth=windowDepth;");
            } else
                fragmentSource.insert(main + std::string("void main() {").size(), "\ngl_FragDepth=windowDepth;\n");
        }
        const auto fragment = m_impl->compiler.compile(ShaderStage::Fragment, fragmentSource);
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
                native->storage->format == VK_FORMAT_R8G8B8A8_UNORM) {
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
                    native->storage->format == VK_FORMAT_R8G8B8A8_UNORM)
                    resident = native->storage;
                texture.unorm8 = resident ? std::span<const std::uint8_t>{} : texture.rendered->resolve();
                texture.unorm8Channels = 4;
                texture.r32 = {};
                texture.r32Bytes = {};
                texture.rgba32 = {};
            }
            const auto textureFormat = resident                                            ? VK_FORMAT_R8G8B8A8_UNORM
                                       : !texture.r32.empty() || !texture.r32Bytes.empty() ? VK_FORMAT_R32_SFLOAT
                                       : texture.unorm8.empty()                            ? VK_FORMAT_R32G32B32A32_SFLOAT
                                       : texture.unorm8Channels == 1                       ? VK_FORMAT_R8_UNORM
                                       : texture.unorm8Channels == 2                       ? VK_FORMAT_R8G8_UNORM
                                                                                           : VK_FORMAT_R8G8B8A8_UNORM;
            VkFormatProperties format{};
            vkGetPhysicalDeviceFormatProperties(m_impl->context->physical, textureFormat, &format);
            const auto r = texture.sampler.regs[0];
            const bool linear = ((r >> 9) & 7) == 1;
            if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
                (linear && !(format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)))
                return reject("Host cannot sample float texture with requested filter");
            constexpr VkSamplerAddressMode address[]{VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
                                                     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
            DrawResources::Texture uploaded;
            uploaded.format = textureFormat;
            uploaded.binding = texture.binding.binding;
            uploaded.width = texture.width;
            uploaded.height = texture.height;
            uploaded.layers = texture.layers;
            uploaded.array = texture.binding.type == Latte::TextureType::TwoDArray;
            uploaded.addressU = address[r & 7];
            uploaded.addressV = address[(r >> 3) & 7];
            uploaded.filter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            uploaded.rendered = resident;
            if (!texture.r32.empty() || !texture.r32Bytes.empty()) {
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
        std::array<float, 1152> constants{};
        std::copy(draw.constants.begin(), draw.constants.end(), constants.begin());
        for (const auto &texture: draw.textures) {
            const auto border = texture.sampler.border();
            std::copy(border.begin(), border.end(), constants.data() + 1024 + std::size_t(texture.binding.binding - 1) * 4);
            const auto offset = 1088 + (texture.binding.binding - 1) * 4;
            const auto sampler = texture.sampler.regs[0];
            constants[offset] = float(sampler & 7);
            constants[offset + 1] = float((sampler >> 3) & 7);
            constants[offset + 2] = float((sampler >> 9) & 7);
        }
        Impl::ResourceKey key;
        key.pipeline = {vertex->words, fragment->words, {}, draw.width, draw.height, draw.channelMask, draw.scissor};
        key.pipeline.blend = blendKey(draw.blend);
        key.pipeline.depth = depthKey(draw.depth);
        key.depthWidth = draw.depth.test ? draw.depth.width : 0;
        key.depthHeight = draw.depth.test ? draw.depth.height : 0;
        key.depthPitch = draw.depth.test ? draw.depth.pitch : 0;
        key.pitch = draw.pitch;
        key.vertexCount = draw.vertices.size();
        const auto estimatedBytes = std::uint64_t(draw.target.size()) * 3 + textureBytes * 2 + draw.vertices.size_bytes() + sizeof(constants) +
                                    (draw.depth.test ? draw.depth.target.size_bytes() * 3 : 0);
        for (const auto &texture: textures) {
            key.pipeline.bindings.push_back(texture.binding);
            key.textureShape.insert(key.textureShape.end(), {texture.width, texture.height, texture.layers, unsigned(texture.array),
                                                             unsigned(texture.format), unsigned(texture.components.r), unsigned(texture.components.g),
                                                             unsigned(texture.components.b), unsigned(texture.components.a),
                                                             unsigned(texture.addressU), unsigned(texture.addressV), unsigned(texture.filter)});
        }
        const auto t3 = std::chrono::steady_clock::now();
        RasterResult result;
        std::chrono::steady_clock::time_point t4, t5;
        bool reused = false;
        {
            auto it = m_impl->resources.find(key);
            // Do not overwrite the source version merely to reuse its bundle.
            // Alternating bundles permit a chain whose discarded outputs never
            // need CPU materialization, while externally held tickets stay valid.
            while (it != m_impl->resources.end() &&
                   ((targetSource && it->second->ownsImage(targetSource)) || (depthSource && it->second->ownsImage(depthSource)))) {
                ++key.chainSlot;
                it = m_impl->resources.find(key);
            }
            if (it == m_impl->resources.end()) {
                if (m_impl->resources.size() >= 32 || m_impl->cachedBytes + estimatedBytes > 384ull * 1024 * 1024 ||
                    m_impl->context->pipelines.size() >= 128) {
                    for (auto &[oldKey, resource]: m_impl->resources)
                        resource->finishPending();
                    m_impl->resources.clear();
                    m_impl->cachedBytes = 0;
                }
                auto resources = std::make_shared<DrawResources>(m_impl->context);
                resources->initialize(vertex->words, fragment->words, constants, textures, draw, targetSource, depthSource);
                it = m_impl->resources.emplace(std::move(key), std::move(resources)).first;
                m_impl->cachedBytes += estimatedBytes;
            } else {
                reused = true;
                ++m_impl->resourceHits;
                const auto oldTextures = it->second->retainedTextures;
                const auto oldBytes = it->second->retainedTextureBytes;
                if (it->second->refresh(constants, textures, draw, m_impl->retainTextures, targetSource, depthSource))
                    ++m_impl->retainedTargets;
                m_impl->retainedTextures += it->second->retainedTextures - oldTextures;
                m_impl->retainedTextureBytes += it->second->retainedTextureBytes - oldBytes;
            }
            t4 = std::chrono::steady_clock::now();
            // Fragment/alpha counters are not measured by this backend yet.
            result = it->second->execute(draw, deferReadback, targetSource, m_impl->lazyReadback, targetSnapshot, depthSource);
            t5 = std::chrono::steady_clock::now();
        }
        m_impl->residentTextureCopies += std::count_if(textures.begin(), textures.end(), [](const auto &texture) { return bool(texture.rendered); });
        if (targetSource)
            ++m_impl->residentTargetCopies;
        if (timing) {
            const auto micros = [](auto begin, auto end) { return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count(); };
            const auto region = targetSource ? VkRect2D{{0, 0}, {draw.width, draw.height}} : readbackRegion(draw);
            Utils::Log::error("[NATIVE_TIMING] target={}x{} textures={} vertices={} reused={} cached={} hits={} retained_targets={} "
                              "shader_us={} context_us={} texture_us={} setup_us={} execute_us={} teardown_us={} readback_bytes={} "
                              "retained_textures={} retained_texture_bytes={} deferred={} resident_texture_copies={} resident_target_copies={} "
                              "readback_transfers={}",
                              draw.width, draw.height, textures.size(), draw.vertices.size(), reused, m_impl->resources.size(), m_impl->resourceHits,
                              m_impl->retainedTargets, micros(t0, t1), micros(t1, t2), micros(t2, t3), micros(t3, t4), micros(t4, t5),
                              micros(t5, std::chrono::steady_clock::now()),
                              m_impl->lazyReadback && deferReadback ? 0 : std::uint64_t(region.extent.width) * region.extent.height * 4,
                              m_impl->retainedTextures, m_impl->retainedTextureBytes, deferReadback, m_impl->residentTextureCopies,
                              m_impl->residentTargetCopies, readbackTransfers());
        }
        ++m_impl->completed;
        return result;
    }
} // namespace Core::Gfx
