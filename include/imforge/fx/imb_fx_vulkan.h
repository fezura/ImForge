#pragma once
// imb::fx Vulkan backend. Include this instead of imb_fx.h.
//
//   imb::fx::VulkanInitDesc d = {...};  imb::fx::Init(d);        // after ImGui_ImplVulkan_Init
//   per frame, after the scene pass ends and BEFORE the ImGui pass begins:
//     imb::fx::FrameDesc fd = { cmd, swapchain_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
//                               fb_width, fb_height, image_index };
//     imb::fx::PrepareFrame(fd);   // records outside any render pass
//   ... vkCmdBeginRenderPass(render_pass) ... ImGui_ImplVulkan_RenderDrawData(..., cmd) ...
//   vkDeviceWaitIdle(device);  imb::fx::Shutdown();
//
// - back_buffer needs VK_IMAGE_USAGE_TRANSFER_SRC_BIT and must be single-sampled (with an MSAA
//   UI pass, pass the resolve target).
// - back_buffer_layout is restored after PrepareFrame. UNDEFINED/PREINITIALIZED end up as
//   TRANSFER_SRC_OPTIMAL.
// - frame_index (< image_count) picks a resource slot. Its previous use must be fenced, same as the
//   command buffer.
// - render_pass: the pass (subpass 0) ImGui renders in, with `msaa` samples.
// - descriptor_pool is optional. If you pass one it needs FREE_DESCRIPTOR_SET_BIT and room for
//   image_count * 9 combined-image-sampler sets.
// - Blur callbacks use the command buffer from Renderer_RenderState (1.91.4+). On older imgui,
//   ImGui must render into the cmd you passed to PrepareFrame.
// - Main viewport only.
// - Shutdown() destroys everything immediately, vkDeviceWaitIdle first.
// - IMB_FX_VULKAN_USE_VOLK (or IMGUI_IMPL_VULKAN_USE_VOLK) includes <volk.h> instead of <vulkan/vulkan.h>.
#include "imb_fx.h"
#include "imb_fx_vulkan_shaders.h"
#if defined(IMB_FX_VULKAN_USE_VOLK) || defined(IMGUI_IMPL_VULKAN_USE_VOLK)
#include <volk.h>
#else
#include <vulkan/vulkan.h>
#endif
#include <math.h>
#include <stdint.h>
#include <string.h>

#define IMB_FX_AVAILABLE 1

namespace imb {
namespace fx {

struct VulkanInitDesc {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    uint32_t image_count = 0;
    VkSampleCountFlagBits msaa = VK_SAMPLE_COUNT_1_BIT;
    const VkAllocationCallbacks* allocator = nullptr;
};

struct FrameDesc {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkImage back_buffer = VK_NULL_HANDLE;
    VkImageLayout back_buffer_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    int width = 0;
    int height = 0;
    uint32_t frame_index = 0;
};

bool Init(const VulkanInitDesc& desc);

namespace vkdetail {

constexpr int kLevels = 5;
constexpr int kMaxSlots = 16;
constexpr int kSetsPerSlot = 2 * (kLevels - 1) + 1;
constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;

struct CompositePC { float rect[4]; float display[4]; float tint[4]; float p0[4]; float p1[4]; };
struct BlurPC { float dir[2]; float pad[2]; };
struct RenderStateMirror { VkCommandBuffer CommandBuffer; VkPipeline Pipeline; VkPipelineLayout PipelineLayout; };

struct Slot {
    int width = 0;
    int height = 0;
    uint32_t level_w[kLevels] = {};
    uint32_t level_h[kLevels] = {};
    VkImage chain = VK_NULL_HANDLE;
    VkImage tmp = VK_NULL_HANDLE;
    VkDeviceMemory chain_mem = VK_NULL_HANDLE;
    VkDeviceMemory tmp_mem = VK_NULL_HANDLE;
    VkImageView chain_view[kLevels] = {};
    VkImageView chain_all = VK_NULL_HANDLE;
    VkImageView tmp_view[kLevels - 1] = {};
    VkFramebuffer chain_fb[kLevels] = {};
    VkFramebuffer tmp_fb[kLevels - 1] = {};
    VkDescriptorSet chain_set[kLevels - 1] = {};
    VkDescriptorSet tmp_set[kLevels - 1] = {};
    VkDescriptorSet final_set = VK_NULL_HANDLE;
};

struct BlurCmd { ImVec4 rect; BlurParams params; };

struct State {
    bool ready = false;
    VulkanInitDesc desc;
    VkPhysicalDeviceMemoryProperties mem_props = {};
    VkDescriptorPool pool = VK_NULL_HANDLE;
    bool owns_pool = false;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout blur_layout = VK_NULL_HANDLE;
    VkPipelineLayout composite_layout = VK_NULL_HANDLE;
    VkRenderPass offscreen_pass = VK_NULL_HANDLE;
    VkPipeline blur_pipe = VK_NULL_HANDLE;
    VkPipeline composite_pipe = VK_NULL_HANDLE;
    Slot slots[kMaxSlots];
    uint32_t slot_count = 0;
    int current_slot = -1;
    VkCommandBuffer current_cmd = VK_NULL_HANDLE;
    ImVector<BlurCmd> cmds;
    int cmds_frame = -1;
};

inline State& GetState() {
    static State s;
    return s;
}

inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }

inline uint32_t FindMemoryType(const State& s, uint32_t type_bits, VkMemoryPropertyFlags props) {
    for (uint32_t i = 0; i < s.mem_props.memoryTypeCount; i++)
        if ((type_bits & (1u << i)) && (s.mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return UINT32_MAX;
}

inline bool CreateImage(State& s, uint32_t w, uint32_t h, uint32_t levels, VkImageUsageFlags usage,
                        VkImage* image, VkDeviceMemory* memory) {
    VkImageCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = kFormat;
    ci.extent.width = w;
    ci.extent.height = h;
    ci.extent.depth = 1;
    ci.mipLevels = levels;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(s.desc.device, &ci, s.desc.allocator, image) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(s.desc.device, *image, &req);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(s, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = FindMemoryType(s, req.memoryTypeBits, 0);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(s.desc.device, &ai, s.desc.allocator, memory) != VK_SUCCESS) return false;
    return vkBindImageMemory(s.desc.device, *image, *memory, 0) == VK_SUCCESS;
}

inline VkImageView CreateView(State& s, VkImage image, uint32_t base_level, uint32_t level_count) {
    VkImageViewCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image = image;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format = kFormat;
    ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ci.subresourceRange.baseMipLevel = base_level;
    ci.subresourceRange.levelCount = level_count;
    ci.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(s.desc.device, &ci, s.desc.allocator, &view);
    return view;
}

inline VkFramebuffer CreateFramebuffer(State& s, VkImageView view, uint32_t w, uint32_t h) {
    VkFramebufferCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ci.renderPass = s.offscreen_pass;
    ci.attachmentCount = 1;
    ci.pAttachments = &view;
    ci.width = w;
    ci.height = h;
    ci.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    vkCreateFramebuffer(s.desc.device, &ci, s.desc.allocator, &fb);
    return fb;
}

inline void WriteSet(State& s, VkDescriptorSet set, VkImageView view) {
    VkDescriptorImageInfo ii = {};
    ii.sampler = s.sampler;
    ii.imageView = view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w = {};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(s.desc.device, 1, &w, 0, nullptr);
}

inline void DestroySlotImages(State& s, Slot& slot) {
    VkDevice dev = s.desc.device;
    const VkAllocationCallbacks* a = s.desc.allocator;
    for (int l = 0; l < kLevels; l++) {
        if (slot.chain_fb[l]) vkDestroyFramebuffer(dev, slot.chain_fb[l], a);
        if (slot.chain_view[l]) vkDestroyImageView(dev, slot.chain_view[l], a);
        slot.chain_fb[l] = VK_NULL_HANDLE;
        slot.chain_view[l] = VK_NULL_HANDLE;
    }
    for (int k = 0; k < kLevels - 1; k++) {
        if (slot.tmp_fb[k]) vkDestroyFramebuffer(dev, slot.tmp_fb[k], a);
        if (slot.tmp_view[k]) vkDestroyImageView(dev, slot.tmp_view[k], a);
        slot.tmp_fb[k] = VK_NULL_HANDLE;
        slot.tmp_view[k] = VK_NULL_HANDLE;
    }
    if (slot.chain_all) vkDestroyImageView(dev, slot.chain_all, a);
    if (slot.chain) vkDestroyImage(dev, slot.chain, a);
    if (slot.tmp) vkDestroyImage(dev, slot.tmp, a);
    if (slot.chain_mem) vkFreeMemory(dev, slot.chain_mem, a);
    if (slot.tmp_mem) vkFreeMemory(dev, slot.tmp_mem, a);
    slot.chain_all = VK_NULL_HANDLE;
    slot.chain = slot.tmp = VK_NULL_HANDLE;
    slot.chain_mem = slot.tmp_mem = VK_NULL_HANDLE;
    slot.width = slot.height = 0;
}

inline bool CreateSlotImages(State& s, Slot& slot, int width, int height) {
    uint32_t bw = (uint32_t)(width > 1 ? width / 2 : 1);
    uint32_t bh = (uint32_t)(height > 1 ? height / 2 : 1);
    for (int l = 0; l < kLevels; l++) {
        slot.level_w[l] = (bw >> l) > 0 ? (bw >> l) : 1;
        slot.level_h[l] = (bh >> l) > 0 ? (bh >> l) : 1;
    }
    bool ok = CreateImage(s, bw, bh, kLevels,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                          &slot.chain, &slot.chain_mem) &&
              CreateImage(s, slot.level_w[1], slot.level_h[1], kLevels - 1,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &slot.tmp,
                          &slot.tmp_mem);
    if (ok) {
        slot.chain_all = CreateView(s, slot.chain, 0, kLevels);
        ok = slot.chain_all != VK_NULL_HANDLE;
    }
    for (int l = 0; ok && l < kLevels; l++) {
        slot.chain_view[l] = CreateView(s, slot.chain, (uint32_t)l, 1);
        ok = slot.chain_view[l] != VK_NULL_HANDLE;
        if (ok && l > 0) {
            slot.chain_fb[l] = CreateFramebuffer(s, slot.chain_view[l], slot.level_w[l], slot.level_h[l]);
            ok = slot.chain_fb[l] != VK_NULL_HANDLE;
        }
    }
    for (int k = 0; ok && k < kLevels - 1; k++) {
        slot.tmp_view[k] = CreateView(s, slot.tmp, (uint32_t)k, 1);
        ok = slot.tmp_view[k] != VK_NULL_HANDLE;
        if (ok) {
            slot.tmp_fb[k] = CreateFramebuffer(s, slot.tmp_view[k], slot.level_w[k + 1], slot.level_h[k + 1]);
            ok = slot.tmp_fb[k] != VK_NULL_HANDLE;
        }
    }
    if (!ok) {
        DestroySlotImages(s, slot);
        return false;
    }
    for (int l = 0; l < kLevels - 1; l++) {
        WriteSet(s, slot.chain_set[l], slot.chain_view[l]);
        WriteSet(s, slot.tmp_set[l], slot.tmp_view[l]);
    }
    WriteSet(s, slot.final_set, slot.chain_all);
    slot.width = width;
    slot.height = height;
    return true;
}

inline VkShaderModule CreateModule(State& s, const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(s.desc.device, &ci, s.desc.allocator, &m);
    return m;
}

inline bool CreateOffscreenPass(State& s) {
    VkAttachmentDescription att = {};
    att.format = kFormat;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref = {};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sp = {};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;
    VkSubpassDependency deps[2] = {};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sp;
    ci.dependencyCount = 2;
    ci.pDependencies = deps;
    return vkCreateRenderPass(s.desc.device, &ci, s.desc.allocator, &s.offscreen_pass) == VK_SUCCESS;
}

inline VkPipeline CreatePipeline(State& s, VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout,
                                 VkRenderPass pass, VkSampleCountFlagBits samples, bool blend) {
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = samples;
    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState ba = {};
    ba.blendEnable = blend ? VK_TRUE : VK_FALSE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.colorBlendOp = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.alphaBlendOp = VK_BLEND_OP_ADD;
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsi = {};
    dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsi.dynamicStateCount = 2;
    dsi.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dsi;
    ci.layout = layout;
    ci.renderPass = pass;
    ci.subpass = 0;
    VkPipeline pipe = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(s.desc.device, VK_NULL_HANDLE, 1, &ci, s.desc.allocator, &pipe);
    return pipe;
}

inline bool CreatePipelines(State& s) {
    VkShaderModule fs_vs = CreateModule(s, vk_shaders::kFullscreenVertSpv, vk_shaders::kFullscreenVertSize);
    VkShaderModule blur_fs = CreateModule(s, vk_shaders::kBlurFragSpv, vk_shaders::kBlurFragSize);
    VkShaderModule comp_vs = CreateModule(s, vk_shaders::kCompositeVertSpv, vk_shaders::kCompositeVertSize);
    VkShaderModule comp_fs = CreateModule(s, vk_shaders::kCompositeFragSpv, vk_shaders::kCompositeFragSize);
    if (fs_vs && blur_fs && comp_vs && comp_fs) {
        s.blur_pipe = CreatePipeline(s, fs_vs, blur_fs, s.blur_layout, s.offscreen_pass, VK_SAMPLE_COUNT_1_BIT, false);
        s.composite_pipe = CreatePipeline(s, comp_vs, comp_fs, s.composite_layout, s.desc.render_pass, s.desc.msaa, true);
    }
    VkShaderModule mods[4] = { fs_vs, blur_fs, comp_vs, comp_fs };
    for (int i = 0; i < 4; i++)
        if (mods[i]) vkDestroyShaderModule(s.desc.device, mods[i], s.desc.allocator);
    return s.blur_pipe != VK_NULL_HANDLE && s.composite_pipe != VK_NULL_HANDLE;
}

inline bool CreateLayouts(State& s) {
    VkSamplerCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = (float)kLevels;
    sci.maxAnisotropy = 1.0f;
    if (vkCreateSampler(s.desc.device, &sci, s.desc.allocator, &s.sampler) != VK_SUCCESS) return false;
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dci.bindingCount = 1;
    dci.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(s.desc.device, &dci, s.desc.allocator, &s.set_layout) != VK_SUCCESS) return false;
    VkPushConstantRange range = {};
    range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    range.size = sizeof(BlurPC);
    VkPipelineLayoutCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pci.setLayoutCount = 1;
    pci.pSetLayouts = &s.set_layout;
    pci.pushConstantRangeCount = 1;
    pci.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(s.desc.device, &pci, s.desc.allocator, &s.blur_layout) != VK_SUCCESS) return false;
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    range.size = sizeof(CompositePC);
    return vkCreatePipelineLayout(s.desc.device, &pci, s.desc.allocator, &s.composite_layout) == VK_SUCCESS;
}

inline bool CreateDescriptorSets(State& s) {
    uint32_t total = s.slot_count * (uint32_t)kSetsPerSlot;
    if (s.desc.descriptor_pool) {
        s.pool = s.desc.descriptor_pool;
        s.owns_pool = false;
    } else {
        VkDescriptorPoolSize size = {};
        size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        size.descriptorCount = total;
        VkDescriptorPoolCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.maxSets = total;
        ci.poolSizeCount = 1;
        ci.pPoolSizes = &size;
        if (vkCreateDescriptorPool(s.desc.device, &ci, s.desc.allocator, &s.pool) != VK_SUCCESS) return false;
        s.owns_pool = true;
    }
    VkDescriptorSetLayout layouts[kSetsPerSlot];
    for (int i = 0; i < kSetsPerSlot; i++) layouts[i] = s.set_layout;
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = s.pool;
    ai.descriptorSetCount = kSetsPerSlot;
    ai.pSetLayouts = layouts;
    for (uint32_t i = 0; i < s.slot_count; i++) {
        VkDescriptorSet sets[kSetsPerSlot];
        if (vkAllocateDescriptorSets(s.desc.device, &ai, sets) != VK_SUCCESS) return false;
        Slot& slot = s.slots[i];
        for (int l = 0; l < kLevels - 1; l++) {
            slot.chain_set[l] = sets[l];
            slot.tmp_set[l] = sets[kLevels - 1 + l];
        }
        slot.final_set = sets[kSetsPerSlot - 1];
    }
    return true;
}

inline void Destroy(State& s) {
    VkDevice dev = s.desc.device;
    const VkAllocationCallbacks* a = s.desc.allocator;
    if (dev) {
        for (uint32_t i = 0; i < s.slot_count; i++) DestroySlotImages(s, s.slots[i]);
        if (s.pool && s.owns_pool) {
            vkDestroyDescriptorPool(dev, s.pool, a);
        } else if (s.pool) {
            for (uint32_t i = 0; i < s.slot_count; i++) {
                Slot& slot = s.slots[i];
                VkDescriptorSet sets[kSetsPerSlot];
                int n = 0;
                for (int l = 0; l < kLevels - 1; l++) {
                    if (slot.chain_set[l]) sets[n++] = slot.chain_set[l];
                    if (slot.tmp_set[l]) sets[n++] = slot.tmp_set[l];
                }
                if (slot.final_set) sets[n++] = slot.final_set;
                if (n) vkFreeDescriptorSets(dev, s.pool, (uint32_t)n, sets);
            }
        }
        if (s.blur_pipe) vkDestroyPipeline(dev, s.blur_pipe, a);
        if (s.composite_pipe) vkDestroyPipeline(dev, s.composite_pipe, a);
        if (s.blur_layout) vkDestroyPipelineLayout(dev, s.blur_layout, a);
        if (s.composite_layout) vkDestroyPipelineLayout(dev, s.composite_layout, a);
        if (s.set_layout) vkDestroyDescriptorSetLayout(dev, s.set_layout, a);
        if (s.offscreen_pass) vkDestroyRenderPass(dev, s.offscreen_pass, a);
        if (s.sampler) vkDestroySampler(dev, s.sampler, a);
    }
    s.cmds.clear();
    s = State();
}

inline void RunBlurPass(State& s, VkCommandBuffer cmd, VkFramebuffer fb, uint32_t w, uint32_t h,
                        VkDescriptorSet set, float dir_x, float dir_y) {
    VkRenderPassBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = s.offscreen_pass;
    bi.framebuffer = fb;
    bi.renderArea.extent.width = w;
    bi.renderArea.extent.height = h;
    vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.blur_pipe);
    VkViewport vp = {};
    vp.width = (float)w;
    vp.height = (float)h;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc = {};
    sc.extent.width = w;
    sc.extent.height = h;
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.blur_layout, 0, 1, &set, 0, nullptr);
    BlurPC pc = {};
    pc.dir[0] = dir_x;
    pc.dir[1] = dir_y;
    vkCmdPushConstants(cmd, s.blur_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

inline void BlurCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd) {
    (void)parent_list;
    State& s = GetState();
    if (!s.ready || s.current_slot < 0) return;
    int idx = (int)(intptr_t)cmd->UserCallbackData;
    if (idx < 0 || idx >= s.cmds.Size) return;
    const BlurCmd& bc = s.cmds[idx];
    const Slot& slot = s.slots[s.current_slot];
    VkCommandBuffer vk_cmd = s.current_cmd;
#if IMGUI_VERSION_NUM >= 19140
    if (const void* rs = ImGui::GetPlatformIO().Renderer_RenderState)
        vk_cmd = ((const RenderStateMirror*)rs)->CommandBuffer;
#endif
    ImDrawData* dd = ImGui::GetDrawData();
    if (!vk_cmd || !dd || dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f) return;
    const float sx = (float)slot.width / dd->DisplaySize.x;
    const float sy = (float)slot.height / dd->DisplaySize.y;
    float cx0 = (Max(cmd->ClipRect.x, bc.rect.x) - dd->DisplayPos.x) * sx;
    float cy0 = (Max(cmd->ClipRect.y, bc.rect.y) - dd->DisplayPos.y) * sy;
    float cx1 = (Min(cmd->ClipRect.z, bc.rect.z) - dd->DisplayPos.x) * sx;
    float cy1 = (Min(cmd->ClipRect.w, bc.rect.w) - dd->DisplayPos.y) * sy;
    cx0 = Max(cx0, 0.0f);
    cy0 = Max(cy0, 0.0f);
    cx1 = Min(cx1, (float)slot.width);
    cy1 = Min(cy1, (float)slot.height);
    if (cx1 <= cx0 || cy1 <= cy0) return;

    CompositePC pc = {};
    pc.rect[0] = bc.rect.x;
    pc.rect[1] = bc.rect.y;
    pc.rect[2] = bc.rect.z;
    pc.rect[3] = bc.rect.w;
    pc.display[0] = dd->DisplayPos.x;
    pc.display[1] = dd->DisplayPos.y;
    pc.display[2] = dd->DisplaySize.x;
    pc.display[3] = dd->DisplaySize.y;
    ImVec4 tint = ImGui::ColorConvertU32ToFloat4(bc.params.tint);
    pc.tint[0] = tint.x;
    pc.tint[1] = tint.y;
    pc.tint[2] = tint.z;
    pc.tint[3] = tint.w;
    float lod = 0.0f;
    if (bc.params.radius > 0.0f) {
        lod = log2f(Max(bc.params.radius, 4.0f) / 4.0f);
        lod = Min(Max(lod, 0.0f), (float)(kLevels - 1));
    }
    pc.p0[0] = bc.params.rounding;
    pc.p0[1] = lod;
    pc.p0[2] = bc.params.saturation;
    pc.p0[3] = bc.params.brightness;
    pc.p1[0] = bc.params.noise;

    vkCmdBindPipeline(vk_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.composite_pipe);
    VkViewport vp = {};
    vp.width = (float)slot.width;
    vp.height = (float)slot.height;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(vk_cmd, 0, 1, &vp);
    VkRect2D sc = {};
    sc.offset.x = (int32_t)cx0;
    sc.offset.y = (int32_t)cy0;
    sc.extent.width = (uint32_t)(cx1 - cx0 + 0.999f);
    sc.extent.height = (uint32_t)(cy1 - cy0 + 0.999f);
    vkCmdSetScissor(vk_cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(vk_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.composite_layout, 0, 1, &slot.final_set, 0,
                            nullptr);
    vkCmdPushConstants(vk_cmd, s.composite_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(pc), &pc);
    vkCmdDraw(vk_cmd, 6, 1, 0, 0);
}

}  // namespace vkdetail

inline bool Init(const VulkanInitDesc& desc) {
    vkdetail::State& s = vkdetail::GetState();
    if (s.ready || s.desc.device) vkdetail::Destroy(s);
    if (!desc.device || !desc.physical_device || !desc.render_pass || desc.image_count == 0 ||
        desc.image_count > (uint32_t)vkdetail::kMaxSlots)
        return false;
    s.desc = desc;
    if (s.desc.msaa == 0) s.desc.msaa = VK_SAMPLE_COUNT_1_BIT;
    s.slot_count = desc.image_count;
    vkGetPhysicalDeviceMemoryProperties(desc.physical_device, &s.mem_props);
    bool ok = vkdetail::CreateLayouts(s) && vkdetail::CreateDescriptorSets(s) && vkdetail::CreateOffscreenPass(s) &&
              vkdetail::CreatePipelines(s);
    if (!ok) {
        vkdetail::Destroy(s);
        return false;
    }
    s.ready = true;
    return true;
}

inline bool IsAvailable() {
    return vkdetail::GetState().ready;
}

inline void Shutdown() {
    vkdetail::State& s = vkdetail::GetState();
    if (!s.desc.device) return;
    vkdetail::Destroy(s);
}

inline void PrepareFrame(const FrameDesc& fd) {
    using namespace vkdetail;
    State& s = GetState();
    s.current_slot = -1;
    s.current_cmd = VK_NULL_HANDLE;
    if (!s.ready || !fd.cmd || !fd.back_buffer || fd.width <= 0 || fd.height <= 0 || fd.frame_index >= s.slot_count)
        return;
    Slot& slot = s.slots[fd.frame_index];
    if (slot.width != fd.width || slot.height != fd.height) {
        DestroySlotImages(s, slot);
        if (!CreateSlotImages(s, slot, fd.width, fd.height)) return;
    }
    VkCommandBuffer cmd = fd.cmd;
    const bool restore = fd.back_buffer_layout != VK_IMAGE_LAYOUT_UNDEFINED &&
                         fd.back_buffer_layout != VK_IMAGE_LAYOUT_PREINITIALIZED;

    VkImageMemoryBarrier b[2] = {};
    for (int i = 0; i < 2; i++) {
        b[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b[i].subresourceRange.levelCount = 1;
        b[i].subresourceRange.layerCount = 1;
    }
    b[0].image = fd.back_buffer;
    b[0].oldLayout = fd.back_buffer_layout;
    b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[1].image = slot.chain;
    b[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, b);

    VkImageBlit blit = {};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1].x = fd.width;
    blit.srcOffsets[1].y = fd.height;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1].x = (int32_t)slot.level_w[0];
    blit.dstOffsets[1].y = (int32_t)slot.level_h[0];
    blit.dstOffsets[1].z = 1;
    vkCmdBlitImage(cmd, fd.back_buffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.chain,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].newLayout = restore ? fd.back_buffer_layout : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, b);

    for (int l = 1; l < kLevels; l++) {
        RunBlurPass(s, cmd, slot.tmp_fb[l - 1], slot.level_w[l], slot.level_h[l], slot.chain_set[l - 1],
                    1.0f / (float)slot.level_w[l - 1], 0.0f);
        RunBlurPass(s, cmd, slot.chain_fb[l], slot.level_w[l], slot.level_h[l], slot.tmp_set[l - 1], 0.0f,
                    1.0f / (float)slot.level_h[l]);
    }
    s.current_slot = (int)fd.frame_index;
    s.current_cmd = cmd;
}

inline void Blur(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const BlurParams& params) {
    vkdetail::State& s = vkdetail::GetState();
    if (!s.ready || !dl || max.x <= min.x || max.y <= min.y) return;
    const int frame = ImGui::GetFrameCount();
    if (s.cmds_frame != frame) {
        s.cmds.resize(0);
        s.cmds_frame = frame;
    }
    vkdetail::BlurCmd c;
    c.rect = ImVec4(min.x, min.y, max.x, max.y);
    c.params = params;
    s.cmds.push_back(c);
    dl->AddCallback(&vkdetail::BlurCallback, (void*)(intptr_t)(s.cmds.Size - 1));
#if IMGUI_VERSION_NUM >= 19280
    dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
#else
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
#endif
}

}  // namespace fx
}  // namespace imb
