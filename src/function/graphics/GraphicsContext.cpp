#include "function/graphics/GraphicsContext.hpp"

// Include backend implementations
#include "function/graphics/vulkan/VulkanGraphicsContext.hpp"

namespace StellarAlia::Function::Graphics {

    std::unique_ptr<GraphicsContext> CreateGraphicsContext(const GraphicsContextCreateInfo& createInfo) {
        switch (createInfo.api) {
            case GraphicsAPI::Vulkan:
                return std::make_unique<VulkanGraphicsContext>();
            
            case GraphicsAPI::OpenGL:
                // TODO: Implement OpenGL backend
                return nullptr;
            
            case GraphicsAPI::DirectX11:
                // TODO: Implement DirectX11 backend
                return nullptr;
            
            case GraphicsAPI::DirectX12:
                // TODO: Implement DirectX12 backend
                return nullptr;
            
            case GraphicsAPI::Metal:
                // TODO: Implement Metal backend
                return nullptr;
            
            case GraphicsAPI::None:
            default:
                return nullptr;
        }
    }

} // namespace StellarAlia::Function::Graphics

