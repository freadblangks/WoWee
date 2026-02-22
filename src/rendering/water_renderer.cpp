#include "rendering/water_renderer.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_utils.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/camera.hpp"
#include "pipeline/adt_loader.hpp"
#include "pipeline/wmo_loader.hpp"
#include "core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace wowee {
namespace rendering {

// Matches set 1 binding 0 in water.frag.glsl
struct WaterMaterialUBO {
    glm::vec4 waterColor;
    float waterAlpha;
    float shimmerStrength;
    float alphaScale;
    float _pad;
};

// Push constants matching water.vert.glsl
struct WaterPushConstants {
    glm::mat4 model;
    float waveAmp;
    float waveFreq;
    float waveSpeed;
    float _pad;
};

WaterRenderer::WaterRenderer() = default;

WaterRenderer::~WaterRenderer() {
    shutdown();
}

bool WaterRenderer::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout) {
    vkCtx = ctx;
    if (!vkCtx) return false;

    LOG_INFO("Initializing water renderer (Vulkan)");
    VkDevice device = vkCtx->getDevice();

    // --- Material descriptor set layout (set 1) ---
    // binding 0: WaterMaterial UBO
    VkDescriptorSetLayoutBinding matBinding{};
    matBinding.binding = 0;
    matBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matBinding.descriptorCount = 1;
    matBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    materialSetLayout = createDescriptorSetLayout(device, { matBinding });
    if (!materialSetLayout) {
        LOG_ERROR("WaterRenderer: failed to create material set layout");
        return false;
    }

    // --- Descriptor pool ---
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = MAX_WATER_SETS;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = MAX_WATER_SETS;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &materialDescPool) != VK_SUCCESS) {
        LOG_ERROR("WaterRenderer: failed to create descriptor pool");
        return false;
    }

    // --- Pipeline layout ---
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(WaterPushConstants);

    std::vector<VkDescriptorSetLayout> setLayouts = { perFrameLayout, materialSetLayout };
    pipelineLayout = createPipelineLayout(device, setLayouts, { pushRange });
    if (!pipelineLayout) {
        LOG_ERROR("WaterRenderer: failed to create pipeline layout");
        return false;
    }

    // --- Shaders ---
    VkShaderModule vertShader, fragShader;
    if (!vertShader.loadFromFile(device, "assets/shaders/water.vert.spv")) {
        LOG_ERROR("WaterRenderer: failed to load vertex shader");
        return false;
    }
    if (!fragShader.loadFromFile(device, "assets/shaders/water.frag.spv")) {
        LOG_ERROR("WaterRenderer: failed to load fragment shader");
        return false;
    }

    // --- Vertex input (interleaved: pos3 + normal3 + uv2 = 8 floats = 32 bytes) ---
    VkVertexInputBindingDescription vertBinding{};
    vertBinding.binding = 0;
    vertBinding.stride = 8 * sizeof(float);
    vertBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Water vertex shader only takes aPos(vec3) at loc 0 and aTexCoord(vec2) at loc 1
    // (normal is computed in shader from wave derivatives)
    std::vector<VkVertexInputAttributeDescription> vertAttribs = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },                     // aPos
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, 6 * sizeof(float) },        // aTexCoord (skip normal)
    };

    VkRenderPass mainPass = vkCtx->getImGuiRenderPass();

    waterPipeline = PipelineBuilder()
        .setShaders(vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                    fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({ vertBinding }, vertAttribs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)  // depth test yes, write no
        .setColorBlendAttachment(PipelineBuilder::blendAlpha())
        .setLayout(pipelineLayout)
        .setRenderPass(mainPass)
        .setDynamicStates({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR })
        .build(device);

    vertShader.destroy();
    fragShader.destroy();

    if (!waterPipeline) {
        LOG_ERROR("WaterRenderer: failed to create pipeline");
        return false;
    }

    LOG_INFO("Water renderer initialized (Vulkan)");
    return true;
}

void WaterRenderer::shutdown() {
    clear();

    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();
    vkDeviceWaitIdle(device);

    if (waterPipeline) { vkDestroyPipeline(device, waterPipeline, nullptr); waterPipeline = VK_NULL_HANDLE; }
    if (pipelineLayout) { vkDestroyPipelineLayout(device, pipelineLayout, nullptr); pipelineLayout = VK_NULL_HANDLE; }
    if (materialDescPool) { vkDestroyDescriptorPool(device, materialDescPool, nullptr); materialDescPool = VK_NULL_HANDLE; }
    if (materialSetLayout) { vkDestroyDescriptorSetLayout(device, materialSetLayout, nullptr); materialSetLayout = VK_NULL_HANDLE; }

    vkCtx = nullptr;
}

VkDescriptorSet WaterRenderer::allocateMaterialSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = materialDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &materialSetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(vkCtx->getDevice(), &allocInfo, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return set;
}

void WaterRenderer::updateMaterialUBO(WaterSurface& surface) {
    glm::vec4 color = getLiquidColor(surface.liquidType);
    float alpha = getLiquidAlpha(surface.liquidType);

    // WMO liquid material override
    if (surface.wmoId != 0) {
        const uint8_t basicType = (surface.liquidType == 0) ? 0 : ((surface.liquidType - 1) % 4);
        if (basicType == 2 || basicType == 3) {
            color = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
            alpha = 0.45f;
        }
    }

    bool canalProfile = (surface.wmoId != 0) || (surface.liquidType == 5);
    float shimmerStrength = canalProfile ? 0.95f : 0.50f;
    float alphaScale = canalProfile ? 0.90f : 1.00f;

    WaterMaterialUBO mat{};
    mat.waterColor = color;
    mat.waterAlpha = alpha;
    mat.shimmerStrength = shimmerStrength;
    mat.alphaScale = alphaScale;

    // Create UBO
    VkBufferCreateInfo bufCI{};
    bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCI.size = sizeof(WaterMaterialUBO);
    bufCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mapInfo{};
    vmaCreateBuffer(vkCtx->getAllocator(), &bufCI, &allocCI,
                    &surface.materialUBO, &surface.materialAlloc, &mapInfo);
    if (mapInfo.pMappedData) {
        std::memcpy(mapInfo.pMappedData, &mat, sizeof(mat));
    }

    // Allocate and write descriptor set
    surface.materialSet = allocateMaterialSet();
    if (surface.materialSet) {
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = surface.materialUBO;
        bufInfo.offset = 0;
        bufInfo.range = sizeof(WaterMaterialUBO);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = surface.materialSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufInfo;

        vkUpdateDescriptorSets(vkCtx->getDevice(), 1, &write, 0, nullptr);
    }
}

// ==============================================================
// Data loading (preserved from GL version — no GL calls)
// ==============================================================

void WaterRenderer::loadFromTerrain(const pipeline::ADTTerrain& terrain, bool append,
                                     int tileX, int tileY) {
    constexpr float TILE_SIZE = 33.33333f / 8.0f;

    if (!append) {
        clear();
    }

    int totalLayers = 0;

    for (int chunkIdx = 0; chunkIdx < 256; chunkIdx++) {
        const auto& chunkWater = terrain.waterData[chunkIdx];
        if (!chunkWater.hasWater()) continue;

        int chunkX = chunkIdx % 16;
        int chunkY = chunkIdx / 16;
        const auto& terrainChunk = terrain.getChunk(chunkX, chunkY);

        for (const auto& layer : chunkWater.layers) {
            WaterSurface surface;

            surface.position = glm::vec3(
                terrainChunk.position[0],
                terrainChunk.position[1],
                layer.minHeight
            );
            surface.origin = glm::vec3(
                surface.position.x - (static_cast<float>(layer.y) * TILE_SIZE),
                surface.position.y - (static_cast<float>(layer.x) * TILE_SIZE),
                layer.minHeight
            );
            surface.stepX = glm::vec3(0.0f, -TILE_SIZE, 0.0f);
            surface.stepY = glm::vec3(-TILE_SIZE, 0.0f, 0.0f);

            surface.minHeight = layer.minHeight;
            surface.maxHeight = layer.maxHeight;
            surface.liquidType = layer.liquidType;

            surface.xOffset = layer.x;
            surface.yOffset = layer.y;
            surface.width = layer.width;
            surface.height = layer.height;

            size_t numVertices = (layer.width + 1) * (layer.height + 1);
            bool useFlat = true;
            if (layer.heights.size() == numVertices) {
                bool sane = true;
                for (float h : layer.heights) {
                    if (!std::isfinite(h) || std::abs(h) > 50000.0f) { sane = false; break; }
                    if (h < layer.minHeight - 8.0f || h > layer.maxHeight + 8.0f) { sane = false; break; }
                }
                if (sane) { useFlat = false; surface.heights = layer.heights; }
            }
            if (useFlat) surface.heights.resize(numVertices, layer.minHeight);

            // Stormwind water lowering
            bool isStormwindArea = (tileX >= 28 && tileX <= 50 && tileY >= 28 && tileY <= 52);
            if (isStormwindArea && layer.minHeight > 94.0f) {
                float tileWorldX = (32.0f - tileX) * 533.33333f;
                float tileWorldY = (32.0f - tileY) * 533.33333f;
                glm::vec3 moonwellPos(-8755.9f, 1108.9f, 96.1f);
                float distToMoonwell = glm::distance(glm::vec2(tileWorldX, tileWorldY),
                                                      glm::vec2(moonwellPos.x, moonwellPos.y));
                if (distToMoonwell > 300.0f) {
                    for (float& h : surface.heights) h -= 1.0f;
                    surface.minHeight -= 1.0f;
                    surface.maxHeight -= 1.0f;
                }
            }

            surface.mask = layer.mask;
            surface.tileX = tileX;
            surface.tileY = tileY;

            createWaterMesh(surface);
            if (surface.indexCount > 0 && vkCtx) {
                updateMaterialUBO(surface);
            }
            surfaces.push_back(std::move(surface));
            totalLayers++;
        }
    }

    LOG_DEBUG("Loaded ", totalLayers, " water layers from MH2O data");
}

void WaterRenderer::removeTile(int tileX, int tileY) {
    int removed = 0;
    auto it = surfaces.begin();
    while (it != surfaces.end()) {
        if (it->tileX == tileX && it->tileY == tileY) {
            destroyWaterMesh(*it);
            it = surfaces.erase(it);
            removed++;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        LOG_DEBUG("Removed ", removed, " water surfaces for tile [", tileX, ",", tileY, "]");
    }
}

void WaterRenderer::loadFromWMO([[maybe_unused]] const pipeline::WMOLiquid& liquid,
                                 [[maybe_unused]] const glm::mat4& modelMatrix,
                                 [[maybe_unused]] uint32_t wmoId) {
    if (!liquid.hasLiquid() || liquid.xTiles == 0 || liquid.yTiles == 0) return;
    if (liquid.xVerts < 2 || liquid.yVerts < 2) return;
    if (liquid.xTiles != liquid.xVerts - 1 || liquid.yTiles != liquid.yVerts - 1) return;
    if (liquid.xTiles > 64 || liquid.yTiles > 64) return;

    WaterSurface surface;
    surface.tileX = -1;
    surface.tileY = -1;
    surface.wmoId = wmoId;
    surface.liquidType = liquid.materialId;
    surface.xOffset = 0;
    surface.yOffset = 0;
    surface.width = static_cast<uint8_t>(std::min<uint32_t>(255, liquid.xTiles));
    surface.height = static_cast<uint8_t>(std::min<uint32_t>(255, liquid.yTiles));

    constexpr float WMO_LIQUID_TILE_SIZE = 4.1666625f;
    const glm::vec3 localBase(liquid.basePosition.x, liquid.basePosition.y, liquid.basePosition.z);
    const glm::vec3 localStepX(WMO_LIQUID_TILE_SIZE, 0.0f, 0.0f);
    const glm::vec3 localStepY(0.0f, WMO_LIQUID_TILE_SIZE, 0.0f);

    surface.origin = glm::vec3(modelMatrix * glm::vec4(localBase, 1.0f));
    surface.stepX = glm::vec3(modelMatrix * glm::vec4(localStepX, 0.0f));
    surface.stepY = glm::vec3(modelMatrix * glm::vec4(localStepY, 0.0f));
    surface.position = surface.origin;

    float stepXLen = glm::length(surface.stepX);
    float stepYLen = glm::length(surface.stepY);
    glm::vec3 planeN = glm::cross(surface.stepX, surface.stepY);
    float nz = (glm::length(planeN) > 1e-4f) ? std::abs(glm::normalize(planeN).z) : 0.0f;
    float spanX = stepXLen * static_cast<float>(surface.width);
    float spanY = stepYLen * static_cast<float>(surface.height);
    if (stepXLen < 0.2f || stepXLen > 12.0f ||
        stepYLen < 0.2f || stepYLen > 12.0f ||
        nz < 0.60f || spanX > 450.0f || spanY > 450.0f) return;

    const int gridWidth = static_cast<int>(surface.width) + 1;
    const int gridHeight = static_cast<int>(surface.height) + 1;
    const int vertexCount = gridWidth * gridHeight;
    surface.heights.assign(vertexCount, surface.origin.z);
    surface.minHeight = surface.origin.z;
    surface.maxHeight = surface.origin.z;

    // Stormwind WMO water lowering
    int tilePosX = static_cast<int>(std::floor((32.0f - surface.origin.x / 533.33333f)));
    int tilePosY = static_cast<int>(std::floor((32.0f - surface.origin.y / 533.33333f)));
    bool isStormwindArea = (tilePosX >= 28 && tilePosX <= 50 && tilePosY >= 28 && tilePosY <= 52);
    if (isStormwindArea && surface.origin.z > 94.0f) {
        glm::vec3 moonwellPos(-8755.9f, 1108.9f, 96.1f);
        float distToMoonwell = glm::distance(glm::vec2(surface.origin.x, surface.origin.y),
                                              glm::vec2(moonwellPos.x, moonwellPos.y));
        if (distToMoonwell > 20.0f) {
            for (float& h : surface.heights) h -= 1.0f;
            surface.minHeight -= 1.0f;
            surface.maxHeight -= 1.0f;
        }
    }

    if (surface.origin.z > 300.0f || surface.origin.z < -100.0f) return;

    size_t tileCount = static_cast<size_t>(surface.width) * static_cast<size_t>(surface.height);
    size_t maskBytes = (tileCount + 7) / 8;
    surface.mask.assign(maskBytes, 0xFF);

    createWaterMesh(surface);
    if (surface.indexCount > 0) {
        if (vkCtx) updateMaterialUBO(surface);
        surfaces.push_back(std::move(surface));
    }
}

void WaterRenderer::removeWMO(uint32_t wmoId) {
    if (wmoId == 0) return;
    auto it = surfaces.begin();
    while (it != surfaces.end()) {
        if (it->wmoId == wmoId) {
            destroyWaterMesh(*it);
            it = surfaces.erase(it);
        } else {
            ++it;
        }
    }
}

void WaterRenderer::clear() {
    for (auto& surface : surfaces) {
        destroyWaterMesh(surface);
    }
    surfaces.clear();

    if (vkCtx && materialDescPool) {
        vkResetDescriptorPool(vkCtx->getDevice(), materialDescPool, 0);
    }
}

// ==============================================================
// Rendering
// ==============================================================

void WaterRenderer::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet,
                            const Camera& /*camera*/, float /*time*/) {
    if (!renderingEnabled || surfaces.empty() || !waterPipeline) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                             0, 1, &perFrameSet, 0, nullptr);

    for (const auto& surface : surfaces) {
        if (surface.vertexBuffer == VK_NULL_HANDLE || surface.indexCount == 0) continue;
        if (!surface.materialSet) continue;

        bool canalProfile = (surface.wmoId != 0) || (surface.liquidType == 5);
        float waveAmp = canalProfile ? 0.04f : 0.06f;
        float waveFreq = canalProfile ? 0.30f : 0.22f;
        float waveSpeed = canalProfile ? 1.20f : 2.00f;

        WaterPushConstants push{};
        push.model = glm::mat4(1.0f);
        push.waveAmp = waveAmp;
        push.waveFreq = waveFreq;
        push.waveSpeed = waveSpeed;

        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                            0, sizeof(WaterPushConstants), &push);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                 1, 1, &surface.materialSet, 0, nullptr);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &surface.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, surface.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(surface.indexCount), 1, 0, 0, 0);
    }
}

// ==============================================================
// Mesh creation (Vulkan upload instead of GL)
// ==============================================================

void WaterRenderer::createWaterMesh(WaterSurface& surface) {
    const int gridWidth = surface.width + 1;
    const int gridHeight = surface.height + 1;
    constexpr float VISUAL_WATER_Z_BIAS = 0.02f;

    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    for (int y = 0; y < gridHeight; y++) {
        for (int x = 0; x < gridWidth; x++) {
            int index = y * gridWidth + x;
            float height = (index < static_cast<int>(surface.heights.size()))
                ? surface.heights[index] : surface.minHeight;

            glm::vec3 pos = surface.origin +
                            surface.stepX * static_cast<float>(x) +
                            surface.stepY * static_cast<float>(y);
            pos.z = height + VISUAL_WATER_Z_BIAS;

            // pos (3 floats)
            vertices.push_back(pos.x);
            vertices.push_back(pos.y);
            vertices.push_back(pos.z);
            // normal (3 floats) - up
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
            vertices.push_back(1.0f);
            // texcoord (2 floats)
            vertices.push_back(static_cast<float>(x) / std::max(1, gridWidth - 1));
            vertices.push_back(static_cast<float>(y) / std::max(1, gridHeight - 1));
        }
    }

    // Generate indices respecting render mask (same logic as GL version)
    for (int y = 0; y < gridHeight - 1; y++) {
        for (int x = 0; x < gridWidth - 1; x++) {
            bool renderTile = true;
            if (!surface.mask.empty()) {
                int tileIndex;
                if (surface.wmoId == 0 && surface.mask.size() >= 8) {
                    int cx = static_cast<int>(surface.xOffset) + x;
                    int cy = static_cast<int>(surface.yOffset) + y;
                    tileIndex = cy * 8 + cx;
                } else {
                    tileIndex = y * surface.width + x;
                }
                int byteIndex = tileIndex / 8;
                int bitIndex = tileIndex % 8;
                if (byteIndex < static_cast<int>(surface.mask.size())) {
                    uint8_t maskByte = surface.mask[byteIndex];
                    bool lsbOrder = (maskByte & (1 << bitIndex)) != 0;
                    bool msbOrder = (maskByte & (1 << (7 - bitIndex))) != 0;
                    renderTile = lsbOrder || msbOrder;

                    if (!renderTile) {
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                if (dx == 0 && dy == 0) continue;
                                int nx = x + dx, ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= gridWidth-1 || ny >= gridHeight-1) continue;
                                int neighborIdx;
                                if (surface.wmoId == 0 && surface.mask.size() >= 8) {
                                    neighborIdx = (static_cast<int>(surface.yOffset) + ny) * 8 +
                                                  (static_cast<int>(surface.xOffset) + nx);
                                } else {
                                    neighborIdx = ny * surface.width + nx;
                                }
                                int nByteIdx = neighborIdx / 8;
                                int nBitIdx = neighborIdx % 8;
                                if (nByteIdx < static_cast<int>(surface.mask.size())) {
                                    uint8_t nMask = surface.mask[nByteIdx];
                                    if ((nMask & (1 << nBitIdx)) || (nMask & (1 << (7 - nBitIdx)))) {
                                        renderTile = true;
                                        goto found_neighbor;
                                    }
                                }
                            }
                        }
                        found_neighbor:;
                    }
                }
            }

            if (!renderTile) continue;

            int topLeft = y * gridWidth + x;
            int topRight = topLeft + 1;
            int bottomLeft = (y + 1) * gridWidth + x;
            int bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    // Fallback: if terrain MH2O mask produced no tiles, render full rect
    if (indices.empty() && surface.wmoId == 0) {
        for (int y = 0; y < gridHeight - 1; y++) {
            for (int x = 0; x < gridWidth - 1; x++) {
                int topLeft = y * gridWidth + x;
                int topRight = topLeft + 1;
                int bottomLeft = (y + 1) * gridWidth + x;
                int bottomRight = bottomLeft + 1;
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);
                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }
    }

    if (indices.empty()) return;
    surface.indexCount = static_cast<int>(indices.size());

    if (!vkCtx) return;

    // Upload vertex buffer
    VkDeviceSize vbSize = vertices.size() * sizeof(float);
    AllocatedBuffer vb = uploadBuffer(*vkCtx, vertices.data(), vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    surface.vertexBuffer = vb.buffer;
    surface.vertexAlloc = vb.allocation;

    // Upload index buffer
    VkDeviceSize ibSize = indices.size() * sizeof(uint32_t);
    AllocatedBuffer ib = uploadBuffer(*vkCtx, indices.data(), ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    surface.indexBuffer = ib.buffer;
    surface.indexAlloc = ib.allocation;
}

void WaterRenderer::destroyWaterMesh(WaterSurface& surface) {
    if (!vkCtx) return;
    VmaAllocator allocator = vkCtx->getAllocator();

    if (surface.vertexBuffer) {
        AllocatedBuffer ab{}; ab.buffer = surface.vertexBuffer; ab.allocation = surface.vertexAlloc;
        destroyBuffer(allocator, ab);
        surface.vertexBuffer = VK_NULL_HANDLE;
    }
    if (surface.indexBuffer) {
        AllocatedBuffer ab{}; ab.buffer = surface.indexBuffer; ab.allocation = surface.indexAlloc;
        destroyBuffer(allocator, ab);
        surface.indexBuffer = VK_NULL_HANDLE;
    }
    if (surface.materialUBO) {
        AllocatedBuffer ab{}; ab.buffer = surface.materialUBO; ab.allocation = surface.materialAlloc;
        destroyBuffer(allocator, ab);
        surface.materialUBO = VK_NULL_HANDLE;
    }
    surface.materialSet = VK_NULL_HANDLE;
}

// ==============================================================
// Query functions (data-only, no GL)
// ==============================================================

std::optional<float> WaterRenderer::getWaterHeightAt(float glX, float glY) const {
    std::optional<float> best;

    for (const auto& surface : surfaces) {
        glm::vec2 rel(glX - surface.origin.x, glY - surface.origin.y);
        glm::vec2 sX(surface.stepX.x, surface.stepX.y);
        glm::vec2 sY(surface.stepY.x, surface.stepY.y);
        float lenSqX = glm::dot(sX, sX);
        float lenSqY = glm::dot(sY, sY);
        if (lenSqX < 1e-6f || lenSqY < 1e-6f) continue;
        float gx = glm::dot(rel, sX) / lenSqX;
        float gy = glm::dot(rel, sY) / lenSqY;

        if (gx < 0.0f || gx > static_cast<float>(surface.width) ||
            gy < 0.0f || gy > static_cast<float>(surface.height)) continue;

        int gridWidth = surface.width + 1;
        int ix = static_cast<int>(gx);
        int iy = static_cast<int>(gy);
        float fx = gx - ix;
        float fy = gy - iy;

        if (ix >= surface.width) { ix = surface.width - 1; fx = 1.0f; }
        if (iy >= surface.height) { iy = surface.height - 1; fy = 1.0f; }
        if (ix < 0 || iy < 0) continue;

        if (!surface.mask.empty()) {
            int tileIndex;
            if (surface.wmoId == 0 && surface.mask.size() >= 8) {
                tileIndex = (static_cast<int>(surface.yOffset) + iy) * 8 +
                            (static_cast<int>(surface.xOffset) + ix);
            } else {
                tileIndex = iy * surface.width + ix;
            }
            int byteIndex = tileIndex / 8;
            int bitIndex = tileIndex % 8;
            if (byteIndex < static_cast<int>(surface.mask.size())) {
                uint8_t maskByte = surface.mask[byteIndex];
                bool renderTile = (maskByte & (1 << bitIndex)) || (maskByte & (1 << (7 - bitIndex)));
                if (!renderTile) continue;
            }
        }

        int idx00 = iy * gridWidth + ix;
        int idx10 = idx00 + 1;
        int idx01 = idx00 + gridWidth;
        int idx11 = idx01 + 1;

        int total = static_cast<int>(surface.heights.size());
        if (idx11 >= total) continue;

        float h00 = surface.heights[idx00], h10 = surface.heights[idx10];
        float h01 = surface.heights[idx01], h11 = surface.heights[idx11];
        float h = h00*(1-fx)*(1-fy) + h10*fx*(1-fy) + h01*(1-fx)*fy + h11*fx*fy;

        if (!best || h > *best) best = h;
    }

    return best;
}

std::optional<uint16_t> WaterRenderer::getWaterTypeAt(float glX, float glY) const {
    std::optional<float> bestHeight;
    std::optional<uint16_t> bestType;

    for (const auto& surface : surfaces) {
        glm::vec2 rel(glX - surface.origin.x, glY - surface.origin.y);
        glm::vec2 sX(surface.stepX.x, surface.stepX.y);
        glm::vec2 sY(surface.stepY.x, surface.stepY.y);
        float lenSqX = glm::dot(sX, sX);
        float lenSqY = glm::dot(sY, sY);
        if (lenSqX < 1e-6f || lenSqY < 1e-6f) continue;

        float gx = glm::dot(rel, sX) / lenSqX;
        float gy = glm::dot(rel, sY) / lenSqY;
        if (gx < 0.0f || gx > static_cast<float>(surface.width) ||
            gy < 0.0f || gy > static_cast<float>(surface.height)) continue;

        int ix = static_cast<int>(gx);
        int iy = static_cast<int>(gy);
        if (ix >= surface.width) ix = surface.width - 1;
        if (iy >= surface.height) iy = surface.height - 1;
        if (ix < 0 || iy < 0) continue;

        if (!surface.mask.empty()) {
            int tileIndex;
            if (surface.wmoId == 0 && surface.mask.size() >= 8) {
                tileIndex = (static_cast<int>(surface.yOffset) + iy) * 8 +
                            (static_cast<int>(surface.xOffset) + ix);
            } else {
                tileIndex = iy * surface.width + ix;
            }
            int byteIndex = tileIndex / 8;
            int bitIndex = tileIndex % 8;
            if (byteIndex < static_cast<int>(surface.mask.size())) {
                uint8_t maskByte = surface.mask[byteIndex];
                bool renderTile = (maskByte & (1 << bitIndex)) || (maskByte & (1 << (7 - bitIndex)));
                if (!renderTile) continue;
            }
        }

        float h = surface.minHeight;
        if (!bestHeight || h > *bestHeight) {
            bestHeight = h;
            bestType = surface.liquidType;
        }
    }

    return bestType;
}

glm::vec4 WaterRenderer::getLiquidColor(uint16_t liquidType) const {
    uint8_t basicType = (liquidType == 0) ? 0 : ((liquidType - 1) % 4);
    switch (basicType) {
        case 0:  return glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
        case 1:  return glm::vec4(0.06f, 0.18f, 0.34f, 1.0f);
        case 2:  return glm::vec4(0.9f, 0.3f, 0.05f, 1.0f);
        case 3:  return glm::vec4(0.2f, 0.6f, 0.1f, 1.0f);
        default: return glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    }
}

float WaterRenderer::getLiquidAlpha(uint16_t liquidType) const {
    uint8_t basicType = (liquidType == 0) ? 0 : ((liquidType - 1) % 4);
    switch (basicType) {
        case 1:  return 0.68f;
        case 2:  return 0.72f;
        case 3:  return 0.62f;
        default: return 0.38f;
    }
}

} // namespace rendering
} // namespace wowee
