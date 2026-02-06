#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wowee {
namespace game {

/**
 * Aura slot data for buff/debuff tracking
 */
struct AuraSlot {
    uint32_t spellId = 0;
    uint8_t flags = 0;         // Active, positive/negative, etc.
    uint8_t level = 0;
    uint8_t charges = 0;
    int32_t durationMs = -1;
    int32_t maxDurationMs = -1;
    uint64_t casterGuid = 0;

    bool isEmpty() const { return spellId == 0; }
};

/**
 * Action bar slot
 */
struct ActionBarSlot {
    enum Type : uint8_t { EMPTY = 0, SPELL = 1, ITEM = 2, MACRO = 3 };
    Type type = EMPTY;
    uint32_t id = 0;              // spellId, itemId, or macroId
    float cooldownRemaining = 0.0f;
    float cooldownTotal = 0.0f;

    bool isReady() const { return cooldownRemaining <= 0.0f; }
    bool isEmpty() const { return type == EMPTY; }
};

/**
 * Floating combat text entry
 */
struct CombatTextEntry {
    enum Type : uint8_t {
        MELEE_DAMAGE, SPELL_DAMAGE, HEAL, MISS, DODGE, PARRY, BLOCK,
        CRIT_DAMAGE, CRIT_HEAL, PERIODIC_DAMAGE, PERIODIC_HEAL, ENVIRONMENTAL
    };
    Type type;
    int32_t amount = 0;
    uint32_t spellId = 0;
    float age = 0.0f;           // Seconds since creation (for fadeout)
    bool isPlayerSource = false; // True if player dealt this

    static constexpr float LIFETIME = 2.5f;
    bool isExpired() const { return age >= LIFETIME; }
};

/**
 * Spell cooldown entry received from server
 */
struct SpellCooldownEntry {
    uint32_t spellId;
    uint16_t itemId;
    uint16_t categoryId;
    uint32_t cooldownMs;
    uint32_t categoryCooldownMs;
};

/**
 * Get human-readable spell cast failure reason (WoW 3.3.5a SpellCastResult)
 */
inline const char* getSpellCastResultString(uint8_t result) {
    switch (result) {
        case 0:   return "Spell failed";
        case 1:   return "Affects dead target";
        case 2:   return "Already at full health";
        case 3:   return "Already at full mana";
        case 4:   return "Already at full power";
        case 5:   return "Already being tamed";
        case 6:   return "Already have charm";
        case 7:   return "Already have summon";
        case 8:   return "Already open";
        case 9:   return "Aura bounced";
        case 10:  return "Autopilot in use";
        case 11:  return "Bad implicit targets";
        case 12:  return "Bad targets";
        case 13:  return "Can't be charmed";
        case 14:  return "Can't be disenchanted";
        case 15:  return "Can't be disenchanted (skill)";
        case 16:  return "Can't be milled";
        case 17:  return "Can't be prospected";
        case 18:  return "Can't cast on tapped";
        case 19:  return "Can't duel while invisible";
        case 20:  return "Can't duel while stealthed";
        case 21:  return "Can't stealth";
        case 22:  return "Caster aurastate";
        case 23:  return "Caster dead";
        case 24:  return "Charmed";
        case 25:  return "Chest in use";
        case 26:  return "Confused";
        case 27:  return "Don't report";
        case 28:  return "Equipped item";
        case 29:  return "Equipped item (class)";
        case 30:  return "Equipped item (class2)";
        case 31:  return "Equipped item (level)";
        case 32:  return "Error";
        case 33:  return "Fizzle";
        case 34:  return "Fleeing";
        case 35:  return "Food too low level";
        case 36:  return "Highlighted rune needed";
        case 37:  return "Immune";
        case 38:  return "Interrupted";
        case 39:  return "Interrupted (combat)";
        case 40:  return "Invalid item";
        case 41:  return "Item already enchanted";
        case 42:  return "Item gone";
        case 43:  return "Item not found";
        case 44:  return "Item not ready";
        case 45:  return "Level requirement";
        case 46:  return "Line of sight";
        case 47:  return "Lowlevel";
        case 48:  return "Low castlevel";
        case 49:  return "Mainhand empty";
        case 50:  return "Moving";
        case 51:  return "Must be behind target";
        case 52:  return "Need ammo";
        case 53:  return "Need ammo pouch";
        case 54:  return "Need exotic ammo";
        case 55:  return "Need more items";
        case 56:  return "No path";
        case 57:  return "Not behind";
        case 58:  return "Not fishable";
        case 59:  return "Not flying";
        case 60:  return "Not here";
        case 61:  return "Not infront";
        case 62:  return "Not in control";
        case 63:  return "Not known";
        case 64:  return "Not mounted";
        case 65:  return "Not on taxi";
        case 66:  return "Not on transport";
        case 67:  return "Not ready";
        case 68:  return "Not shapeshift";
        case 69:  return "Not standing";
        case 70:  return "Not tradeable";
        case 71:  return "Not trading";
        case 72:  return "Not unsheathed";
        case 73:  return "Not while ghost";
        case 74:  return "Not while looting";
        case 75:  return "No charges remain";
        case 76:  return "No champion";
        case 77:  return "No combo points";
        case 78:  return "No dueling";
        case 79:  return "No endurance";
        case 80:  return "No fish";
        case 81:  return "No items while shapeshifted";
        case 82:  return "No mounts allowed here";
        case 83:  return "No pet";
        case 84:  return "No power";
        case 85:  return "Nothing to dispel";
        case 86:  return "Nothing to steal";
        case 87:  return "Only above water";
        case 88:  return "Only daytime";
        case 89:  return "Only indoors";
        case 90:  return "Only mounted";
        case 91:  return "Only nighttime";
        case 92:  return "Only outdoors";
        case 93:  return "Only shapeshift";
        case 94:  return "Only stealthed";
        case 95:  return "Only underwater";
        case 96:  return "Out of range";
        case 97:  return "Pacified";
        case 98:  return "Possessed";
        case 99:  return "Reagents";
        case 100: return "Requires area";
        case 101: return "Requires spell focus";
        case 102: return "Rooted";
        case 103: return "Silenced";
        case 104: return "Spell in progress";
        case 105: return "Spell learned";
        case 106: return "Spell unavailable";
        case 107: return "Stunned";
        case 108: return "Targets dead";
        case 109: return "Target not dead";
        case 110: return "Target not in party";
        case 111: return "Target not in raid";
        case 112: return "Target friendly";
        case 113: return "Target is player";
        case 114: return "Target is player controlled";
        case 115: return "Target not dead";
        case 116: return "Target not in party";
        case 117: return "Target not player";
        case 118: return "Target no pockets";
        case 119: return "Target no weapons";
        case 120: return "Target out of range";
        case 121: return "Target unskinnable";
        case 122: return "Thirst satiated";
        case 123: return "Too close";
        case 124: return "Too many of item";
        case 125: return "Totem category";
        case 126: return "Totems";
        case 127: return "Training points";
        case 128: return "Try again";
        case 129: return "Unit not behind";
        case 130: return "Unit not infront";
        case 131: return "Wrong pet food";
        case 132: return "Not while fatigued";
        case 133: return "Target not in instance";
        case 134: return "Not while trading";
        case 135: return "Target not in raid";
        case 136: return "Target feign dead";
        case 137: return "Disabled by power scaling";
        case 138: return "Quest players only";
        case 139: return "Not idle";
        case 140: return "Not inactive";
        case 141: return "Partial playtime";
        case 142: return "No playtime";
        case 143: return "Not in battleground";
        case 144: return "Not in raid instance";
        case 145: return "Only in arena";
        case 146: return "Target locked to raid instance";
        case 147: return "On use enchant";
        case 148: return "Not on ground";
        case 149: return "Custom error";
        case 150: return "Can't open lock";
        case 151: return "Wrong artifact equipped";
        case 173: return "Not enough mana";
        case 174: return "Not enough health";
        case 175: return "Not enough holy power";
        case 176: return "Not enough rage";
        case 177: return "Not enough energy";
        case 178: return "Not enough runes";
        case 179: return "Not enough runic power";
        default:  return nullptr;
    }
}

} // namespace game
} // namespace wowee
