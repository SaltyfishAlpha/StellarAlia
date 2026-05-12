#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

// Forward-declared so EditorUI.hpp stays cheap to include.
namespace StellarAlia::Editor { class EditorDiagnostics; }
namespace StellarAlia::Editor { class CommandManager; }

struct GLFWwindow;
struct ImFont;

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
    // engineAssetsDir is used to locate fonts/fa-solid-900.otf for the icon font.
    bool Init(GLFWwindow* window, RHI::VulkanDevice* device,
              const std::filesystem::path& engineAssetsDir = {});

    // Returns the loaded Font Awesome 6 icon font, or nullptr if not loaded.
    [[nodiscard]] ImFont* GetIconFont() const { return m_iconFont; }

    // Destroy ImGui resources. Must be called before VulkanDevice::Shutdown().
    void Shutdown();

    // Register a panel. EditorUI takes ownership; OnOpen() is called immediately.
    void RegisterWindow(std::unique_ptr<IEditorWindow> panel);

    // ── Per-frame API ─────────────────────────────────────────────────────────

    // Call before NewFrame() to suppress ImGui mouse hover/click events this
    // frame (e.g. while the viewport cursor is captured for camera look).
    void SetMouseCapture(bool captured) { m_mouseCapture = captured; }

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

    // Temporarily hide / restore all registered panels without changing isOpen.
    void TogglePanelsHidden();
    bool ArePanelsHidden() const;

    // File menu callbacks — set before DrawPanels() is first called.
    struct FileCallbacks {
        std::function<void()> onNewScene;
        std::function<void()> onSaveScene;
        std::function<void()> onNewProject;
        std::function<void()> onOpenProject;
    };
    void SetFileCallbacks(FileCallbacks cbs) { m_fileCallbacks = std::move(cbs); }

    // Assets menu callbacks (assets panel actions).
    struct AssetCallbacks {
        std::function<void()> onImport;       // open import-file modal
        std::function<void()> onRefresh;      // rescan registry
        std::function<void()> onReimportAll;  // force-recook all project assets
    };
    void SetAssetCallbacks(AssetCallbacks cbs) { m_assetCallbacks = std::move(cbs); }

    // Wire the diagnostic bus so the menu bar can show an error/warning badge.
    void SetDiagnostics(EditorDiagnostics* diags) { m_diagnostics = diags; }

    // Wire the command manager so the Edit menu can show Undo/Redo descriptions.
    // onUndo / onRedo are called when the menu items are clicked.
    void SetCommandManager(CommandManager* mgr,
                           std::function<void()> onUndo,
                           std::function<void()> onRedo) {
        m_cmdMgr  = mgr;
        m_onUndo  = std::move(onUndo);
        m_onRedo  = std::move(onRedo);
    }

private:
    RHI::VulkanDevice*                          m_device        = nullptr;
    std::vector<std::unique_ptr<IEditorWindow>> m_windows;
    bool                                        m_initialized   = false;
    FileCallbacks                               m_fileCallbacks;
    AssetCallbacks                              m_assetCallbacks;
    EditorDiagnostics*                          m_diagnostics   = nullptr;
    CommandManager*                             m_cmdMgr        = nullptr;
    std::function<void()>                       m_onUndo;
    std::function<void()>                       m_onRedo;
    bool                                        m_panelsHidden  = false;
    bool                                        m_mouseCapture  = false;
    ImFont*                                     m_iconFont      = nullptr;
};

} // namespace StellarAlia::Editor
