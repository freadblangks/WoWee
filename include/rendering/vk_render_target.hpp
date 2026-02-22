#pragma once

#include "rendering/vk_utils.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

namespace wowee {
namespace rendering {

class VkContext;

/**
 * Off-screen render target encapsulating VkRenderPass + VkFramebuffer + color VkImage.
 * Used for minimap compositing, world map compositing, and other off-screen passes.
 */
class VkRenderTarget {
public:
    VkRenderTarget() = default;
    ~VkRenderTarget();

    VkRenderTarget(const VkRenderTarget&) = delete;
    VkRenderTarget& operator=(const VkRenderTarget&) = delete;

    /**
     * Create the render target with given dimensions and format.
     * Creates: color image, image view, sampler, render pass, framebuffer.
     * When withDepth is true, also creates a D32_SFLOAT depth attachment.
     */
    bool create(VkContext& ctx, uint32_t width, uint32_t height,
                VkFormat format = VK_FORMAT_R8G8B8A8_UNORM, bool withDepth = false);

    /**
     * Destroy all Vulkan resources.
     */
    void destroy(VkDevice device, VmaAllocator allocator);

    /**
     * Begin the off-screen render pass (clears to given color).
     * Must be called outside any other active render pass.
     */
    void beginPass(VkCommandBuffer cmd,
                   const VkClearColorValue& clear = {{0.0f, 0.0f, 0.0f, 1.0f}});

    /**
     * End the off-screen render pass.
     * After this, the color image is in SHADER_READ_ONLY_OPTIMAL layout.
     */
    void endPass(VkCommandBuffer cmd);

    // Accessors
    VkImage getColorImage() const { return colorImage_.image; }
    VkImageView getColorImageView() const { return colorImage_.imageView; }
    VkSampler getSampler() const { return sampler_; }
    VkRenderPass getRenderPass() const { return renderPass_; }
    VkExtent2D getExtent() const { return { colorImage_.extent.width, colorImage_.extent.height }; }
    VkFormat getFormat() const { return colorImage_.format; }
    bool isValid() const { return framebuffer_ != VK_NULL_HANDLE; }

    /**
     * Descriptor info for binding the color attachment as a texture in a shader.
     */
    VkDescriptorImageInfo descriptorInfo() const;

private:
    AllocatedImage colorImage_{};
    AllocatedImage depthImage_{};
    bool hasDepth_ = false;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
};

} // namespace rendering
} // namespace wowee
