#pragma once

#include <string>
#include <cstdint>
#include <vector>

typedef unsigned int GLuint;

namespace wowee {
namespace rendering {

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool open(const std::string& path);
    void update(float deltaTime);
    void close();

    bool isReady() const { return textureReady; }
    GLuint getTextureId() const { return textureId; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    bool decodeNextFrame();
    void uploadFrame();

    void* formatCtx = nullptr;
    void* codecCtx = nullptr;
    void* frame = nullptr;
    void* rgbFrame = nullptr;
    void* packet = nullptr;
    void* swsCtx = nullptr;

    int videoStreamIndex = -1;
    int width = 0;
    int height = 0;
    double frameTime = 1.0 / 30.0;
    double accumulator = 0.0;
    bool eof = false;

    GLuint textureId = 0;
    bool textureReady = false;
    std::string sourcePath;
    std::vector<uint8_t> rgbBuffer;
};

} // namespace rendering
} // namespace wowee
