#pragma once

#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace wowee {
namespace rendering {

class Shader;
class Camera;

/**
 * Star field renderer
 *
 * Renders a field of stars across the night sky.
 * Stars fade in at dusk and out at dawn.
 */
class StarField {
public:
    StarField();
    ~StarField();

    bool initialize();
    void shutdown();

    /**
     * Render the star field
     * @param camera Camera for view matrix
     * @param timeOfDay Time of day in hours (0-24)
     * @param cloudDensity Optional cloud density from lighting (0-1, reduces star visibility)
     * @param fogDensity Optional fog density from lighting (reduces star visibility)
     */
    void render(const Camera& camera, float timeOfDay,
                float cloudDensity = 0.0f, float fogDensity = 0.0f);

    /**
     * Update star twinkle animation
     */
    void update(float deltaTime);

    /**
     * Enable/disable star rendering
     */
    void setEnabled(bool enabled) { renderingEnabled = enabled; }
    bool isEnabled() const { return renderingEnabled; }

    /**
     * Get number of stars
     */
    int getStarCount() const { return starCount; }

private:
    void generateStars();
    void createStarBuffers();
    void destroyStarBuffers();

    float getStarIntensity(float timeOfDay) const;

    std::unique_ptr<Shader> starShader;

    struct Star {
        glm::vec3 position;
        float brightness;    // 0.3 to 1.0
        float twinklePhase;  // 0 to 2π for animation
    };

    std::vector<Star> stars;
    int starCount = 1000;

    uint32_t vao = 0;
    uint32_t vbo = 0;

    float twinkleTime = 0.0f;
    bool renderingEnabled = true;
};

} // namespace rendering
} // namespace wowee
