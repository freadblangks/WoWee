#include "core/application.hpp"
#include "core/logger.hpp"
#include <exception>
#include <csignal>
#include <SDL2/SDL.h>
#include <X11/Xlib.h>

static void releaseMouseGrab() {
    // Bypass SDL — talk to X11 directly (signal-safe enough for our purposes)
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy) {
        XUngrabPointer(dpy, CurrentTime);
        XUngrabKeyboard(dpy, CurrentTime);
        XFlush(dpy);
        XCloseDisplay(dpy);
    }
}

static void crashHandler(int sig) {
    releaseMouseGrab();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGFPE,  crashHandler);
    std::signal(SIGTERM, crashHandler);
    std::signal(SIGINT,  crashHandler);
    try {
        wowee::core::Logger::getInstance().setLogLevel(wowee::core::LogLevel::DEBUG);
        LOG_INFO("=== Wowee Native Client ===");
        LOG_INFO("Starting application...");

        wowee::core::Application app;

        if (!app.initialize()) {
            LOG_FATAL("Failed to initialize application");
            return 1;
        }

        app.run();
        app.shutdown();

        LOG_INFO("Application exited successfully");
        return 0;
    }
    catch (const std::exception& e) {
        releaseMouseGrab();
        LOG_FATAL("Unhandled exception: ", e.what());
        return 1;
    }
    catch (...) {
        releaseMouseGrab();
        LOG_FATAL("Unknown exception occurred");
        return 1;
    }
}
