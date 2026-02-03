#include "audio/footstep_manager.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace wowee {
namespace audio {

namespace {

std::vector<std::string> buildClassicFootstepSet(const std::string& material) {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'L'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\mFootMediumLarge" + material + std::string(1, c) + ".wav");
    }
    return out;
}

std::vector<std::string> buildAltFootstepSet(const std::string& folder, const std::string& stem) {
    std::vector<std::string> out;
    for (int i = 1; i <= 8; ++i) {
        char index[4];
        std::snprintf(index, sizeof(index), "%02d", i);
        out.push_back("Sound\\Character\\Footsteps\\" + folder + "\\" + stem + "_" + index + ".wav");
    }
    return out;
}

} // namespace

FootstepManager::FootstepManager() : rng(std::random_device{}()) {}

FootstepManager::~FootstepManager() {
    shutdown();
}

bool FootstepManager::initialize(pipeline::AssetManager* assets) {
    assetManager = assets;
    sampleCount = 0;
    for (auto& surface : surfaces) {
        surface.clips.clear();
    }

    if (!assetManager) {
        return false;
    }

    preloadSurface(FootstepSurface::STONE, buildClassicFootstepSet("Stone"));
    preloadSurface(FootstepSurface::DIRT, buildClassicFootstepSet("Dirt"));
    preloadSurface(FootstepSurface::GRASS, buildClassicFootstepSet("Grass"));
    preloadSurface(FootstepSurface::WOOD, buildClassicFootstepSet("Wood"));
    preloadSurface(FootstepSurface::SNOW, buildClassicFootstepSet("Snow"));
    preloadSurface(FootstepSurface::WATER, buildClassicFootstepSet("Water"));

    // Alternate naming seen in some builds (especially metals).
    preloadSurface(FootstepSurface::METAL, buildAltFootstepSet("MediumLargeMetalFootsteps", "MediumLargeFootstepMetal"));
    if (surfaces[static_cast<size_t>(FootstepSurface::METAL)].clips.empty()) {
        preloadSurface(FootstepSurface::METAL, buildClassicFootstepSet("Metal"));
    }

    LOG_INFO("Footstep manager initialized (", sampleCount, " clips)");
    return sampleCount > 0;
}

void FootstepManager::shutdown() {
    stopCurrentProcess();
    std::remove(tempFilePath.c_str());
    for (auto& surface : surfaces) {
        surface.clips.clear();
    }
    sampleCount = 0;
    assetManager = nullptr;
}

void FootstepManager::update(float) {
    reapFinishedProcess();
}

void FootstepManager::playFootstep(FootstepSurface surface, bool sprinting) {
    if (!assetManager || sampleCount == 0) {
        return;
    }
    reapFinishedProcess();
    playRandomStep(surface, sprinting);
}

void FootstepManager::preloadSurface(FootstepSurface surface, const std::vector<std::string>& candidates) {
    if (!assetManager) {
        return;
    }

    auto& list = surfaces[static_cast<size_t>(surface)].clips;
    for (const std::string& path : candidates) {
        if (!assetManager->fileExists(path)) {
            continue;
        }
        auto data = assetManager->readFile(path);
        if (data.empty()) {
            continue;
        }
        list.push_back({path, std::move(data)});
        sampleCount++;
    }

    if (!list.empty()) {
        LOG_INFO("Footsteps ", surfaceName(surface), ": loaded ", list.size(), " clips");
    }
}

void FootstepManager::stopCurrentProcess() {
    if (playerPid > 0) {
        kill(-playerPid, SIGTERM);
        kill(playerPid, SIGTERM);
        int status = 0;
        waitpid(playerPid, &status, 0);
        playerPid = -1;
    }
}

void FootstepManager::reapFinishedProcess() {
    if (playerPid <= 0) {
        return;
    }
    int status = 0;
    pid_t result = waitpid(playerPid, &status, WNOHANG);
    if (result == playerPid) {
        playerPid = -1;
    }
}

bool FootstepManager::playRandomStep(FootstepSurface surface, bool sprinting) {
    auto& list = surfaces[static_cast<size_t>(surface)].clips;
    if (list.empty()) {
        list = surfaces[static_cast<size_t>(FootstepSurface::STONE)].clips;
        if (list.empty()) {
            return false;
        }
    }

    // Keep one active step at a time to avoid ffplay process buildup.
    if (playerPid > 0) {
        return false;
    }

    std::uniform_int_distribution<size_t> clipDist(0, list.size() - 1);
    const Sample& sample = list[clipDist(rng)];

    std::ofstream out(tempFilePath, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(sample.data.data()), static_cast<std::streamsize>(sample.data.size()));
    out.close();

    // Subtle variation for less repetitive cadence.
    std::uniform_real_distribution<float> pitchDist(0.97f, 1.05f);
    std::uniform_real_distribution<float> volumeDist(0.92f, 1.00f);
    float pitch = pitchDist(rng);
    float volume = volumeDist(rng) * (sprinting ? 1.0f : 0.88f);
    if (volume > 1.0f) volume = 1.0f;
    if (volume < 0.1f) volume = 0.1f;

    std::string filter = "asetrate=44100*" + std::to_string(pitch) +
                         ",aresample=44100,volume=" + std::to_string(volume);

    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        FILE* outFile = freopen("/dev/null", "w", stdout);
        FILE* errFile = freopen("/dev/null", "w", stderr);
        (void)outFile;
        (void)errFile;
        execlp("ffplay", "ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet",
               "-af", filter.c_str(), tempFilePath.c_str(), nullptr);
        _exit(1);
    } else if (pid > 0) {
        playerPid = pid;
        return true;
    }

    return false;
}

const char* FootstepManager::surfaceName(FootstepSurface surface) {
    switch (surface) {
        case FootstepSurface::STONE: return "stone";
        case FootstepSurface::DIRT: return "dirt";
        case FootstepSurface::GRASS: return "grass";
        case FootstepSurface::WOOD: return "wood";
        case FootstepSurface::METAL: return "metal";
        case FootstepSurface::WATER: return "water";
        case FootstepSurface::SNOW: return "snow";
        default: return "unknown";
    }
}

} // namespace audio
} // namespace wowee
