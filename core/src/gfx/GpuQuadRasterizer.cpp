#include "GpuQuadRasterizer.hpp"

#include <cstring>
#include <stdexcept>

#include "GpuQuadShaders.hpp"

namespace Core::Gfx {

    namespace {
        constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM; // matches the CPU framebuffer's RGBX byte order
        constexpr std::uint32_t kMaxVertices = 1u << 18; // 262144 verts (~87k triangles) per frame
        constexpr std::uint32_t kMaxTextures = 1024; // descriptor-set budget

        std::uint64_t hashRgba(const std::uint8_t *p, std::size_t n)
        {
            std::uint64_t h = 1469598103934665603ull;
            for (std::size_t i = 0; i < n; i++) {
                h ^= p[i];
                h *= 1099511628211ull;
            }
            return h;
        }
    } // namespace

    GpuQuadRasterizer::GpuQuadRasterizer(VkPhysicalDevice phys, VkDevice device, VkQueue queue, VkCommandPool pool, std::uint32_t width,
                                         std::uint32_t height) :
        m_phys(phys), m_device(device), m_queue(queue), m_pool(pool), m_width(width), m_height(height)
    {
        createOffscreenTarget();
        createRenderPass();
        createSampler();
        createDescriptorPool();
        createPipeline();

        // Framebuffer over the offscreen colour view.
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = m_renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &m_colorView;
        fb.width = m_width;
        fb.height = m_height;
        fb.layers = 1;
        if (vkCreateFramebuffer(m_device, &fb, nullptr, &m_framebuffer) != VK_SUCCESS)
            throw std::runtime_error("GpuQuadRasterizer: failed to create framebuffer");

        m_vertexCapacity = kMaxVertices;
        createBuffer(static_cast<VkDeviceSize>(m_vertexCapacity) * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_vertexBuffer, m_vertexMem);
        vkMapMemory(m_device, m_vertexMem, 0, VK_WHOLE_SIZE, 0, &m_vertexMapped);

        createBuffer(static_cast<VkDeviceSize>(m_width) * m_height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_readback, m_readbackMem);

        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(m_device, &fi, nullptr, &m_fence);
    }

    GpuQuadRasterizer::~GpuQuadRasterizer()
    {
        vkDeviceWaitIdle(m_device);
        for (auto &[key, t]: m_textures) {
            if (t.view)
                vkDestroyImageView(m_device, t.view, nullptr);
            if (t.image)
                vkDestroyImage(m_device, t.image, nullptr);
            if (t.memory)
                vkFreeMemory(m_device, t.memory, nullptr);
        }
        if (m_fence)
            vkDestroyFence(m_device, m_fence, nullptr);
        if (m_vertexMapped)
            vkUnmapMemory(m_device, m_vertexMem);
        if (m_vertexBuffer)
            vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        if (m_vertexMem)
            vkFreeMemory(m_device, m_vertexMem, nullptr);
        if (m_readback)
            vkDestroyBuffer(m_device, m_readback, nullptr);
        if (m_readbackMem)
            vkFreeMemory(m_device, m_readbackMem, nullptr);
        if (m_pipeline)
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
        if (m_pipelineLayout)
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        if (m_setLayout)
            vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
        if (m_descriptorPool)
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        if (m_sampler)
            vkDestroySampler(m_device, m_sampler, nullptr);
        if (m_framebuffer)
            vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
        if (m_renderPass)
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        if (m_colorView)
            vkDestroyImageView(m_device, m_colorView, nullptr);
        if (m_color)
            vkDestroyImage(m_device, m_color, nullptr);
        if (m_colorMem)
            vkFreeMemory(m_device, m_colorMem, nullptr);
    }

    std::uint32_t GpuQuadRasterizer::findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags props) const
    {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(m_phys, &mp);
        for (std::uint32_t i = 0; i < mp.memoryTypeCount; i++)
            if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        throw std::runtime_error("GpuQuadRasterizer: no suitable memory type");
    }

    void GpuQuadRasterizer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer &buf,
                                         VkDeviceMemory &mem) const
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error("GpuQuadRasterizer: failed to create buffer");
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_device, buf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
        if (vkAllocateMemory(m_device, &ai, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("GpuQuadRasterizer: failed to allocate buffer memory");
        vkBindBufferMemory(m_device, buf, mem, 0);
    }

    VkCommandBuffer GpuQuadRasterizer::beginOneTime() const
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool = m_pool;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(m_device, &ai, &cmd);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        return cmd;
    }

    void GpuQuadRasterizer::endOneTime(VkCommandBuffer cmd) const
    {
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_queue);
        vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
    }

    void GpuQuadRasterizer::createOffscreenTarget()
    {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = kColorFormat;
        ii.extent = {m_width, m_height, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_device, &ii, nullptr, &m_color) != VK_SUCCESS)
            throw std::runtime_error("GpuQuadRasterizer: failed to create offscreen image");
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(m_device, m_color, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(m_device, &ai, nullptr, &m_colorMem);
        vkBindImageMemory(m_device, m_color, m_colorMem, 0);

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_color;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = kColorFormat;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(m_device, &vi, nullptr, &m_colorView);
    }

    void GpuQuadRasterizer::createRenderPass()
    {
        VkAttachmentDescription color{};
        color.format = kColorFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // ready for readback copy
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;
        if (vkCreateRenderPass(m_device, &rp, nullptr, &m_renderPass) != VK_SUCCESS)
            throw std::runtime_error("GpuQuadRasterizer: failed to create render pass");
    }

    void GpuQuadRasterizer::createSampler()
    {
        VkSamplerCreateInfo s{};
        s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        s.magFilter = VK_FILTER_NEAREST; // match the CPU rasteriser's nearest sampling
        s.minFilter = VK_FILTER_NEAREST;
        s.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        s.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        vkCreateSampler(m_device, &s, nullptr, &m_sampler);
    }

    void GpuQuadRasterizer::createDescriptorPool()
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1;
        li.pBindings = &b;
        vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_setLayout);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures};
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &ps;
        pi.maxSets = kMaxTextures;
        vkCreateDescriptorPool(m_device, &pi, nullptr, &m_descriptorPool);
    }

    void GpuQuadRasterizer::createPipeline()
    {
        const auto makeModule = [&](const std::uint32_t *code, std::size_t bytes) {
            VkShaderModuleCreateInfo mi{};
            mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            mi.codeSize = bytes;
            mi.pCode = code;
            VkShaderModule m;
            if (vkCreateShaderModule(m_device, &mi, nullptr, &m) != VK_SUCCESS)
                throw std::runtime_error("GpuQuadRasterizer: failed to create shader module");
            return m;
        };
        VkShaderModule vert = makeModule(kQuadVertSpv, sizeof(kQuadVertSpv));
        VkShaderModule frag = makeModule(kQuadFragSpv, sizeof(kQuadFragSpv));

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkVertexInputBindingDescription bind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x)};
        attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, u)};
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &bind;
        vin.vertexAttributeDescriptionCount = 2;
        vin.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0, 1};
        VkRect2D sc{{0, 0}, {m_width, m_height}};
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1;
        vs.pViewports = &vp;
        vs.scissorCount = 1;
        vs.pScissors = &sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE; // GX2 winding varies; the CPU rasteriser is two-sided too
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE; // src-alpha over: opaque texels (a=1) overwrite, translucent blend
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &blend;

        VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &m_setLayout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pc;
        vkCreatePipelineLayout(m_device, &pl, nullptr, &m_pipelineLayout);

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vin;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vs;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pColorBlendState = &cb;
        gp.layout = m_pipelineLayout;
        gp.renderPass = m_renderPass;
        gp.subpass = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_pipeline) != VK_SUCCESS)
            throw std::runtime_error("GpuQuadRasterizer: failed to create graphics pipeline");

        vkDestroyShaderModule(m_device, vert, nullptr);
        vkDestroyShaderModule(m_device, frag, nullptr);
    }

    GpuQuadRasterizer::Texture &GpuQuadRasterizer::getOrCreateTexture(std::uint64_t key, const std::uint8_t *rgba, std::uint32_t tw, std::uint32_t th)
    {
        const std::uint64_t hash = hashRgba(rgba, static_cast<std::size_t>(tw) * th * 4);
        Texture &t = m_textures[key];
        if (t.image != VK_NULL_HANDLE && t.w == tw && t.h == th && t.hash == hash)
            return t; // cached and unchanged — no upload

        // (Re)create the image if new or the dimensions changed.
        if (t.image == VK_NULL_HANDLE || t.w != tw || t.h != th) {
            vkDeviceWaitIdle(m_device);
            if (t.view)
                vkDestroyImageView(m_device, t.view, nullptr);
            if (t.image)
                vkDestroyImage(m_device, t.image, nullptr);
            if (t.memory)
                vkFreeMemory(m_device, t.memory, nullptr);
            t.view = VK_NULL_HANDLE;
            t.image = VK_NULL_HANDLE;
            t.memory = VK_NULL_HANDLE;

            VkImageCreateInfo ii{};
            ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ii.imageType = VK_IMAGE_TYPE_2D;
            ii.format = VK_FORMAT_R8G8B8A8_UNORM;
            ii.extent = {tw, th, 1};
            ii.mipLevels = 1;
            ii.arrayLayers = 1;
            ii.samples = VK_SAMPLE_COUNT_1_BIT;
            ii.tiling = VK_IMAGE_TILING_OPTIMAL;
            ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            vkCreateImage(m_device, &ii, nullptr, &t.image);
            VkMemoryRequirements req;
            vkGetImageMemoryRequirements(m_device, t.image, &req);
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(m_device, &ai, nullptr, &t.memory);
            vkBindImageMemory(m_device, t.image, t.memory, 0);

            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = t.image;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R8G8B8A8_UNORM;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCreateImageView(m_device, &vi, nullptr, &t.view);

            if (t.descriptor == VK_NULL_HANDLE) {
                VkDescriptorSetAllocateInfo da{};
                da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                da.descriptorPool = m_descriptorPool;
                da.descriptorSetCount = 1;
                da.pSetLayouts = &m_setLayout;
                vkAllocateDescriptorSets(m_device, &da, &t.descriptor);
            }
            VkDescriptorImageInfo di{m_sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = t.descriptor;
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &di;
            vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
        }

        // Upload the pixels through a staging buffer.
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(tw) * th * 4;
        VkBuffer staging;
        VkDeviceMemory stagingMem;
        createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
                     stagingMem);
        void *mapped;
        vkMapMemory(m_device, stagingMem, 0, bytes, 0, &mapped);
        std::memcpy(mapped, rgba, bytes);
        vkUnmapMemory(m_device, stagingMem);

        VkCommandBuffer cmd = beginOneTime();
        VkImageMemoryBarrier toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = t.image;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {tw, th, 1};
        vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toRead = toDst;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
        endOneTime(cmd);

        vkDestroyBuffer(m_device, staging, nullptr);
        vkFreeMemory(m_device, stagingMem, nullptr);

        t.w = tw;
        t.h = th;
        t.hash = hash;
        return t;
    }

    void GpuQuadRasterizer::begin()
    {
        m_vertexCount = 0;
        m_draws.clear();
    }

    void GpuQuadRasterizer::drawTriangles(std::uint64_t key, const std::uint8_t *rgba, std::uint32_t tw, std::uint32_t th, const Vertex *verts,
                                          std::uint32_t count)
    {
        if (count == 0 || tw == 0 || th == 0 || !rgba)
            return;
        if (m_vertexCount + count > m_vertexCapacity)
            return; // over budget for this frame; drop the draw rather than overflow
        const Texture &t = getOrCreateTexture(key, rgba, tw, th);
        std::memcpy(static_cast<Vertex *>(m_vertexMapped) + m_vertexCount, verts, count * sizeof(Vertex));
        m_draws.push_back({t.descriptor, m_vertexCount, count});
        m_vertexCount += count;
    }

    void GpuQuadRasterizer::end(std::uint8_t *outRgbx)
    {
        VkCommandBuffer cmd = beginOneTime();

        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_renderPass;
        rp.framebuffer = m_framebuffer;
        rp.renderArea = {{0, 0}, {m_width, m_height}};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        if (!m_draws.empty()) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
            const float invSize[2] = {1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height)};
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(invSize), invSize);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
            for (const Draw &d: m_draws) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &d.descriptor, 0, nullptr);
                vkCmdDraw(cmd, d.vertexCount, 1, d.firstVertex, 0);
            }
        }
        vkCmdEndRenderPass(cmd);

        // The render pass leaves the image in TRANSFER_SRC_OPTIMAL; copy it out.
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {m_width, m_height, 1};
        vkCmdCopyImageToBuffer(cmd, m_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback, 1, &region);

        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkResetFences(m_device, 1, &m_fence);
        vkQueueSubmit(m_queue, 1, &si, m_fence);
        vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
        vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);

        void *mapped;
        vkMapMemory(m_device, m_readbackMem, 0, static_cast<VkDeviceSize>(m_width) * m_height * 4, 0, &mapped);
        std::memcpy(outRgbx, mapped, static_cast<std::size_t>(m_width) * m_height * 4);
        vkUnmapMemory(m_device, m_readbackMem);
    }

} // namespace Core::Gfx
