#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

// Test-only synchronous float-target probe. No window, game data or renderer state.
class VulkanFragmentProbe {
    public:
        using Inputs = std::array<std::array<float, 4>, 4>;
        struct Texture {
                std::uint32_t binding;
                std::array<std::uint8_t, 16> rgba; // 2x2 RGBA8, one mip
                VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                VkFilter filter = VK_FILTER_NEAREST;
        };
        VulkanFragmentProbe() = default;
        VulkanFragmentProbe(const VulkanFragmentProbe &) = delete;
        VulkanFragmentProbe &operator=(const VulkanFragmentProbe &) = delete;
        ~VulkanFragmentProbe()
        {
            if (device)
                vkDeviceWaitIdle(device);
            if (pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
            if (layout)
                vkDestroyPipelineLayout(device, layout, nullptr);
            for (auto module: modules)
                vkDestroyShaderModule(device, module, nullptr);
            if (descriptorPool)
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (setLayout)
                vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
            if (framebuffer)
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            if (pass)
                vkDestroyRenderPass(device, pass, nullptr);
            if (view)
                vkDestroyImageView(device, view, nullptr);
            if (image)
                vkDestroyImage(device, image, nullptr);
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
            if (device)
                vkDestroyDevice(device, nullptr);
            if (instance)
                vkDestroyInstance(instance, nullptr);
        }

        void initialize(const std::vector<std::uint32_t> &vertex, const std::vector<std::uint32_t> &fragment,
                        const std::array<float, 1024> &constants, const std::vector<Texture> &textures = {})
        {
            VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            check(vkCreateInstance(&ii, nullptr, &instance), "create instance");
            std::uint32_t count = 0;
            check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate devices");
            std::vector<VkPhysicalDevice> devices(count);
            check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate devices");
            std::uint32_t family = 0;
            for (auto candidate: devices) {
                VkFormatProperties format{};
                vkGetPhysicalDeviceFormatProperties(candidate, VK_FORMAT_R32G32B32A32_SFLOAT, &format);
                if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
                    continue;
                std::uint32_t n = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
                std::vector<VkQueueFamilyProperties> queues(n);
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, queues.data());
                for (unsigned i = 0; i < n; ++i) {
                    if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                        physical = candidate;
                        family = i;
                        break;
                    }
                }
                if (physical)
                    break;
            }
            if (!physical)
                throw std::runtime_error("No Vulkan graphics device with float color target support");
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(physical, &props);
            deviceName = props.deviceName;
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
            VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pi.queueFamilyIndex = family;
            pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(device, &pi, nullptr, &pool), "create command pool");
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool = pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device, &ai, &command), "allocate commands");

            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            ci.extent = {4, 4, 1};
            ci.mipLevels = ci.arrayLayers = 1;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.tiling = VK_IMAGE_TILING_OPTIMAL;
            ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            check(vkCreateImage(device, &ci, nullptr, &image), "create image");
            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(device, image, &req);
            check(vkBindImageMemory(device, image, allocate(req, 0), 0), "bind image");
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = image;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = ci.format;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device, &vi, nullptr, &view), "create view");
            VkAttachmentDescription attachment{};
            attachment.format = ci.format;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &color;
            VkSubpassDependency dependency{};
            dependency.srcSubpass = 0;
            dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            ri.attachmentCount = 1;
            ri.pAttachments = &attachment;
            ri.subpassCount = 1;
            ri.pSubpasses = &subpass;
            ri.dependencyCount = 1;
            ri.pDependencies = &dependency;
            check(vkCreateRenderPass(device, &ri, nullptr, &pass), "create render pass");
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = pass;
            fi.attachmentCount = 1;
            fi.pAttachments = &view;
            fi.width = fi.height = 4;
            fi.layers = 1;
            check(vkCreateFramebuffer(device, &fi, nullptr, &framebuffer), "create framebuffer");

            VkDeviceMemory uniformMemory{};
            std::array<float, 1152> uniformData{};
            std::copy(constants.begin(), constants.end(), uniformData.begin());
            uniform = buffer(sizeof(uniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniformMemory);
            void *mapped = nullptr;
            check(vkMapMemory(device, uniformMemory, 0, sizeof(uniformData), 0, &mapped), "map constants");
            std::memcpy(mapped, uniformData.data(), sizeof(uniformData));
            vkUnmapMemory(device, uniformMemory);
            readback = buffer(sizeof(Inputs) * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readbackMemory);
            std::vector<VkDescriptorSetLayoutBinding> bindings{{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
            for (const auto &texture: textures)
                bindings.push_back({texture.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
            VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            si.bindingCount = bindings.size();
            si.pBindings = bindings.data();
            check(vkCreateDescriptorSetLayout(device, &si, nullptr, &setLayout), "create descriptor layout");
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
            VkDescriptorBufferInfo bi{uniform, 0, sizeof(uniformData)};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = descriptor;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &bi;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            for (const auto &texture: textures)
                uploadTexture(texture);

            VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Inputs)};
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
            VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport viewport{0, 0, 4, 4, 0, 1};
            VkRect2D scissor{{0, 0}, {4, 4}};
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
            blend.colorWriteMask = 15;
            VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blending.attachmentCount = 1;
            blending.pAttachments = &blend;
            VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            gp.stageCount = 2;
            gp.pStages = stages;
            gp.pVertexInputState = &input;
            gp.pInputAssemblyState = &assembly;
            gp.pViewportState = &viewportState;
            gp.pRasterizationState = &raster;
            gp.pMultisampleState = &samples;
            gp.pColorBlendState = &blending;
            gp.layout = layout;
            gp.renderPass = pass;
            check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline), "create pipeline");
        }

        std::array<std::array<float, 4>, 16> draw(const Inputs &inputs)
        {
            check(vkResetCommandBuffer(command, 0), "reset commands");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(vkBeginCommandBuffer(command, &begin), "begin commands");
            VkClearValue clear{};
            VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rp.renderPass = pass;
            rp.framebuffer = framebuffer;
            rp.renderArea.extent = {4, 4};
            rp.clearValueCount = 1;
            rp.pClearValues = &clear;
            vkCmdBeginRenderPass(command, &rp, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0, nullptr);
            vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(inputs), inputs.data());
            vkCmdDraw(command, 3, 1, 0, 0);
            vkCmdEndRenderPass(command);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {4, 4, 1};
            vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            check(vkEndCommandBuffer(command), "end commands");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "submit");
            check(vkQueueWaitIdle(queue), "wait readback");
            std::array<std::array<float, 4>, 16> result{};
            void *mapped = nullptr;
            check(vkMapMemory(device, readbackMemory, 0, sizeof(result), 0, &mapped), "map readback");
            std::memcpy(result.data(), mapped, sizeof(result));
            vkUnmapMemory(device, readbackMemory);
            return result;
        }

        std::string deviceName;

    private:
        void uploadTexture(const Texture &texture)
        {
            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R8G8B8A8_UNORM;
            ci.extent = {2, 2, 1};
            ci.mipLevels = ci.arrayLayers = 1;
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
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = ci.format;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView textureView{};
            check(vkCreateImageView(device, &vi, nullptr, &textureView), "create texture view");
            textureViews.push_back(textureView);
            VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            si.magFilter = si.minFilter = texture.filter;
            si.addressModeU = si.addressModeV = si.addressModeW = texture.address;
            VkSampler sampler{};
            check(vkCreateSampler(device, &si, nullptr, &sampler), "create sampler");
            samplers.push_back(sampler);
            VkDeviceMemory stagingMemory{};
            const auto staging = buffer(texture.rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stagingMemory);
            void *mapped = nullptr;
            check(vkMapMemory(device, stagingMemory, 0, texture.rgba.size(), 0, &mapped), "map texture staging");
            std::memcpy(mapped, texture.rgba.data(), texture.rgba.size());
            vkUnmapMemory(device, stagingMemory);
            check(vkResetCommandBuffer(command, 0), "reset upload commands");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(vkBeginCommandBuffer(command, &begin), "begin upload commands");
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = textureImage;
            barrier.subresourceRange = vi.subresourceRange;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = ci.extent;
            vkCmdCopyBufferToImage(command, staging, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.oldLayout = barrier.newLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &barrier);
            check(vkEndCommandBuffer(command), "end upload commands");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "submit texture upload");
            check(vkQueueWaitIdle(queue), "wait texture upload");
            VkDescriptorImageInfo info{sampler, textureView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = descriptor;
            write.dstBinding = texture.binding;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &info;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        static void check(VkResult result, const char *operation)
        {
            if (result != VK_SUCCESS)
                throw std::runtime_error(std::string(operation) + ": Vulkan result " + std::to_string(result));
        }
        VkDeviceMemory allocate(const VkMemoryRequirements &req, VkMemoryPropertyFlags flags)
        {
            VkPhysicalDeviceMemoryProperties props{};
            vkGetPhysicalDeviceMemoryProperties(physical, &props);
            for (unsigned i = 0; i < props.memoryTypeCount; ++i) {
                if (!(req.memoryTypeBits & (1u << i)) || (props.memoryTypes[i].propertyFlags & flags) != flags)
                    continue;
                VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                ai.allocationSize = req.size;
                ai.memoryTypeIndex = i;
                VkDeviceMemory memory{};
                check(vkAllocateMemory(device, &ai, nullptr, &memory), "allocate memory");
                memories.push_back(memory);
                return memory;
            }
            throw std::runtime_error("No suitable Vulkan memory type");
        }
        VkBuffer buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkDeviceMemory &memory)
        {
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size = bytes;
            bi.usage = usage;
            VkBuffer result{};
            check(vkCreateBuffer(device, &bi, nullptr, &result), "create buffer");
            buffers.push_back(result);
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, result, &req);
            memory = allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            check(vkBindBufferMemory(device, result, memory, 0), "bind buffer");
            return result;
        }
        VkInstance instance{};
        VkPhysicalDevice physical{};
        VkDevice device{};
        VkQueue queue{};
        VkCommandPool pool{};
        VkCommandBuffer command{};
        VkImage image{};
        VkImageView view{};
        VkRenderPass pass{};
        VkFramebuffer framebuffer{};
        VkDescriptorSetLayout setLayout{};
        VkDescriptorPool descriptorPool{};
        VkDescriptorSet descriptor{};
        VkPipelineLayout layout{};
        VkPipeline pipeline{};
        VkBuffer uniform{}, readback{};
        VkDeviceMemory readbackMemory{};
        std::vector<VkBuffer> buffers;
        std::vector<VkDeviceMemory> memories;
        std::vector<VkShaderModule> modules;
        std::vector<VkImage> textureImages;
        std::vector<VkImageView> textureViews;
        std::vector<VkSampler> samplers;
};
