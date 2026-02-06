#include "ui/inventory_screen.hpp"
#include "game/game_handler.hpp"
#include "core/input.hpp"
#include <imgui.h>
#include <SDL2/SDL.h>
#include <cstdio>

namespace wowee {
namespace ui {

ImVec4 InventoryScreen::getQualityColor(game::ItemQuality quality) {
    switch (quality) {
        case game::ItemQuality::POOR:      return ImVec4(0.62f, 0.62f, 0.62f, 1.0f); // Grey
        case game::ItemQuality::COMMON:    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);    // White
        case game::ItemQuality::UNCOMMON:  return ImVec4(0.12f, 1.0f, 0.0f, 1.0f);   // Green
        case game::ItemQuality::RARE:      return ImVec4(0.0f, 0.44f, 0.87f, 1.0f);  // Blue
        case game::ItemQuality::EPIC:      return ImVec4(0.64f, 0.21f, 0.93f, 1.0f); // Purple
        case game::ItemQuality::LEGENDARY: return ImVec4(1.0f, 0.50f, 0.0f, 1.0f);   // Orange
        default:                           return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

game::EquipSlot InventoryScreen::getEquipSlotForType(uint8_t inventoryType, game::Inventory& inv) {
    switch (inventoryType) {
        case 1:  return game::EquipSlot::HEAD;
        case 2:  return game::EquipSlot::NECK;
        case 3:  return game::EquipSlot::SHOULDERS;
        case 4:  return game::EquipSlot::SHIRT;
        case 5:  return game::EquipSlot::CHEST;
        case 6:  return game::EquipSlot::WAIST;
        case 7:  return game::EquipSlot::LEGS;
        case 8:  return game::EquipSlot::FEET;
        case 9:  return game::EquipSlot::WRISTS;
        case 10: return game::EquipSlot::HANDS;
        case 11: {
            if (inv.getEquipSlot(game::EquipSlot::RING1).empty())
                return game::EquipSlot::RING1;
            return game::EquipSlot::RING2;
        }
        case 12: {
            if (inv.getEquipSlot(game::EquipSlot::TRINKET1).empty())
                return game::EquipSlot::TRINKET1;
            return game::EquipSlot::TRINKET2;
        }
        case 13: // One-Hand
        case 21: // Main Hand
            return game::EquipSlot::MAIN_HAND;
        case 17: // Two-Hand
            return game::EquipSlot::MAIN_HAND;
        case 14: // Shield
        case 22: // Off Hand
        case 23: // Held In Off-hand
            return game::EquipSlot::OFF_HAND;
        case 15: // Ranged (bow/gun)
        case 25: // Thrown
        case 26: // Ranged
            return game::EquipSlot::RANGED;
        case 16: return game::EquipSlot::BACK;
        case 19: return game::EquipSlot::TABARD;
        case 20: return game::EquipSlot::CHEST; // Robe
        default: return game::EquipSlot::NUM_SLOTS;
    }
}

void InventoryScreen::pickupFromBackpack(game::Inventory& inv, int index) {
    const auto& slot = inv.getBackpackSlot(index);
    if (slot.empty()) return;
    holdingItem = true;
    heldItem = slot.item;
    heldSource = HeldSource::BACKPACK;
    heldBackpackIndex = index;
    heldEquipSlot = game::EquipSlot::NUM_SLOTS;
    inv.clearBackpackSlot(index);
    inventoryDirty = true;
}

void InventoryScreen::pickupFromEquipment(game::Inventory& inv, game::EquipSlot slot) {
    const auto& es = inv.getEquipSlot(slot);
    if (es.empty()) return;
    holdingItem = true;
    heldItem = es.item;
    heldSource = HeldSource::EQUIPMENT;
    heldBackpackIndex = -1;
    heldEquipSlot = slot;
    inv.clearEquipSlot(slot);
    equipmentDirty = true;
    inventoryDirty = true;
}

void InventoryScreen::placeInBackpack(game::Inventory& inv, int index) {
    if (!holdingItem) return;
    const auto& target = inv.getBackpackSlot(index);
    if (target.empty()) {
        inv.setBackpackSlot(index, heldItem);
        holdingItem = false;
    } else {
        // Swap
        game::ItemDef targetItem = target.item;
        inv.setBackpackSlot(index, heldItem);
        heldItem = targetItem;
        heldSource = HeldSource::BACKPACK;
        heldBackpackIndex = index;
    }
    inventoryDirty = true;
}

void InventoryScreen::placeInEquipment(game::Inventory& inv, game::EquipSlot slot) {
    if (!holdingItem) return;

    // Validate: check if the held item can go in this slot
    if (heldItem.inventoryType > 0) {
        game::EquipSlot validSlot = getEquipSlotForType(heldItem.inventoryType, inv);
        if (validSlot == game::EquipSlot::NUM_SLOTS) return;

        bool valid = (slot == validSlot);
        if (!valid) {
            if (heldItem.inventoryType == 11)
                valid = (slot == game::EquipSlot::RING1 || slot == game::EquipSlot::RING2);
            else if (heldItem.inventoryType == 12)
                valid = (slot == game::EquipSlot::TRINKET1 || slot == game::EquipSlot::TRINKET2);
        }
        if (!valid) return;
    } else {
        return;
    }

    const auto& target = inv.getEquipSlot(slot);
    if (target.empty()) {
        inv.setEquipSlot(slot, heldItem);
        holdingItem = false;
    } else {
        game::ItemDef targetItem = target.item;
        inv.setEquipSlot(slot, heldItem);
        heldItem = targetItem;
        heldSource = HeldSource::EQUIPMENT;
        heldEquipSlot = slot;
    }

    // Two-handed weapon in main hand clears the off-hand slot
    if (slot == game::EquipSlot::MAIN_HAND &&
        inv.getEquipSlot(game::EquipSlot::MAIN_HAND).item.inventoryType == 17) {
        const auto& offHand = inv.getEquipSlot(game::EquipSlot::OFF_HAND);
        if (!offHand.empty()) {
            inv.addItem(offHand.item);
            inv.clearEquipSlot(game::EquipSlot::OFF_HAND);
        }
    }

    // Equipping off-hand unequips a 2H weapon from main hand
    if (slot == game::EquipSlot::OFF_HAND &&
        inv.getEquipSlot(game::EquipSlot::MAIN_HAND).item.inventoryType == 17) {
        inv.addItem(inv.getEquipSlot(game::EquipSlot::MAIN_HAND).item);
        inv.clearEquipSlot(game::EquipSlot::MAIN_HAND);
    }

    equipmentDirty = true;
    inventoryDirty = true;
}

void InventoryScreen::cancelPickup(game::Inventory& inv) {
    if (!holdingItem) return;
    if (heldSource == HeldSource::BACKPACK && heldBackpackIndex >= 0) {
        if (inv.getBackpackSlot(heldBackpackIndex).empty()) {
            inv.setBackpackSlot(heldBackpackIndex, heldItem);
        } else {
            inv.addItem(heldItem);
        }
    } else if (heldSource == HeldSource::EQUIPMENT && heldEquipSlot != game::EquipSlot::NUM_SLOTS) {
        if (inv.getEquipSlot(heldEquipSlot).empty()) {
            inv.setEquipSlot(heldEquipSlot, heldItem);
            equipmentDirty = true;
        } else {
            inv.addItem(heldItem);
        }
    } else {
        inv.addItem(heldItem);
    }
    holdingItem = false;
    inventoryDirty = true;
}

void InventoryScreen::renderHeldItem() {
    if (!holdingItem) return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    float size = 36.0f;
    ImVec2 pos(mousePos.x - size * 0.5f, mousePos.y - size * 0.5f);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec4 qColor = getQualityColor(heldItem.quality);
    ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(qColor);

    drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
                            IM_COL32(40, 35, 30, 200));
    drawList->AddRect(pos, ImVec2(pos.x + size, pos.y + size),
                      borderCol, 0.0f, 0, 2.0f);

    char abbr[4] = {};
    if (!heldItem.name.empty()) {
        abbr[0] = heldItem.name[0];
        if (heldItem.name.size() > 1) abbr[1] = heldItem.name[1];
    }
    float textW = ImGui::CalcTextSize(abbr).x;
    drawList->AddText(ImVec2(pos.x + (size - textW) * 0.5f, pos.y + 2.0f),
                      ImGui::ColorConvertFloat4ToU32(qColor), abbr);

    if (heldItem.stackCount > 1) {
        char countStr[16];
        snprintf(countStr, sizeof(countStr), "%u", heldItem.stackCount);
        float cw = ImGui::CalcTextSize(countStr).x;
        drawList->AddText(ImVec2(pos.x + size - cw - 2.0f, pos.y + size - 14.0f),
                          IM_COL32(255, 255, 255, 220), countStr);
    }
}

// ============================================================
// Bags window (B key) — bottom of screen, no equipment panel
// ============================================================

void InventoryScreen::render(game::Inventory& inventory, uint64_t moneyCopper) {
    // B key toggle (edge-triggered)
    bool uiWantsKeyboard = ImGui::GetIO().WantCaptureKeyboard;
    bool bDown = !uiWantsKeyboard && core::Input::getInstance().isKeyPressed(SDL_SCANCODE_B);
    if (bDown && !bKeyWasDown) {
        open = !open;
    }
    bKeyWasDown = bDown;

    // C key toggle for character screen (edge-triggered)
    bool cDown = !uiWantsKeyboard && core::Input::getInstance().isKeyPressed(SDL_SCANCODE_C);
    if (cDown && !cKeyWasDown) {
        characterOpen = !characterOpen;
    }
    cKeyWasDown = cDown;

    if (!open) {
        if (holdingItem) cancelPickup(inventory);
        return;
    }

    // Escape cancels held item
    if (holdingItem && !uiWantsKeyboard && core::Input::getInstance().isKeyPressed(SDL_SCANCODE_ESCAPE)) {
        cancelPickup(inventory);
    }

    // Right-click anywhere while holding = cancel
    if (holdingItem && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        cancelPickup(inventory);
    }

    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y;

    // Calculate bag window size
    constexpr float slotSize = 40.0f;
    constexpr int columns = 4;
    int rows = (inventory.getBackpackSize() + columns - 1) / columns;
    float bagContentH = rows * (slotSize + 4.0f) + 40.0f; // slots + header + money

    // Check for extra bags and add space
    for (int bag = 0; bag < game::Inventory::NUM_BAG_SLOTS; bag++) {
        int bagSize = inventory.getBagSize(bag);
        if (bagSize <= 0) continue;
        int bagRows = (bagSize + columns - 1) / columns;
        bagContentH += bagRows * (slotSize + 4.0f) + 30.0f; // slots + header
    }

    float windowW = columns * (slotSize + 4.0f) + 30.0f;
    float windowH = bagContentH + 50.0f; // padding

    // Position at bottom-right of screen
    float posX = screenW - windowW - 10.0f;
    float posY = screenH - windowH - 60.0f; // above action bar area

    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowW, windowH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("Bags", &open, flags)) {
        ImGui::End();
        return;
    }

    renderBackpackPanel(inventory);

    // Money display
    ImGui::Spacing();
    uint64_t gold = moneyCopper / 10000;
    uint64_t silver = (moneyCopper / 100) % 100;
    uint64_t copper = moneyCopper % 100;
    ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "%llug %llus %lluc",
                       static_cast<unsigned long long>(gold),
                       static_cast<unsigned long long>(silver),
                       static_cast<unsigned long long>(copper));
    ImGui::End();

    // Draw held item at cursor
    renderHeldItem();
}

// ============================================================
// Character screen (C key) — standalone equipment window
// ============================================================

void InventoryScreen::renderCharacterScreen(game::Inventory& inventory) {
    if (!characterOpen) return;

    ImGui::SetNextWindowPos(ImVec2(20.0f, 80.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220.0f, 520.0f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Character", &characterOpen, flags)) {
        ImGui::End();
        return;
    }

    renderEquipmentPanel(inventory);

    ImGui::End();

    // If both bags and character are open, allow drag-and-drop between them
    // (held item rendering is handled in render())
    if (open) {
        renderHeldItem();
    }
}

void InventoryScreen::renderEquipmentPanel(game::Inventory& inventory) {
    ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Equipment");
    ImGui::Separator();

    static const game::EquipSlot leftSlots[] = {
        game::EquipSlot::HEAD, game::EquipSlot::NECK,
        game::EquipSlot::SHOULDERS, game::EquipSlot::BACK,
        game::EquipSlot::CHEST, game::EquipSlot::SHIRT,
        game::EquipSlot::TABARD, game::EquipSlot::WRISTS,
    };
    static const game::EquipSlot rightSlots[] = {
        game::EquipSlot::HANDS, game::EquipSlot::WAIST,
        game::EquipSlot::LEGS, game::EquipSlot::FEET,
        game::EquipSlot::RING1, game::EquipSlot::RING2,
        game::EquipSlot::TRINKET1, game::EquipSlot::TRINKET2,
    };

    constexpr float slotSize = 36.0f;
    constexpr float spacing = 4.0f;

    int rows = 8;
    for (int r = 0; r < rows; r++) {
        {
            const auto& slot = inventory.getEquipSlot(leftSlots[r]);
            const char* label = game::getEquipSlotName(leftSlots[r]);
            char id[64];
            snprintf(id, sizeof(id), "##eq_l_%d", r);
            ImGui::PushID(id);
            renderItemSlot(inventory, slot, slotSize, label,
                           SlotKind::EQUIPMENT, -1, leftSlots[r]);
            ImGui::PopID();
        }

        ImGui::SameLine(slotSize + spacing + 60.0f);

        {
            const auto& slot = inventory.getEquipSlot(rightSlots[r]);
            const char* label = game::getEquipSlotName(rightSlots[r]);
            char id[64];
            snprintf(id, sizeof(id), "##eq_r_%d", r);
            ImGui::PushID(id);
            renderItemSlot(inventory, slot, slotSize, label,
                           SlotKind::EQUIPMENT, -1, rightSlots[r]);
            ImGui::PopID();
        }
    }

    // Weapon row
    ImGui::Spacing();
    ImGui::Separator();

    static const game::EquipSlot weaponSlots[] = {
        game::EquipSlot::MAIN_HAND,
        game::EquipSlot::OFF_HAND,
        game::EquipSlot::RANGED,
    };
    for (int i = 0; i < 3; i++) {
        if (i > 0) ImGui::SameLine();
        const auto& slot = inventory.getEquipSlot(weaponSlots[i]);
        const char* label = game::getEquipSlotName(weaponSlots[i]);
        char id[64];
        snprintf(id, sizeof(id), "##eq_w_%d", i);
        ImGui::PushID(id);
        renderItemSlot(inventory, slot, slotSize, label,
                       SlotKind::EQUIPMENT, -1, weaponSlots[i]);
        ImGui::PopID();
    }
}

void InventoryScreen::renderBackpackPanel(game::Inventory& inventory) {
    ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Backpack");
    ImGui::Separator();

    constexpr float slotSize = 40.0f;
    constexpr int columns = 4;

    for (int i = 0; i < inventory.getBackpackSize(); i++) {
        if (i % columns != 0) ImGui::SameLine();

        const auto& slot = inventory.getBackpackSlot(i);
        char id[32];
        snprintf(id, sizeof(id), "##bp_%d", i);
        ImGui::PushID(id);
        renderItemSlot(inventory, slot, slotSize, nullptr,
                       SlotKind::BACKPACK, i, game::EquipSlot::NUM_SLOTS);
        ImGui::PopID();
    }

    // Show extra bags if equipped
    for (int bag = 0; bag < game::Inventory::NUM_BAG_SLOTS; bag++) {
        int bagSize = inventory.getBagSize(bag);
        if (bagSize <= 0) continue;

        ImGui::Spacing();
        ImGui::Separator();
        char bagLabel[32];
        snprintf(bagLabel, sizeof(bagLabel), "Bag %d", bag + 1);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), "%s", bagLabel);

        for (int s = 0; s < bagSize; s++) {
            if (s % columns != 0) ImGui::SameLine();
            const auto& slot = inventory.getBagSlot(bag, s);
            char sid[32];
            snprintf(sid, sizeof(sid), "##bag%d_%d", bag, s);
            ImGui::PushID(sid);
            renderItemSlot(inventory, slot, slotSize, nullptr,
                           SlotKind::BACKPACK, -1, game::EquipSlot::NUM_SLOTS);
            ImGui::PopID();
        }
    }
}

void InventoryScreen::renderItemSlot(game::Inventory& inventory, const game::ItemSlot& slot,
                                      float size, const char* label,
                                      SlotKind kind, int backpackIndex,
                                      game::EquipSlot equipSlot) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    bool isEmpty = slot.empty();

    // Determine if this is a valid drop target for held item
    bool validDrop = false;
    if (holdingItem) {
        if (kind == SlotKind::BACKPACK && backpackIndex >= 0) {
            validDrop = true;
        } else if (kind == SlotKind::EQUIPMENT && heldItem.inventoryType > 0) {
            game::EquipSlot validSlot = getEquipSlotForType(heldItem.inventoryType, inventory);
            validDrop = (equipSlot == validSlot);
            if (!validDrop && heldItem.inventoryType == 11)
                validDrop = (equipSlot == game::EquipSlot::RING1 || equipSlot == game::EquipSlot::RING2);
            if (!validDrop && heldItem.inventoryType == 12)
                validDrop = (equipSlot == game::EquipSlot::TRINKET1 || equipSlot == game::EquipSlot::TRINKET2);
        }
    }

    if (isEmpty) {
        ImU32 bgCol = IM_COL32(30, 30, 30, 200);
        ImU32 borderCol = IM_COL32(60, 60, 60, 200);

        if (validDrop) {
            bgCol = IM_COL32(20, 50, 20, 200);
            borderCol = IM_COL32(0, 180, 0, 200);
        }

        drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), bgCol);
        drawList->AddRect(pos, ImVec2(pos.x + size, pos.y + size), borderCol);

        if (label) {
            char abbr[4] = {};
            abbr[0] = label[0];
            if (label[1]) abbr[1] = label[1];
            float textW = ImGui::CalcTextSize(abbr).x;
            drawList->AddText(ImVec2(pos.x + (size - textW) * 0.5f, pos.y + size * 0.3f),
                              IM_COL32(80, 80, 80, 180), abbr);
        }

        ImGui::InvisibleButton("slot", ImVec2(size, size));

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && holdingItem && validDrop) {
            if (kind == SlotKind::BACKPACK && backpackIndex >= 0) {
                placeInBackpack(inventory, backpackIndex);
            } else if (kind == SlotKind::EQUIPMENT) {
                placeInEquipment(inventory, equipSlot);
            }
        }

        if (label && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", label);
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Empty");
            ImGui::EndTooltip();
        }
    } else {
        const auto& item = slot.item;
        ImVec4 qColor = getQualityColor(item.quality);
        ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(qColor);

        ImU32 bgCol = IM_COL32(40, 35, 30, 220);
        if (holdingItem && validDrop) {
            bgCol = IM_COL32(30, 55, 30, 220);
            borderCol = IM_COL32(0, 200, 0, 220);
        }

        drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), bgCol);
        drawList->AddRect(pos, ImVec2(pos.x + size, pos.y + size),
                          borderCol, 0.0f, 0, 2.0f);

        char abbr[4] = {};
        if (!item.name.empty()) {
            abbr[0] = item.name[0];
            if (item.name.size() > 1) abbr[1] = item.name[1];
        }
        float textW = ImGui::CalcTextSize(abbr).x;
        drawList->AddText(ImVec2(pos.x + (size - textW) * 0.5f, pos.y + 2.0f),
                          ImGui::ColorConvertFloat4ToU32(qColor), abbr);

        if (item.stackCount > 1) {
            char countStr[16];
            snprintf(countStr, sizeof(countStr), "%u", item.stackCount);
            float cw = ImGui::CalcTextSize(countStr).x;
            drawList->AddText(ImVec2(pos.x + size - cw - 2.0f, pos.y + size - 14.0f),
                              IM_COL32(255, 255, 255, 220), countStr);
        }

        ImGui::InvisibleButton("slot", ImVec2(size, size));

        // Left-click: pickup or place/swap
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (!holdingItem) {
                if (kind == SlotKind::BACKPACK && backpackIndex >= 0) {
                    pickupFromBackpack(inventory, backpackIndex);
                } else if (kind == SlotKind::EQUIPMENT) {
                    pickupFromEquipment(inventory, equipSlot);
                }
            } else {
                if (kind == SlotKind::BACKPACK && backpackIndex >= 0) {
                    placeInBackpack(inventory, backpackIndex);
                } else if (kind == SlotKind::EQUIPMENT && validDrop) {
                    placeInEquipment(inventory, equipSlot);
                }
            }
        }

        // Right-click: vendor sell (if vendor mode) or auto-equip/unequip
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !holdingItem) {
            if (vendorMode_ && gameHandler_ && kind == SlotKind::BACKPACK && backpackIndex >= 0) {
                // Sell to vendor
                gameHandler_->sellItemBySlot(backpackIndex);
            } else if (kind == SlotKind::EQUIPMENT) {
                // Unequip: move to free backpack slot
                int freeSlot = inventory.findFreeBackpackSlot();
                if (freeSlot >= 0) {
                    inventory.setBackpackSlot(freeSlot, item);
                    inventory.clearEquipSlot(equipSlot);
                    equipmentDirty = true;
                    inventoryDirty = true;
                }
            } else if (kind == SlotKind::BACKPACK && backpackIndex >= 0 && item.inventoryType > 0) {
                // Auto-equip
                uint8_t equippingType = item.inventoryType;
                game::EquipSlot targetSlot = getEquipSlotForType(equippingType, inventory);
                if (targetSlot != game::EquipSlot::NUM_SLOTS) {
                    const auto& eqSlot = inventory.getEquipSlot(targetSlot);
                    if (eqSlot.empty()) {
                        inventory.setEquipSlot(targetSlot, item);
                        inventory.clearBackpackSlot(backpackIndex);
                    } else {
                        game::ItemDef equippedItem = eqSlot.item;
                        inventory.setEquipSlot(targetSlot, item);
                        inventory.setBackpackSlot(backpackIndex, equippedItem);
                    }
                    if (targetSlot == game::EquipSlot::MAIN_HAND && equippingType == 17) {
                        const auto& offHand = inventory.getEquipSlot(game::EquipSlot::OFF_HAND);
                        if (!offHand.empty()) {
                            inventory.addItem(offHand.item);
                            inventory.clearEquipSlot(game::EquipSlot::OFF_HAND);
                        }
                    }
                    if (targetSlot == game::EquipSlot::OFF_HAND &&
                        inventory.getEquipSlot(game::EquipSlot::MAIN_HAND).item.inventoryType == 17) {
                        inventory.addItem(inventory.getEquipSlot(game::EquipSlot::MAIN_HAND).item);
                        inventory.clearEquipSlot(game::EquipSlot::MAIN_HAND);
                    }
                    equipmentDirty = true;
                    inventoryDirty = true;
                }
            }
        }

        if (ImGui::IsItemHovered() && !holdingItem) {
            renderItemTooltip(item);
        }
    }
}

void InventoryScreen::renderItemTooltip(const game::ItemDef& item) {
    ImGui::BeginTooltip();

    ImVec4 qColor = getQualityColor(item.quality);
    ImGui::TextColored(qColor, "%s", item.name.c_str());

    // Slot type
    if (item.inventoryType > 0) {
        const char* slotName = "";
        switch (item.inventoryType) {
            case 1:  slotName = "Head"; break;
            case 2:  slotName = "Neck"; break;
            case 3:  slotName = "Shoulder"; break;
            case 4:  slotName = "Shirt"; break;
            case 5:  slotName = "Chest"; break;
            case 6:  slotName = "Waist"; break;
            case 7:  slotName = "Legs"; break;
            case 8:  slotName = "Feet"; break;
            case 9:  slotName = "Wrist"; break;
            case 10: slotName = "Hands"; break;
            case 11: slotName = "Finger"; break;
            case 12: slotName = "Trinket"; break;
            case 13: slotName = "One-Hand"; break;
            case 14: slotName = "Shield"; break;
            case 15: slotName = "Ranged"; break;
            case 16: slotName = "Back"; break;
            case 17: slotName = "Two-Hand"; break;
            case 18: slotName = "Bag"; break;
            case 19: slotName = "Tabard"; break;
            case 20: slotName = "Robe"; break;
            case 21: slotName = "Main Hand"; break;
            case 22: slotName = "Off Hand"; break;
            case 23: slotName = "Held In Off-hand"; break;
            case 25: slotName = "Thrown"; break;
            case 26: slotName = "Ranged"; break;
            default: slotName = ""; break;
        }
        if (slotName[0]) {
            if (!item.subclassName.empty()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s  %s", slotName, item.subclassName.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", slotName);
            }
        }
    }

    // Armor
    if (item.armor > 0) {
        ImGui::Text("%d Armor", item.armor);
    }

    // Stats
    if (item.stamina != 0) ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "+%d Stamina", item.stamina);
    if (item.strength != 0) ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "+%d Strength", item.strength);
    if (item.agility != 0) ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "+%d Agility", item.agility);
    if (item.intellect != 0) ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "+%d Intellect", item.intellect);
    if (item.spirit != 0) ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "+%d Spirit", item.spirit);

    // Stack info
    if (item.maxStack > 1) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Stack: %u/%u", item.stackCount, item.maxStack);
    }

    // Sell price (when vendor is open)
    if (vendorMode_ && gameHandler_) {
        const auto* info = gameHandler_->getItemInfo(item.itemId);
        if (info && info->sellPrice > 0) {
            uint32_t g = info->sellPrice / 10000;
            uint32_t s = (info->sellPrice / 100) % 100;
            uint32_t c = info->sellPrice % 100;
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Sell Price: %ug %us %uc", g, s, c);
        }
    }

    ImGui::EndTooltip();
}

} // namespace ui
} // namespace wowee
