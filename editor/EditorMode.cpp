#include "EditorMode.hpp"

#include "engine/Application.hpp"
#include "engine/EnginePlayState.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Components.hpp"
#include "input/EditorInputMaps.hpp"
#include "platform/input/GLFWInputProvider.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/vulkan/VulkanCommandList.hpp"
#include "function/scene/SceneSerializer.hpp"

#include "ui/panels/PlaybackPanel.hpp"
#include "ui/panels/SceneHierarchyPanel.hpp"
#include "ui/panels/InspectorPanel.hpp"
#include "ui/panels/SettingsPanel.hpp"
#include "ui/panels/WorldSettingsPanel.hpp"

#include "core/logs/Log.hpp"

#include <GLFW/glfw3.h>
#include <filesystem>
#include <glm/gtc/matrix_inverse.hpp>

namespace fs = std::filesystem;

namespace StellarAlia::Editor {

void EditorMode::OnAttach(Application& app) {
    m_app = &app;

    // ── Input maps ────────────────────────────────────────────────────────────
    InputSystem& input = app.GetInputSystem();
    input.RegisterMaps(MakeViewportMaps());
    input.PushMap("Viewport");
    m_viewportActive = true;

    // ── Load default scene ────────────────────────────────────────────────────
    const fs::path scenePath =
        fs::path(app.GetDesc().assetsDir) / "scenes" / "area_light_test.sascene";

    Scene& scene = app.GetScene();
    if (SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_INFO("EditorMode: loaded '{}'", scenePath.string());
    } else {
        SA_LOG_WARN("EditorMode: could not load '{}' — starting with empty scene",
                    scenePath.string());
    }

    // ── Apply world settings (background mode, tonemap, IBL) ─────────────────
    app.GetRenderer().ApplyWorldSettings(scene.GetWorldSettings());
    SA_LOG_INFO("EditorMode: world settings applied");

    // ── EditorUI ──────────────────────────────────────────────────────────────
    auto* glfwWin = static_cast<GLFWwindow*>(app.GetNativeWindow());
    if (!m_ui.Init(glfwWin, &app.GetVulkanDevice()))
        SA_LOG_WARN("EditorMode: UI init failed — editor panels unavailable");

    // Register built-in panels.
    // The hierarchy panel is registered first; the inspector holds a raw pointer
    // to it (safe — both are owned by m_ui and live for the same duration).
    m_ui.RegisterWindow(std::make_unique<PlaybackPanel>(app));
    auto hierarchyOwned = std::make_unique<SceneHierarchyPanel>(scene, input);
    m_hierarchyPanel = hierarchyOwned.get();
    m_ui.RegisterWindow(std::move(hierarchyOwned));
    m_ui.RegisterWindow(std::make_unique<InspectorPanel>(scene, *m_hierarchyPanel));
    m_ui.RegisterWindow(std::make_unique<SettingsPanel>(
        &m_overlaySettings, &app.GetPhysicsDebugSettings()));
    m_ui.RegisterWindow(std::make_unique<WorldSettingsPanel>(scene, app.GetRenderer()));

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

    // Gizmo mode shortcuts — T / R / S; guard S with !mouseLook to avoid WASD conflict.
    if (input.WasActivated("GizmoTranslate"))
        m_overlaySettings.gizmoMode = GizmoMode::Translate;
    else if (input.WasActivated("GizmoRotate"))
        m_overlaySettings.gizmoMode = GizmoMode::Rotate;
    else if (!mouseLook && input.WasActivated("GizmoScale"))
        m_overlaySettings.gizmoMode = GizmoMode::Scale;

    m_camera.Update(input, dt, mouseLook);
    DrawOverlays();
}

void EditorMode::DrawOverlays() {
    if (!m_overlaySettings.enabled) {
        m_app->GetRenderer().SetSelectedEntity(entt::null);
        m_app->GetRenderer().SetInfiniteGrid(false);
        return;
    }

    DebugDraw& dd    = m_app->GetDebugDraw();
    Scene&     scene = m_app->GetScene();

    m_app->GetRenderer().SetInfiniteGrid(m_overlaySettings.drawGrid);

    if (m_overlaySettings.drawWorldAxes)
        dd.DrawAxes(glm::mat4(1.f));

    if (m_overlaySettings.drawCameraFrustum) {
        const uint32_t sw = m_app->GetVulkanDevice().GetSwapchainWidth();
        const uint32_t sh = m_app->GetVulkanDevice().GetSwapchainHeight();
        bool hasCam = false;
        scene.View<CameraComponent, ActiveCameraTag>().each(
            [&](auto) { hasCam = true; });
        if (hasCam && sw > 0 && sh > 0) {
            const CameraData cam = SceneRenderer::ExtractCamera(scene, sw, sh);
            const glm::mat4  ivp = glm::inverse(cam.proj * cam.view);
            dd.DrawFrustum(ivp, {1.f, 0.9f, 0.2f, 1.f});
        }
    }

    // ── Selection-dependent overlays ──────────────────────────────────────────
    const entt::entity selected = m_hierarchyPanel
        ? static_cast<entt::entity>(m_hierarchyPanel->GetSelectedEntity())
        : entt::null;

    auto& reg = scene.Registry();

    // Only request outline when the selected entity or any descendant has renderable
    // geometry — prevents the outline pass from reading a stale mask (cameras, lights).
    // BFS traversal so selecting a parent with only child meshes still shows an outline.
    bool hasMesh = false;
    if (selected != entt::null) {
        std::vector<entt::entity> toVisit = { selected };
        for (size_t i = 0; i < toVisit.size() && !hasMesh; ++i) {
            entt::entity e = toVisit[i];
            if (reg.try_get<StaticMeshComponent>(e) || reg.try_get<SkinnedMeshComponent>(e))
                hasMesh = true;
            if (const auto* hc = reg.try_get<HierarchyComponent>(e))
                for (entt::entity child : hc->children)
                    toVisit.push_back(child);
        }
    }

    m_app->GetRenderer().SetSelectedEntity(
        (m_overlaySettings.drawSelectionAABB && hasMesh) ? selected : entt::null);
    m_app->GetRenderer().SetOutlineWidth(m_overlaySettings.outlineWidth);

    if (selected == entt::null) return;

    const auto* wtc = reg.try_get<WorldTransformComponent>(selected);
    if (!wtc) return;

    if (m_overlaySettings.drawEntityAxes)
        dd.DrawAxes(wtc->matrix);

    if (m_overlaySettings.drawSelectionCollider) {
        const auto* col = reg.try_get<ColliderComponent>(selected);
        if (col) {
            // Decompose TRS matrix: normalize rotation columns to strip scale
            // before quat_cast — a raw quat_cast on a scaled matrix gives a
            // non-unit quaternion which distorts the wireframe.
            const glm::vec3 pos(wtc->matrix[3]);
            const glm::mat3 rotMat(
                glm::normalize(glm::vec3(wtc->matrix[0])),
                glm::normalize(glm::vec3(wtc->matrix[1])),
                glm::normalize(glm::vec3(wtc->matrix[2]))
            );
            const glm::quat rot   = glm::quat_cast(rotMat);
            const glm::vec4 color = { 0.2f, 1.0f, 0.3f, 1.f };

            switch (col->shape) {
                case ColliderComponent::Shape::Box:
                    dd.DrawBox(pos, col->extents, rot, color);
                    break;
                case ColliderComponent::Shape::Sphere:
                    dd.DrawSphere(pos, col->extents.x, color);
                    break;
                case ColliderComponent::Shape::Capsule:
                    dd.DrawCapsule(
                        pos - rot * glm::vec3(0.f, col->extents.y, 0.f),
                        pos + rot * glm::vec3(0.f, col->extents.y, 0.f),
                        col->extents.x, color);
                    break;
            }
        }
    }

    if (m_overlaySettings.drawGizmo) {
        m_gizmo.mode = m_overlaySettings.gizmoMode;
        m_gizmo.Draw(dd, wtc->matrix);
    }
}

void EditorMode::OnPlayStateChanged(EnginePlayState newState) {
    m_overlaySettings.enabled = (newState == EnginePlayState::Editing);

    InputSystem&                 input    = m_app->GetInputSystem();
    Platform::GLFWInputProvider& provider = m_app->GetInputProvider();

    if (newState != EnginePlayState::Editing) {
        // Entering game / paused mode: release cursor and stop driving the
        // editor camera so it doesn't move while the player uses the game view.
        if (m_viewportActive) {
            provider.SetCursorCapture(false);
            input.PopMap();
            m_viewportActive = false;
        }
    } else {
        // Returning to edit mode: restore the viewport input map.
        if (!m_viewportActive) {
            input.PushMap("Viewport");
            m_viewportActive = true;
        }
    }
}

CameraData EditorMode::GetCameraData(float aspectRatio) const {
    if (m_app->GetPlayState() != EnginePlayState::Editing) {
        // Game / Paused: use the scene's active camera entity.
        const uint32_t refW = 1920;
        const uint32_t refH = (aspectRatio > 0.f)
            ? static_cast<uint32_t>(static_cast<float>(refW) / aspectRatio)
            : refW;
        return SceneRenderer::ExtractCamera(m_app->GetScene(), refW, refH);
    }
    return m_camera.GetCameraData(aspectRatio);
}

} // namespace StellarAlia::Editor
