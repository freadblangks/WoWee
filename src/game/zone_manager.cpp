#include "game/zone_manager.hpp"
#include "core/logger.hpp"
#include <cstdlib>
#include <ctime>
#include <unordered_set>

namespace wowee {
namespace game {

void ZoneManager::initialize() {
    // Elwynn Forest (zone 12)
    ZoneInfo elwynn;
    elwynn.id = 12;
    elwynn.name = "Elwynn Forest";
    elwynn.musicPaths = {
        "Sound\\Music\\ZoneMusic\\Forest\\DayForest01.mp3",
        "Sound\\Music\\ZoneMusic\\Forest\\DayForest02.mp3",
        "Sound\\Music\\ZoneMusic\\Forest\\DayForest03.mp3",
    };
    zones[12] = elwynn;

    // Stormwind City (zone 1519)
    ZoneInfo stormwind;
    stormwind.id = 1519;
    stormwind.name = "Stormwind City";
    stormwind.musicPaths = {
        "Sound\\Music\\CityMusic\\Stormwind\\stormwind04-zone.mp3",
        "Sound\\Music\\CityMusic\\Stormwind\\stormwind05-zone.mp3",
        "Sound\\Music\\CityMusic\\Stormwind\\stormwind06-zone.mp3",
        "Sound\\Music\\CityMusic\\Stormwind\\stormwind07-zone.mp3",
        "Sound\\Music\\CityMusic\\Stormwind\\stormwind08-zone.mp3",
        "Sound\\Music\\CityMusic\\Stormwind\\stormwind09-zone.mp3",
        "Sound\\Music\\CityMusic\\Stormwind\\stormwind10-zone.mp3",
    };
    zones[1519] = stormwind;

    // Dun Morogh (zone 1) - neighboring zone
    ZoneInfo dunmorogh;
    dunmorogh.id = 1;
    dunmorogh.name = "Dun Morogh";
    dunmorogh.musicPaths = {
        "Sound\\Music\\ZoneMusic\\Mountain\\DayMountain01.mp3",
        "Sound\\Music\\ZoneMusic\\Mountain\\DayMountain02.mp3",
        "Sound\\Music\\ZoneMusic\\Mountain\\DayMountain03.mp3",
    };
    zones[1] = dunmorogh;

    // Westfall (zone 40)
    ZoneInfo westfall;
    westfall.id = 40;
    westfall.name = "Westfall";
    westfall.musicPaths = {
        "Sound\\Music\\ZoneMusic\\Plains\\DayPlains01.mp3",
        "Sound\\Music\\ZoneMusic\\Plains\\DayPlains02.mp3",
        "Sound\\Music\\ZoneMusic\\Plains\\DayPlains03.mp3",
    };
    zones[40] = westfall;

    // Tile-to-zone mappings for Azeroth (Eastern Kingdoms)
    // Elwynn Forest tiles
    for (int tx = 31; tx <= 34; tx++) {
        for (int ty = 48; ty <= 51; ty++) {
            tileToZone[tx * 100 + ty] = 12;  // Elwynn
        }
    }

    // Stormwind City tiles (northern part of Elwynn area)
    tileToZone[31 * 100 + 47] = 1519;
    tileToZone[32 * 100 + 47] = 1519;
    tileToZone[33 * 100 + 47] = 1519;

    // Westfall tiles (west of Elwynn)
    for (int ty = 48; ty <= 51; ty++) {
        tileToZone[35 * 100 + ty] = 40;
        tileToZone[36 * 100 + ty] = 40;
    }

    // Dun Morogh tiles (south/east of Elwynn)
    for (int tx = 31; tx <= 34; tx++) {
        tileToZone[tx * 100 + 52] = 1;
        tileToZone[tx * 100 + 53] = 1;
    }

    std::srand(static_cast<unsigned>(std::time(nullptr)));

    LOG_INFO("Zone manager initialized: ", zones.size(), " zones, ", tileToZone.size(), " tile mappings");
}

uint32_t ZoneManager::getZoneId(int tileX, int tileY) const {
    int key = tileX * 100 + tileY;
    auto it = tileToZone.find(key);
    if (it != tileToZone.end()) {
        return it->second;
    }
    return 0;  // Unknown zone
}

const ZoneInfo* ZoneManager::getZoneInfo(uint32_t zoneId) const {
    auto it = zones.find(zoneId);
    if (it != zones.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string ZoneManager::getRandomMusic(uint32_t zoneId) const {
    auto it = zones.find(zoneId);
    if (it == zones.end() || it->second.musicPaths.empty()) {
        return "";
    }

    const auto& paths = it->second.musicPaths;
    return paths[std::rand() % paths.size()];
}

std::vector<std::string> ZoneManager::getAllMusicPaths() const {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& [zoneId, zone] : zones) {
        (void)zoneId;
        for (const auto& path : zone.musicPaths) {
            if (path.empty()) continue;
            if (seen.insert(path).second) {
                out.push_back(path);
            }
        }
    }
    return out;
}

} // namespace game
} // namespace wowee
