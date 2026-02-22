#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL2/SDL.h>
#include <vector>
#include <functional>
#include <cstdint>

namespace wowee {
namespace rendering {

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct FrameData {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
};

class VkContext {
public:
    VkContext() = default;
    ~VkContext();

    VkContext(const VkContext&) = delete;
    VkContext& operator=(const VkContext&) = delete;

    bool initialize(SDL_Window* window);
    void shutdown();

    // Swapchain management
    bool recreateSwapchain(int width, int height);

    // Frame operations
    VkCommandBuffer beginFrame(uint32_t& imageIndex);
    void endFrame(VkCommandBuffer cmd, uint32_t imageIndex);

    // Single-time command buffer helpers
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cmd);

    // Immediate submit for one-off GPU work (descriptor pool creation, etc.)
    void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

    // Accessors
    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    uint32_t getGraphicsQueueFamily() const { return graphicsQueueFamily; }
    VmaAllocator getAllocator() const { return allocator; }
    VkSurfaceKHR getSurface() const { return surface; }

    VkSwapchainKHR getSwapchain() const { return swapchain; }
    VkFormat getSwapchainFormat() const { return swapchainFormat; }
    VkExtent2D getSwapchainExtent() const { return swapchainExtent; }
    const std::vector<VkImageView>& getSwapchainImageViews() const { return swapchainImageViews; }
    uint32_t getSwapchainImageCount() const { return static_cast<uint32_t>(swapchainImages.size()); }

    uint32_t getCurrentFrame() const { return currentFrame; }
    const FrameData& getCurrentFrameData() const { return frames[currentFrame]; }

    // For ImGui
    VkRenderPass getImGuiRenderPass() const { return imguiRenderPass; }
    VkDescriptorPool getImGuiDescriptorPool() const { return imguiDescriptorPool; }
    const std::vector<VkFramebuffer>& getSwapchainFramebuffers() const { return swapchainFramebuffers; }

    bool isSwapchainDirty() const { return swapchainDirty; }

    // MSAA
    VkSampleCountFlagBits getMsaaSamples() const { return msaaSamples_; }
    void setMsaaSamples(VkSampleCountFlagBits samples);
    VkSampleCountFlagBits getMaxUsableSampleCount() const;

    // UI texture upload: creates a Vulkan texture from RGBA data and returns
    // a VkDescriptorSet suitable for use as ImTextureID.
    // The caller does NOT need to free the result — resources are tracked and
    // cleaned up when the VkContext is destroyed.
    VkDescriptorSet uploadImGuiTexture(const uint8_t* rgba, int width, int height);

private:
    bool createInstance(SDL_Window* window);
    bool createSurface(SDL_Window* window);
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createSwapchain(int width, int height);
    void destroySwapchain();
    bool createCommandPools();
    bool createSyncObjects();
    bool createImGuiResources();
    void destroyImGuiResources();

    // vk-bootstrap objects (kept alive for swapchain recreation etc.)
    vkb::Instance vkbInstance_;
    vkb::PhysicalDevice vkbPhysicalDevice_;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = 0;
    uint32_t presentQueueFamily = 0;

    // Swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent = {0, 0};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    bool swapchainDirty = false;

    // Per-frame resources
    FrameData frames[MAX_FRAMES_IN_FLIGHT];
    uint32_t currentFrame = 0;

    // Immediate submit resources
    VkCommandPool immCommandPool = VK_NULL_HANDLE;
    VkFence immFence = VK_NULL_HANDLE;

    // Depth buffer (shared across all framebuffers)
    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VmaAllocation depthAllocation = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    bool createDepthBuffer();
    void destroyDepthBuffer();

    // MSAA resources
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkImage msaaColorImage_ = VK_NULL_HANDLE;
    VkImageView msaaColorView_ = VK_NULL_HANDLE;
    VmaAllocation msaaColorAllocation_ = VK_NULL_HANDLE;

    bool createMsaaColorImage();
    void destroyMsaaColorImage();

    // ImGui resources
    VkRenderPass imguiRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    // Shared sampler for UI textures (created on first uploadImGuiTexture call)
    VkSampler uiTextureSampler_ = VK_NULL_HANDLE;

    // Tracked UI textures for cleanup
    struct UiTexture {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
    };
    std::vector<UiTexture> uiTextures_;

#ifndef NDEBUG
    bool enableValidation = true;
#else
    bool enableValidation = false;
#endif
};

} // namespace rendering
} // namespace wowee
