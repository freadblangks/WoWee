#include "rendering/skybox.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_frame_data.hpp"
#include "core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace wowee {
namespace rendering {

Skybox::Skybox() = default;

Skybox::~Skybox() {
    shutdown();
}

bool Skybox::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout) {
    LOG_INFO("Initializing skybox");

    vkCtx = ctx;

    VkDevice device = vkCtx->getDevice();

    // Load SPIR-V shaders
    VkShaderModule vertModule;
    if (!vertModule.loadFromFile(device, "assets/shaders/skybox.vert.spv")) {
        LOG_ERROR("Failed to load skybox vertex shader");
        return false;
    }

    VkShaderModule fragModule;
    if (!fragModule.loadFromFile(device, "assets/shaders/skybox.frag.spv")) {
        LOG_ERROR("Failed to load skybox fragment shader");
        return false;
    }

    VkPipelineShaderStageCreateInfo vertStage = vertModule.stageInfo(VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo fragStage = fragModule.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT);

    // Push constant range: horizonColor (vec4) + zenithColor (vec4) + timeOfDay (float) = 36 bytes
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(float);  // 36 bytes

    // Create pipeline layout with perFrameLayout (set 0) + push constants
    pipelineLayout = createPipelineLayout(device, {perFrameLayout}, {pushRange});
    if (pipelineLayout == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create skybox pipeline layout");
        return false;
    }

    // Fullscreen triangle — no vertex buffer, no vertex input.
    // Dynamic viewport and scissor
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({}, {})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)  // depth test on, write off, LEQUAL for far plane
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device);

    // Shader modules can be freed after pipeline creation
    vertModule.destroy();
    fragModule.destroy();

    if (pipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create skybox pipeline");
        return false;
    }

    LOG_INFO("Skybox initialized");
    return true;
}

void Skybox::recreatePipelines() {
    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();

    if (pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }

    VkShaderModule vertModule;
    if (!vertModule.loadFromFile(device, "assets/shaders/skybox.vert.spv")) {
        LOG_ERROR("Skybox::recreatePipelines: failed to load vertex shader");
        return;
    }
    VkShaderModule fragModule;
    if (!fragModule.loadFromFile(device, "assets/shaders/skybox.frag.spv")) {
        LOG_ERROR("Skybox::recreatePipelines: failed to load fragment shader");
        vertModule.destroy();
        return;
    }

    VkPipelineShaderStageCreateInfo vertStage = vertModule.stageInfo(VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo fragStage = fragModule.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT);

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({}, {})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device);

    vertModule.destroy();
    fragModule.destroy();

    if (pipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Skybox::recreatePipelines: failed to create pipeline");
    }
}

void Skybox::shutdown() {
    if (vkCtx) {
        VkDevice device = vkCtx->getDevice();
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
    }

    vkCtx = nullptr;
}

void Skybox::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, float time) {
    if (pipeline == VK_NULL_HANDLE || !renderingEnabled) {
        return;
    }

    // Push constant data
    struct SkyPushConstants {
        glm::vec4 horizonColor;
        glm::vec4 zenithColor;
        float timeOfDay;
    };

    SkyPushConstants push{};
    glm::vec3 horizon = getHorizonColor(time);
    glm::vec3 zenith = getZenithColor(time);
    push.horizonColor = glm::vec4(horizon, 1.0f);
    push.zenithColor  = glm::vec4(zenith,  1.0f);
    push.timeOfDay    = time;

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind per-frame descriptor set (set 0 — camera UBO)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
        0, 1, &perFrameSet, 0, nullptr);

    // Push constants
    vkCmdPushConstants(cmd, pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(push), &push);

    // Draw fullscreen triangle — no vertex buffer needed
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void Skybox::update(float deltaTime) {
    if (timeProgressionEnabled) {
        timeOfDay += deltaTime * timeSpeed;

        // Wrap around 24 hours
        if (timeOfDay >= 24.0f) {
            timeOfDay -= 24.0f;
        }
    }
}

void Skybox::setTimeOfDay(float time) {
    // Clamp to 0-24 range
    while (time < 0.0f) time += 24.0f;
    while (time >= 24.0f) time -= 24.0f;

    timeOfDay = time;
}

glm::vec3 Skybox::getHorizonColor(float time) const {
    // Time-based horizon colors
    // 0-6: Night (dark blue)
    // 6-8: Dawn (orange/pink)
    // 8-16: Day (light blue)
    // 16-18: Dusk (orange/red)
    // 18-24: Night (dark blue)

    if (time < 5.0f || time >= 21.0f) {
        // Night - dark blue/purple horizon
        return glm::vec3(0.05f, 0.05f, 0.15f);
    }
    else if (time >= 5.0f && time < 7.0f) {
        // Dawn - blend from night to orange
        float t = (time - 5.0f) / 2.0f;
        glm::vec3 night = glm::vec3(0.05f, 0.05f, 0.15f);
        glm::vec3 dawn = glm::vec3(1.0f, 0.5f, 0.2f);
        return glm::mix(night, dawn, t);
    }
    else if (time >= 7.0f && time < 9.0f) {
        // Morning - blend from orange to blue
        float t = (time - 7.0f) / 2.0f;
        glm::vec3 dawn = glm::vec3(1.0f, 0.5f, 0.2f);
        glm::vec3 day = glm::vec3(0.6f, 0.7f, 0.9f);
        return glm::mix(dawn, day, t);
    }
    else if (time >= 9.0f && time < 17.0f) {
        // Day - light blue horizon
        return glm::vec3(0.6f, 0.7f, 0.9f);
    }
    else if (time >= 17.0f && time < 19.0f) {
        // Dusk - blend from blue to orange/red
        float t = (time - 17.0f) / 2.0f;
        glm::vec3 day = glm::vec3(0.6f, 0.7f, 0.9f);
        glm::vec3 dusk = glm::vec3(1.0f, 0.4f, 0.1f);
        return glm::mix(day, dusk, t);
    }
    else {
        // Evening - blend from orange to night
        float t = (time - 19.0f) / 2.0f;
        glm::vec3 dusk = glm::vec3(1.0f, 0.4f, 0.1f);
        glm::vec3 night = glm::vec3(0.05f, 0.05f, 0.15f);
        return glm::mix(dusk, night, t);
    }
}

glm::vec3 Skybox::getZenithColor(float time) const {
    // Zenith (top of sky) colors

    if (time < 5.0f || time >= 21.0f) {
        // Night - very dark blue, almost black
        return glm::vec3(0.01f, 0.01f, 0.05f);
    }
    else if (time >= 5.0f && time < 7.0f) {
        // Dawn - blend from night to light blue
        float t = (time - 5.0f) / 2.0f;
        glm::vec3 night = glm::vec3(0.01f, 0.01f, 0.05f);
        glm::vec3 dawn = glm::vec3(0.3f, 0.4f, 0.7f);
        return glm::mix(night, dawn, t);
    }
    else if (time >= 7.0f && time < 9.0f) {
        // Morning - blend to bright blue
        float t = (time - 7.0f) / 2.0f;
        glm::vec3 dawn = glm::vec3(0.3f, 0.4f, 0.7f);
        glm::vec3 day = glm::vec3(0.2f, 0.5f, 1.0f);
        return glm::mix(dawn, day, t);
    }
    else if (time >= 9.0f && time < 17.0f) {
        // Day - bright blue zenith
        return glm::vec3(0.2f, 0.5f, 1.0f);
    }
    else if (time >= 17.0f && time < 19.0f) {
        // Dusk - blend to darker blue
        float t = (time - 17.0f) / 2.0f;
        glm::vec3 day = glm::vec3(0.2f, 0.5f, 1.0f);
        glm::vec3 dusk = glm::vec3(0.1f, 0.2f, 0.4f);
        return glm::mix(day, dusk, t);
    }
    else {
        // Evening - blend to night
        float t = (time - 19.0f) / 2.0f;
        glm::vec3 dusk = glm::vec3(0.1f, 0.2f, 0.4f);
        glm::vec3 night = glm::vec3(0.01f, 0.01f, 0.05f);
        return glm::mix(dusk, night, t);
    }
}

glm::vec3 Skybox::getSkyColor(float altitude, float time) const {
    // Blend between horizon and zenith based on altitude
    glm::vec3 horizon = getHorizonColor(time);
    glm::vec3 zenith = getZenithColor(time);

    // Use power curve for more natural gradient
    float t = std::pow(std::max(altitude, 0.0f), 0.5f);

    return glm::mix(horizon, zenith, t);
}

} // namespace rendering
} // namespace wowee
