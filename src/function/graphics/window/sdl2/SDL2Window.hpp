#pragma once

/**
 * @file SDL2Window.hpp
 * @brief SDL2 implementation of Window
 */

#include "function/graphics/window/Window.hpp"
#include <SDL2/SDL.h>
#include <vector>

namespace StellarAlia::Function::Graphics::Window {

    /**
     * @brief SDL2 window implementation
     */
    class SDL2Window : public Window {
    public:
        SDL2Window();
        ~SDL2Window() override;

        bool Initialize(const WindowCreateInfo& createInfo) override;
        void Shutdown() override;
        bool PollEvents() override;
        void* GetNativeHandle() const override;
        const char* const* GetVulkanInstanceExtensions(uint32_t* extensionCount) const override;
        bool CreateVulkanSurface(VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) const override;
        WindowBackend GetBackend() const override { return WindowBackend::SDL2; }
        uint32_t GetWidth() const override;
        uint32_t GetHeight() const override;
        bool ShouldClose() const override;
        void SetTitle(const char* title) override;
        bool WasResized() const override;

    private:
        SDL_Window* m_window = nullptr;
        bool m_shouldClose = false;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        mutable std::vector<const char*> m_cachedExtensions;
        mutable bool m_wasResized = false;
    };

} // namespace StellarAlia::Function::Graphics::Window

