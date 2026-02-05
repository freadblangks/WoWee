#include "ui/character_create_screen.hpp"
#include "rendering/character_preview.hpp"
#include "game/game_handler.hpp"
#include <imgui.h>
#include <cstring>

namespace wowee {
namespace ui {

static const game::Race allRaces[] = {
    // Alliance
    game::Race::HUMAN, game::Race::DWARF, game::Race::NIGHT_ELF,
    game::Race::GNOME, game::Race::DRAENEI,
    // Horde
    game::Race::ORC, game::Race::UNDEAD, game::Race::TAUREN,
    game::Race::TROLL, game::Race::BLOOD_ELF,
};
static constexpr int RACE_COUNT = 10;
static constexpr int ALLIANCE_COUNT = 5;

static const game::Class allClasses[] = {
    game::Class::WARRIOR, game::Class::PALADIN, game::Class::HUNTER,
    game::Class::ROGUE, game::Class::PRIEST, game::Class::DEATH_KNIGHT,
    game::Class::SHAMAN, game::Class::MAGE, game::Class::WARLOCK,
    game::Class::DRUID,
};

CharacterCreateScreen::CharacterCreateScreen() {
    reset();
}

CharacterCreateScreen::~CharacterCreateScreen() = default;

void CharacterCreateScreen::reset() {
    std::memset(nameBuffer, 0, sizeof(nameBuffer));
    raceIndex = 0;
    classIndex = 0;
    genderIndex = 0;
    skin = 0;
    face = 0;
    hairStyle = 0;
    hairColor = 0;
    facialHair = 0;
    statusMessage.clear();
    statusIsError = false;
    updateAvailableClasses();

    // Reset preview tracking to force model reload on next render
    prevRaceIndex_ = -1;
    prevGenderIndex_ = -1;
    prevSkin_ = -1;
    prevFace_ = -1;
    prevHairStyle_ = -1;
    prevHairColor_ = -1;
    prevFacialHair_ = -1;
}

void CharacterCreateScreen::initializePreview(pipeline::AssetManager* am) {
    if (!preview_) {
        preview_ = std::make_unique<rendering::CharacterPreview>();
        preview_->initialize(am);
    }
    // Force model reload
    prevRaceIndex_ = -1;
}

void CharacterCreateScreen::update(float deltaTime) {
    if (preview_) {
        preview_->update(deltaTime);
    }
}

void CharacterCreateScreen::setStatus(const std::string& msg, bool isError) {
    statusMessage = msg;
    statusIsError = isError;
}

void CharacterCreateScreen::updateAvailableClasses() {
    availableClasses.clear();
    game::Race race = allRaces[raceIndex];
    for (auto cls : allClasses) {
        if (game::isValidRaceClassCombo(race, cls)) {
            availableClasses.push_back(cls);
        }
    }
    // Clamp class index
    if (classIndex >= static_cast<int>(availableClasses.size())) {
        classIndex = 0;
    }
}

void CharacterCreateScreen::updatePreviewIfNeeded() {
    if (!preview_) return;

    bool changed = (raceIndex != prevRaceIndex_ ||
                    genderIndex != prevGenderIndex_ ||
                    skin != prevSkin_ ||
                    face != prevFace_ ||
                    hairStyle != prevHairStyle_ ||
                    hairColor != prevHairColor_ ||
                    facialHair != prevFacialHair_);

    if (changed) {
        preview_->loadCharacter(
            allRaces[raceIndex],
            static_cast<game::Gender>(genderIndex),
            static_cast<uint8_t>(skin),
            static_cast<uint8_t>(face),
            static_cast<uint8_t>(hairStyle),
            static_cast<uint8_t>(hairColor),
            static_cast<uint8_t>(facialHair));

        prevRaceIndex_ = raceIndex;
        prevGenderIndex_ = genderIndex;
        prevSkin_ = skin;
        prevFace_ = face;
        prevHairStyle_ = hairStyle;
        prevHairColor_ = hairColor;
        prevFacialHair_ = facialHair;
    }
}

void CharacterCreateScreen::render(game::GameHandler& /*gameHandler*/) {
    // Render the preview to FBO before the ImGui frame
    if (preview_) {
        updatePreviewIfNeeded();
        preview_->render();
    }

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    bool hasPreview = (preview_ && preview_->getTextureId() != 0);
    float previewWidth = hasPreview ? 320.0f : 0.0f;
    float controlsWidth = 540.0f;
    float totalWidth = hasPreview ? (previewWidth + controlsWidth + 16.0f) : 600.0f;
    float totalHeight = 580.0f;

    ImGui::SetNextWindowSize(ImVec2(totalWidth, totalHeight), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - totalWidth) * 0.5f,
                                    (displaySize.y - totalHeight) * 0.5f),
                            ImGuiCond_Always);

    ImGui::Begin("Create Character", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    if (hasPreview) {
        // Left panel: 3D preview
        ImGui::BeginChild("##preview_panel", ImVec2(previewWidth, -40.0f), false);
        {
            // Display the FBO texture (flip Y for OpenGL)
            float imgW = previewWidth - 8.0f;
            float imgH = imgW * (static_cast<float>(preview_->getHeight()) /
                                  static_cast<float>(preview_->getWidth()));
            if (imgH > totalHeight - 80.0f) {
                imgH = totalHeight - 80.0f;
                imgW = imgH * (static_cast<float>(preview_->getWidth()) /
                               static_cast<float>(preview_->getHeight()));
            }

            ImGui::Image(
                static_cast<ImTextureID>(preview_->getTextureId()),
                ImVec2(imgW, imgH),
                ImVec2(0.0f, 1.0f),  // UV top-left (flipped Y)
                ImVec2(1.0f, 0.0f)); // UV bottom-right (flipped Y)

            // Mouse drag rotation on the preview image
            if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                float deltaX = ImGui::GetIO().MouseDelta.x;
                preview_->rotate(deltaX * 0.5f);
            }

            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Drag to rotate");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right panel: controls
        ImGui::BeginChild("##controls_panel", ImVec2(0, -40.0f), false);
    }

    // Name input
    ImGui::Text("Name:");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer));

    ImGui::Spacing();

    // Race selection
    ImGui::Text("Race:");
    ImGui::Spacing();
    ImGui::Indent(10.0f);
    ImGui::TextColored(ImVec4(0.3f, 0.5f, 1.0f, 1.0f), "Alliance:");
    ImGui::SameLine();
    for (int i = 0; i < ALLIANCE_COUNT; ++i) {
        if (i > 0) ImGui::SameLine();
        bool selected = (raceIndex == i);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 1.0f, 0.8f));
        if (ImGui::SmallButton(game::getRaceName(allRaces[i]))) {
            if (raceIndex != i) {
                raceIndex = i;
                classIndex = 0;
                skin = face = hairStyle = hairColor = facialHair = 0;
                updateAvailableClasses();
            }
        }
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Horde:");
    ImGui::SameLine();
    for (int i = ALLIANCE_COUNT; i < RACE_COUNT; ++i) {
        if (i > ALLIANCE_COUNT) ImGui::SameLine();
        bool selected = (raceIndex == i);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.3f, 0.3f, 0.8f));
        if (ImGui::SmallButton(game::getRaceName(allRaces[i]))) {
            if (raceIndex != i) {
                raceIndex = i;
                classIndex = 0;
                skin = face = hairStyle = hairColor = facialHair = 0;
                updateAvailableClasses();
            }
        }
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::Unindent(10.0f);

    ImGui::Spacing();

    // Class selection
    ImGui::Text("Class:");
    ImGui::SameLine(80);
    if (!availableClasses.empty()) {
        ImGui::BeginGroup();
        for (int i = 0; i < static_cast<int>(availableClasses.size()); ++i) {
            if (i > 0) ImGui::SameLine();
            bool selected = (classIndex == i);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 0.8f));
            if (ImGui::SmallButton(game::getClassName(availableClasses[i]))) {
                classIndex = i;
            }
            if (selected) ImGui::PopStyleColor();
        }
        ImGui::EndGroup();
    }

    ImGui::Spacing();

    // Gender
    ImGui::Text("Gender:");
    ImGui::SameLine(80);
    ImGui::RadioButton("Male", &genderIndex, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Female", &genderIndex, 1);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Appearance sliders
    game::Race currentRace = allRaces[raceIndex];
    game::Gender currentGender = static_cast<game::Gender>(genderIndex);

    ImGui::Text("Appearance");
    ImGui::Spacing();

    float sliderWidth = hasPreview ? 180.0f : 200.0f;
    float labelCol = hasPreview ? 100.0f : 120.0f;

    auto slider = [&](const char* label, int* val, int maxVal) {
        ImGui::Text("%s", label);
        ImGui::SameLine(labelCol);
        ImGui::SetNextItemWidth(sliderWidth);
        char id[32];
        snprintf(id, sizeof(id), "##%s", label);
        ImGui::SliderInt(id, val, 0, maxVal);
    };

    slider("Skin",           &skin,      game::getMaxSkin(currentRace, currentGender));
    slider("Face",           &face,      game::getMaxFace(currentRace, currentGender));
    slider("Hair Style",     &hairStyle, game::getMaxHairStyle(currentRace, currentGender));
    slider("Hair Color",     &hairColor, game::getMaxHairColor(currentRace, currentGender));
    slider("Facial Feature", &facialHair, game::getMaxFacialFeature(currentRace, currentGender));

    ImGui::Spacing();

    // Status message
    if (!statusMessage.empty()) {
        ImGui::Separator();
        ImGui::Spacing();
        ImVec4 color = statusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%s", statusMessage.c_str());
    }

    if (hasPreview) {
        ImGui::EndChild(); // controls_panel
    }

    // Bottom buttons (outside children)
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Create", ImVec2(150, 35))) {
        std::string name(nameBuffer);
        if (name.empty()) {
            setStatus("Please enter a character name.", true);
        } else if (availableClasses.empty()) {
            setStatus("No valid class for this race.", true);
        } else {
            game::CharCreateData data;
            data.name = name;
            data.race = allRaces[raceIndex];
            data.characterClass = availableClasses[classIndex];
            data.gender = currentGender;
            data.skin = static_cast<uint8_t>(skin);
            data.face = static_cast<uint8_t>(face);
            data.hairStyle = static_cast<uint8_t>(hairStyle);
            data.hairColor = static_cast<uint8_t>(hairColor);
            data.facialHair = static_cast<uint8_t>(facialHair);
            if (onCreate) {
                onCreate(data);
            }
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Back", ImVec2(150, 35))) {
        if (onCancel) {
            onCancel();
        }
    }

    ImGui::End();
}

} // namespace ui
} // namespace wowee
