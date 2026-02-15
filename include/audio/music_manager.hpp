#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace audio {

class MusicManager {
public:
    MusicManager();
    ~MusicManager();

    bool initialize(pipeline::AssetManager* assets);
    void shutdown();

    void playMusic(const std::string& mpqPath, bool loop = true);
    void playFilePath(const std::string& filePath, bool loop = true);
    void stopMusic(float fadeMs = 2000.0f);
    void crossfadeTo(const std::string& mpqPath, float fadeMs = 3000.0f);
    void crossfadeToFile(const std::string& filePath, float fadeMs = 3000.0f);
    void update(float deltaTime);
    void setVolume(int volume);
    int getVolume() const { return volumePercent; }
    void setUnderwaterMode(bool underwater);
    void preloadMusic(const std::string& mpqPath);

    bool isPlaying() const { return playing; }
    bool isInitialized() const { return assetManager != nullptr; }
    const std::string& getCurrentTrack() const { return currentTrack; }

private:
    pipeline::AssetManager* assetManager = nullptr;
    std::string currentTrack;
    bool currentTrackIsFile = false;
    bool playing = false;
    int volumePercent = 30;
    bool underwaterMode = false;

    // Crossfade state
    bool crossfading = false;
    std::string pendingTrack;
    bool pendingIsFile = false;
    float fadeTimer = 0.0f;
    float fadeDuration = 0.0f;

    std::unordered_map<std::string, std::vector<uint8_t>> musicDataCache_;
};

} // namespace audio
} // namespace wowee
