#include "EditorMode.hpp"

#include "engine/Application.hpp"
#include "input/EditorInputMaps.hpp"
#include "platform/input/GLFWInputProvider.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/vulkan/VulkanCommandList.hpp"
#include "function/scene/SceneSerializer.hpp"

#include "ui/panels/SceneHierarchyPanel.hpp"
#include "ui/panels/InspectorPanel.hpp"
#include "ui/panels/SettingsPanel.hpp"

#include "core/logs/Log.hpp"

#include <GLFW/glfw3.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace StellarAlia::Editor {

void EditorMode::OnAttach(Application& app) {
    m_app = &app;

    // ── Input maps ────────────────────────────────────────────────────────────
    InputSystem& input = app.GetInputSystem();
    input.RegisterMaps(MakeViewportMaps());
    input.PushMap("Viewport");

    // ── Load default scene ────────────────────────────────────────────────────
    const fs::path scenePath =
        fs::path(app.GetDesc().assetsDir) / "scenes" / "metal_rough_spheres.sascene";

    Scene& scene = app.GetScene();
    if (SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_INFO("EditorMode: loaded '{}'", scenePath.string());
    } else {
        SA_LOG_WARN("EditorMode: could not load '{}' — starting with empty scene",
                    scenePath.string());
    }

    // ── IBL (world settings are populated by the scene file) ─────────────────
    if (!app.GetRenderer().SetIBL(scene.GetWorldSettings()))
        SA_LOG_WARN("EditorMode: IBL unavailable — cook assets to enable skybox");

    // ── EditorUI ──────────────────────────────────────────────────────────────
    auto* glfwWin = static_cast<GLFWwindow*>(app.GetNativeWindow());
    if (!m_ui.Init(glfwWin, &app.GetVulkanDevice()))
        SA_LOG_WARN("EditorMode: UI init failed — editor panels unavailable");

    // Register built-in panels.
    // The hierarchy panel is registered first; the inspector holds a raw pointer
    // to it (safe — both are owned by m_ui and live for the same duration).
    auto hierarchyOwned = std::make_unique<SceneHierarchyPanel>(scene);
    const SceneHierarchyPanel* hierarchyPtr = hierarchyOwned.get();
    m_ui.RegisterWindow(std::move(hierarchyOwned));
    m_ui.RegisterWindow(std::make_unique<InspectorPanel>(scene, *hierarchyPtr));
    m_ui.RegisterWindow(std::make_unique<SettingsPanel>());

    SA_LOG_INFO("EditorMode: attached");
    SA_LOG_INFO("  RMB + Mouse / Right stick — Look");
    SA_LOG_INFO("  WASD / Left stick         — Move");
    SA_LOG_INFO("  Left Shift / LB           — Sprint");
}

void EditorMode::OnDetach() {
    m_ui.Shutdown();

    // Restore cursor in case it was captured during shutdown.
    if (m_app)
        m_app->GetInputProvider().SetCursorCapture(false);
    m_app = nullptr;
    SA_LOG_INFO("EditorMode: detached");
}

void EditorMode::OnRenderUI(RHI::IRHICommandList* cmd) {
    m_ui.NewFrame();
    m_ui.DrawPanels();
    m_ui.Render(cmd);
}

void EditorMode::OnUpdate(float dt) {
    InputSystem&         input    = m_app->GetInputSystem();
    Platform::GLFWInputProvider& provider = m_app->GetInputProvider();

    // Cursor capture — right mouse button for KBM, always active for Gamepad.
    const bool mouseLook = input.IsActive("MouseLook") ||
                           input.ActiveFamily() == DeviceFamily::Gamepad;
    if (input.WasActivated("MouseLook"))
        provider.SetCursorCapture(true);
    else if (input.WasDeactivated("MouseLook"))
        provider.SetCursorCapture(false);

    m_camera.Update(input, dt, mouseLook);
}

CameraData EditorMode::GetCameraData(float aspectRatio) const {
    return m_camera.GetCameraData(aspectRatio);
}

} // namespace StellarAlia::Editor
