#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering {

class Camera;

/**
 * Renders quest markers as billboarded sprites above NPCs
 * Uses BLP textures from Interface\GossipFrame\
 */
class QuestMarkerRenderer {
public:
    QuestMarkerRenderer();
    ~QuestMarkerRenderer();

    bool initialize(pipeline::AssetManager* assetManager);
    void shutdown();

    /**
     * Add or update a quest marker at a position
     * @param guid NPC GUID
     * @param position World position (NPC base position)
     * @param markerType 0=available(!), 1=turnin(?), 2=incomplete(?)
     * @param boundingHeight NPC bounding height (optional, default 2.0f)
     */
    void setMarker(uint64_t guid, const glm::vec3& position, int markerType, float boundingHeight = 2.0f);

    /**
     * Remove a quest marker
     */
    void removeMarker(uint64_t guid);

    /**
     * Clear all markers
     */
    void clear();

    /**
     * Render all quest markers (call after world rendering, before UI)
     */
    void render(const Camera& camera);

private:
    struct Marker {
        glm::vec3 position;
        int type; // 0=available, 1=turnin, 2=incomplete
        float boundingHeight = 2.0f;
    };

    std::unordered_map<uint64_t, Marker> markers_;
    
    // OpenGL resources
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    uint32_t shaderProgram_ = 0;
    uint32_t textures_[3] = {0, 0, 0}; // available, turnin, incomplete

    void createQuad();
    void loadTextures(pipeline::AssetManager* assetManager);
    void createShader();
};

} // namespace rendering
} // namespace wowee
