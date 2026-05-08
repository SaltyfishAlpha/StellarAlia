#include "EditorMode.hpp"

#include "engine/Application.hpp"
#include "resource/AssetRegistry.hpp"
#include "engine/EnginePlayState.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Components.hpp"
#include "input/EditorInputMaps.hpp"
#include "platform/input/GLFWInputProvider.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/vulkan/VulkanCommandList.hpp"
#include "ui/panels/PlaybackPanel.hpp"
#include "ui/panels/SceneHierarchyPanel.hpp"
#include "ui/panels/InspectorPanel.hpp"
#include "ui/panels/SettingsPanel.hpp"
#include "ui/panels/WorldSettingsPanel.hpp"
#include "ui/panels/AssetsPanel.hpp"

#include "engine/SaProject.hpp"
#include "function/scene/SceneSerializer.hpp"

#include "core/logs/Log.hpp"
#include "function/animation/AnimationSystem.hpp"

#include <GLFW/glfw3.h>
#include <filesystem>
#include <glm/gtc/matrix_inverse.hpp>

namespace fs = std::filesystem;

namespace StellarAlia::Editor {

// File-static drop target: GLFW drop callback can't capture, so we use this
// single-editor-instance shortcut.  Cleared in OnDetach.
static AssetsPanel* s_dropTarget = nullptr;

void EditorMode::OnAttach(Application& app) {
    m_app = &app;

    // ── Input maps ────────────────────────────────────────────────────────────
    InputSystem& input = app.GetInputSystem();
    input.RegisterMaps(MakeViewportMaps());
    input.PushMap("Viewport");
    m_viewportActive = true;

    // ── Load project and startup scene ───────────────────────────────────────
    Scene& scene = app.GetScene();
    const std::string& projectDir = app.GetDesc().projectDir;
    if (!projectDir.empty()) {
        fs::path projFile;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(projectDir, ec)) {
            if (entry.path().extension() == ".saproject") {
                projFile = entry.path();
                break;
            }
        }
        if (!projFile.empty()) {
            SaProject proj;
            if (LoadSaProject(projFile, proj) && !proj.startupScene.empty()) {
                const fs::path scenePath = fs::path(projectDir) / proj.startupScene;
                if (SceneSerializer::LoadFromFile(scene, scenePath))
                    SA_LOG_INFO("EditorMode: loaded '{}'", scenePath.string());
                else
                    SA_LOG_WARN("EditorMode: could not load startup scene '{}'",
                                scenePath.string());
            }
        } else {
            SA_LOG_INFO("EditorMode: no .saproject found in '{}' — empty scene", projectDir);
        }
    }

    // ── Scan asset registry ──────────────────────────────────────────────────
    m_assetRegistry = &app.GetAssetRegistry();
    {
        const fs::path engineAssets = app.GetDesc().engineAssetsDir;
        const fs::path projectAssets =
            projectDir.empty() ? fs::path{} : fs::path(projectDir) / "assets";
        m_assetRegistry->Scan(projectAssets, engineAssets);
        SA_LOG_INFO("EditorMode: asset registry scanned ({} assets)",
                    m_assetRegistry->Count());
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
    hierarchyOwned->SetRegistry(m_assetRegistry);
    hierarchyOwned->SetSceneLoadCallback([this](const fs::path& path) {
        LoadScene(path);
    });
    m_hierarchyPanel = hierarchyOwned.get();
    m_ui.RegisterWindow(std::move(hierarchyOwned));
    m_ui.RegisterWindow(std::make_unique<InspectorPanel>(scene, *m_hierarchyPanel, m_assetRegistry));
    m_ui.RegisterWindow(std::make_unique<SettingsPanel>(
        &m_overlaySettings, &app.GetPhysicsDebugSettings()));
    m_ui.RegisterWindow(std::make_unique<WorldSettingsPanel>(scene, app.GetRenderer(), m_assetRegistry));
    {
        auto assetsOwned = std::make_unique<AssetsPanel>(
            projectDir, app.GetDesc().cookCacheDir, m_assetRegistry);
        m_assetsPanel = assetsOwned.get();
        m_assetsPanel->SetSceneLoadCallback([this](const fs::path& path) {
            LoadScene(path);
        });
        // After import: rescan registry with both dirs so the Inspector picker updates.
        m_assetsPanel->SetImportCallback([this]() {
            const std::string& pd = m_app->GetDesc().projectDir;
            m_assetRegistry->Scan(
                pd.empty() ? fs::path{} : fs::path(pd) / "assets",
                m_app->GetDesc().engineAssetsDir);
        });
        m_ui.RegisterWindow(std::move(assetsOwned));
    }

    // ── GLFW drop callback (import via drag-and-drop from Explorer) ────────────
    // GLFWWindow already owns the window user pointer for resize events, so we
    // use a file-static pointer to forward drop events to the panel.
    s_dropTarget = m_assetsPanel;
    glfwSetDropCallback(glfwWin, [](GLFWwindow*, int count, const char** paths) {
        if (s_dropTarget) s_dropTarget->EnqueueDroppedPaths(count, paths);
    });

    SA_LOG_INFO("EditorMode: attached");
    SA_LOG_INFO("  RMB + Mouse / Right stick — Look");
    SA_LOG_INFO("  WASD / Left stick         — Move");
    SA_LOG_INFO("  Left Shift / LB           — Sprint");
}

void EditorMode::OnDetach() {
    s_dropTarget = nullptr;
    if (m_app) {
        auto* glfwWin = static_cast<GLFWwindow*>(m_app->GetNativeWindow());
        if (glfwWin) glfwSetDropCallback(glfwWin, nullptr);
    }
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
            const glm::vec3 drawPos = pos + rot * col->offset;
            const glm::quat drawRot = rot * col->rotation;

            switch (col->shape) {
                case ColliderComponent::Shape::Box:
                    dd.DrawBox(drawPos, col->extents, drawRot, color);
                    break;
                case ColliderComponent::Shape::Sphere:
                    dd.DrawSphere(drawPos, col->extents.x, color);
                    break;
                case ColliderComponent::Shape::Capsule:
                    dd.DrawCapsule(
                        drawPos - drawRot * glm::vec3(0.f, col->extents.y, 0.f),
                        drawPos + drawRot * glm::vec3(0.f, col->extents.y, 0.f),
                        col->extents.x, color);
                    break;
            }
        }
    }

    if (m_overlaySettings.drawSkeletonGizmo &&
        reg.any_of<SkinnedMeshComponent>(selected))
    {
        AnimationSystem& anim    = m_app->GetAnimationSystem();
        auto poses    = anim.GetBoneGlobalPoses(selected);
        auto skeleton = anim.GetBoneSkeleton(selected);

        if (!poses.empty()) {
            const glm::mat4& worldMat = wtc->matrix;
            const glm::vec4 jointColor = { 1.f, 0.85f, 0.1f, 1.f };
            const glm::vec4 boneColor  = { 0.8f, 0.8f, 0.8f, 1.f };
            constexpr float kJointRadius = 0.025f;

            for (size_t bi = 0; bi < poses.size(); ++bi) {
                const glm::vec3 worldPos =
                    glm::vec3(worldMat * glm::vec4(glm::vec3(poses[bi][3]), 1.f));
                dd.DrawSphereOverlay(worldPos, kJointRadius, jointColor, 8);

                if (bi < skeleton.size() && skeleton[bi].parentIndex >= 0) {
                    const int32_t pi = skeleton[bi].parentIndex;
                    const glm::vec3 parentPos =
                        glm::vec3(worldMat * glm::vec4(glm::vec3(poses[pi][3]), 1.f));
                    dd.DrawLineOverlay(parentPos, worldPos, boneColor);
                }
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

void EditorMode::LoadScene(const fs::path& path) {
    Scene& scene = m_app->GetScene();
    scene.Clear();
    if (!SceneSerializer::LoadFromFile(scene, path)) {
        SA_LOG_WARN("EditorMode: failed to load scene '{}'", path.string());
        m_app->RebuildDrawList();  // empty scene still needs a clean draw list
        return;
    }
    SA_LOG_INFO("EditorMode: loaded scene '{}'", path.string());
    m_app->GetRenderer().ApplyWorldSettings(scene.GetWorldSettings());
    m_app->PrepareAnimatedEntities();
    m_app->RebuildDrawList();
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
