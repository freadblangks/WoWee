#include "audio/mount_sound_manager.hpp"
#include "audio/audio_engine.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"

namespace wowee {
namespace audio {

MountSoundManager::MountSoundManager() = default;

MountSoundManager::~MountSoundManager() {
    shutdown();
}

bool MountSoundManager::initialize(pipeline::AssetManager* assets) {
    assetManager_ = assets;
    LOG_INFO("Mount sound manager initialized");
    return true;
}

void MountSoundManager::shutdown() {
    stopAllMountSounds();
    mounted_ = false;
    assetManager_ = nullptr;
}

void MountSoundManager::update(float deltaTime) {
    (void)deltaTime;

    if (!mounted_) {
        return;
    }

    updateMountSounds();
}

void MountSoundManager::onMount(uint32_t creatureDisplayId, bool isFlying) {
    mounted_ = true;
    currentDisplayId_ = creatureDisplayId;
    currentMountType_ = detectMountType(creatureDisplayId);
    flying_ = isFlying;
    moving_ = false;

    LOG_INFO("Mount sound: mounted on display ID ", creatureDisplayId,
             " type=", static_cast<int>(currentMountType_),
             " flying=", flying_);

    updateMountSounds();
}

void MountSoundManager::onDismount() {
    stopAllMountSounds();
    mounted_ = false;
    currentMountType_ = MountType::NONE;
    currentDisplayId_ = 0;
    flying_ = false;
    moving_ = false;
}

void MountSoundManager::setMoving(bool moving) {
    if (moving_ != moving) {
        moving_ = moving;
        if (mounted_) {
            updateMountSounds();
        }
    }
}

void MountSoundManager::setFlying(bool flying) {
    if (flying_ != flying) {
        flying_ = flying;
        if (mounted_) {
            updateMountSounds();
        }
    }
}

void MountSoundManager::setGrounded(bool grounded) {
    setFlying(!grounded);
}

MountType MountSoundManager::detectMountType(uint32_t creatureDisplayId) const {
    // TODO: Load from CreatureDisplayInfo.dbc or CreatureModelData.dbc
    // For now, use simple heuristics based on common display IDs
    // This is a placeholder - we'd need proper DBC parsing for accuracy

    // Common flying mount display IDs (approximate ranges)
    // Gryphons: ~2300-2310
    // Wyverns: ~1566-1570
    // Drakes: ~25830-25870
    // Phoenixes: ~17890-17900

    if (creatureDisplayId >= 2300 && creatureDisplayId <= 2320) return MountType::FLYING; // Gryphons
    if (creatureDisplayId >= 1560 && creatureDisplayId <= 1580) return MountType::FLYING; // Wyverns
    if (creatureDisplayId >= 25800 && creatureDisplayId <= 25900) return MountType::FLYING; // Drakes
    if (creatureDisplayId >= 17880 && creatureDisplayId <= 17910) return MountType::FLYING; // Phoenixes

    // Most other mounts are ground
    return MountType::GROUND;
}

void MountSoundManager::updateMountSounds() {
    if (!AudioEngine::instance().isInitialized() || !mounted_) {
        return;
    }

    // TODO: Implement actual mount sound playback
    // For now, just log state changes
    static bool lastMoving = false;
    static bool lastFlying = false;

    if (moving_ != lastMoving || flying_ != lastFlying) {
        LOG_INFO("Mount sound state: moving=", moving_, " flying=", flying_,
                 " type=", static_cast<int>(currentMountType_));
        lastMoving = moving_;
        lastFlying = flying_;
    }

    // TODO: Load and play appropriate looping sounds:
    // - Flying + moving: wing flaps (fast loop)
    // - Flying + idle: wing flaps (slow loop) or hovering sound
    // - Ground + moving: galloping/hoofbeats (pace based on speed)
    // - Ground + idle: breathing, fidgeting sounds (occasional)
}

void MountSoundManager::stopAllMountSounds() {
    // TODO: Stop any active looping mount sounds
    playingMovementSound_ = false;
    playingIdleSound_ = false;
}

} // namespace audio
} // namespace wowee
