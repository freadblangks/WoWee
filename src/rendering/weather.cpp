#include "rendering/weather.hpp"
#include "rendering/camera.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/vk_utils.hpp"
#include "core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <cmath>
#include <cstring>

namespace wowee {
namespace rendering {

Weather::Weather() {
}

Weather::~Weather() {
    shutdown();
}

bool Weather::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout) {
    LOG_INFO("Initializing weather system");

    vkCtx = ctx;
    VkDevice device = vkCtx->getDevice();

    // Load SPIR-V shaders
    VkShaderModule vertModule;
    if (!vertModule.loadFromFile(device, "assets/shaders/weather.vert.spv")) {
        LOG_ERROR("Failed to load weather vertex shader");
        return false;
    }

    VkShaderModule fragModule;
    if (!fragModule.loadFromFile(device, "assets/shaders/weather.frag.spv")) {
        LOG_ERROR("Failed to load weather fragment shader");
        return false;
    }

    VkPipelineShaderStageCreateInfo vertStage = vertModule.stageInfo(VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo fragStage = fragModule.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT);

    // Push constant range: { float particleSize; float pad0; float pad1; float pad2; vec4 particleColor; } = 32 bytes
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 32;  // 4 floats + vec4

    // Create pipeline layout with perFrameLayout (set 0) + push constants
    pipelineLayout = createPipelineLayout(device, {perFrameLayout}, {pushRange});
    if (pipelineLayout == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create weather pipeline layout");
        return false;
    }

    // Vertex input: position only (vec3), stride = 3 * sizeof(float)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 3 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
    posAttr.offset = 0;

    // Dynamic viewport and scissor
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({binding}, {posAttr})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS)  // depth test on, write off (transparent particles)
        .setColorBlendAttachment(PipelineBuilder::blendAlpha())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device);

    vertModule.destroy();
    fragModule.destroy();

    if (pipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create weather pipeline");
        return false;
    }

    // Create a dynamic mapped vertex buffer large enough for MAX_PARTICLES
    dynamicVBSize = MAX_PARTICLES * sizeof(glm::vec3);
    AllocatedBuffer buf = createBuffer(vkCtx->getAllocator(), dynamicVBSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    dynamicVB = buf.buffer;
    dynamicVBAlloc = buf.allocation;
    dynamicVBAllocInfo = buf.info;

    if (dynamicVB == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create weather dynamic vertex buffer");
        return false;
    }

    // Reserve space for particles
    particles.reserve(MAX_PARTICLES);
    particlePositions.reserve(MAX_PARTICLES);

    LOG_INFO("Weather system initialized");
    return true;
}

void Weather::recreatePipelines() {
    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();

    if (pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }

    VkShaderModule vertModule;
    if (!vertModule.loadFromFile(device, "assets/shaders/weather.vert.spv")) {
        LOG_ERROR("Weather::recreatePipelines: failed to load vertex shader");
        return;
    }
    VkShaderModule fragModule;
    if (!fragModule.loadFromFile(device, "assets/shaders/weather.frag.spv")) {
        LOG_ERROR("Weather::recreatePipelines: failed to load fragment shader");
        vertModule.destroy();
        return;
    }

    VkPipelineShaderStageCreateInfo vertStage = vertModule.stageInfo(VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo fragStage = fragModule.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT);

    // Vertex input (same as initialize)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 3 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
    posAttr.offset = 0;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({binding}, {posAttr})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS)
        .setColorBlendAttachment(PipelineBuilder::blendAlpha())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device);

    vertModule.destroy();
    fragModule.destroy();

    if (pipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Weather::recreatePipelines: failed to create pipeline");
    }
}

void Weather::update(const Camera& camera, float deltaTime) {
    if (!enabled || weatherType == Type::NONE) {
        return;
    }

    // Initialize particles if needed
    if (particles.empty()) {
        resetParticles(camera);
    }

    // Calculate active particle count based on intensity
    int targetParticleCount = static_cast<int>(MAX_PARTICLES * intensity);

    // Adjust particle count
    while (static_cast<int>(particles.size()) < targetParticleCount) {
        Particle p;
        p.position = getRandomPosition(camera.getPosition());
        p.position.y = camera.getPosition().y + SPAWN_HEIGHT;
        p.lifetime = 0.0f;

        if (weatherType == Type::RAIN) {
            p.velocity = glm::vec3(0.0f, -50.0f, 0.0f);  // Fast downward
            p.maxLifetime = 5.0f;
        } else {  // SNOW
            p.velocity = glm::vec3(0.0f, -5.0f, 0.0f);   // Slow downward
            p.maxLifetime = 10.0f;
        }

        particles.push_back(p);
    }

    while (static_cast<int>(particles.size()) > targetParticleCount) {
        particles.pop_back();
    }

    // Update each particle
    for (auto& particle : particles) {
        updateParticle(particle, camera, deltaTime);
    }

    // Update position buffer
    particlePositions.clear();
    for (const auto& particle : particles) {
        particlePositions.push_back(particle.position);
    }
}

void Weather::updateParticle(Particle& particle, const Camera& camera, float deltaTime) {
    // Update lifetime
    particle.lifetime += deltaTime;

    // Reset if lifetime exceeded or too far from camera
    glm::vec3 cameraPos = camera.getPosition();
    float distance = glm::length(particle.position - cameraPos);

    if (particle.lifetime >= particle.maxLifetime || distance > SPAWN_VOLUME_SIZE ||
        particle.position.y < cameraPos.y - 20.0f) {
        // Respawn at top
        particle.position = getRandomPosition(cameraPos);
        particle.position.y = cameraPos.y + SPAWN_HEIGHT;
        particle.lifetime = 0.0f;
    }

    // Add wind effect for snow
    if (weatherType == Type::SNOW) {
        float windX = std::sin(particle.lifetime * 0.5f) * 2.0f;
        float windZ = std::cos(particle.lifetime * 0.3f) * 2.0f;
        particle.velocity.x = windX;
        particle.velocity.z = windZ;
    }

    // Update position
    particle.position += particle.velocity * deltaTime;
}

void Weather::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (!enabled || weatherType == Type::NONE || particlePositions.empty() ||
        pipeline == VK_NULL_HANDLE) {
        return;
    }

    // Upload particle positions to mapped buffer
    VkDeviceSize uploadSize = particlePositions.size() * sizeof(glm::vec3);
    if (uploadSize > 0 && dynamicVBAllocInfo.pMappedData) {
        std::memcpy(dynamicVBAllocInfo.pMappedData, particlePositions.data(), uploadSize);
    }

    // Push constant data: { float particleSize; float pad0; float pad1; float pad2; vec4 particleColor; }
    struct WeatherPush {
        float particleSize;
        float pad0;
        float pad1;
        float pad2;
        glm::vec4 particleColor;
    };

    WeatherPush push{};
    if (weatherType == Type::RAIN) {
        push.particleSize = 3.0f;
        push.particleColor = glm::vec4(0.7f, 0.8f, 0.9f, 0.6f);
    } else {  // SNOW
        push.particleSize = 8.0f;
        push.particleColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.9f);
    }

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind per-frame descriptor set (set 0 - camera UBO)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
        0, 1, &perFrameSet, 0, nullptr);

    // Push constants
    vkCmdPushConstants(cmd, pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(push), &push);

    // Bind vertex buffer
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &dynamicVB, &offset);

    // Draw particles as points
    vkCmdDraw(cmd, static_cast<uint32_t>(particlePositions.size()), 1, 0, 0);
}

void Weather::resetParticles(const Camera& camera) {
    particles.clear();

    int particleCount = static_cast<int>(MAX_PARTICLES * intensity);
    glm::vec3 cameraPos = camera.getPosition();

    for (int i = 0; i < particleCount; ++i) {
        Particle p;
        p.position = getRandomPosition(cameraPos);
        p.position.y = cameraPos.y + SPAWN_HEIGHT * (static_cast<float>(rand()) / RAND_MAX);
        p.lifetime = 0.0f;

        if (weatherType == Type::RAIN) {
            p.velocity = glm::vec3(0.0f, -50.0f, 0.0f);
            p.maxLifetime = 5.0f;
        } else {  // SNOW
            p.velocity = glm::vec3(0.0f, -5.0f, 0.0f);
            p.maxLifetime = 10.0f;
        }

        particles.push_back(p);
    }
}

glm::vec3 Weather::getRandomPosition(const glm::vec3& center) const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    float x = center.x + dist(gen) * SPAWN_VOLUME_SIZE;
    float z = center.z + dist(gen) * SPAWN_VOLUME_SIZE;
    float y = center.y;

    return glm::vec3(x, y, z);
}

void Weather::setIntensity(float intensity) {
    this->intensity = glm::clamp(intensity, 0.0f, 1.0f);
}

int Weather::getParticleCount() const {
    return static_cast<int>(particles.size());
}

void Weather::shutdown() {
    if (vkCtx) {
        VkDevice device = vkCtx->getDevice();
        VmaAllocator allocator = vkCtx->getAllocator();

        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
        if (dynamicVB != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, dynamicVB, dynamicVBAlloc);
            dynamicVB = VK_NULL_HANDLE;
            dynamicVBAlloc = VK_NULL_HANDLE;
        }
    }

    vkCtx = nullptr;
    particles.clear();
    particlePositions.clear();
}

} // namespace rendering
} // namespace wowee
