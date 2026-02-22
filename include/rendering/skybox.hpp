#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace wowee {
namespace rendering {

class VkContext;

/**
 * Skybox renderer
 *
 * Renders an atmospheric sky dome with gradient colors.
 * The sky uses a dome/sphere approach for realistic appearance.
 */
class Skybox {
public:
    Skybox();
    ~Skybox();

    bool initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout);
    void shutdown();

    /**
     * Render the skybox
     * @param cmd Command buffer to record into
     * @param perFrameSet Per-frame descriptor set (set 0, contains camera UBO)
     * @param timeOfDay Time of day in hours (0-24), affects sky color
     */
    void render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, float timeOfDay = 12.0f);

    /**
     * Enable/disable skybox rendering
     */
    void setEnabled(bool enabled) { renderingEnabled = enabled; }
    bool isEnabled() const { return renderingEnabled; }

    /**
     * Set time of day (0-24 hours)
     * 0 = midnight, 6 = dawn, 12 = noon, 18 = dusk, 24 = midnight
     */
    void setTimeOfDay(float time);
    float getTimeOfDay() const { return timeOfDay; }

    /**
     * Enable/disable time progression
     */
    void setTimeProgression(bool enabled) { timeProgressionEnabled = enabled; }
    bool isTimeProgressionEnabled() const { return timeProgressionEnabled; }

    /**
     * Update time progression
     */
    void update(float deltaTime);

    /**
     * Get horizon color for fog (public for fog system)
     */
    glm::vec3 getHorizonColor(float time) const;

private:
    void createSkyDome();
    void destroySkyDome();

    glm::vec3 getSkyColor(float altitude, float time) const;
    glm::vec3 getZenithColor(float time) const;

    VkContext* vkCtx = nullptr;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAlloc = VK_NULL_HANDLE;

    int indexCount = 0;

    float timeOfDay = 12.0f;  // Default: noon
    float timeSpeed = 1.0f;   // 1.0 = 1 hour per real second
    bool timeProgressionEnabled = false;
    bool renderingEnabled = true;
};

} // namespace rendering
} // namespace wowee
