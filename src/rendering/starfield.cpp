#include "rendering/starfield.hpp"
#include "rendering/shader.hpp"
#include "rendering/camera.hpp"
#include "core/logger.hpp"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <random>

namespace wowee {
namespace rendering {

StarField::StarField() = default;

StarField::~StarField() {
    shutdown();
}

bool StarField::initialize() {
    LOG_INFO("Initializing star field");

    // Create star shader
    starShader = std::make_unique<Shader>();

    // Vertex shader - simple point rendering
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in float aBrightness;
        layout (location = 2) in float aTwinklePhase;

        uniform mat4 view;
        uniform mat4 projection;
        uniform float time;
        uniform float intensity;

        out float Brightness;

        void main() {
            // Remove translation from view matrix (stars are infinitely far)
            mat4 viewNoTranslation = mat4(mat3(view));

            gl_Position = projection * viewNoTranslation * vec4(aPos, 1.0);

            // Twinkle effect (subtle brightness variation)
            float twinkle = sin(time * 2.0 + aTwinklePhase) * 0.2 + 0.8;  // 0.6 to 1.0

            Brightness = aBrightness * twinkle * intensity;

            // Point size based on brightness
            gl_PointSize = 2.0 + aBrightness * 2.0;  // 2-4 pixels
        }
    )";

    // Fragment shader - star color
    const char* fragmentShaderSource = R"(
        #version 330 core
        in float Brightness;

        out vec4 FragColor;

        void main() {
            // Circular point (not square)
            vec2 coord = gl_PointCoord - vec2(0.5);
            float dist = length(coord);
            if (dist > 0.5) {
                discard;
            }

            // Soften edges
            float alpha = smoothstep(0.5, 0.3, dist);

            // Star color (slightly blue-white)
            vec3 starColor = vec3(0.9, 0.95, 1.0);

            FragColor = vec4(starColor * Brightness, alpha * Brightness);
        }
    )";

    if (!starShader->loadFromSource(vertexShaderSource, fragmentShaderSource)) {
        LOG_ERROR("Failed to create star shader");
        return false;
    }

    // Generate random stars
    generateStars();

    // Create OpenGL buffers
    createStarBuffers();

    LOG_INFO("Star field initialized: ", starCount, " stars");
    return true;
}

void StarField::shutdown() {
    destroyStarBuffers();
    starShader.reset();
    stars.clear();
}

void StarField::render(const Camera& camera, float timeOfDay,
                       float cloudDensity, float fogDensity) {
    if (!renderingEnabled || vao == 0 || !starShader || stars.empty()) {
        return;
    }

    // Get star intensity based on time of day
    float intensity = getStarIntensity(timeOfDay);

    // Reduce intensity based on cloud density and fog (more clouds/fog = fewer visible stars)
    intensity *= (1.0f - glm::clamp(cloudDensity * 0.7f, 0.0f, 1.0f));
    intensity *= (1.0f - glm::clamp(fogDensity * 0.3f, 0.0f, 1.0f));

    // Don't render if stars would be invisible
    if (intensity <= 0.01f) {
        return;
    }

    // Enable blending for star glow
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Enable point sprites
    glEnable(GL_PROGRAM_POINT_SIZE);

    // Disable depth writing (stars are background)
    glDepthMask(GL_FALSE);

    starShader->use();

    // Set uniforms
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix();

    starShader->setUniform("view", view);
    starShader->setUniform("projection", projection);
    starShader->setUniform("time", twinkleTime);
    starShader->setUniform("intensity", intensity);

    // Render stars as points
    glBindVertexArray(vao);
    glDrawArrays(GL_POINTS, 0, starCount);
    glBindVertexArray(0);

    // Restore state
    glDepthMask(GL_TRUE);
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_BLEND);
}

void StarField::update(float deltaTime) {
    // Update twinkle animation
    twinkleTime += deltaTime;
}

void StarField::generateStars() {
    stars.clear();
    stars.reserve(starCount);

    // Random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> phiDist(0.0f, M_PI / 2.0f);  // 0 to 90 degrees (hemisphere)
    std::uniform_real_distribution<float> thetaDist(0.0f, 2.0f * M_PI);  // 0 to 360 degrees
    std::uniform_real_distribution<float> brightnessDist(0.3f, 1.0f);  // Varying brightness
    std::uniform_real_distribution<float> twinkleDist(0.0f, 2.0f * M_PI);  // Random twinkle phase

    const float radius = 900.0f;  // Slightly larger than skybox

    for (int i = 0; i < starCount; i++) {
        Star star;

        // Spherical coordinates (hemisphere)
        float phi = phiDist(gen);      // Elevation angle
        float theta = thetaDist(gen);  // Azimuth angle

        // Convert to Cartesian coordinates
        float x = radius * std::sin(phi) * std::cos(theta);
        float y = radius * std::sin(phi) * std::sin(theta);
        float z = radius * std::cos(phi);

        star.position = glm::vec3(x, y, z);
        star.brightness = brightnessDist(gen);
        star.twinklePhase = twinkleDist(gen);

        stars.push_back(star);
    }

    LOG_DEBUG("Generated ", stars.size(), " stars");
}

void StarField::createStarBuffers() {
    // Prepare vertex data (position, brightness, twinkle phase)
    std::vector<float> vertexData;
    vertexData.reserve(stars.size() * 5);  // 3 pos + 1 brightness + 1 phase

    for (const auto& star : stars) {
        vertexData.push_back(star.position.x);
        vertexData.push_back(star.position.y);
        vertexData.push_back(star.position.z);
        vertexData.push_back(star.brightness);
        vertexData.push_back(star.twinklePhase);
    }

    // Create OpenGL buffers
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STATIC_DRAW);

    // Set vertex attributes
    // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Brightness
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Twinkle phase
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void StarField::destroyStarBuffers() {
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
}

float StarField::getStarIntensity(float timeOfDay) const {
    // Stars visible at night (fade in/out at dusk/dawn)

    // Full night: 20:00-4:00
    if (timeOfDay >= 20.0f || timeOfDay < 4.0f) {
        return 1.0f;
    }
    // Fade in at dusk: 18:00-20:00
    else if (timeOfDay >= 18.0f && timeOfDay < 20.0f) {
        return (timeOfDay - 18.0f) / 2.0f;  // 0 to 1 over 2 hours
    }
    // Fade out at dawn: 4:00-6:00
    else if (timeOfDay >= 4.0f && timeOfDay < 6.0f) {
        return 1.0f - (timeOfDay - 4.0f) / 2.0f;  // 1 to 0 over 2 hours
    }
    // Daytime: no stars
    else {
        return 0.0f;
    }
}

} // namespace rendering
} // namespace wowee
