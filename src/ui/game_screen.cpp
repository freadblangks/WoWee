#include "ui/game_screen.hpp"
#include "core/application.hpp"
#include "core/input.hpp"
#include "rendering/renderer.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/camera.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_loader.hpp"
#include "core/logger.hpp"
#include <imgui.h>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <unordered_set>

namespace {
    constexpr float ZEROPOINT = 32.0f * 533.33333f;

    glm::vec3 wowToGL(float wowX, float wowY, float wowZ) {
        return { -(wowZ - ZEROPOINT), -(wowX - ZEROPOINT), wowY };
    }

    bool raySphereIntersect(const wowee::rendering::Ray& ray, const glm::vec3& center, float radius, float& tOut) {
        glm::vec3 oc = ray.origin - center;
        float b = glm::dot(oc, ray.direction);
        float c = glm::dot(oc, oc) - radius * radius;
        float discriminant = b * b - c;
        if (discriminant < 0.0f) return false;
        float t = -b - std::sqrt(discriminant);
        if (t < 0.0f) t = -b + std::sqrt(discriminant);
        if (t < 0.0f) return false;
        tOut = t;
        return true;
    }

    std::string getEntityName(const std::shared_ptr<wowee::game::Entity>& entity) {
        if (entity->getType() == wowee::game::ObjectType::PLAYER) {
            auto player = std::static_pointer_cast<wowee::game::Player>(entity);
            if (!player->getName().empty()) return player->getName();
        } else if (entity->getType() == wowee::game::ObjectType::UNIT) {
            auto unit = std::static_pointer_cast<wowee::game::Unit>(entity);
            if (!unit->getName().empty()) return unit->getName();
        }
        return "Unknown";
    }
}

namespace wowee { namespace ui {

GameScreen::GameScreen() {
}

void GameScreen::render(game::GameHandler& gameHandler) {
    // Process targeting input before UI windows
    processTargetInput(gameHandler);

    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Player Info", nullptr, &showPlayerInfo);
            ImGui::MenuItem("Entity List", nullptr, &showEntityWindow);
            ImGui::MenuItem("Chat", nullptr, &showChatWindow);
            bool invOpen = inventoryScreen.isOpen();
            if (ImGui::MenuItem("Inventory", "B", &invOpen)) {
                inventoryScreen.setOpen(invOpen);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Player unit frame (top-left)
    renderPlayerFrame(gameHandler);

    // Target frame (only when we have a target)
    if (gameHandler.hasTarget()) {
        renderTargetFrame(gameHandler);
    }

    // Render windows
    if (showPlayerInfo) {
        renderPlayerInfo(gameHandler);
    }

    if (showEntityWindow) {
        renderEntityList(gameHandler);
    }

    if (showChatWindow) {
        renderChatWindow(gameHandler);
    }

    // Inventory (B key toggle handled inside)
    inventoryScreen.render(gameHandler.getInventory());

    if (inventoryScreen.consumeEquipmentDirty()) {
        updateCharacterGeosets(gameHandler.getInventory());
        updateCharacterTextures(gameHandler.getInventory());
        core::Application::getInstance().loadEquippedWeapons();
    }

    // Update renderer face-target position
    auto* renderer = core::Application::getInstance().getRenderer();
    if (renderer) {
        static glm::vec3 targetGLPos;
        if (gameHandler.hasTarget()) {
            auto target = gameHandler.getTarget();
            if (target) {
                targetGLPos = wowToGL(target->getX(), target->getY(), target->getZ());
                renderer->setTargetPosition(&targetGLPos);
            } else {
                renderer->setTargetPosition(nullptr);
            }
        } else {
            renderer->setTargetPosition(nullptr);
        }
    }
}

void GameScreen::renderPlayerInfo(game::GameHandler& gameHandler) {
    ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::Begin("Player Info", &showPlayerInfo);

    const auto& movement = gameHandler.getMovementInfo();

    ImGui::Text("Position & Movement");
    ImGui::Separator();
    ImGui::Spacing();

    // Position
    ImGui::Text("Position:");
    ImGui::Indent();
    ImGui::Text("X: %.2f", movement.x);
    ImGui::Text("Y: %.2f", movement.y);
    ImGui::Text("Z: %.2f", movement.z);
    ImGui::Text("Orientation: %.2f rad (%.1f deg)", movement.orientation, movement.orientation * 180.0f / 3.14159f);
    ImGui::Unindent();

    ImGui::Spacing();

    // Movement flags
    ImGui::Text("Movement Flags: 0x%08X", movement.flags);
    ImGui::Text("Time: %u ms", movement.time);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Connection state
    ImGui::Text("Connection State:");
    ImGui::Indent();
    auto state = gameHandler.getState();
    switch (state) {
        case game::WorldState::IN_WORLD:
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "In World");
            break;
        case game::WorldState::AUTHENTICATED:
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Authenticated");
            break;
        case game::WorldState::ENTERING_WORLD:
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Entering World...");
            break;
        default:
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "State: %d", static_cast<int>(state));
            break;
    }
    ImGui::Unindent();

    ImGui::End();
}

void GameScreen::renderEntityList(game::GameHandler& gameHandler) {
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 290), ImGuiCond_FirstUseEver);
    ImGui::Begin("Entities", &showEntityWindow);

    const auto& entityManager = gameHandler.getEntityManager();
    const auto& entities = entityManager.getEntities();

    ImGui::Text("Entities in View: %zu", entities.size());
    ImGui::Separator();
    ImGui::Spacing();

    if (entities.empty()) {
        ImGui::TextDisabled("No entities in view");
    } else {
        // Entity table
        if (ImGui::BeginTable("EntitiesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            const auto& playerMovement = gameHandler.getMovementInfo();
            float playerX = playerMovement.x;
            float playerY = playerMovement.y;
            float playerZ = playerMovement.z;

            for (const auto& [guid, entity] : entities) {
                ImGui::TableNextRow();

                // GUID
                ImGui::TableSetColumnIndex(0);
                std::stringstream guidStr;
                guidStr << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(16) << guid;
                ImGui::Text("%s", guidStr.str().c_str());

                // Type
                ImGui::TableSetColumnIndex(1);
                switch (entity->getType()) {
                    case game::ObjectType::PLAYER:
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Player");
                        break;
                    case game::ObjectType::UNIT:
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Unit");
                        break;
                    case game::ObjectType::GAMEOBJECT:
                        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "GameObject");
                        break;
                    default:
                        ImGui::Text("Object");
                        break;
                }

                // Name (for players and units)
                ImGui::TableSetColumnIndex(2);
                if (entity->getType() == game::ObjectType::PLAYER) {
                    auto player = std::static_pointer_cast<game::Player>(entity);
                    ImGui::Text("%s", player->getName().c_str());
                } else if (entity->getType() == game::ObjectType::UNIT) {
                    auto unit = std::static_pointer_cast<game::Unit>(entity);
                    if (!unit->getName().empty()) {
                        ImGui::Text("%s", unit->getName().c_str());
                    } else {
                        ImGui::TextDisabled("--");
                    }
                } else {
                    ImGui::TextDisabled("--");
                }

                // Position
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.1f, %.1f, %.1f", entity->getX(), entity->getY(), entity->getZ());

                // Distance from player
                ImGui::TableSetColumnIndex(4);
                float dx = entity->getX() - playerX;
                float dy = entity->getY() - playerY;
                float dz = entity->getZ() - playerZ;
                float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                ImGui::Text("%.1f", distance);
            }

            ImGui::EndTable();
        }
    }

    ImGui::End();
}

void GameScreen::renderChatWindow(game::GameHandler& gameHandler) {
    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(520, 390), ImGuiCond_FirstUseEver);
    ImGui::Begin("Chat", nullptr, ImGuiWindowFlags_NoCollapse);

    // Chat history
    const auto& chatHistory = gameHandler.getChatHistory();

    ImGui::BeginChild("ChatHistory", ImVec2(0, -70), true, ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& msg : chatHistory) {
        ImVec4 color = getChatTypeColor(msg.type);
        ImGui::PushStyleColor(ImGuiCol_Text, color);

        std::stringstream ss;

        if (msg.type == game::ChatType::TEXT_EMOTE) {
            ss << "You " << msg.message;
        } else {
            ss << "[" << getChatTypeName(msg.type) << "] ";

            if (!msg.senderName.empty()) {
                ss << msg.senderName << ": ";
            }

            ss << msg.message;
        }

        ImGui::TextWrapped("%s", ss.str().c_str());
        ImGui::PopStyleColor();
    }

    // Auto-scroll to bottom
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Chat input
    ImGui::Text("Type:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    const char* chatTypes[] = { "SAY", "YELL", "PARTY", "GUILD" };
    ImGui::Combo("##ChatType", &selectedChatType, chatTypes, 4);

    ImGui::SameLine();
    ImGui::Text("Message:");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(-1);
    if (refocusChatInput) {
        ImGui::SetKeyboardFocusHere();
        refocusChatInput = false;
    }
    if (ImGui::InputText("##ChatInput", chatInputBuffer, sizeof(chatInputBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        sendChatMessage(gameHandler);
        refocusChatInput = true;
    }

    if (ImGui::IsItemActive()) {
        chatInputActive = true;
    } else {
        chatInputActive = false;
    }

    ImGui::End();
}

void GameScreen::processTargetInput(game::GameHandler& gameHandler) {
    auto& io = ImGui::GetIO();
    auto& input = core::Input::getInstance();

    // Tab targeting (when keyboard not captured by UI)
    if (!io.WantCaptureKeyboard) {
        if (input.isKeyJustPressed(SDL_SCANCODE_TAB)) {
            const auto& movement = gameHandler.getMovementInfo();
            gameHandler.tabTarget(movement.x, movement.y, movement.z);
        }

        if (input.isKeyJustPressed(SDL_SCANCODE_ESCAPE)) {
            gameHandler.clearTarget();
        }
    }

    // Left-click targeting (when mouse not captured by UI)
    if (!io.WantCaptureMouse && input.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
        auto* renderer = core::Application::getInstance().getRenderer();
        auto* camera = renderer ? renderer->getCamera() : nullptr;
        auto* window = core::Application::getInstance().getWindow();

        if (camera && window) {
            glm::vec2 mousePos = input.getMousePosition();
            float screenW = static_cast<float>(window->getWidth());
            float screenH = static_cast<float>(window->getHeight());

            rendering::Ray ray = camera->screenToWorldRay(mousePos.x, mousePos.y, screenW, screenH);

            float closestT = 1e30f;
            uint64_t closestGuid = 0;

            for (const auto& [guid, entity] : gameHandler.getEntityManager().getEntities()) {
                auto t = entity->getType();
                if (t != game::ObjectType::UNIT && t != game::ObjectType::PLAYER) continue;

                glm::vec3 entityGL = wowToGL(entity->getX(), entity->getY(), entity->getZ());
                // Add half-height offset so we target the body center, not feet
                entityGL.z += 3.0f;

                float hitT;
                if (raySphereIntersect(ray, entityGL, 3.0f, hitT)) {
                    if (hitT < closestT) {
                        closestT = hitT;
                        closestGuid = guid;
                    }
                }
            }

            if (closestGuid != 0) {
                gameHandler.setTarget(closestGuid);
            }
            // Don't clear on miss — left-click is also used for camera orbit
        }
    }
}

void GameScreen::renderPlayerFrame(game::GameHandler& gameHandler) {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 30.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(250.0f, 0.0f), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

    if (ImGui::Begin("##PlayerFrame", nullptr, flags)) {
        // Use selected character info if available, otherwise defaults
        std::string playerName = "Adventurer";
        uint32_t playerLevel = 1;
        uint32_t playerHp = 100;
        uint32_t playerMaxHp = 100;

        const auto& characters = gameHandler.getCharacters();
        if (!characters.empty()) {
            // Use the first (or most recently selected) character
            const auto& ch = characters[0];
            playerName = ch.name;
            playerLevel = ch.level;
            // Characters don't store HP; use level-scaled estimate
            playerMaxHp = 20 + playerLevel * 10;
            playerHp = playerMaxHp;
        }

        // Name in green (friendly player color)
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", playerName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Lv %u", playerLevel);

        // Health bar
        float pct = static_cast<float>(playerHp) / static_cast<float>(playerMaxHp);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
        char overlay[64];
        snprintf(overlay, sizeof(overlay), "%u / %u", playerHp, playerMaxHp);
        ImGui::ProgressBar(pct, ImVec2(-1, 18), overlay);
        ImGui::PopStyleColor();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void GameScreen::renderTargetFrame(game::GameHandler& gameHandler) {
    auto target = gameHandler.getTarget();
    if (!target) return;

    auto* window = core::Application::getInstance().getWindow();
    float screenW = window ? static_cast<float>(window->getWidth()) : 1280.0f;

    float frameW = 250.0f;
    float frameX = (screenW - frameW) / 2.0f;

    ImGui::SetNextWindowPos(ImVec2(frameX, 30.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(frameW, 0.0f), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

    if (ImGui::Begin("##TargetFrame", nullptr, flags)) {
        // Entity name and type
        std::string name = getEntityName(target);

        ImVec4 nameColor;
        switch (target->getType()) {
            case game::ObjectType::PLAYER:
                nameColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);  // Green
                break;
            case game::ObjectType::UNIT:
                nameColor = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);  // Yellow
                break;
            default:
                nameColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                break;
        }

        ImGui::TextColored(nameColor, "%s", name.c_str());

        // Level (for units/players)
        if (target->getType() == game::ObjectType::UNIT || target->getType() == game::ObjectType::PLAYER) {
            auto unit = std::static_pointer_cast<game::Unit>(target);
            ImGui::SameLine();
            ImGui::TextDisabled("Lv %u", unit->getLevel());

            // Health bar
            uint32_t hp = unit->getHealth();
            uint32_t maxHp = unit->getMaxHealth();
            if (maxHp > 0) {
                float pct = static_cast<float>(hp) / static_cast<float>(maxHp);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                    pct > 0.5f ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                    pct > 0.2f ? ImVec4(0.8f, 0.8f, 0.2f, 1.0f) :
                                 ImVec4(0.8f, 0.2f, 0.2f, 1.0f));

                char overlay[64];
                snprintf(overlay, sizeof(overlay), "%u / %u", hp, maxHp);
                ImGui::ProgressBar(pct, ImVec2(-1, 18), overlay);
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("No health data");
            }
        }

        // Distance
        const auto& movement = gameHandler.getMovementInfo();
        float dx = target->getX() - movement.x;
        float dy = target->getY() - movement.y;
        float dz = target->getZ() - movement.z;
        float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        ImGui::TextDisabled("%.1f yd", distance);
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void GameScreen::sendChatMessage(game::GameHandler& gameHandler) {
    if (strlen(chatInputBuffer) > 0) {
        std::string input(chatInputBuffer);

        // Check for slash command emotes
        if (input.size() > 1 && input[0] == '/') {
            std::string command = input.substr(1);
            // Convert to lowercase
            for (char& c : command) c = std::tolower(c);

            std::string emoteText = rendering::Renderer::getEmoteText(command);
            if (!emoteText.empty()) {
                // Play the emote animation
                auto* renderer = core::Application::getInstance().getRenderer();
                if (renderer) {
                    renderer->playEmote(command);
                }

                // Build emote message — targeted or untargeted
                std::string chatText;
                if (gameHandler.hasTarget()) {
                    auto target = gameHandler.getTarget();
                    if (target) {
                        std::string targetName = getEntityName(target);
                        chatText = command + " at " + targetName + ".";
                    } else {
                        chatText = emoteText;
                    }
                } else {
                    chatText = command + ".";  // First person: "You wave."
                }

                // Add local chat message
                game::MessageChatData msg;
                msg.type = game::ChatType::TEXT_EMOTE;
                msg.language = game::ChatLanguage::COMMON;
                msg.message = chatText;
                gameHandler.addLocalChatMessage(msg);

                chatInputBuffer[0] = '\0';
                return;
            }
            // Not a recognized emote — fall through and send as normal chat
        }

        game::ChatType type;
        switch (selectedChatType) {
            case 0: type = game::ChatType::SAY; break;
            case 1: type = game::ChatType::YELL; break;
            case 2: type = game::ChatType::PARTY; break;
            case 3: type = game::ChatType::GUILD; break;
            default: type = game::ChatType::SAY; break;
        }

        gameHandler.sendChatMessage(type, chatInputBuffer);

        // Clear input
        chatInputBuffer[0] = '\0';
    }
}

const char* GameScreen::getChatTypeName(game::ChatType type) const {
    switch (type) {
        case game::ChatType::SAY: return "SAY";
        case game::ChatType::YELL: return "YELL";
        case game::ChatType::EMOTE: return "EMOTE";
        case game::ChatType::TEXT_EMOTE: return "EMOTE";
        case game::ChatType::PARTY: return "PARTY";
        case game::ChatType::GUILD: return "GUILD";
        case game::ChatType::OFFICER: return "OFFICER";
        case game::ChatType::RAID: return "RAID";
        case game::ChatType::RAID_LEADER: return "RAID LEADER";
        case game::ChatType::RAID_WARNING: return "RAID WARNING";
        case game::ChatType::WHISPER: return "WHISPER";
        case game::ChatType::WHISPER_INFORM: return "TO";
        case game::ChatType::SYSTEM: return "SYSTEM";
        case game::ChatType::CHANNEL: return "CHANNEL";
        case game::ChatType::ACHIEVEMENT: return "ACHIEVEMENT";
        default: return "UNKNOWN";
    }
}

ImVec4 GameScreen::getChatTypeColor(game::ChatType type) const {
    switch (type) {
        case game::ChatType::SAY:
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // White
        case game::ChatType::YELL:
            return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red
        case game::ChatType::EMOTE:
            return ImVec4(1.0f, 0.7f, 0.3f, 1.0f);  // Orange
        case game::ChatType::TEXT_EMOTE:
            return ImVec4(1.0f, 0.7f, 0.3f, 1.0f);  // Orange
        case game::ChatType::PARTY:
            return ImVec4(0.5f, 0.5f, 1.0f, 1.0f);  // Light blue
        case game::ChatType::GUILD:
            return ImVec4(0.3f, 1.0f, 0.3f, 1.0f);  // Green
        case game::ChatType::OFFICER:
            return ImVec4(0.3f, 0.8f, 0.3f, 1.0f);  // Dark green
        case game::ChatType::RAID:
            return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange
        case game::ChatType::WHISPER:
            return ImVec4(1.0f, 0.5f, 1.0f, 1.0f);  // Pink
        case game::ChatType::WHISPER_INFORM:
            return ImVec4(1.0f, 0.5f, 1.0f, 1.0f);  // Pink
        case game::ChatType::SYSTEM:
            return ImVec4(1.0f, 1.0f, 0.3f, 1.0f);  // Yellow
        case game::ChatType::CHANNEL:
            return ImVec4(1.0f, 0.7f, 0.7f, 1.0f);  // Light pink
        case game::ChatType::ACHIEVEMENT:
            return ImVec4(1.0f, 1.0f, 0.0f, 1.0f);  // Bright yellow
        default:
            return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Gray
    }
}

void GameScreen::updateCharacterGeosets(game::Inventory& inventory) {
    auto& app = core::Application::getInstance();
    auto* renderer = app.getRenderer();
    if (!renderer) return;

    uint32_t instanceId = renderer->getCharacterInstanceId();
    if (instanceId == 0) return;

    auto* charRenderer = renderer->getCharacterRenderer();
    if (!charRenderer) return;

    auto* assetManager = app.getAssetManager();

    // Load ItemDisplayInfo.dbc for geosetGroup lookup
    std::shared_ptr<pipeline::DBCFile> displayInfoDbc;
    if (assetManager) {
        displayInfoDbc = assetManager->loadDBC("ItemDisplayInfo.dbc");
    }

    // Helper: get geosetGroup field for an equipped item's displayInfoId
    // DBC binary fields: 7=geosetGroup_1, 8=geosetGroup_2, 9=geosetGroup_3
    auto getGeosetGroup = [&](uint32_t displayInfoId, int groupField) -> uint32_t {
        if (!displayInfoDbc || displayInfoId == 0) return 0;
        int32_t recIdx = displayInfoDbc->findRecordById(displayInfoId);
        if (recIdx < 0) return 0;
        return displayInfoDbc->getUInt32(static_cast<uint32_t>(recIdx), 7 + groupField);
    };

    // Helper: find first equipped item matching inventoryType, return its displayInfoId
    auto findEquippedDisplayId = [&](std::initializer_list<uint8_t> types) -> uint32_t {
        for (int s = 0; s < game::Inventory::NUM_EQUIP_SLOTS; s++) {
            const auto& slot = inventory.getEquipSlot(static_cast<game::EquipSlot>(s));
            if (!slot.empty()) {
                for (uint8_t t : types) {
                    if (slot.item.inventoryType == t)
                        return slot.item.displayInfoId;
                }
            }
        }
        return 0;
    };

    // Helper: check if any equipment slot has the given inventoryType
    auto hasEquippedType = [&](std::initializer_list<uint8_t> types) -> bool {
        for (int s = 0; s < game::Inventory::NUM_EQUIP_SLOTS; s++) {
            const auto& slot = inventory.getEquipSlot(static_cast<game::EquipSlot>(s));
            if (!slot.empty()) {
                for (uint8_t t : types) {
                    if (slot.item.inventoryType == t) return true;
                }
            }
        }
        return false;
    };

    // Base geosets always present
    std::unordered_set<uint16_t> geosets;
    for (uint16_t i = 0; i <= 18; i++) {
        geosets.insert(i);
    }
    geosets.insert(101);  // Hair
    geosets.insert(201);  // Facial
    geosets.insert(701);  // Ears

    // Chest/Shirt: inventoryType 4 (shirt), 5 (chest), 20 (robe)
    // geosetGroup_1 > 0 → use mesh variant (502+), otherwise bare (501) + texture only
    {
        uint32_t did = findEquippedDisplayId({4, 5, 20});
        uint32_t gg = getGeosetGroup(did, 0);
        geosets.insert(static_cast<uint16_t>(gg > 0 ? 501 + gg : (did > 0 ? 501 : 501)));
        // geosetGroup_3 > 0 on robes also shows kilt legs (1302)
        uint32_t gg3 = getGeosetGroup(did, 2);
        if (gg3 > 0) {
            geosets.insert(static_cast<uint16_t>(1301 + gg3));
        }
    }

    // Legs: inventoryType 7
    // geosetGroup_1 > 0 → kilt/skirt mesh (1302+), otherwise bare legs (1301) + texture
    {
        uint32_t did = findEquippedDisplayId({7});
        uint32_t gg = getGeosetGroup(did, 0);
        // Only add leg geoset if robe hasn't already set a kilt geoset
        if (geosets.count(1302) == 0 && geosets.count(1303) == 0) {
            geosets.insert(static_cast<uint16_t>(gg > 0 ? 1301 + gg : 1301));
        }
    }

    // Feet/Boots: inventoryType 8
    // geosetGroup_1 > 0 → boot mesh (402+), otherwise bare feet (401) + texture
    {
        uint32_t did = findEquippedDisplayId({8});
        uint32_t gg = getGeosetGroup(did, 0);
        geosets.insert(static_cast<uint16_t>(gg > 0 ? 401 + gg : 401));
    }

    // Gloves/Hands: inventoryType 10
    // geosetGroup_1 > 0 → glove mesh (302+), otherwise bare hands (301)
    {
        uint32_t did = findEquippedDisplayId({10});
        uint32_t gg = getGeosetGroup(did, 0);
        geosets.insert(static_cast<uint16_t>(gg > 0 ? 301 + gg : 301));
    }

    // Back/Cloak: inventoryType 16 — geoset only, no skin texture (cloaks are separate models)
    geosets.insert(hasEquippedType({16}) ? 1502 : 1501);

    // Tabard: inventoryType 19
    if (hasEquippedType({19})) {
        geosets.insert(1201);
    }

    charRenderer->setActiveGeosets(instanceId, geosets);
}

void GameScreen::updateCharacterTextures(game::Inventory& inventory) {
    auto& app = core::Application::getInstance();
    auto* renderer = app.getRenderer();
    if (!renderer) return;

    auto* charRenderer = renderer->getCharacterRenderer();
    if (!charRenderer) return;

    auto* assetManager = app.getAssetManager();
    if (!assetManager) return;

    const auto& bodySkinPath = app.getBodySkinPath();
    const auto& underwearPaths = app.getUnderwearPaths();
    uint32_t skinSlot = app.getSkinTextureSlotIndex();

    if (bodySkinPath.empty()) return;

    // Component directory names indexed by region
    static const char* componentDirs[] = {
        "ArmUpperTexture",   // 0
        "ArmLowerTexture",   // 1
        "HandTexture",       // 2
        "TorsoUpperTexture", // 3
        "TorsoLowerTexture", // 4
        "LegUpperTexture",   // 5
        "LegLowerTexture",   // 6
        "FootTexture",       // 7
    };

    // Load ItemDisplayInfo.dbc
    auto displayInfoDbc = assetManager->loadDBC("ItemDisplayInfo.dbc");
    if (!displayInfoDbc) return;

    // Collect equipment texture regions from all equipped items
    std::vector<std::pair<int, std::string>> regionLayers;

    for (int s = 0; s < game::Inventory::NUM_EQUIP_SLOTS; s++) {
        const auto& slot = inventory.getEquipSlot(static_cast<game::EquipSlot>(s));
        if (slot.empty() || slot.item.displayInfoId == 0) continue;

        int32_t recIdx = displayInfoDbc->findRecordById(slot.item.displayInfoId);
        if (recIdx < 0) continue;

        // DBC fields 15-22 = texture_1 through texture_8 (regions 0-7)
        // (binary DBC has inventoryIcon_2 at field 6, shifting fields +1 vs CSV)
        for (int region = 0; region < 8; region++) {
            uint32_t fieldIdx = 15 + region;
            std::string texName = displayInfoDbc->getString(static_cast<uint32_t>(recIdx), fieldIdx);
            if (texName.empty()) continue;

            // Actual MPQ files have a gender suffix: _M (male), _F (female), _U (unisex)
            // Try gender-specific first, then unisex fallback
            std::string base = "Item\\TextureComponents\\" +
                std::string(componentDirs[region]) + "\\" + texName;
            std::string malePath = base + "_M.blp";
            std::string unisexPath = base + "_U.blp";
            std::string fullPath;
            if (assetManager->fileExists(malePath)) {
                fullPath = malePath;
            } else if (assetManager->fileExists(unisexPath)) {
                fullPath = unisexPath;
            } else {
                // Last resort: try without suffix
                fullPath = base + ".blp";
            }
            regionLayers.emplace_back(region, fullPath);
        }
    }

    // Re-composite: base skin + underwear + equipment regions
    GLuint newTex = charRenderer->compositeWithRegions(bodySkinPath, underwearPaths, regionLayers);
    if (newTex != 0) {
        charRenderer->setModelTexture(1, skinSlot, newTex);
    }

    // Cloak cape texture — separate from skin atlas, uses texture slot type-2 (Object Skin)
    uint32_t cloakSlot = app.getCloakTextureSlotIndex();
    if (cloakSlot > 0) {
        // Find equipped cloak (inventoryType 16)
        uint32_t cloakDisplayId = 0;
        for (int s = 0; s < game::Inventory::NUM_EQUIP_SLOTS; s++) {
            const auto& slot = inventory.getEquipSlot(static_cast<game::EquipSlot>(s));
            if (!slot.empty() && slot.item.inventoryType == 16 && slot.item.displayInfoId != 0) {
                cloakDisplayId = slot.item.displayInfoId;
                break;
            }
        }

        if (cloakDisplayId > 0) {
            int32_t recIdx = displayInfoDbc->findRecordById(cloakDisplayId);
            if (recIdx >= 0) {
                // DBC field 3 = modelTexture_1 (cape texture name)
                std::string capeName = displayInfoDbc->getString(static_cast<uint32_t>(recIdx), 3);
                if (!capeName.empty()) {
                    std::string capePath = "Item\\ObjectComponents\\Cape\\" + capeName + ".blp";
                    GLuint capeTex = charRenderer->loadTexture(capePath);
                    if (capeTex != 0) {
                        charRenderer->setModelTexture(1, cloakSlot, capeTex);
                        LOG_INFO("Cloak texture applied: ", capePath);
                    }
                }
            }
        } else {
            // No cloak equipped — reset to white fallback
            charRenderer->resetModelTexture(1, cloakSlot);
        }
    }
}

}} // namespace wowee::ui
