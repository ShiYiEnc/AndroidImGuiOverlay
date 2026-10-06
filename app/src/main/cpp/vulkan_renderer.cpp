#include "vulkan_renderer.h"
#include "imgui_impl_vulkan.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan operation failed (VkResult " + std::to_string(result) + ")");
}
void backendCheck(VkResult result) { if (result < 0) check(result); }
}

VulkanRenderer::VulkanRenderer(std::vector<unsigned char> font, float density)
    : font_(std::move(font)), density_(density) {}
VulkanRenderer::~VulkanRenderer() { shutdown(); }

void VulkanRenderer::instance() {
    if (instance_) return;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ImGui Overlay"; app.apiVersion = VK_API_VERSION_1_0;
    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app; info.enabledExtensionCount = 2; info.ppEnabledExtensionNames = extensions;
    check(vkCreateInstance(&info, nullptr, &instance_));
}
void VulkanRenderer::device(VkSurfaceKHR surface) {
    if (device_) {
        VkBool32 present = false; check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_, family_, surface, &present));
        if (!present) throw std::runtime_error("Selected Vulkan queue cannot present this Surface");
        return;
    }
    uint32_t count = 0; check(vkEnumeratePhysicalDevices(instance_, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count); check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()));
    for (VkPhysicalDevice gpu : devices) {
        uint32_t extCount = 0; check(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, nullptr));
        std::vector<VkExtensionProperties> exts(extCount); check(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, exts.data()));
        bool swapchain = false;
        for (const auto& e : exts) if (std::string(e.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) swapchain = true;
        if (!swapchain) continue;
        uint32_t queues = 0; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queues, nullptr);
        std::vector<VkQueueFamilyProperties> properties(queues); vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queues, properties.data());
        for (uint32_t i = 0; i < queues; ++i) {
            VkBool32 present = false; check(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present));
            if (!(properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            physical_ = gpu; family_ = i; break;
        }
        if (physical_) break;
    }
    if (!physical_) throw std::runtime_error("No Vulkan graphics/present queue with swapchain support");
    float priority = 1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family_; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queueInfo; info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = &extension;
    check(vkCreateDevice(physical_, &info, nullptr, &device_));
    vkGetDeviceQueue(device_, family_, 0, &queue_);
}

void VulkanRenderer::attach(int slot, WindowPtr window, uint64_t generation) {
    Target& t = targets_.at(slot);
    destroyTarget(t);
    t.window = std::move(window); t.generation = generation;
    t.width = ANativeWindow_getWidth(t.window.get()); t.height = ANativeWindow_getHeight(t.window.get());
    instance();
    VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR}; info.window = t.window.get();
    check(vkCreateAndroidSurfaceKHR(instance_, &info, nullptr, &t.surface));
    device(t.surface);
    uint32_t count = 0; check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, t.surface, &count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(count); check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, t.surface, &count, formats.data()));
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_UNDEFINED) { t.format = VK_FORMAT_R8G8B8A8_UNORM; t.colorSpace = f.colorSpace; break; }
        if ((f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            t.format = f.format; t.colorSpace = f.colorSpace; break;
        }
    }
    if (t.format == VK_FORMAT_UNDEFINED) throw std::runtime_error("Vulkan Surface has no transparent RGBA format");
    if (buildSwapchain(t)) createContext(t);
}
void VulkanRenderer::resize(int slot, uint64_t generation, int width, int height) {
    Target& t = targets_.at(slot);
    if (generation != t.generation) return;
    if (width != t.width || height != t.height) { t.width = width; t.height = height; t.rebuild = true; }
}
void VulkanRenderer::detach(int slot, uint64_t generation) {
    Target& t = targets_.at(slot);
    if (generation == t.generation) destroyTarget(t);
}

bool VulkanRenderer::buildSwapchain(Target& t) {
    if (t.width <= 0 || t.height <= 0) return false;
    VkSurfaceCapabilitiesKHR cap{}; check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, t.surface, &cap));
    VkExtent2D extent = cap.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(static_cast<uint32_t>(t.width), cap.minImageExtent.width, cap.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(t.height), cap.minImageExtent.height, cap.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false;
    if (cap.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) t.alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    else if (cap.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) t.alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    else throw std::runtime_error("Vulkan Surface cannot composite premultiplied transparency");
    if (!(cap.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) throw std::runtime_error("Surface cannot be used as a color attachment");
    // Queue work for both contexts must be retired before replacing either swapchain.
    check(vkDeviceWaitIdle(device_));
    destroySwapchain(t);
    t.minImages = std::max(2u, cap.minImageCount);
    if (cap.maxImageCount && cap.maxImageCount < t.minImages) throw std::runtime_error("Surface supports fewer than two swapchain images");
    uint32_t desired = t.minImages + 1;
    if (cap.maxImageCount) desired = std::min(desired, cap.maxImageCount);
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = t.surface; info.minImageCount = desired; info.imageFormat = t.format; info.imageColorSpace = t.colorSpace;
    info.imageExtent = extent; info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; info.preTransform = cap.currentTransform;
    info.compositeAlpha = t.alpha; info.presentMode = VK_PRESENT_MODE_FIFO_KHR; info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &info, nullptr, &t.swapchain));
    uint32_t count = 0; check(vkGetSwapchainImagesKHR(device_, t.swapchain, &count, nullptr));
    t.images.resize(count); check(vkGetSwapchainImagesKHR(device_, t.swapchain, &count, t.images.data()));
    if (!t.renderPass) {
        VkAttachmentDescription color{};
        color.format = t.format; color.samples = VK_SAMPLE_COUNT_1_BIT; color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE; color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
        dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1; rp.pAttachments = &color; rp.subpassCount = 1; rp.pSubpasses = &subpass;
        rp.dependencyCount = 1; rp.pDependencies = &dependency;
        check(vkCreateRenderPass(device_, &rp, nullptr, &t.renderPass));
    }
    t.views.resize(count); t.buffers.resize(count); t.rendered.resize(count);
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = t.images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = t.format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
        check(vkCreateImageView(device_, &view, nullptr, &t.views[i]));
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = t.renderPass; fb.attachmentCount = 1; fb.pAttachments = &t.views[i]; fb.width = extent.width; fb.height = extent.height; fb.layers = 1;
        check(vkCreateFramebuffer(device_, &fb, nullptr, &t.buffers[i]));
        // Present semaphores belong to images: reacquisition guarantees the previous present consumed them.
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &t.rendered[i]));
    }
    for (auto& f : t.flights) {
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = family_; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device_, &pool, nullptr, &f.pool));
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = f.pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_, &allocate, &f.command));
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device_, &fence, nullptr, &f.fence));
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &f.acquired));
    }
    t.width = extent.width; t.height = extent.height; t.frame = 0; t.rebuild = false;
    if (t.context) {
        ImGui::SetCurrentContext(t.context);
        ImGui_ImplVulkan_Shutdown();
        // The upstream backend leaves sampler sets in caller-owned pools on shutdown.
        check(vkResetDescriptorPool(device_, t.descriptors, 0));
        ImGui_ImplVulkan_InitInfo backend{};
        backend.ApiVersion = VK_API_VERSION_1_0; backend.Instance = instance_; backend.PhysicalDevice = physical_; backend.Device = device_;
        backend.QueueFamily = family_; backend.Queue = queue_; backend.DescriptorPool = t.descriptors;
        backend.MinImageCount = t.minImages; backend.ImageCount = count; backend.PipelineInfoMain.RenderPass = t.renderPass;
        backend.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT; backend.CheckVkResultFn = backendCheck;
        ImGui_ImplVulkan_Init(&backend);
    }
    return true;
}

void VulkanRenderer::createContext(Target& t) {
    t.context = ImGui::CreateContext(); ImGui::SetCurrentContext(t.context);
    ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr; io.BackendPlatformName = "android_overlay_jni";
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle(); style.ScaleAllSizes(density_ * 1.15f); style.WindowBorderSize = 0;
    style.WindowRounding = 0; style.FrameRounding = 3 * density_;
    style.Colors[ImGuiCol_WindowBg] = {0.96f, 0.98f, 0.98f, 1.0f};
    ImFontConfig fontConfig{}; fontConfig.FontDataOwnedByAtlas = false;
    if (!io.Fonts->AddFontFromMemoryTTF(font_.data(), static_cast<int>(font_.size()), 17 * density_, &fontConfig,
                                       io.Fonts->GetGlyphRangesChineseFull())) throw std::runtime_error("Chinese font initialization failed");
    VkDescriptorPoolSize pools[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64}, {VK_DESCRIPTOR_TYPE_SAMPLER, 16}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; pool.maxSets = 96; pool.poolSizeCount = 3; pool.pPoolSizes = pools;
    check(vkCreateDescriptorPool(device_, &pool, nullptr, &t.descriptors));
    ImGui_ImplVulkan_InitInfo backend{};
    backend.ApiVersion = VK_API_VERSION_1_0; backend.Instance = instance_; backend.PhysicalDevice = physical_; backend.Device = device_;
    backend.QueueFamily = family_; backend.Queue = queue_; backend.DescriptorPool = t.descriptors;
    backend.MinImageCount = t.minImages; backend.ImageCount = static_cast<uint32_t>(t.images.size());
    backend.PipelineInfoMain.RenderPass = t.renderPass; backend.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    backend.CheckVkResultFn = backendCheck;
    if (!ImGui_ImplVulkan_Init(&backend)) throw std::runtime_error("ImGui Vulkan backend initialization failed");
}

void VulkanRenderer::touch(uint64_t generation, int action, float x, float y) {
    Target& t = targets_[1];
    if (generation != t.generation || !t.context) return;
    ImGui::SetCurrentContext(t.context); ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen); io.AddMousePosEvent(x, y);
    if (action == 0) io.AddMouseButtonEvent(0, true);
    else if (action == 1 || action == 3) { io.AddMouseButtonEvent(0, false); if (action == 3) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); }
}
void VulkanRenderer::render(TemplateState& state, const OverlayMetrics& metrics, float delta) {
    if (metrics.density != density_) {
        check(vkDeviceWaitIdle(device_));
        for (auto& t : targets_) {
            if (!t.context) continue;
            ImGui::SetCurrentContext(t.context);
            ImGui_ImplVulkan_Shutdown(); ImGui::DestroyContext(t.context); t.context = nullptr;
            vkDestroyDescriptorPool(device_, t.descriptors, nullptr); t.descriptors = VK_NULL_HANDLE;
        }
        density_ = metrics.density;
    }
    // Draw the panel first so its settings are visible in the overlay on the same tick.
    for (int slot : {1, 0}) {
        Target& t = targets_[slot]; if (!t.window) continue;
        if (t.rebuild && !buildSwapchain(t)) continue;
        if (!t.context) createContext(t);
        draw(t, slot, state, metrics, delta);
    }
}
void VulkanRenderer::draw(Target& t, int slot, TemplateState& state, const OverlayMetrics& metrics, float delta) {
    Flight& f = t.flights[t.frame];
    VkResult result = vkWaitForFences(device_, 1, &f.fence, VK_TRUE, 1000000);
    if (result == VK_TIMEOUT) return; check(result);
    uint32_t image;
    result = vkAcquireNextImageKHR(device_, t.swapchain, 1000000, f.acquired, VK_NULL_HANDLE, &image);
    if (result == VK_TIMEOUT || result == VK_NOT_READY) return;
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { t.rebuild = true; return; }
    if (result == VK_ERROR_SURFACE_LOST_KHR) throw std::runtime_error("Vulkan Surface lost; restart the overlay");
    bool suboptimal = result == VK_SUBOPTIMAL_KHR;
    if (!suboptimal) check(result);
    ImGui::SetCurrentContext(t.context);
    ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = {static_cast<float>(t.width), static_cast<float>(t.height)};
    io.DeltaTime = std::clamp(delta, 0.001f, 0.1f); io.DisplayFramebufferScale = {1, 1};
    ImGui_ImplVulkan_NewFrame(); ImGui::NewFrame();
    if (slot == 1) DrawPanel(state);
    else { OverlayMetrics m = metrics; m.width = t.width; m.height = t.height; DrawOverlay(*ImGui::GetBackgroundDrawList(), m, state); }
    ImGui::Render();
    check(vkResetCommandPool(device_, f.pool, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(f.command, &begin));
    VkClearValue clear{}; // Transparent black plus backend blending yields premultiplied RGB/alpha.
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; pass.renderPass = t.renderPass; pass.framebuffer = t.buffers[image];
    pass.renderArea.extent = {static_cast<uint32_t>(t.width), static_cast<uint32_t>(t.height)}; pass.clearValueCount = 1; pass.pClearValues = &clear;
    vkCmdBeginRenderPass(f.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), f.command);
    vkCmdEndRenderPass(f.command); check(vkEndCommandBuffer(f.command));
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &f.acquired;
    submit.pWaitDstStageMask = &stage; submit.commandBufferCount = 1; submit.pCommandBuffers = &f.command;
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &t.rendered[image];
    check(vkResetFences(device_, 1, &f.fence)); check(vkQueueSubmit(queue_, 1, &submit, f.fence));
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount = 1; present.pWaitSemaphores = &t.rendered[image];
    present.swapchainCount = 1; present.pSwapchains = &t.swapchain; present.pImageIndices = &image;
    result = vkQueuePresentKHR(queue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || suboptimal) t.rebuild = true;
    else check(result);
    t.frame = (t.frame + 1) % t.flights.size();
}

void VulkanRenderer::destroySwapchain(Target& t) noexcept {
    if (!device_) return;
    for (auto& f : t.flights) {
        vkDestroyFence(device_, f.fence, nullptr); vkDestroySemaphore(device_, f.acquired, nullptr);
        vkDestroyCommandPool(device_, f.pool, nullptr); f = {};
    }
    for (auto fb : t.buffers) vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto view : t.views) vkDestroyImageView(device_, view, nullptr);
    for (auto semaphore : t.rendered) vkDestroySemaphore(device_, semaphore, nullptr);
    t.buffers.clear(); t.views.clear(); t.rendered.clear(); t.images.clear();
    vkDestroySwapchainKHR(device_, t.swapchain, nullptr); t.swapchain = VK_NULL_HANDLE;
}
void VulkanRenderer::destroyTarget(Target& t) noexcept {
    if (device_) vkDeviceWaitIdle(device_);
    if (t.context) {
        ImGui::SetCurrentContext(t.context);
        try { if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplVulkan_Shutdown(); } catch (...) {}
        ImGui::DestroyContext(t.context); t.context = nullptr;
    }
    destroySwapchain(t);
    if (device_) { vkDestroyDescriptorPool(device_, t.descriptors, nullptr); vkDestroyRenderPass(device_, t.renderPass, nullptr); }
    if (instance_) vkDestroySurfaceKHR(instance_, t.surface, nullptr);
    t = Target{};
}
bool VulkanRenderer::hasSurface() const { return targets_[0].window || targets_[1].window; }
void VulkanRenderer::shutdown() noexcept {
    for (auto& t : targets_) destroyTarget(t);
    if (device_) vkDestroyDevice(device_, nullptr); device_ = VK_NULL_HANDLE; queue_ = VK_NULL_HANDLE;
    if (instance_) vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; physical_ = VK_NULL_HANDLE; family_ = UINT32_MAX;
}
