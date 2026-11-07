// imb::fx blur on SDL3 + Vulkan: animated background, a window blurred with
// imb::fx::BlurCurrentWindow() and a second one with sliders. Needs Dear ImGui >= 1.90.1.
// Link SDL3, vulkan-1 and imgui (imgui*.cpp, imgui_impl_sdl3.cpp, imgui_impl_vulkan.cpp);
// include ImForge/include, imgui and imgui/backends.
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "imforge/fx/imb_fx_vulkan.h"
#include "demo_shaders.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if IMGUI_VERSION_NUM < 19010
#error "This demo needs Dear ImGui 1.90.1+ (imgui_impl_sdl3 and ImGui_ImplVulkan_InitInfo::RenderPass)"
#endif

static const uint32_t kMinImageCount = 2;
static const uint32_t kMaxImages = 8;
static const int kQuadCount = 14;

struct QuadPC {
    float center_size[4];
    float color[4];
    float angle[4];
};

struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer scene_fb = VK_NULL_HANDLE;
    VkFramebuffer ui_fb = VK_NULL_HANDLE;
};

struct Semaphores {
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore complete = VK_NULL_HANDLE;
};

struct App {
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSurfaceFormatKHR format = {};
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent = {};
    VkRenderPass scene_pass = VK_NULL_HANDLE;
    VkRenderPass ui_pass = VK_NULL_HANDLE;
    VkDescriptorPool imgui_pool = VK_NULL_HANDLE;
    VkPipelineLayout quad_layout = VK_NULL_HANDLE;
    VkPipeline quad_pipe = VK_NULL_HANDLE;
    Frame frames[kMaxImages];
    uint32_t image_count = 0;
    Semaphores sems[kMaxImages + 1];
    uint32_t sem_count = 0;
    uint32_t sem_index = 0;
    bool rebuild = false;
};

static void Check(VkResult err) {
    if (err == VK_SUCCESS) return;
    fprintf(stderr, "[vulkan] VkResult = %d\n", (int)err);
    if (err < 0) abort();
}

static void CreateInstance(App& app) {
    uint32_t sdl_ext_count = 0;
    const char* const* sdl_ext = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
    ImVector<const char*> extensions;
    for (uint32_t i = 0; i < sdl_ext_count; i++) extensions.push_back(sdl_ext[i]);

    uint32_t prop_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &prop_count, nullptr);
    ImVector<VkExtensionProperties> props;
    props.resize((int)prop_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &prop_count, props.Data);
    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    for (const VkExtensionProperties& p : props)
        if (strcmp(p.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif
    VkApplicationInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "imb_fx vulkan blur demo";
    ai.apiVersion = VK_API_VERSION_1_0;
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = (uint32_t)extensions.Size;
    ci.ppEnabledExtensionNames = extensions.Data;
    Check(vkCreateInstance(&ci, nullptr, &app.instance));
}

static void PickDevice(App& app) {
    uint32_t count = 0;
    Check(vkEnumeratePhysicalDevices(app.instance, &count, nullptr));
    ImVector<VkPhysicalDevice> gpus;
    gpus.resize((int)count);
    Check(vkEnumeratePhysicalDevices(app.instance, &count, gpus.Data));
    if (count == 0) {
        fprintf(stderr, "No Vulkan device found\n");
        exit(1);
    }
    app.gpu = gpus[0];
    for (VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(gpu, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            app.gpu = gpu;
            break;
        }
    }
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(app.gpu, &family_count, nullptr);
    ImVector<VkQueueFamilyProperties> families;
    families.resize((int)family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(app.gpu, &family_count, families.Data);
    app.queue_family = (uint32_t)-1;
    for (uint32_t i = 0; i < family_count; i++) {
        if ((families[(int)i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            SDL_Vulkan_GetPresentationSupport(app.instance, app.gpu, i)) {
            app.queue_family = i;
            break;
        }
    }
    if (app.queue_family == (uint32_t)-1) {
        fprintf(stderr, "No graphics+present queue family\n");
        exit(1);
    }
}

static void CreateDevice(App& app) {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = app.queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    ImVector<const char*> extensions;
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    uint32_t prop_count = 0;
    vkEnumerateDeviceExtensionProperties(app.gpu, nullptr, &prop_count, nullptr);
    ImVector<VkExtensionProperties> props;
    props.resize((int)prop_count);
    vkEnumerateDeviceExtensionProperties(app.gpu, nullptr, &prop_count, props.Data);
    for (const VkExtensionProperties& p : props)
        if (strcmp(p.extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) == 0)
            extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif
    VkDeviceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = (uint32_t)extensions.Size;
    ci.ppEnabledExtensionNames = extensions.Data;
    Check(vkCreateDevice(app.gpu, &ci, nullptr, &app.device));
    vkGetDeviceQueue(app.device, app.queue_family, 0, &app.queue);
}

static VkRenderPass CreatePass(App& app, bool clear) {
    VkAttachmentDescription att = {};
    att.format = app.format.format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout = clear ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref = {};
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sp = {};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;
    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = clear ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    VkRenderPassCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sp;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    VkRenderPass pass = VK_NULL_HANDLE;
    Check(vkCreateRenderPass(app.device, &ci, nullptr, &pass));
    return pass;
}

static void DestroySwapchainResources(App& app) {
    for (uint32_t i = 0; i < app.image_count; i++) {
        Frame& f = app.frames[i];
        if (f.scene_fb) vkDestroyFramebuffer(app.device, f.scene_fb, nullptr);
        if (f.ui_fb) vkDestroyFramebuffer(app.device, f.ui_fb, nullptr);
        if (f.view) vkDestroyImageView(app.device, f.view, nullptr);
        if (f.fence) vkDestroyFence(app.device, f.fence, nullptr);
        if (f.pool) vkDestroyCommandPool(app.device, f.pool, nullptr);
        f = Frame();
    }
    for (uint32_t i = 0; i < app.sem_count; i++) {
        vkDestroySemaphore(app.device, app.sems[i].acquired, nullptr);
        vkDestroySemaphore(app.device, app.sems[i].complete, nullptr);
        app.sems[i] = Semaphores();
    }
    app.image_count = 0;
    app.sem_count = 0;
}

static void CreateSwapchain(App& app, int w, int h) {
    VkSwapchainKHR old = app.swapchain;
    VkSurfaceCapabilitiesKHR caps;
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app.gpu, app.surface, &caps));
    uint32_t count = kMinImageCount + 1;
    if (count < caps.minImageCount) count = caps.minImageCount;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    if (count > kMaxImages) count = kMaxImages;
    app.extent.width = caps.currentExtent.width != 0xFFFFFFFFu ? caps.currentExtent.width : (uint32_t)w;
    app.extent.height = caps.currentExtent.height != 0xFFFFFFFFu ? caps.currentExtent.height : (uint32_t)h;

    VkSwapchainCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = app.surface;
    ci.minImageCount = count;
    ci.imageFormat = app.format.format;
    ci.imageColorSpace = app.format.colorSpace;
    ci.imageExtent = app.extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    Check(vkCreateSwapchainKHR(app.device, &ci, nullptr, &app.swapchain));
    if (old) vkDestroySwapchainKHR(app.device, old, nullptr);

    VkImage images[kMaxImages];
    uint32_t image_count = 0;
    Check(vkGetSwapchainImagesKHR(app.device, app.swapchain, &image_count, nullptr));
    if (image_count > kMaxImages) image_count = kMaxImages;
    Check(vkGetSwapchainImagesKHR(app.device, app.swapchain, &image_count, images));
    app.image_count = image_count;

    for (uint32_t i = 0; i < image_count; i++) {
        Frame& f = app.frames[i];
        f.image = images[i];
        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = f.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = app.format.format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        Check(vkCreateImageView(app.device, &vci, nullptr, &f.view));
        VkFramebufferCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.attachmentCount = 1;
        fci.pAttachments = &f.view;
        fci.width = app.extent.width;
        fci.height = app.extent.height;
        fci.layers = 1;
        fci.renderPass = app.scene_pass;
        Check(vkCreateFramebuffer(app.device, &fci, nullptr, &f.scene_fb));
        fci.renderPass = app.ui_pass;
        Check(vkCreateFramebuffer(app.device, &fci, nullptr, &f.ui_fb));
        VkCommandPoolCreateInfo pci = {};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = app.queue_family;
        Check(vkCreateCommandPool(app.device, &pci, nullptr, &f.pool));
        VkCommandBufferAllocateInfo cai = {};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = f.pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        Check(vkAllocateCommandBuffers(app.device, &cai, &f.cmd));
        VkFenceCreateInfo fi = {};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        Check(vkCreateFence(app.device, &fi, nullptr, &f.fence));
    }
    app.sem_count = image_count + 1;
    app.sem_index = 0;
    for (uint32_t i = 0; i < app.sem_count; i++) {
        VkSemaphoreCreateInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        Check(vkCreateSemaphore(app.device, &si, nullptr, &app.sems[i].acquired));
        Check(vkCreateSemaphore(app.device, &si, nullptr, &app.sems[i].complete));
    }
}

static VkShaderModule CreateModule(App& app, const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(app.device, &ci, nullptr, &m));
    return m;
}

static void CreateQuadPipeline(App& app) {
    VkPushConstantRange range = {};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.size = sizeof(QuadPC);
    VkPipelineLayoutCreateInfo lci = {};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &range;
    Check(vkCreatePipelineLayout(app.device, &lci, nullptr, &app.quad_layout));

    VkShaderModule vs = CreateModule(app, demo_shaders::kBgQuadVertSpv, demo_shaders::kBgQuadVertSize);
    VkShaderModule fs = CreateModule(app, demo_shaders::kBgQuadFragSpv, demo_shaders::kBgQuadFragSize);
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
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState ba = {};
    ba.blendEnable = VK_TRUE;
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
    ci.layout = app.quad_layout;
    ci.renderPass = app.scene_pass;
    Check(vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &ci, nullptr, &app.quad_pipe));
    vkDestroyShaderModule(app.device, vs, nullptr);
    vkDestroyShaderModule(app.device, fs, nullptr);
}

static void Hsv(float h, float s, float v, float* rgb) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h - floorf(h), s, v, r, g, b);
    rgb[0] = r;
    rgb[1] = g;
    rgb[2] = b;
}

static void DrawBackground(App& app, VkCommandBuffer cmd, float time) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app.quad_pipe);
    VkViewport vp = {};
    vp.width = (float)app.extent.width;
    vp.height = (float)app.extent.height;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc = {};
    sc.extent = app.extent;
    vkCmdSetScissor(cmd, 0, 1, &sc);
    const float aspect = vp.width / vp.height;
    for (int i = 0; i < kQuadCount; i++) {
        const float t = (float)i / (float)kQuadCount;
        const float speed = 0.25f + 0.35f * t;
        const float orbit = 0.25f + 0.6f * t;
        const float a = time * speed + t * 6.2831853f;
        QuadPC pc = {};
        pc.center_size[0] = cosf(a) * orbit;
        pc.center_size[1] = sinf(a * 1.3f) * orbit * 0.8f;
        pc.center_size[2] = 0.12f + 0.1f * sinf(time * 0.7f + t * 9.0f);
        pc.center_size[3] = pc.center_size[2];
        Hsv(t + time * 0.03f, 0.75f, 0.95f, pc.color);
        pc.color[3] = 0.85f;
        pc.angle[0] = time * (0.5f + t) + t;
        pc.angle[1] = aspect;
        vkCmdPushConstants(cmd, app.quad_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
}

static void RenderFrame(App& app, ImDrawData* draw_data, float time) {
    Semaphores& sem = app.sems[app.sem_index];
    uint32_t idx = 0;
    VkResult err = vkAcquireNextImageKHR(app.device, app.swapchain, UINT64_MAX, sem.acquired, VK_NULL_HANDLE, &idx);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) app.rebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) Check(err);

    Frame& f = app.frames[idx];
    Check(vkWaitForFences(app.device, 1, &f.fence, VK_TRUE, UINT64_MAX));
    Check(vkResetFences(app.device, 1, &f.fence));
    Check(vkResetCommandPool(app.device, f.pool, 0));
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(f.cmd, &bi));

    VkClearValue clear = {};
    clear.color.float32[0] = 0.06f;
    clear.color.float32[1] = 0.07f;
    clear.color.float32[2] = 0.10f;
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = app.scene_pass;
    rp.framebuffer = f.scene_fb;
    rp.renderArea.extent = app.extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(f.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    DrawBackground(app, f.cmd, time);
    vkCmdEndRenderPass(f.cmd);

    imb::fx::FrameDesc fd;
    fd.cmd = f.cmd;
    fd.back_buffer = f.image;
    fd.back_buffer_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    fd.width = (int)app.extent.width;
    fd.height = (int)app.extent.height;
    fd.frame_index = idx;
    imb::fx::PrepareFrame(fd);

    rp.renderPass = app.ui_pass;
    rp.framebuffer = f.ui_fb;
    rp.clearValueCount = 0;
    vkCmdBeginRenderPass(f.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(draw_data, f.cmd);
    vkCmdEndRenderPass(f.cmd);
    Check(vkEndCommandBuffer(f.cmd));

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &sem.acquired;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &sem.complete;
    Check(vkQueueSubmit(app.queue, 1, &si, f.fence));

    if (app.rebuild) return;
    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &sem.complete;
    pi.swapchainCount = 1;
    pi.pSwapchains = &app.swapchain;
    pi.pImageIndices = &idx;
    err = vkQueuePresentKHR(app.queue, &pi);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) app.rebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) Check(err);
    app.sem_index = (app.sem_index + 1) % app.sem_count;
}

static void DrawUi() {
    static float radius = 18.0f;
    static float rounding = 12.0f;
    static float saturation = 1.2f;
    static float brightness = 0.9f;
    static float noise = 0.4f;
    static ImVec4 tint(1.0f, 1.0f, 1.0f, 1.0f);
    static bool enabled = true;

    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 150), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.15f);
    ImGui::Begin("Frosted (BlurCurrentWindow 12px)");
    imb::fx::BlurCurrentWindow(12.0f);
    ImGui::TextWrapped("This window calls imb::fx::BlurCurrentWindow(12.f) right after Begin(). "
                       "Drag it over the moving quads.");
    ImGui::Text("fx available: %s", imb::fx::IsAvailable() ? "yes" : "no");
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(480, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 320), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("Blur controls");
    if (enabled) {
        imb::fx::BlurParams p;
        p.radius = radius;
        p.rounding = rounding;
        p.tint = ImGui::ColorConvertFloat4ToU32(tint);
        p.saturation = saturation;
        p.brightness = brightness;
        p.noise = noise;
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        imb::fx::Blur(ImGui::GetWindowDrawList(), pos, ImVec2(pos.x + size.x, pos.y + size.y), p);
    }
    ImGui::Checkbox("Enabled", &enabled);
    ImGui::SliderFloat("Radius", &radius, 0.0f, 64.0f, "%.1f px");
    ImGui::SliderFloat("Rounding", &rounding, 0.0f, 48.0f, "%.1f");
    ImGui::SliderFloat("Saturation", &saturation, 0.0f, 2.0f);
    ImGui::SliderFloat("Brightness", &brightness, 0.0f, 2.0f);
    ImGui::SliderFloat("Noise", &noise, 0.0f, 1.0f);
    ImGui::ColorEdit4("Tint", (float*)&tint);
    ImGui::End();
}

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }
    App app;
    SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                              SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    app.window = SDL_CreateWindow("ImForge imb::fx Vulkan blur demo", 1280, 720, flags);
    if (!app.window) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }
    CreateInstance(app);
    PickDevice(app);
    CreateDevice(app);
    if (!SDL_Vulkan_CreateSurface(app.window, app.instance, nullptr, &app.surface)) {
        printf("Failed to create Vulkan surface: %s\n", SDL_GetError());
        return 1;
    }
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(app.gpu, app.surface, &count, nullptr);
        ImVector<VkSurfaceFormatKHR> formats;
        formats.resize((int)count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(app.gpu, app.surface, &count, formats.Data);
        app.format = formats[0];
        if (count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) app.format.format = VK_FORMAT_B8G8R8A8_UNORM;
        for (const VkSurfaceFormatKHR& sf : formats)
            if (sf.format == VK_FORMAT_B8G8R8A8_UNORM || sf.format == VK_FORMAT_R8G8B8A8_UNORM) {
                app.format = sf;
                break;
            }
    }
    app.scene_pass = CreatePass(app, true);
    app.ui_pass = CreatePass(app, false);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(app.window, &w, &h);
    CreateSwapchain(app, w, h);
    CreateQuadPipeline(app);
    {
        VkDescriptorPoolSize size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 };
        VkDescriptorPoolCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = 16;
        ci.poolSizeCount = 1;
        ci.pPoolSizes = &size;
        Check(vkCreateDescriptorPool(app.device, &ci, nullptr, &app.imgui_pool));
    }
    SDL_SetWindowPosition(app.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(app.window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 12.0f;
    ImGui_ImplSDL3_InitForVulkan(app.window);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = app.instance;
    init_info.PhysicalDevice = app.gpu;
    init_info.Device = app.device;
    init_info.QueueFamily = app.queue_family;
    init_info.Queue = app.queue;
    init_info.DescriptorPool = app.imgui_pool;
    init_info.RenderPass = app.ui_pass;
    init_info.Subpass = 0;
    init_info.MinImageCount = kMinImageCount;
    init_info.ImageCount = app.image_count;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = Check;
    ImGui_ImplVulkan_Init(&init_info);

    imb::fx::VulkanInitDesc fx;
    fx.instance = app.instance;
    fx.physical_device = app.gpu;
    fx.device = app.device;
    fx.queue_family = app.queue_family;
    fx.queue = app.queue;
    fx.render_pass = app.ui_pass;
    fx.color_format = app.format.format;
    fx.image_count = kMaxImages;
    fx.msaa = VK_SAMPLE_COUNT_1_BIT;
    if (!imb::fx::Init(fx)) fprintf(stderr, "imb::fx::Init failed; windows will not be blurred\n");

    const Uint64 start = SDL_GetTicks();
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(app.window))
                done = true;
        }
        if (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }
        SDL_GetWindowSizeInPixels(app.window, &w, &h);
        if (w > 0 && h > 0 &&
            (app.rebuild || app.extent.width != (uint32_t)w || app.extent.height != (uint32_t)h)) {
            Check(vkDeviceWaitIdle(app.device));
            DestroySwapchainResources(app);
            CreateSwapchain(app, w, h);
            ImGui_ImplVulkan_SetMinImageCount(kMinImageCount);
            app.rebuild = false;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        DrawUi();
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        if (draw_data->DisplaySize.x > 0.0f && draw_data->DisplaySize.y > 0.0f)
            RenderFrame(app, draw_data, (float)(SDL_GetTicks() - start) * 0.001f);
    }

    Check(vkDeviceWaitIdle(app.device));
    imb::fx::Shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    DestroySwapchainResources(app);
    vkDestroySwapchainKHR(app.device, app.swapchain, nullptr);
    vkDestroyPipeline(app.device, app.quad_pipe, nullptr);
    vkDestroyPipelineLayout(app.device, app.quad_layout, nullptr);
    vkDestroyDescriptorPool(app.device, app.imgui_pool, nullptr);
    vkDestroyRenderPass(app.device, app.scene_pass, nullptr);
    vkDestroyRenderPass(app.device, app.ui_pass, nullptr);
    SDL_Vulkan_DestroySurface(app.instance, app.surface, nullptr);
    vkDestroyDevice(app.device, nullptr);
    vkDestroyInstance(app.instance, nullptr);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
