#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace wowee {
namespace game {

struct ZoneInfo {
    uint32_t id;
    std::string name;
    std::vector<std::string> musicPaths;  // MPQ paths to music files
};

class ZoneManager {
public:
    void initialize();

    uint32_t getZoneId(int tileX, int tileY) const;
    const ZoneInfo* getZoneInfo(uint32_t zoneId) const;
    std::string getRandomMusic(uint32_t zoneId) const;
    std::vector<std::string> getAllMusicPaths() const;

private:
    // tile key = tileX * 100 + tileY
    std::unordered_map<int, uint32_t> tileToZone;
    std::unordered_map<uint32_t, ZoneInfo> zones;
};

} // namespace game
} // namespace wowee
