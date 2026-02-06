#pragma once

#include "game/inventory.hpp"
#include "game/world_packets.hpp"
#include <imgui.h>
#include <functional>

namespace wowee {
namespace game { class GameHandler; }
namespace ui {

class InventoryScreen {
public:
    /// Render bags window (B key). Positioned at bottom of screen.
    void render(game::Inventory& inventory, uint64_t moneyCopper);

    /// Render character screen (C key). Standalone equipment window.
    void renderCharacterScreen(game::Inventory& inventory);

    bool isOpen() const { return open; }
    void toggle() { open = !open; }
    void setOpen(bool o) { open = o; }

    bool isCharacterOpen() const { return characterOpen; }
    void toggleCharacter() { characterOpen = !characterOpen; }
    void setCharacterOpen(bool o) { characterOpen = o; }

    /// Enable vendor mode: right-clicking bag items sells them.
    void setVendorMode(bool enabled, game::GameHandler* handler) {
        vendorMode_ = enabled;
        gameHandler_ = handler;
    }

    /// Returns true if equipment changed since last call, and clears the flag.
    bool consumeEquipmentDirty() { bool d = equipmentDirty; equipmentDirty = false; return d; }
    /// Returns true if any inventory slot changed since last call, and clears the flag.
    bool consumeInventoryDirty() { bool d = inventoryDirty; inventoryDirty = false; return d; }

private:
    bool open = false;
    bool characterOpen = false;
    bool bKeyWasDown = false;
    bool cKeyWasDown = false;
    bool equipmentDirty = false;
    bool inventoryDirty = false;

    // Vendor sell mode
    bool vendorMode_ = false;
    game::GameHandler* gameHandler_ = nullptr;

    // Drag-and-drop held item state
    bool holdingItem = false;
    game::ItemDef heldItem;
    enum class HeldSource { NONE, BACKPACK, EQUIPMENT };
    HeldSource heldSource = HeldSource::NONE;
    int heldBackpackIndex = -1;
    game::EquipSlot heldEquipSlot = game::EquipSlot::NUM_SLOTS;

    void renderEquipmentPanel(game::Inventory& inventory);
    void renderBackpackPanel(game::Inventory& inventory);

    // Slot rendering with interaction support
    enum class SlotKind { BACKPACK, EQUIPMENT };
    void renderItemSlot(game::Inventory& inventory, const game::ItemSlot& slot,
                        float size, const char* label,
                        SlotKind kind, int backpackIndex,
                        game::EquipSlot equipSlot);
    void renderItemTooltip(const game::ItemDef& item);

    // Held item helpers
    void pickupFromBackpack(game::Inventory& inv, int index);
    void pickupFromEquipment(game::Inventory& inv, game::EquipSlot slot);
    void placeInBackpack(game::Inventory& inv, int index);
    void placeInEquipment(game::Inventory& inv, game::EquipSlot slot);
    void cancelPickup(game::Inventory& inv);
    game::EquipSlot getEquipSlotForType(uint8_t inventoryType, game::Inventory& inv);
    void renderHeldItem();

    static ImVec4 getQualityColor(game::ItemQuality quality);
};

} // namespace ui
} // namespace wowee
