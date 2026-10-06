#pragma once
#include <android/native_window.h>
#include <vulkan/vulkan.h>
#include "template_ui.h"
#include <array>
#include <memory>
#include <vector>

using WindowPtr = std::shared_ptr<ANativeWindow>;

class VulkanRenderer {
public:
    VulkanRenderer(std::vector<unsigned char> font, float density);
    ~VulkanRenderer();
    void attach(int slot, WindowPtr window, uint64_t generation);
    void resize(int slot, uint64_t generation, int width, int height);
    void detach(int slot, uint64_t generation);
    void touch(uint64_t generation, int action, float x, float y);
    void render(TemplateState& state, const OverlayMetrics& metrics, float delta);
    void shutdown() noexcept;
    bool hasSurface() const;
private:
    struct Flight {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
    };
    struct Target {
        WindowPtr window;
        uint64_t generation = 0;
        ImGuiContext* context = nullptr;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        VkDescriptorPool descriptors = VK_NULL_HANDLE;
        std::vector<VkImage> images;
        std::vector<VkImageView> views;
        std::vector<VkFramebuffer> buffers;
        std::vector<VkSemaphore> rendered;
        std::array<Flight, 2> flights;
        uint32_t frame = 0, minImages = 2;
        int width = 0, height = 0;
        bool rebuild = true;
    };
    std::array<Target, 2> targets_;
    std::vector<unsigned char> font_;
    float density_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = UINT32_MAX;
    void instance();
    void device(VkSurfaceKHR surface);
    bool buildSwapchain(Target& t);
    void createContext(Target& t);
    void draw(Target& t, int slot, TemplateState& state, const OverlayMetrics& metrics, float delta);
    void destroySwapchain(Target& t) noexcept;
    void destroyTarget(Target& t) noexcept;
};
