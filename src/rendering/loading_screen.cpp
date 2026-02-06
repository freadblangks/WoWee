#include "rendering/loading_screen.hpp"
#include "core/logger.hpp"
#include <random>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace wowee {
namespace rendering {

LoadingScreen::LoadingScreen() {
    // Add loading screen image paths
    imagePaths.push_back("assets/loading1.jpeg");
    imagePaths.push_back("assets/loading2.jpeg");
}

LoadingScreen::~LoadingScreen() {
    shutdown();
}

bool LoadingScreen::initialize() {
    LOG_INFO("Initializing loading screen");

    // Create simple shader for textured quad
    const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);
            TexCoord = aTexCoord;
        }
    )";

    const char* fragmentSrc = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D screenTexture;
        void main() {
            FragColor = texture(screenTexture, TexCoord);
        }
    )";

    // Compile vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSrc, nullptr);
    glCompileShader(vertexShader);

    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        LOG_ERROR("Loading screen vertex shader compilation failed: ", infoLog);
        return false;
    }

    // Compile fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSrc, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        LOG_ERROR("Loading screen fragment shader compilation failed: ", infoLog);
        return false;
    }

    // Link shader program
    shaderId = glCreateProgram();
    glAttachShader(shaderId, vertexShader);
    glAttachShader(shaderId, fragmentShader);
    glLinkProgram(shaderId);

    glGetProgramiv(shaderId, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(shaderId, 512, nullptr, infoLog);
        LOG_ERROR("Loading screen shader linking failed: ", infoLog);
        return false;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    createQuad();
    selectRandomImage();

    LOG_INFO("Loading screen initialized");
    return true;
}

void LoadingScreen::shutdown() {
    if (textureId) {
        glDeleteTextures(1, &textureId);
        textureId = 0;
    }
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (shaderId) {
        glDeleteProgram(shaderId);
        shaderId = 0;
    }
}

void LoadingScreen::selectRandomImage() {
    if (imagePaths.empty()) return;

    // Seed with current time
    unsigned seed = static_cast<unsigned>(
        std::chrono::system_clock::now().time_since_epoch().count());
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> distribution(0, imagePaths.size() - 1);

    currentImageIndex = distribution(generator);
    LOG_INFO("Selected loading screen: ", imagePaths[currentImageIndex]);

    loadImage(imagePaths[currentImageIndex]);
}

bool LoadingScreen::loadImage(const std::string& path) {
    // Delete old texture if exists
    if (textureId) {
        glDeleteTextures(1, &textureId);
        textureId = 0;
    }

    // Load image with stb_image
    int channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &imageWidth, &imageHeight, &channels, 4);

    if (!data) {
        LOG_ERROR("Failed to load loading screen image: ", path);
        return false;
    }

    LOG_INFO("Loaded loading screen image: ", imageWidth, "x", imageHeight);

    // Create OpenGL texture
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth, imageHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void LoadingScreen::createQuad() {
    // Full-screen quad vertices (position + texcoord)
    float vertices[] = {
        // Position    // TexCoord
        -1.0f,  1.0f,  0.0f, 1.0f,  // Top-left
        -1.0f, -1.0f,  0.0f, 0.0f,  // Bottom-left
         1.0f, -1.0f,  1.0f, 0.0f,  // Bottom-right

        -1.0f,  1.0f,  0.0f, 1.0f,  // Top-left
         1.0f, -1.0f,  1.0f, 0.0f,  // Bottom-right
         1.0f,  1.0f,  1.0f, 1.0f   // Top-right
    };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void LoadingScreen::render() {
    if (!textureId || !vao || !shaderId) return;

    // Clear screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Disable depth test for 2D rendering
    glDisable(GL_DEPTH_TEST);

    // Use shader and bind texture
    glUseProgram(shaderId);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);

    // Draw quad
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Re-enable depth test
    glEnable(GL_DEPTH_TEST);
}

} // namespace rendering
} // namespace wowee
