#include "function/graphics/window/sdl2/SDL2Window.hpp"
#include "core/logs/Log.hpp"
#include <SDL2/SDL_vulkan.h>

namespace StellarAlia::Function::Graphics::Window {

    SDL2Window::SDL2Window() = default;

    SDL2Window::~SDL2Window() {
        Shutdown();
    }

    bool SDL2Window::Initialize(const WindowCreateInfo& createInfo) {
        if (m_window) {
            SA_LOG_WARN("SDL2Window already initialized");
            return false;
        }

        // Initialize SDL2 if not already initialized
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            SA_LOG_ERROR("Failed to initialize SDL2: {}", SDL_GetError());
            return false;
        }

        uint32_t flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN;
        if (createInfo.resizable) {
            flags |= SDL_WINDOW_RESIZABLE;
        }
        if (createInfo.fullscreen) {
            flags |= SDL_WINDOW_FULLSCREEN;
        }

        m_window = SDL_CreateWindow(
            createInfo.title,
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            static_cast<int>(createInfo.width),
            static_cast<int>(createInfo.height),
            flags
        );

        if (!m_window) {
            SA_LOG_ERROR("Failed to create SDL2 window: {}", SDL_GetError());
            SDL_Quit();
            return false;
        }

        m_width = createInfo.width;
        m_height = createInfo.height;
        m_shouldClose = false;

        SA_LOG_INFO("SDL2 window created: {}x{}", m_width, m_height);
        return true;
    }

    void SDL2Window::Shutdown() {
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        SDL_Quit();
    }

    bool SDL2Window::PollEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                m_shouldClose = true;
                return false;
            }
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    m_width = static_cast<uint32_t>(event.window.data1);
                    m_height = static_cast<uint32_t>(event.window.data2);
                    m_wasResized = true;
                }
            }
        }
        return !m_shouldClose;
    }

    void* SDL2Window::GetNativeHandle() const {
        return m_window;
    }

    const char* const* SDL2Window::GetVulkanInstanceExtensions(uint32_t* extensionCount) const {
        if (!m_window) {
            *extensionCount = 0;
            return nullptr;
        }

        unsigned int count = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(m_window, &count, nullptr)) {
            SA_LOG_ERROR("Failed to get Vulkan instance extension count: {}", SDL_GetError());
            *extensionCount = 0;
            return nullptr;
        }

        m_cachedExtensions.resize(count);
        if (!SDL_Vulkan_GetInstanceExtensions(m_window, &count, m_cachedExtensions.data())) {
            SA_LOG_ERROR("Failed to get Vulkan instance extensions: {}", SDL_GetError());
            *extensionCount = 0;
            return nullptr;
        }

        *extensionCount = count;
        return m_cachedExtensions.data();
    }

    bool SDL2Window::CreateVulkanSurface(VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) const {
        if (!m_window) {
            return false;
        }

        if (!SDL_Vulkan_CreateSurface(m_window, instance, surface)) {
            SA_LOG_ERROR("Failed to create Vulkan surface from SDL2 window: {}", SDL_GetError());
            return false;
        }

        return true;
    }

    uint32_t SDL2Window::GetWidth() const {
        if (m_window) {
            int w, h;
            SDL_GetWindowSize(m_window, &w, &h);
            return static_cast<uint32_t>(w);
        }
        return m_width;
    }

    uint32_t SDL2Window::GetHeight() const {
        if (m_window) {
            int w, h;
            SDL_GetWindowSize(m_window, &w, &h);
            return static_cast<uint32_t>(h);
        }
        return m_height;
    }

    bool SDL2Window::ShouldClose() const {
        return m_shouldClose;
    }

    void SDL2Window::SetTitle(const char* title) {
        if (m_window) {
            SDL_SetWindowTitle(m_window, title);
        }
    }

    bool SDL2Window::WasResized() const {
        bool resized = m_wasResized;
        m_wasResized = false;  // Reset after checking
        return resized;
    }

} // namespace StellarAlia::Function::Graphics::Window

