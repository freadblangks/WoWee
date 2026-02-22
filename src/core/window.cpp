#include "core/window.hpp"
#include "core/logger.hpp"
#include "rendering/vk_context.hpp"
#include <SDL2/SDL_vulkan.h>

namespace wowee {
namespace core {

Window::Window(const WindowConfig& config)
    : config(config)
    , width(config.width)
    , height(config.height)
    , windowedWidth(config.width)
    , windowedHeight(config.height)
    , fullscreen(config.fullscreen)
    , vsync(config.vsync) {
}

Window::~Window() {
    shutdown();
}

bool Window::initialize() {
    LOG_INFO("Initializing window: ", config.title);

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        LOG_ERROR("Failed to initialize SDL: ", SDL_GetError());
        return false;
    }

    // Create Vulkan window (no GL attributes needed)
    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN;
    if (config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    window = SDL_CreateWindow(
        config.title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        flags
    );

    if (!window) {
        LOG_ERROR("Failed to create window: ", SDL_GetError());
        return false;
    }

    // Initialize Vulkan context
    vkContext = std::make_unique<rendering::VkContext>();
    if (!vkContext->initialize(window)) {
        LOG_ERROR("Failed to initialize Vulkan context");
        return false;
    }

    LOG_INFO("Window initialized successfully (Vulkan)");
    return true;
}

void Window::shutdown() {
    if (vkContext) {
        vkContext->shutdown();
        vkContext.reset();
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_Quit();
    LOG_INFO("Window shutdown complete");
}

void Window::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            shouldCloseFlag = true;
        }
        else if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                width = event.window.data1;
                height = event.window.data2;
                if (vkContext) {
                    vkContext->markSwapchainDirty();
                }
                LOG_DEBUG("Window resized to ", width, "x", height);
            }
        }
    }
}

void Window::setFullscreen(bool enable) {
    if (!window) return;
    if (enable == fullscreen) return;
    if (enable) {
        windowedWidth = width;
        windowedHeight = height;
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            LOG_WARNING("Failed to enter fullscreen: ", SDL_GetError());
            return;
        }
        fullscreen = true;
        SDL_GetWindowSize(window, &width, &height);
    } else {
        if (SDL_SetWindowFullscreen(window, 0) != 0) {
            LOG_WARNING("Failed to exit fullscreen: ", SDL_GetError());
            return;
        }
        fullscreen = false;
        SDL_SetWindowSize(window, windowedWidth, windowedHeight);
        width = windowedWidth;
        height = windowedHeight;
    }
    if (vkContext) {
        vkContext->markSwapchainDirty();
    }
}

void Window::setVsync([[maybe_unused]] bool enable) {
    // VSync in Vulkan is controlled by present mode (set at swapchain creation)
    // For now, store the preference — applied on next swapchain recreation
    vsync = enable;
    LOG_INFO("VSync preference set to ", enable ? "on" : "off", " (applied on swapchain recreation)");
}

void Window::applyResolution(int w, int h) {
    if (!window) return;
    if (w <= 0 || h <= 0) return;
    if (fullscreen) {
        windowedWidth = w;
        windowedHeight = h;
        return;
    }
    SDL_SetWindowSize(window, w, h);
    width = w;
    height = h;
    windowedWidth = w;
    windowedHeight = h;
    if (vkContext) {
        vkContext->markSwapchainDirty();
    }
}

} // namespace core
} // namespace wowee
