#include "GpuRenderGraph.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "GpuQuadShaders.hpp"

namespace Core::Gfx {

    namespace {
        constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM; // matches the CPU framebuffer's RGBX byte order
        constexpr std::uint32_t kMaxVertices = 1u << 19; // 524288 verts per frame (a full multi-pass menu)
        constexpr std::uint32_t kMaxSets = 4096; // descriptor-set budget: targets + loaded textures

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

    GpuRenderGraph::GpuRenderGraph(VkPhysicalDevice phys, VkDevice device, VkQueue queue, VkCommandPool pool, std::uint32_t maxWidth,
                                   std::uint32_t maxHeight) :
        m_phys(phys), m_device(device), m_queue(queue), m_pool(pool), m_maxWidth(maxWidth), m_maxHeight(maxHeight)
    {
        createRenderPass();
        createSampler();
        createDescriptorInfra();
        createPipeline();

        m_vertexCapacity = kMaxVertices;
        createBuffer(static_cast<VkDeviceSize>(m_vertexCapacity) * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_vertexBuffer, m_vertexMem);
        vkMapMemory(m_device, m_vertexMem, 0, VK_WHOLE_SIZE, 0, &m_vertexMapped);

        createBuffer(static_cast<VkDeviceSize>(m_maxWidth) * m_maxHeight * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_readback, m_readbackMem);

        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(m_device, &fi, nullptr, &m_fence);
    }

    GpuRenderGraph::~GpuRenderGraph()
    {
        vkDeviceWaitIdle(m_device);
        for (auto &[addr, t]: m_targets) {
            if (t.framebuffer)
                vkDestroyFramebuffer(m_device, t.framebuffer, nullptr);
            if (t.view)
                vkDestroyImageView(m_device, t.view, nullptr);
            if (t.image)
                vkDestroyImage(m_device, t.image, nullptr);
            if (t.memory)
                vkFreeMemory(m_device, t.memory, nullptr);
        }
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
        if (m_renderPass)
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    }

    std::uint32_t GpuRenderGraph::findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags props) const
    {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(m_phys, &mp);
        for (std::uint32_t i = 0; i < mp.memoryTypeCount; i++)
            if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        throw std::runtime_error("GpuRenderGraph: no suitable memory type");
    }

    void GpuRenderGraph::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer &buf,
                                      VkDeviceMemory &mem) const
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error("GpuRenderGraph: failed to create buffer");
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_device, buf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
        if (vkAllocateMemory(m_device, &ai, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("GpuRenderGraph: failed to allocate buffer memory");
        vkBindBufferMemory(m_device, buf, mem, 0);
    }

    VkCommandBuffer GpuRenderGraph::beginOneTime() const
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

    void GpuRenderGraph::endOneTime(VkCommandBuffer cmd) const
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

    void GpuRenderGraph::createRenderPass()
    {
        // Targets are persistent across frames and their layout is tracked manually, so the pass
        // preserves contents (LOAD) and keeps the image in COLOR_ATTACHMENT_OPTIMAL. Clears are done
        // inside the pass via vkCmdClearAttachments so a cleared target and a preserved one share it.
        VkAttachmentDescription color{};
        color.format = kColorFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        // Serialise against the explicit barriers we insert around each pass (sampling a previously
        // rendered target, reading the scan target back).
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 2;
        rp.pDependencies = deps;
        if (vkCreateRenderPass(m_device, &rp, nullptr, &m_renderPass) != VK_SUCCESS)
            throw std::runtime_error("GpuRenderGraph: failed to create render pass");
    }

    void GpuRenderGraph::createSampler()
    {
        VkSamplerCreateInfo s{};
        s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        s.magFilter = VK_FILTER_LINEAR; // targets are downsampled for bloom — linear avoids aliasing
        s.minFilter = VK_FILTER_LINEAR;
        s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        vkCreateSampler(m_device, &s, nullptr, &m_sampler);
    }

    void GpuRenderGraph::createDescriptorInfra()
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

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxSets};
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &ps;
        pi.maxSets = kMaxSets;
        vkCreateDescriptorPool(m_device, &pi, nullptr, &m_descriptorPool);
    }

    void GpuRenderGraph::createPipeline()
    {
        const auto makeModule = [&](const std::uint32_t *code, std::size_t bytes) {
            VkShaderModuleCreateInfo mi{};
            mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            mi.codeSize = bytes;
            mi.pCode = code;
            VkShaderModule m;
            if (vkCreateShaderModule(m_device, &mi, nullptr, &m) != VK_SUCCESS)
                throw std::runtime_error("GpuRenderGraph: failed to create shader module");
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

        // Viewport/scissor are dynamic: one pipeline serves every target size.
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1;
        vs.scissorCount = 1;

        VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = dyn;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE;
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
        gp.pDynamicState = &ds;
        gp.layout = m_pipelineLayout;
        gp.renderPass = m_renderPass;
        gp.subpass = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_pipeline) != VK_SUCCESS)
            throw std::runtime_error("GpuRenderGraph: failed to create graphics pipeline");

        vkDestroyShaderModule(m_device, vert, nullptr);
        vkDestroyShaderModule(m_device, frag, nullptr);
    }

    GpuRenderGraph::Target &GpuRenderGraph::getOrCreateTarget(std::uint32_t addr, std::uint32_t w, std::uint32_t h)
    {
        if (w == 0)
            w = 1;
        if (h == 0)
            h = 1;
        Target &t = m_targets[addr];
        if (t.image != VK_NULL_HANDLE && t.w == w && t.h == h)
            return t; // persistent across frames

        // New target or a resize: (re)create the image, view, framebuffer.
        if (t.image != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_device);
            if (t.framebuffer)
                vkDestroyFramebuffer(m_device, t.framebuffer, nullptr);
            if (t.view)
                vkDestroyImageView(m_device, t.view, nullptr);
            if (t.image)
                vkDestroyImage(m_device, t.image, nullptr);
            if (t.memory)
                vkFreeMemory(m_device, t.memory, nullptr);
            t.framebuffer = VK_NULL_HANDLE;
            t.view = VK_NULL_HANDLE;
            t.image = VK_NULL_HANDLE;
            t.memory = VK_NULL_HANDLE;
        }

        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = kColorFormat;
        ii.extent = {w, h, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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
        vi.format = kColorFormat;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(m_device, &vi, nullptr, &t.view);

        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = m_renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &t.view;
        fb.width = w;
        fb.height = h;
        fb.layers = 1;
        vkCreateFramebuffer(m_device, &fb, nullptr, &t.framebuffer);

        if (t.descriptor == VK_NULL_HANDLE) {
            VkDescriptorSetAllocateInfo da{};
            da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            da.descriptorPool = m_descriptorPool;
            da.descriptorSetCount = 1;
            da.pSetLayouts = &m_setLayout;
            vkAllocateDescriptorSets(m_device, &da, &t.descriptor);
        }
        VkDescriptorImageInfo di{m_sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr{};
        wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet = t.descriptor;
        wr.dstBinding = 0;
        wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.pImageInfo = &di;
        vkUpdateDescriptorSets(m_device, 1, &wr, 0, nullptr);

        t.w = w;
        t.h = h;
        t.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return t;
    }

    GpuRenderGraph::Texture &GpuRenderGraph::getOrCreateTexture(std::uint32_t key, const std::uint8_t *rgba, std::uint32_t tw, std::uint32_t th)
    {
        const std::uint64_t hash = hashRgba(rgba, static_cast<std::size_t>(tw) * th * 4);
        Texture &t = m_textures[key];
        if (t.image != VK_NULL_HANDLE && t.w == tw && t.h == th && t.hash == hash)
            return t; // cached and unchanged — no upload

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
            VkWriteDescriptorSet wr{};
            wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr.dstSet = t.descriptor;
            wr.dstBinding = 0;
            wr.descriptorCount = 1;
            wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wr.pImageInfo = &di;
            vkUpdateDescriptorSets(m_device, 1, &wr, 0, nullptr);
        }

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

    void GpuRenderGraph::transitionTarget(VkCommandBuffer cmd, Target &t, VkImageLayout newLayout)
    {
        if (t.layout == newLayout)
            return;
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = t.layout;
        b.newLayout = newLayout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        t.layout = newLayout;
    }

    GpuRenderGraph::Pass &GpuRenderGraph::currentPassFor(std::uint32_t target)
    {
        if (!m_passes.empty() && m_passes.back().target == target)
            return m_passes.back();
        m_passes.push_back(Pass{});
        m_passes.back().target = target;
        return m_passes.back();
    }

    void GpuRenderGraph::beginFrame()
    {
        m_passes.clear();
        m_vertexCount = 0;
        m_curTarget = 0;
    }

    void GpuRenderGraph::bindTarget(std::uint32_t addr, std::uint32_t w, std::uint32_t h)
    {
        getOrCreateTarget(addr, w, h);
        m_curTarget = addr;
    }

    void GpuRenderGraph::clearTarget(std::uint32_t addr, std::uint32_t w, std::uint32_t h, const float rgba[4])
    {
        getOrCreateTarget(addr, w, h);
        // A clear always begins a fresh pass so it applies after any earlier draws into this target
        // in stream order; subsequent draws into the bound target land in this same pass and render
        // over the clear. It does not change the current render target (SetColorBuffer owns that).
        m_passes.push_back(Pass{});
        Pass &p = m_passes.back();
        p.target = addr;
        p.clear = true;
        p.clearColor[0] = rgba[0];
        p.clearColor[1] = rgba[1];
        p.clearColor[2] = rgba[2];
        p.clearColor[3] = rgba[3];
    }

    void GpuRenderGraph::draw(std::uint32_t srcAddr, const std::uint8_t *srcRgba, std::uint32_t tw, std::uint32_t th, const Vertex *verts,
                              std::uint32_t count)
    {
        if (count == 0 || m_curTarget == 0)
            return;
        if (m_vertexCount + count > m_vertexCapacity)
            return; // over budget for this frame; drop rather than overflow

        DrawCmd dc{};
        dc.firstVertex = m_vertexCount;
        dc.vertexCount = count;
        if (srcRgba == nullptr) {
            // Sampling a colour target rendered earlier this frame (render-to-texture).
            auto it = m_targets.find(srcAddr);
            if (it == m_targets.end())
                return; // unknown target; nothing to sample
            if (srcAddr == m_curTarget)
                return; // can't sample the target we're rendering into
            dc.descriptor = it->second.descriptor;
            dc.srcTarget = srcAddr;
        } else {
            if (tw == 0 || th == 0)
                return;
            Texture &t = getOrCreateTexture(srcAddr, srcRgba, tw, th);
            dc.descriptor = t.descriptor;
            dc.srcTarget = 0;
        }

        std::memcpy(static_cast<Vertex *>(m_vertexMapped) + m_vertexCount, verts, count * sizeof(Vertex));
        Pass &p = currentPassFor(m_curTarget);
        if (dc.srcTarget != 0)
            p.sources.insert(dc.srcTarget);
        p.draws.push_back(dc);
        m_vertexCount += count;
    }

    void GpuRenderGraph::present(std::uint32_t scanAddr, std::uint8_t *outRgbx)
    {
        std::memset(outRgbx, 0, static_cast<std::size_t>(m_maxWidth) * m_maxHeight * 4);

        // WEMU_GPU_GRAPH_STATS=N: every N presents, report the shape of the recorded graph — how many
        // distinct colour targets, passes, and render-to-texture (sampled-target) draws it composited.
        // Confirms the multi-target compositing path is actually exercised (vs. a single flat target).
        static const std::uint32_t statsEvery = []() -> std::uint32_t {
            const char *e = std::getenv("WEMU_GPU_GRAPH_STATS");
            return e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 10)) : 0;
        }();
        if (statsEvery) {
            static std::uint32_t s_present = 0;
            if ((s_present++ % statsEvery) == 0) {
                std::uint32_t rttDraws = 0, totalDraws = 0;
                for (const Pass &p: m_passes) {
                    totalDraws += static_cast<std::uint32_t>(p.draws.size());
                    for (const DrawCmd &d: p.draws)
                        rttDraws += (d.srcTarget != 0);
                }
                std::fprintf(stderr, "[GRAPH] present #%u: targets=%zu passes=%zu draws=%u rtt=%u scan=0x%08X\n", s_present - 1, m_targets.size(),
                             m_passes.size(), totalDraws, rttDraws, scanAddr);
            }
        }

        VkCommandBuffer cmd = beginOneTime();

        for (Pass &pass: m_passes) {
            auto tgtIt = m_targets.find(pass.target);
            if (tgtIt == m_targets.end())
                continue;
            Target &tgt = tgtIt->second;

            // Make every sampled source readable, then make the target writable.
            for (std::uint32_t src: pass.sources) {
                auto sit = m_targets.find(src);
                if (sit != m_targets.end())
                    transitionTarget(cmd, sit->second, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            transitionTarget(cmd, tgt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            VkRenderPassBeginInfo rp{};
            rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp.renderPass = m_renderPass;
            rp.framebuffer = tgt.framebuffer;
            rp.renderArea = {{0, 0}, {tgt.w, tgt.h}};
            rp.clearValueCount = 0;
            vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

            if (pass.clear) {
                VkClearAttachment ca{};
                ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                ca.colorAttachment = 0;
                ca.clearValue.color = {{pass.clearColor[0], pass.clearColor[1], pass.clearColor[2], pass.clearColor[3]}};
                VkClearRect cr{};
                cr.rect = {{0, 0}, {tgt.w, tgt.h}};
                cr.baseArrayLayer = 0;
                cr.layerCount = 1;
                vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
            }

            if (!pass.draws.empty()) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
                VkViewport vp{0, 0, static_cast<float>(tgt.w), static_cast<float>(tgt.h), 0, 1};
                VkRect2D sc{{0, 0}, {tgt.w, tgt.h}};
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);
                const float invSize[2] = {1.0f / static_cast<float>(tgt.w), 1.0f / static_cast<float>(tgt.h)};
                vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(invSize), invSize);
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
                for (const DrawCmd &d: pass.draws) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &d.descriptor, 0, nullptr);
                    vkCmdDraw(cmd, d.vertexCount, 1, d.firstVertex, 0);
                }
            }
            vkCmdEndRenderPass(cmd);
            tgt.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // render pass finalLayout
        }

        // Read the scan target back into the output framebuffer.
        Target *scan = nullptr;
        auto scanIt = m_targets.find(scanAddr);
        if (scanIt != m_targets.end())
            scan = &scanIt->second;

        if (scan) {
            transitionTarget(cmd, *scan, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {scan->w, scan->h, 1};
            vkCmdCopyImageToBuffer(cmd, scan->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback, 1, &region);
        }

        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkResetFences(m_device, 1, &m_fence);
        vkQueueSubmit(m_queue, 1, &si, m_fence);
        vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
        vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);

        if (scan) {
            void *mapped;
            const VkDeviceSize bytes = static_cast<VkDeviceSize>(scan->w) * scan->h * 4;
            vkMapMemory(m_device, m_readbackMem, 0, bytes, 0, &mapped);
            const std::uint32_t rows = std::min(scan->h, m_maxHeight);
            const std::uint32_t cols = std::min(scan->w, m_maxWidth);
            const auto *srcBase = static_cast<const std::uint8_t *>(mapped);
            for (std::uint32_t y = 0; y < rows; y++)
                std::memcpy(outRgbx + static_cast<std::size_t>(y) * m_maxWidth * 4, srcBase + static_cast<std::size_t>(y) * scan->w * 4,
                            static_cast<std::size_t>(cols) * 4);
            vkUnmapMemory(m_device, m_readbackMem);
        }
    }

} // namespace Core::Gfx
