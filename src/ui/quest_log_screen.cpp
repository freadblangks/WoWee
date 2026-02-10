#include "ui/quest_log_screen.hpp"
#include "core/application.hpp"
#include "core/input.hpp"
#include <imgui.h>

namespace wowee { namespace ui {

namespace {
// Helper function to replace gender placeholders, pronouns, and name
std::string replaceGenderPlaceholders(const std::string& text, game::GameHandler& gameHandler) {
    game::Gender gender = game::Gender::NONBINARY;
    std::string playerName = "Adventurer";
    const auto* character = gameHandler.getActiveCharacter();
    if (character) {
        gender = character->gender;
        if (!character->name.empty()) {
            playerName = character->name;
        }
    }
    game::Pronouns pronouns = game::Pronouns::forGender(gender);

    std::string result = text;

    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\n\r"));
        s.erase(s.find_last_not_of(" \t\n\r") + 1);
    };

    // Replace simple placeholders
    size_t pos = 0;
    while ((pos = result.find('$', pos)) != std::string::npos) {
        if (pos + 1 >= result.length()) break;

        char code = result[pos + 1];
        std::string replacement;

        switch (code) {
            case 'n': replacement = playerName; break;
            case 'p': replacement = pronouns.subject; break;
            case 'o': replacement = pronouns.object; break;
            case 's': replacement = pronouns.possessive; break;
            case 'S': replacement = pronouns.possessiveP; break;
            case 'g': pos++; continue;
            default: pos++; continue;
        }

        result.replace(pos, 2, replacement);
        pos += replacement.length();
    }

    // Replace $g placeholders
    pos = 0;
    while ((pos = result.find("$g", pos)) != std::string::npos) {
        size_t endPos = result.find(';', pos);
        if (endPos == std::string::npos) break;

        std::string placeholder = result.substr(pos + 2, endPos - pos - 2);

        std::vector<std::string> parts;
        size_t start = 0;
        size_t colonPos;
        while ((colonPos = placeholder.find(':', start)) != std::string::npos) {
            std::string part = placeholder.substr(start, colonPos - start);
            trim(part);
            parts.push_back(part);
            start = colonPos + 1;
        }
        std::string lastPart = placeholder.substr(start);
        trim(lastPart);
        parts.push_back(lastPart);

        std::string replacement;
        if (parts.size() >= 3) {
            switch (gender) {
                case game::Gender::MALE: replacement = parts[0]; break;
                case game::Gender::FEMALE: replacement = parts[1]; break;
                case game::Gender::NONBINARY: replacement = parts[2]; break;
            }
        } else if (parts.size() >= 2) {
            switch (gender) {
                case game::Gender::MALE: replacement = parts[0]; break;
                case game::Gender::FEMALE: replacement = parts[1]; break;
                case game::Gender::NONBINARY:
                    replacement = parts[0].length() <= parts[1].length() ? parts[0] : parts[1];
                    break;
            }
        } else {
            pos = endPos + 1;
            continue;
        }

        result.replace(pos, endPos - pos + 1, replacement);
        pos += replacement.length();
    }

    return result;
}
} // anonymous namespace

void QuestLogScreen::render(game::GameHandler& gameHandler) {
    // L key toggle (edge-triggered)
    bool uiWantsKeyboard = ImGui::GetIO().WantCaptureKeyboard;
    bool lDown = !uiWantsKeyboard && core::Input::getInstance().isKeyPressed(SDL_SCANCODE_L);
    if (lDown && !lKeyWasDown) {
        open = !open;
    }
    lKeyWasDown = lDown;

    if (!open) return;

    auto* window = core::Application::getInstance().getWindow();
    float screenW = window ? static_cast<float>(window->getWidth()) : 1280.0f;
    float screenH = window ? static_cast<float>(window->getHeight()) : 720.0f;

    float logW = 380.0f;
    float logH = std::min(450.0f, screenH - 120.0f);
    float logX = (screenW - logW) * 0.5f;
    float logY = 80.0f;

    ImGui::SetNextWindowPos(ImVec2(logX, logY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(logW, logH), ImGuiCond_FirstUseEver);

    bool stillOpen = true;
    if (ImGui::Begin("Quest Log", &stillOpen)) {
        const auto& quests = gameHandler.getQuestLog();

        if (quests.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No active quests.");
        } else {
            // Left panel: quest list
            ImGui::BeginChild("QuestList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), true);
            for (size_t i = 0; i < quests.size(); i++) {
                const auto& q = quests[i];
                ImGui::PushID(static_cast<int>(i));

                ImVec4 color = q.complete
                    ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f)   // Green for complete
                    : ImVec4(1.0f, 0.82f, 0.0f, 1.0f);  // Gold for active

                bool selected = (selectedIndex == static_cast<int>(i));
                if (ImGui::Selectable("##quest", selected, 0, ImVec2(0, 20))) {
                    selectedIndex = static_cast<int>(i);
                }
                ImGui::SameLine();
                ImGui::TextColored(color, "%s%s",
                    q.title.c_str(),
                    q.complete ? " (Complete)" : "");

                ImGui::PopID();
            }
            ImGui::EndChild();

            // Details panel for selected quest
            if (selectedIndex >= 0 && selectedIndex < static_cast<int>(quests.size())) {
                const auto& sel = quests[static_cast<size_t>(selectedIndex)];

                if (!sel.objectives.empty()) {
                    ImGui::Separator();
                    std::string processedObjectives = replaceGenderPlaceholders(sel.objectives, gameHandler);
                    ImGui::TextWrapped("%s", processedObjectives.c_str());
                }

                // Abandon button
                if (!sel.complete) {
                    ImGui::Separator();
                    if (ImGui::Button("Abandon Quest")) {
                        gameHandler.abandonQuest(sel.questId);
                        if (selectedIndex >= static_cast<int>(quests.size())) {
                            selectedIndex = static_cast<int>(quests.size()) - 1;
                        }
                    }
                }
            }
        }
    }
    ImGui::End();

    if (!stillOpen) {
        open = false;
    }
}

}} // namespace wowee::ui
