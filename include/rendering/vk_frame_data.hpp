#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <chrono>

namespace wowee {
namespace rendering {

// Must match the PerFrame UBO layout in all shaders (std140 alignment)
struct GPUPerFrameData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 lightSpaceMatrix;
    glm::vec4 lightDir;       // xyz = direction, w = unused
    glm::vec4 lightColor;     // xyz = color, w = unused
    glm::vec4 ambientColor;   // xyz = color, w = unused
    glm::vec4 viewPos;        // xyz = camera pos, w = unused
    glm::vec4 fogColor;       // xyz = color, w = unused
    glm::vec4 fogParams;      // x = fogStart, y = fogEnd, z = time, w = unused
    glm::vec4 shadowParams;   // x = enabled(0/1), y = strength, z = unused, w = unused
};

// Push constants for the model matrix (most common case)
struct GPUPushConstants {
    glm::mat4 model;
};

// Push constants for shadow rendering passes
struct ShadowPush {
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
};

// Uniform buffer for shadow rendering parameters (matches shader std140 layout)
struct ShadowParamsUBO {
    int32_t useBones;
    int32_t useTexture;
    int32_t alphaTest;
    int32_t foliageSway;
    float windTime;
    float foliageMotionDamp;
};

// Timer utility for performance profiling queries
struct QueryTimer {
    double* totalMs = nullptr;
    uint32_t* callCount = nullptr;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    QueryTimer(double* total, uint32_t* calls) : totalMs(total), callCount(calls) {}
    ~QueryTimer() {
        if (callCount) {
            (*callCount)++;
        }
        if (totalMs) {
            auto end = std::chrono::steady_clock::now();
            *totalMs += std::chrono::duration<double, std::milli>(end - start).count();
        }
    }
};

} // namespace rendering
} // namespace wowee
