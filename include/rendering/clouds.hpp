#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

namespace wowee {
namespace rendering {

class VkContext;

/**
 * Procedural cloud renderer (Vulkan)
 *
 * Renders animated procedural clouds on a sky hemisphere using FBM noise.
 * Two noise layers at different frequencies produce realistic cloud shapes.
 *
 * Pipeline layout:
 *   set 0 = perFrameLayout  (camera UBO — view, projection, etc.)
 *   push  = CloudPush       (vec4 cloudColor + float density + float windOffset = 24 bytes)
 *
 * The vertex shader reads view/projection from set 0 directly; no per-object
 * model matrix is needed (clouds are locked to the sky dome).
 */
class Clouds {
public:
    Clouds();
    ~Clouds();

    /**
     * Initialize the cloud system.
     * @param ctx            Vulkan context
     * @param perFrameLayout Descriptor set layout for set 0 (camera UBO)
     */
    bool initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout);
    void shutdown();
    void recreatePipelines();

    /**
     * Render clouds.
     * @param cmd         Command buffer to record into
     * @param perFrameSet Per-frame descriptor set (set 0, camera UBO)
     * @param timeOfDay   Time of day in hours (0-24)
     */
    void render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, float timeOfDay);

    /**
     * Update cloud animation (wind drift).
     * @param deltaTime Seconds since last frame
     */
    void update(float deltaTime);

    // --- Enable / disable ---
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // --- Cloud parameters ---
    /** Cloud coverage, 0 = clear, 1 = overcast. */
    void setDensity(float density);
    float getDensity() const { return density_; }

    void setWindSpeed(float speed) { windSpeed_ = speed; }
    float getWindSpeed() const { return windSpeed_; }

private:
    // Push constant block — must match clouds.frag.glsl
    struct CloudPush {
        glm::vec4 cloudColor; // 16 bytes (xyz = colour, w unused)
        float     density;    //  4 bytes
        float     windOffset; //  4 bytes
        // total = 24 bytes
    };
    static_assert(sizeof(CloudPush) == 24, "CloudPush size mismatch");

    void generateMesh();
    void createBuffers();
    void destroyBuffers();

    glm::vec3 getCloudColor(float timeOfDay) const;

    // Vulkan objects
    VkContext*       vkCtx_          = nullptr;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkBuffer         vertexBuffer_   = VK_NULL_HANDLE;
    VmaAllocation    vertexAlloc_    = VK_NULL_HANDLE;
    VkBuffer         indexBuffer_    = VK_NULL_HANDLE;
    VmaAllocation    indexAlloc_     = VK_NULL_HANDLE;

    // Mesh data (CPU side, used during initialization only)
    std::vector<glm::vec3>   vertices_;
    std::vector<uint32_t>    indices_;
    int                      indexCount_ = 0;

    // Cloud parameters
    bool  enabled_    = true;
    float density_    = 0.5f;
    float windSpeed_  = 1.0f;
    float windOffset_ = 0.0f; // Accumulated wind movement

    // Mesh generation parameters
    static constexpr int   SEGMENTS = 32;
    static constexpr int   RINGS    = 8;
    static constexpr float RADIUS   = 900.0f; // Slightly smaller than skybox
};

} // namespace rendering
} // namespace wowee
