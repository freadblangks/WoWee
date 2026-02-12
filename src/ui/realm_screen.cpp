#include "ui/realm_screen.hpp"
#include <imgui.h>

namespace wowee { namespace ui {

RealmScreen::RealmScreen() {
}

void RealmScreen::render(auth::AuthHandler& authHandler) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pad(24.0f, 24.0f);
    ImVec2 winSize(vp->Size.x - pad.x * 2.0f, vp->Size.y - pad.y * 2.0f);
    if (winSize.x < 720.0f) winSize.x = 720.0f;
    if (winSize.y < 540.0f) winSize.y = 540.0f;

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x - winSize.x) * 0.5f,
                                   vp->Pos.y + (vp->Size.y - winSize.y) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
    ImGui::Begin("Realm Selection", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::Text("Select a Realm");
    ImGui::Separator();
    ImGui::Spacing();

    // Status message
    if (!statusMessage.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", statusMessage.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Get realm list
    const auto& realms = authHandler.getRealms();

    if (realms.empty()) {
        ImGui::Text("No realms available. Requesting realm list...");
        authHandler.requestRealmList();
    } else if (realms.size() == 1 && !realmSelected && !realms[0].lock) {
        // Auto-select the only available realm
        selectedRealmIndex = 0;
        realmSelected = true;
        selectedRealmName = realms[0].name;
        selectedRealmAddress = realms[0].address;
        setStatus("Auto-selecting realm: " + realms[0].name);
        if (onRealmSelected) {
            onRealmSelected(selectedRealmName, selectedRealmAddress);
        }
    } else {
        // Realm table
        if (ImGui::BeginTable("RealmsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Population", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Characters", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < realms.size(); ++i) {
                const auto& realm = realms[i];

                ImGui::TableNextRow();

                // Name column (selectable)
                ImGui::TableSetColumnIndex(0);
                bool isSelected = (selectedRealmIndex == static_cast<int>(i));
                if (ImGui::Selectable(realm.name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selectedRealmIndex = static_cast<int>(i);
                }

                // Type column
                ImGui::TableSetColumnIndex(1);
                if (realm.icon == 0) {
                    ImGui::Text("Normal");
                } else if (realm.icon == 1) {
                    ImGui::Text("PvP");
                } else if (realm.icon == 4) {
                    ImGui::Text("RP");
                } else if (realm.icon == 6) {
                    ImGui::Text("RP-PvP");
                } else {
                    ImGui::Text("Type %d", realm.icon);
                }

                // Population column
                ImGui::TableSetColumnIndex(2);
                ImVec4 popColor = getPopulationColor(realm.population);
                ImGui::PushStyleColor(ImGuiCol_Text, popColor);
                if (realm.population < 0.5f) {
                    ImGui::Text("Low");
                } else if (realm.population < 1.0f) {
                    ImGui::Text("Medium");
                } else if (realm.population < 2.0f) {
                    ImGui::Text("High");
                } else {
                    ImGui::Text("Full");
                }
                ImGui::PopStyleColor();

                // Characters column
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", realm.characters);

                // Status column
                ImGui::TableSetColumnIndex(4);
                const char* status = getRealmStatus(realm.flags);
                if (realm.lock) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    ImGui::Text("Locked");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                    ImGui::Text("%s", status);
                    ImGui::PopStyleColor();
                }
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Selected realm info
        if (selectedRealmIndex >= 0 && selectedRealmIndex < static_cast<int>(realms.size())) {
            const auto& realm = realms[selectedRealmIndex];

            ImGui::Text("Selected Realm:");
            ImGui::Indent();
            ImGui::Text("Name: %s", realm.name.c_str());
            ImGui::Text("Address: %s", realm.address.c_str());
            ImGui::Text("Characters: %d", realm.characters);
            if (realm.hasVersionInfo()) {
                ImGui::Text("Version: %d.%d.%d (build %d)",
                    realm.majorVersion, realm.minorVersion, realm.patchVersion, realm.build);
            }
            ImGui::Unindent();

            ImGui::Spacing();

            // Connect button
            if (!realm.lock) {
                if (ImGui::Button("Enter Realm", ImVec2(120, 0))) {
                    realmSelected = true;
                    selectedRealmName = realm.name;
                    selectedRealmAddress = realm.address;
                    setStatus("Connecting to realm: " + realm.name);

                    // Call callback
                    if (onRealmSelected) {
                        onRealmSelected(selectedRealmName, selectedRealmAddress);
                    }
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::Button("Realm Locked", ImVec2(120, 0));
                ImGui::PopStyleColor();
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Refresh button
    if (ImGui::Button("Refresh Realm List", ImVec2(150, 0))) {
        authHandler.requestRealmList();
        setStatus("Refreshing realm list...");
    }

    ImGui::End();
}

void RealmScreen::setStatus(const std::string& message) {
    statusMessage = message;
}

const char* RealmScreen::getRealmStatus(uint8_t flags) const {
    if (flags & 0x01) return "Invalid";
    if (flags & 0x02) return "Offline";
    return "Online";
}

ImVec4 RealmScreen::getPopulationColor(float population) const {
    if (population < 0.5f) {
        return ImVec4(0.3f, 1.0f, 0.3f, 1.0f);  // Green - Low
    } else if (population < 1.0f) {
        return ImVec4(1.0f, 1.0f, 0.3f, 1.0f);  // Yellow - Medium
    } else if (population < 2.0f) {
        return ImVec4(1.0f, 0.6f, 0.0f, 1.0f);  // Orange - High
    } else {
        return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red - Full
    }
}

}} // namespace wowee::ui
