#include "function/graphics/window/Window.hpp"

// Include backend implementations
#include "function/graphics/window/sdl2/SDL2Window.hpp"
#include "function/graphics/window/glfw/GLFWWindow.hpp"

namespace StellarAlia::Function::Graphics::Window {

    std::unique_ptr<Window> CreateWindow(const WindowCreateInfo& createInfo) {
        switch (createInfo.backend) {
            case WindowBackend::SDL2:
                return std::make_unique<SDL2Window>();
            
            case WindowBackend::GLFW:
                return std::make_unique<GLFWWindow>();
            
            case WindowBackend::None:
            default:
                return nullptr;
        }
    }

} // namespace StellarAlia::Function::Graphics::Window

