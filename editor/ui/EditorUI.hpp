#pragma once

#include <cstdint>
#include <memory>
#include <vector>

struct GLFWwindow;

// Full definition required: vector<unique_ptr<IEditorWindow>> needs the
// complete type for destruction when EditorUI is destroyed.
#include "ui/IEditorWindow.hpp"

namespace StellarAlia::RHI { class VulkanDevice; class IRHICommandList; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// EditorUI — ImGui lifecycle manager for the editor layer.
//
// Lifecycle (mirrors AppMode):
//   Init()        ← once after renderer is ready
//   Per frame:
//     NewFrame()       ← start ImGui frame + build DockSpace
//     DrawPanels()     ← call OnDraw() on all registered windows
//     Render(cmd)      ← record ImGui draw calls into the command buffer
//   Shutdown()    ← once before renderer shutdown
//
// Custom panels:
//   RegisterWindow(std::make_unique<MyPanel>());
// ─────────────────────────────────────────────────────────────────────────────
class EditorUI {
public:
    EditorUI()  = default;
    ~EditorUI() = default;

    EditorUI(const EditorUI&)            = delete;
    EditorUI& operator=(const EditorUI&) = delete;

    // Initialise ImGui + GLFW/Vulkan backends.
    // Call once after VulkanDevice and the GLFW window are ready.
    bool Init(GLFWwindow* window, RHI::VulkanDevice* device);

    // Destroy ImGui resources. Must be called before VulkanDevice::Shutdown().
    void Shutdown();

    // Register a panel. EditorUI takes ownership; OnOpen() is called immediately.
    void RegisterWindow(std::unique_ptr<IEditorWindow> panel);

    // ── Per-frame API ─────────────────────────────────────────────────────────

    // Start a new ImGui frame and build the full-screen DockSpace.
    void NewFrame();

    // Iterate all open panels, calling OnDraw() wrapped in Begin/End.
    void DrawPanels();

    // Finalise ImGui frame and record draw commands into the active command buffer.
    // cmd must be the IRHICommandList returned by the current BeginFrame.
    // The swapchain image must be in COLOR_ATTACHMENT_OPTIMAL at call time.
    void Render(RHI::IRHICommandList* cmd);

    // Notify backend when swapchain is recreated (pass new min image count).
    void OnSwapchainResize(uint32_t minImageCount);

private:
    RHI::VulkanDevice*                          m_device      = nullptr;
    std::vector<std::unique_ptr<IEditorWindow>> m_windows;
    bool                                        m_initialized = false;
    // ImGui manages its own descriptor pool (DescriptorPoolSize path).
    // No VkDescriptorPool member needed in this header.
};

} // namespace StellarAlia::Editor
