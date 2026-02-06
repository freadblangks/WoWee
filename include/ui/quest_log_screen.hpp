#pragma once

#include "game/game_handler.hpp"
#include <imgui.h>

namespace wowee { namespace ui {

class QuestLogScreen {
public:
    void render(game::GameHandler& gameHandler);
    bool isOpen() const { return open; }
    void toggle() { open = !open; }
    void setOpen(bool o) { open = o; }

private:
    bool open = false;
    bool lKeyWasDown = false;
    int selectedIndex = -1;
};

}} // namespace wowee::ui
