#include <iostream>
#include <chrono>
#include <memory>

#include "resource/config_manager/ConfigManager.hpp"
#include "core/logs/Log.hpp"
#include "function/graphics/GraphicsContext.hpp"
#include "function/graphics/vulkan/VulkanGraphicsContext.hpp"
#include "function/graphics/WindowSystem.hpp"

using namespace StellarAlia::Function::Graphics;
using StellarAlia::Function::Graphics::WindowSystemCreateInfo;

// Type alias to avoid Windows macro conflict
using WindowType = StellarAlia::Function::Graphics::WindowSystem;

int main(int argc, char* argv[]) {
    // Initialize the logging system
    StellarAlia::Core::Log::Initialize();
    
    SA_LOG_INFO("=== StellarAlia Graphics Framework Test ===");
    SA_LOG_INFO("Testing window and graphics context initialization");
    
    // ============================================================================
    // Test 1: Window Creation (set up window system)
    // ============================================================================
    SA_LOG_INFO("\n[Test 1] Creating window system...");

    const auto& appConfig = StellarAlia::Resource::ConfigManager::Get();
    WindowSystemCreateInfo windowInfo;
    windowInfo.width = 1280;
    windowInfo.height = 720;
    windowInfo.title = appConfig.windowTitle.c_str();
    windowInfo.resizable = true;
    
    // Shared ownership so we can pass the window to the render system safely
    std::shared_ptr<WindowType> window = std::make_shared<WindowType>();
    SA_LOG_INFO("Using GLFW window backend");
    
    if (!window->Initialize(windowInfo)) {
        SA_LOG_ERROR("Failed to initialize window!");
        StellarAlia::Core::Log::Shutdown();
        return 1;
    }
    
    SA_LOG_INFO("Window created successfully!");
    SA_LOG_INFO("  Backend: GLFW");
    SA_LOG_INFO("  Size: {}x{}", window->GetWidth(), window->GetHeight());

    // ============================================================================
    // Test 2: Render System Creation (graphics context)
    // ============================================================================
    SA_LOG_INFO("\n[Test 2] Creating render system (graphics context)...");

    GraphicsContextCreateInfo contextInfo;
    contextInfo.enableValidation = true;
    contextInfo.window = window;  // Pass shared window to graphics context
    
    // Verify window is valid before proceeding
    if (!contextInfo.window) {
        SA_LOG_ERROR("Window pointer is null! Cannot create graphics context.");
        window->Shutdown();
        StellarAlia::Core::Log::Shutdown();
        return 1;
    }
    
    SA_LOG_INFO("Window pointer is valid: {}", static_cast<void*>(contextInfo.window.get()));
    
    // Shared render system so lifetime can be managed alongside the window
    std::shared_ptr<GraphicsContext> graphicsContext = std::make_shared<VulkanGraphicsContext>();

    if (!graphicsContext) {
        SA_LOG_ERROR("Failed to create VulkanGraphicsContext instance!");
        window->Shutdown();
        StellarAlia::Core::Log::Shutdown();
        return 1;
    }

    SA_LOG_INFO("About to initialize graphics context...");
    if (!graphicsContext->Initialize(contextInfo)) {
        SA_LOG_ERROR("Failed to initialize graphics context!");
        window->Shutdown();
        StellarAlia::Core::Log::Shutdown();
        return 1;
    }
    
    SA_LOG_INFO("Graphics context initialized successfully!");
    SA_LOG_INFO("  API: {}", 
        graphicsContext->GetAPI() == GraphicsAPI::Vulkan ? "Vulkan" : "Unknown");
    SA_LOG_INFO("  Resolution: {}x{}", 
        graphicsContext->GetWidth(), graphicsContext->GetHeight());
    SA_LOG_INFO("  Initialized: {}", graphicsContext->IsInitialized());
    
    // ============================================================================
    // Test 3: Render Loop
    // ============================================================================
    SA_LOG_INFO("\n[Test 3] Starting render loop (10 seconds)...");
    SA_LOG_INFO("Close the window or wait for timeout to exit");
    
    auto startTime = std::chrono::steady_clock::now();
    const auto testDuration = std::chrono::seconds(10);
    uint32_t frameCount = 0;
    bool running = true;
    
    while (running) {
        // Check if window should close
        if (!window->PollEvents() || window->ShouldClose()) {
            SA_LOG_INFO("Window close requested");
            running = false;
            break;
        }
        
        // Check timeout
        auto currentTime = std::chrono::steady_clock::now();
        if (currentTime - startTime >= testDuration) {
            SA_LOG_INFO("Test duration reached");
            running = false;
            break;
        }
        
        // Render frame
        graphicsContext->BeginFrame();
        graphicsContext->EndFrame();
        graphicsContext->Present();
        
        frameCount++;
        
        // Log FPS every second
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - startTime).count();
        if (elapsed > 0 && frameCount % 60 == 0) {
            float fps = (frameCount * 1000.0f) / elapsed;
            SA_LOG_DEBUG("FPS: {:.2f} (Frame: {})", fps, frameCount);
        }
    }
    
    float totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count() / 1000.0f;
    float avgFps = frameCount / totalTime;
    
    SA_LOG_INFO("\nRender loop completed:");
    SA_LOG_INFO("  Total frames: {}", frameCount);
    SA_LOG_INFO("  Total time: {:.2f} seconds", totalTime);
    SA_LOG_INFO("  Average FPS: {:.2f}", avgFps);
    
    // ============================================================================
    // Test 4: Cleanup
    // ============================================================================
    SA_LOG_INFO("\n[Test 4] Cleaning up...");
    
    graphicsContext->WaitIdle();
    graphicsContext->Shutdown();
    SA_LOG_INFO("Graphics context shut down");
    
    window->Shutdown();
    SA_LOG_INFO("Window shut down");
    
    // ============================================================================
    // Test Summary
    // ============================================================================
    SA_LOG_INFO("\n=== Test Summary ===");
    SA_LOG_INFO("Window creation and initialization: PASSED");
    SA_LOG_INFO("Graphics context creation and initialization: PASSED");
    SA_LOG_INFO("Render loop execution: PASSED");
    SA_LOG_INFO("Cleanup: PASSED");
    SA_LOG_INFO("\nAll tests completed successfully!");
    
    // Shutdown logging
    StellarAlia::Core::Log::Shutdown();
    
    return 0;
}
