#include "ui/auth_screen.hpp"
#include <imgui.h>
#include <sstream>

namespace wowee { namespace ui {

AuthScreen::AuthScreen() {
}

void AuthScreen::render(auth::AuthHandler& authHandler) {
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("WoW 3.3.5a Authentication", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Connect to Authentication Server");
    ImGui::Separator();
    ImGui::Spacing();

    // Server settings
    ImGui::Text("Server Settings");
    ImGui::InputText("Hostname", hostname, sizeof(hostname));
    ImGui::InputInt("Port", &port);
    if (port < 1) port = 1;
    if (port > 65535) port = 65535;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Credentials
    ImGui::Text("Credentials");
    ImGui::InputText("Username", username, sizeof(username));

    // Password with visibility toggle
    ImGuiInputTextFlags passwordFlags = showPassword ? 0 : ImGuiInputTextFlags_Password;
    ImGui::InputText("Password", password, sizeof(password), passwordFlags);
    ImGui::SameLine();
    if (ImGui::Checkbox("Show", &showPassword)) {
        // Checkbox state changed
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Connection status
    if (!statusMessage.empty()) {
        if (statusIsError) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        }
        ImGui::TextWrapped("%s", statusMessage.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Connect button
    if (authenticating) {
        authTimer += ImGui::GetIO().DeltaTime;

        // Show progress with elapsed time
        char progressBuf[128];
        snprintf(progressBuf, sizeof(progressBuf), "Authenticating... (%.0fs)", authTimer);
        ImGui::Text("%s", progressBuf);

        // Check authentication status
        auto state = authHandler.getState();
        if (state == auth::AuthState::AUTHENTICATED) {
            setStatus("Authentication successful!", false);
            authenticating = false;

            // Call success callback
            if (onSuccess) {
                onSuccess();
            }
        } else if (state == auth::AuthState::FAILED) {
            if (!failureReason.empty()) {
                setStatus(failureReason, true);
            } else {
                setStatus("Authentication failed", true);
            }
            authenticating = false;
        } else if (authTimer >= AUTH_TIMEOUT) {
            setStatus("Connection timed out - server did not respond", true);
            authenticating = false;
            authHandler.disconnect();
        }
    } else {
        if (ImGui::Button("Connect", ImVec2(120, 0))) {
            attemptAuth(authHandler);
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear", ImVec2(120, 0))) {
            statusMessage.clear();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Single-player mode button
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Single-Player Mode");
    ImGui::TextWrapped("Skip server connection and play offline with local rendering.");

    if (ImGui::Button("Start Single Player", ImVec2(240, 30))) {
        // Call single-player callback
        if (onSinglePlayer) {
            onSinglePlayer();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Info text
    ImGui::TextWrapped("Enter your account credentials to connect to the authentication server.");
    ImGui::TextWrapped("Default port is 3724.");

    ImGui::End();
}

void AuthScreen::attemptAuth(auth::AuthHandler& authHandler) {
    // Validate inputs
    if (strlen(username) == 0) {
        setStatus("Username cannot be empty", true);
        return;
    }

    if (strlen(password) == 0) {
        setStatus("Password cannot be empty", true);
        return;
    }

    if (strlen(hostname) == 0) {
        setStatus("Hostname cannot be empty", true);
        return;
    }

    // Attempt connection
    std::stringstream ss;
    ss << "Connecting to " << hostname << ":" << port << "...";
    setStatus(ss.str(), false);

    // Wire up failure callback to capture specific error reason
    failureReason.clear();
    authHandler.setOnFailure([this](const std::string& reason) {
        failureReason = reason;
    });

    if (authHandler.connect(hostname, static_cast<uint16_t>(port))) {
        authenticating = true;
        authTimer = 0.0f;
        setStatus("Connected, authenticating...", false);

        // Send authentication credentials
        authHandler.authenticate(username, password);
    } else {
        std::stringstream errSs;
        errSs << "Failed to connect to " << hostname << ":" << port
              << " - check that the server is online and the address is correct";
        setStatus(errSs.str(), true);
    }
}

void AuthScreen::setStatus(const std::string& message, bool isError) {
    statusMessage = message;
    statusIsError = isError;
}

}} // namespace wowee::ui
