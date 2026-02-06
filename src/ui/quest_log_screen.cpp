#include "ui/quest_log_screen.hpp"
#include "core/application.hpp"
#include "core/input.hpp"
#include <imgui.h>

namespace wowee { namespace ui {

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
                    ImGui::TextWrapped("%s", sel.objectives.c_str());
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
