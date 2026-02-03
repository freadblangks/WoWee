#include "rendering/water_renderer.hpp"
#include "rendering/shader.hpp"
#include "rendering/camera.hpp"
#include "pipeline/adt_loader.hpp"
#include "core/logger.hpp"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace wowee {
namespace rendering {

WaterRenderer::WaterRenderer() = default;

WaterRenderer::~WaterRenderer() {
    shutdown();
}

bool WaterRenderer::initialize() {
    LOG_INFO("Initializing water renderer");

    // Create water shader
    waterShader = std::make_unique<Shader>();

    // Vertex shader
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aTexCoord;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        uniform float time;

        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;
        out float WaveOffset;

        void main() {
            // Simple pass-through for debugging (no wave animation)
            vec3 pos = aPos;

            FragPos = vec3(model * vec4(pos, 1.0));
            // Use mat3(model) directly - avoids expensive inverse() per vertex
            Normal = mat3(model) * aNormal;
            TexCoord = aTexCoord;
            WaveOffset = 0.0;

            gl_Position = projection * view * vec4(FragPos, 1.0);
        }
    )";

    // Fragment shader
    const char* fragmentShaderSource = R"(
        #version 330 core
        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;
        in float WaveOffset;

        uniform vec3 viewPos;
        uniform vec4 waterColor;
        uniform float waterAlpha;
        uniform float time;

        out vec4 FragColor;

        void main() {
            // Normalize interpolated normal
            vec3 norm = normalize(Normal);

            // Simple directional light (sun)
            vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
            float diff = max(dot(norm, lightDir), 0.0);

            // Specular highlights (shininess for water)
            vec3 viewDir = normalize(viewPos - FragPos);
            vec3 reflectDir = reflect(-lightDir, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 64.0);

            // Animated texture coordinates for flowing effect
            vec2 uv1 = TexCoord + vec2(time * 0.02, time * 0.01);
            vec2 uv2 = TexCoord + vec2(-time * 0.01, time * 0.015);

            // Combine lighting
            vec3 ambient = vec3(0.3) * waterColor.rgb;
            vec3 diffuse = vec3(0.6) * diff * waterColor.rgb;
            vec3 specular = vec3(1.0) * spec;

            // Add wave offset to brightness
            float brightness = 1.0 + WaveOffset * 0.1;

            vec3 result = (ambient + diffuse + specular) * brightness;

            // Apply transparency
            FragColor = vec4(result, waterAlpha);
        }
    )";

    if (!waterShader->loadFromSource(vertexShaderSource, fragmentShaderSource)) {
        LOG_ERROR("Failed to create water shader");
        return false;
    }

    LOG_INFO("Water renderer initialized");
    return true;
}

void WaterRenderer::shutdown() {
    clear();
    waterShader.reset();
}

void WaterRenderer::loadFromTerrain(const pipeline::ADTTerrain& terrain, bool append,
                                     int tileX, int tileY) {
    if (!append) {
        LOG_INFO("Loading water from terrain (replacing)");
        clear();
    } else {
        LOG_INFO("Loading water from terrain (appending)");
    }

    // Load water surfaces from MH2O data
    int totalLayers = 0;

    for (int chunkIdx = 0; chunkIdx < 256; chunkIdx++) {
        const auto& chunkWater = terrain.waterData[chunkIdx];

        if (!chunkWater.hasWater()) {
            continue;
        }

        // Get the terrain chunk for position reference
        int chunkX = chunkIdx % 16;
        int chunkY = chunkIdx / 16;
        const auto& terrainChunk = terrain.getChunk(chunkX, chunkY);

        // Process each water layer in this chunk
        for (const auto& layer : chunkWater.layers) {
            WaterSurface surface;

            // Use the chunk base position - layer offsets will be applied in mesh generation
            // to match terrain's coordinate transformation
            surface.position = glm::vec3(
                terrainChunk.position[0],
                terrainChunk.position[1],
                layer.minHeight
            );

            // Debug log first few water surfaces
            if (totalLayers < 5) {
                LOG_DEBUG("Water layer ", totalLayers, ": chunk=", chunkIdx,
                         " liquidType=", layer.liquidType,
                         " offset=(", (int)layer.x, ",", (int)layer.y, ")",
                         " size=", (int)layer.width, "x", (int)layer.height,
                         " height range=[", layer.minHeight, ",", layer.maxHeight, "]");
            }

            surface.minHeight = layer.minHeight;
            surface.maxHeight = layer.maxHeight;
            surface.liquidType = layer.liquidType;

            // Store dimensions
            surface.xOffset = layer.x;
            surface.yOffset = layer.y;
            surface.width = layer.width;
            surface.height = layer.height;

            // Copy height data
            if (!layer.heights.empty()) {
                surface.heights = layer.heights;
            } else {
                // Flat water at minHeight if no height data
                size_t numVertices = (layer.width + 1) * (layer.height + 1);
                surface.heights.resize(numVertices, layer.minHeight);
            }

            // Copy render mask
            surface.mask = layer.mask;

            surface.tileX = tileX;
            surface.tileY = tileY;
            createWaterMesh(surface);
            surfaces.push_back(surface);
            totalLayers++;
        }
    }

    LOG_INFO("Loaded ", totalLayers, " water layers from MH2O data");
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

void WaterRenderer::clear() {
    for (auto& surface : surfaces) {
        destroyWaterMesh(surface);
    }
    surfaces.clear();
}

void WaterRenderer::render(const Camera& camera, float time) {
    if (!renderingEnabled || surfaces.empty() || !waterShader) {
        return;
    }

    // Enable alpha blending for transparent water
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Disable depth writing so terrain shows through water
    glDepthMask(GL_FALSE);

    waterShader->use();

    // Set uniforms
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix();

    waterShader->setUniform("view", view);
    waterShader->setUniform("projection", projection);
    waterShader->setUniform("viewPos", camera.getPosition());
    waterShader->setUniform("time", time);

    // Render each water surface
    for (const auto& surface : surfaces) {
        if (surface.vao == 0) {
            continue;
        }

        // Model matrix (identity, position already in vertices)
        glm::mat4 model = glm::mat4(1.0f);
        waterShader->setUniform("model", model);

        // Set liquid-specific color and alpha
        glm::vec4 color = getLiquidColor(surface.liquidType);
        float alpha = getLiquidAlpha(surface.liquidType);

        waterShader->setUniform("waterColor", color);
        waterShader->setUniform("waterAlpha", alpha);

        // Render
        glBindVertexArray(surface.vao);
        glDrawElements(GL_TRIANGLES, surface.indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    // Restore state
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void WaterRenderer::createWaterMesh(WaterSurface& surface) {
    // Variable-size grid based on water layer dimensions
    const int gridWidth = surface.width + 1;   // Vertices = tiles + 1
    const int gridHeight = surface.height + 1;
    const float TILE_SIZE = 33.33333f / 8.0f;  // Size of one tile (same as terrain unitSize)

    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    // Generate vertices
    // Match terrain coordinate transformation: pos[0] = baseX - (y * unitSize), pos[1] = baseY - (x * unitSize)
    for (int y = 0; y < gridHeight; y++) {
        for (int x = 0; x < gridWidth; x++) {
            int index = y * gridWidth + x;

            // Use per-vertex height data if available, otherwise flat at minHeight
            float height;
            if (index < static_cast<int>(surface.heights.size())) {
                height = surface.heights[index];
            } else {
                height = surface.minHeight;
            }

            // Position - match terrain coordinate transformation (swap and negate)
            // Terrain uses: X = baseX - (offsetY * unitSize), Y = baseY - (offsetX * unitSize)
            // Also apply layer offset within chunk (xOffset, yOffset)
            float posX = surface.position.x - ((surface.yOffset + y) * TILE_SIZE);
            float posY = surface.position.y - ((surface.xOffset + x) * TILE_SIZE);
            float posZ = height;

            // Debug first surface's corner vertices
            static int debugCount = 0;
            if (debugCount < 4 && (x == 0 || x == gridWidth-1) && (y == 0 || y == gridHeight-1)) {
                LOG_DEBUG("Water vertex: (", posX, ", ", posY, ", ", posZ, ")");
                debugCount++;
            }

            vertices.push_back(posX);
            vertices.push_back(posY);
            vertices.push_back(posZ);

            // Normal (pointing up for water surface)
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
            vertices.push_back(1.0f);

            // Texture coordinates
            vertices.push_back(static_cast<float>(x) / std::max(1, gridWidth - 1));
            vertices.push_back(static_cast<float>(y) / std::max(1, gridHeight - 1));
        }
    }

    // Generate indices (triangles), respecting the render mask
    for (int y = 0; y < gridHeight - 1; y++) {
        for (int x = 0; x < gridWidth - 1; x++) {
            // Check render mask - each bit represents a tile
            bool renderTile = true;
            if (!surface.mask.empty()) {
                int tileIndex = y * surface.width + x;
                int byteIndex = tileIndex / 8;
                int bitIndex = tileIndex % 8;
                if (byteIndex < static_cast<int>(surface.mask.size())) {
                    renderTile = (surface.mask[byteIndex] & (1 << bitIndex)) != 0;
                }
            }

            if (!renderTile) {
                continue;  // Skip this tile
            }

            int topLeft = y * gridWidth + x;
            int topRight = topLeft + 1;
            int bottomLeft = (y + 1) * gridWidth + x;
            int bottomRight = bottomLeft + 1;

            // First triangle
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Second triangle
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    if (indices.empty()) {
        // No visible tiles
        return;
    }

    surface.indexCount = static_cast<int>(indices.size());

    // Create OpenGL buffers
    glGenVertexArrays(1, &surface.vao);
    glGenBuffers(1, &surface.vbo);
    glGenBuffers(1, &surface.ebo);

    glBindVertexArray(surface.vao);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, surface.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, surface.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    // Set vertex attributes
    // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Texture coordinates
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void WaterRenderer::destroyWaterMesh(WaterSurface& surface) {
    if (surface.vao != 0) {
        glDeleteVertexArrays(1, &surface.vao);
        surface.vao = 0;
    }
    if (surface.vbo != 0) {
        glDeleteBuffers(1, &surface.vbo);
        surface.vbo = 0;
    }
    if (surface.ebo != 0) {
        glDeleteBuffers(1, &surface.ebo);
        surface.ebo = 0;
    }
}

std::optional<float> WaterRenderer::getWaterHeightAt(float glX, float glY) const {
    const float TILE_SIZE = 33.33333f / 8.0f;
    std::optional<float> best;

    for (size_t si = 0; si < surfaces.size(); si++) {
        const auto& surface = surfaces[si];
        float gy = (surface.position.x - glX) / TILE_SIZE - static_cast<float>(surface.yOffset);
        float gx = (surface.position.y - glY) / TILE_SIZE - static_cast<float>(surface.xOffset);

        if (gx < 0.0f || gx > static_cast<float>(surface.width) ||
            gy < 0.0f || gy > static_cast<float>(surface.height)) {
            continue;
        }

        int gridWidth = surface.width + 1;

        // Bilinear interpolation
        int ix = static_cast<int>(gx);
        int iy = static_cast<int>(gy);
        float fx = gx - ix;
        float fy = gy - iy;

        // Clamp to valid vertex range
        if (ix >= surface.width) { ix = surface.width - 1; fx = 1.0f; }
        if (iy >= surface.height) { iy = surface.height - 1; fy = 1.0f; }

        int idx00 = iy * gridWidth + ix;
        int idx10 = idx00 + 1;
        int idx01 = idx00 + gridWidth;
        int idx11 = idx01 + 1;

        int total = static_cast<int>(surface.heights.size());
        if (idx11 >= total) continue;

        float h00 = surface.heights[idx00];
        float h10 = surface.heights[idx10];
        float h01 = surface.heights[idx01];
        float h11 = surface.heights[idx11];

        float h = h00 * (1-fx) * (1-fy) + h10 * fx * (1-fy) +
                  h01 * (1-fx) * fy     + h11 * fx * fy;

        if (!best || h > *best) {
            best = h;
        }
    }

    return best;
}

glm::vec4 WaterRenderer::getLiquidColor(uint8_t liquidType) const {
    // WoW 3.3.5a LiquidType.dbc IDs:
    // 1,5,9,13,17 = Water variants (still, slow, fast)
    // 2,6,10,14   = Ocean
    // 3,7,11,15   = Magma
    // 4,8,12      = Slime
    // Map to basic type using (id - 1) % 4 for standard IDs, or handle ranges
    uint8_t basicType;
    if (liquidType == 0) {
        basicType = 0;  // Water (fallback)
    } else {
        basicType = ((liquidType - 1) % 4);
    }

    switch (basicType) {
        case 0:  // Water
            return glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
        case 1:  // Ocean
            return glm::vec4(0.1f, 0.3f, 0.5f, 1.0f);
        case 2:  // Magma
            return glm::vec4(0.9f, 0.3f, 0.05f, 1.0f);
        case 3:  // Slime
            return glm::vec4(0.2f, 0.6f, 0.1f, 1.0f);
        default:
            return glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);  // Water fallback
    }
}

float WaterRenderer::getLiquidAlpha(uint8_t liquidType) const {
    uint8_t basicType = (liquidType == 0) ? 0 : ((liquidType - 1) % 4);
    switch (basicType) {
        case 2:  return 0.85f;  // Magma - mostly opaque
        case 3:  return 0.75f;  // Slime - semi-opaque
        default: return 0.55f;  // Water/Ocean - semi-transparent
    }
}

} // namespace rendering
} // namespace wowee
