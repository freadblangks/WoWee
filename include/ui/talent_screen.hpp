#pragma once

#include "game/game_handler.hpp"
#include <imgui.h>
#include <GL/glew.h>
#include <unordered_map>
#include <string>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace ui {

class TalentScreen {
public:
    void render(game::GameHandler& gameHandler);
    bool isOpen() const { return open; }
    void toggle() { open = !open; }
    void setOpen(bool o) { open = o; }

private:
    void renderTalentTrees(game::GameHandler& gameHandler);
    void renderTalentTree(game::GameHandler& gameHandler, uint32_t tabId);
    void renderTalent(game::GameHandler& gameHandler, const game::GameHandler::TalentEntry& talent);

    void loadSpellDBC(pipeline::AssetManager* assetManager);
    void loadSpellIconDBC(pipeline::AssetManager* assetManager);
    GLuint getSpellIcon(uint32_t iconId, pipeline::AssetManager* assetManager);

    bool open = false;
    bool nKeyWasDown = false;

    // DBC caches
    bool spellDbcLoaded = false;
    bool iconDbcLoaded = false;
    std::unordered_map<uint32_t, uint32_t> spellIconIds;  // spellId -> iconId
    std::unordered_map<uint32_t, std::string> spellIconPaths;  // iconId -> path
    std::unordered_map<uint32_t, GLuint> spellIconCache;  // iconId -> texture
    std::unordered_map<uint32_t, std::string> spellTooltips;  // spellId -> description
};

} // namespace ui
} // namespace wowee
