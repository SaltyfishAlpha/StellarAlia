#include "EditorMode.hpp"

#include "ApplicationPath.hpp"
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
#include "ui/panels/PerformancePanel.hpp"
#include "ui/panels/PostProcessPanel.hpp"
#include "ui/panels/WorldSettingsPanel.hpp"
#include "ui/panels/AssetsPanel.hpp"
#include "ui/panels/ConsolePanel.hpp"
#include "ui/panels/ShortcutsPanel.hpp"
#include "resource/EntityTemplateRegistry.hpp"

#include "project/ProjectManager.hpp"
#include "ui/panels/ProjectBrowserPanel.hpp"

#include "engine/SaProject.hpp"
#include "function/scene/SceneSerializer.hpp"

#include "core/logs/Log.hpp"
#include "function/animation/AnimationSystem.hpp"
#include "shader_cook/ShaderCookLib.hpp"
#include "ui/EditorIcons.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#if __has_include(<ImGuizmo.h>)
#include <ImGuizmo.h>
#include <imgui_internal.h>
#define SA_HAS_IMGUIZMO 1
#endif

namespace fs = std::filesystem;

namespace StellarAlia::Editor {

// File-static drop target: GLFW drop callback can't capture, so we use this
// single-editor-instance shortcut.  Cleared in OnDetach.
static AssetsPanel* s_dropTarget = nullptr;

void EditorMode::OnAttach(Application& app) {
    m_app = &app;

    // ── Input maps ────────────────────────────────────────────────────────────
    InputSystem& input = app.GetInputSystem();
    m_shortcutConfig.Load(fs::path(StellarAliaApp::BIN_DIR) / "editor_shortcuts.json");
    input.RegisterMaps(m_shortcutConfig.ApplyTo(MakeViewportMaps()));
    input.PushMap("Viewport");
    m_viewportActive = true;

    // ── Load project and startup scene ───────────────────────────────────────
    Scene& scene = app.GetScene();
    const std::string& projectDir = app.GetDesc().projectDir;
    fs::path projFile;
    if (!projectDir.empty()) {
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

        // Scan entity templates from engine assets dir.
        m_templateRegistry.Scan(engineAssets);
        SA_LOG_INFO("EditorMode: template registry scanned ({} templates)",
                    m_templateRegistry.Entries().size());
    }

    // ── Apply world settings (background mode, tonemap, IBL) ─────────────────
    app.GetRenderer().ApplyWorldSettings(scene.GetWorldSettings());
    SA_LOG_INFO("EditorMode: world settings applied");

    // ── Log capture (attach before panels so early SA_LOG_* calls are caught) ──
    m_logCapture = std::make_unique<EditorLogCapture>();

    // ── EditorUI ──────────────────────────────────────────────────────────────
    auto* glfwWin = static_cast<GLFWwindow*>(app.GetNativeWindow());
    if (!m_ui.Init(glfwWin, &app.GetVulkanDevice(), app.GetDesc().engineAssetsDir))
        SA_LOG_WARN("EditorMode: UI init failed — editor panels unavailable");

    // Register built-in panels.
    // The hierarchy panel is registered first; the inspector holds a raw pointer
    // to it (safe — both are owned by m_ui and live for the same duration).
    m_ui.SetDiagnostics(&m_diagnostics);
    m_ui.RegisterWindow(std::make_unique<PlaybackPanel>(app, &m_diagnostics));
    auto hierarchyOwned = std::make_unique<SceneHierarchyPanel>(scene, input);
    hierarchyOwned->SetRegistry(m_assetRegistry);
    hierarchyOwned->SetTemplateRegistry(&m_templateRegistry);
    hierarchyOwned->SetSceneLoadCallback([this](const fs::path& path) {
        LoadScene(path);
    });
    m_hierarchyPanel = hierarchyOwned.get();
    hierarchyOwned->SetFocusEntityCallback([this](glm::vec3 worldPos) {
        m_camera.FocusOn(worldPos);
    });
    m_ui.RegisterWindow(std::move(hierarchyOwned));
    {
        auto inspOwned = std::make_unique<InspectorPanel>(scene, *m_hierarchyPanel,
                                                         m_assetRegistry,
                                                         app.GetRenderer().GetMaterialManager());
        m_inspectorPanel = inspOwned.get();
        m_ui.RegisterWindow(std::move(inspOwned));
    }
    m_ui.RegisterWindow(std::make_unique<SettingsPanel>(
        &m_overlaySettings, &app.GetPhysicsDebugSettings()));
    m_ui.RegisterWindow(std::make_unique<PerformancePanel>(
        &app.GetRenderer().GetRenderGraph(), &app.GetVulkanDevice()));
    m_ui.RegisterWindow(std::make_unique<WorldSettingsPanel>(scene, app.GetRenderer(), m_assetRegistry));
    m_ui.RegisterWindow(std::make_unique<PostProcessPanel>(scene, app.GetRenderer(), m_assetRegistry));
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
        m_assetsPanel->SetInput(&input);
        m_assetsPanel->SetCookShadersCallback([this]() { CookProjectShaders(); });
        m_assetsPanel->SetDiagnostics(&m_diagnostics);
        m_assetsPanel->SetMaterialManager(app.GetRenderer().GetMaterialManager());
        // Eagerly scan and cook project assets before the first frame so the
        // Inspector's material picker is populated on startup.
        m_assetsPanel->RunInitialScan();
        m_ui.RegisterWindow(std::move(assetsOwned));
    }

    if (m_inspectorPanel) m_inspectorPanel->SetAssetsPanel(m_assetsPanel);

    m_ui.RegisterWindow(std::make_unique<ConsolePanel>(m_diagnostics, m_logCapture->GetSink()));
    m_ui.RegisterWindow(std::make_unique<ShortcutsPanel>(
        m_shortcutConfig, input,
        fs::path(StellarAliaApp::BIN_DIR) / "editor_shortcuts.json"));

    // ── GLFW drop callback (import via drag-and-drop from Explorer) ────────────
    // GLFWWindow already owns the window user pointer for resize events, so we
    // use a file-static pointer to forward drop events to the panel.
    s_dropTarget = m_assetsPanel;
    glfwSetDropCallback(glfwWin, [](GLFWwindow*, int count, const char** paths) {
        if (s_dropTarget) s_dropTarget->EnqueueDroppedPaths(count, paths);
    });

    // ── File menu callbacks ───────────────────────────────────────────────────
    m_ui.SetFileCallbacks({
        .onNewScene    = [this]() { NewScene(); },
        .onSaveScene   = [this]() { SaveScene(); },
        .onNewProject  = [this]() { m_showProjectBrowser = true; },
        .onOpenProject = [this]() { m_showProjectBrowser = true; },
    });

    // ── Assets menu callbacks ─────────────────────────────────────────────────
    m_ui.SetAssetCallbacks({
        [this]() { if (m_assetsPanel) m_assetsPanel->RequestImport(); },
        [this]() { if (m_assetsPanel) m_assetsPanel->RequestRefresh(); },
        [this]() { if (m_assetsPanel) m_assetsPanel->RequestReimportAll(); }
    });

    // ── Project browser ───────────────────────────────────────────────────────
    const fs::path binDir = fs::path(StellarAliaApp::BIN_DIR);
    m_recentsConfigPath   = binDir / "recent_projects.json";
    m_projectManager.LoadRecents(m_recentsConfigPath);

    m_projectBrowserPanel = std::make_unique<ProjectBrowserPanel>(
        m_projectManager, app.GetDesc().engineAssetsDir);
    m_projectBrowserPanel->SetOnProjectSelected([this](const fs::path& path) {
        m_pendingProjectLoad = path;
    });

    if (projectDir.empty() || projFile.empty())
        m_showProjectBrowser = true;

    // ── EditorIconCache (must be after ImGui Vulkan backend init) ────────────
    m_iconCache = std::make_unique<EditorIconCache>();
    m_iconCache->Init(&app.GetVulkanDevice(),
                      &app.GetResourceManager(),
                      app.GetDesc().engineAssetsDir);
    if (m_assetsPanel)    m_assetsPanel->SetIconCache(m_iconCache.get(), m_ui.GetIconFont());
    if (m_inspectorPanel) m_inspectorPanel->SetIconCache(m_iconCache.get());
    if (m_inspectorPanel) m_inspectorPanel->SetIconFont(m_ui.GetIconFont());

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
    const fs::path defaultPath = fs::path(StellarAliaApp::BIN_DIR) / "editor_shortcuts.json";
    if (m_shortcutConfig.IsDirty() && m_shortcutConfig.GetConfigPath() != defaultPath)
        m_shortcutConfig.Save();
    if (m_iconCache) { m_iconCache->Shutdown(); m_iconCache.reset(); }
    m_ui.Shutdown();
    m_logCapture.reset();  // removes sink from spdlog before logger shuts down

    // Restore cursor in case it was captured during shutdown.
    if (m_app)
        m_app->GetInputProvider().SetCursorCapture(false);
    m_app = nullptr;
    SA_LOG_INFO("EditorMode: detached");
}

void EditorMode::OnRenderUI(RHI::IRHICommandList* cmd) {
    m_ui.NewFrame();
    DrawBillboardIcons();

    if (m_projectBrowserPanel) {
        if (m_showProjectBrowser) {
            m_projectBrowserPanel->Open();
            m_showProjectBrowser = false;
        }
        m_projectBrowserPanel->OnDraw();
    }

    m_ui.DrawPanels();
    DrawImGuizmo();
    HandleViewportInteraction();
    m_ui.Render(cmd);
}

void EditorMode::OnUpdate(float dt) {
    // Flush deferred project load — must run before RenderFrame (before vkAcquireNextImageKHR).
    if (!m_pendingProjectLoad.empty()) {
        const fs::path path = std::move(m_pendingProjectLoad);
        m_pendingProjectLoad.clear();
        LoadProject(path);
        return;   // skip this frame's input/camera update; next frame will be the new project
    }

    InputSystem&         input    = m_app->GetInputSystem();
    Platform::GLFWInputProvider& provider = m_app->GetInputProvider();

    // Push "TextInput" map only when an InputText widget is actively consuming
    // typed characters. WantCaptureKeyboard is too broad — it is also true when
    // any ImGui panel has keyboard-nav focus, which would block WASD/shortcuts
    // whenever the user clicks a panel. WantTextInput is set only for actual
    // text-entry widgets (rename, console, etc.).
    const bool wantKeys = ImGui::GetIO().WantTextInput;
    if (wantKeys && !m_textInputMapPushed) {
        input.PushMap("TextInput");
        m_textInputMapPushed = true;
    } else if (!wantKeys && m_textInputMapPushed) {
        input.PopMap();
        m_textInputMapPushed = false;
    }

    // Release cursor capture while the transform gizmo is being dragged so the
    // mouse pointer remains visible and ImGuizmo can process the drag correctly.
    if (m_gizmoIsUsing)
        provider.SetCursorCapture(false);

    // Cursor capture — right mouse button for KBM, always active for Gamepad.
    const bool mouseLook = !m_gizmoIsUsing &&
                           (input.IsActive("MouseLook") ||
                            input.ActiveFamily() == DeviceFamily::Gamepad);
    if (!m_gizmoIsUsing && input.WasActivated("MouseLook"))
        provider.SetCursorCapture(true);
    else if (input.WasDeactivated("MouseLook"))
        provider.SetCursorCapture(false);

    // File shortcuts — Ctrl+N / Ctrl+S; composite bindings also prevent N/S from
    // leaking into camera WASD or gizmo-scale when the modifier is held.
    if (input.WasActivated("NewScene"))
        NewScene();
    else if (input.WasActivated("SaveScene"))
        SaveScene();

    if (input.WasActivated("TogglePanels"))
        m_ui.TogglePanelsHidden();

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
    auto&      reg   = scene.Registry();

    const entt::entity selected = m_hierarchyPanel
        ? static_cast<entt::entity>(m_hierarchyPanel->GetSelectedEntity())
        : entt::null;

    m_app->GetRenderer().SetInfiniteGrid(m_overlaySettings.drawGrid);

    if (m_overlaySettings.drawWorldAxes)
        dd.DrawAxes(glm::mat4(1.f));

    if (m_overlaySettings.drawCameraFrustum) {
        const uint32_t sw = m_app->GetVulkanDevice().GetSwapchainWidth();
        const uint32_t sh = m_app->GetVulkanDevice().GetSwapchainHeight();
        if (sw > 0 && sh > 0) {
            const float aspect = static_cast<float>(sw) / static_cast<float>(sh);
            scene.View<CameraComponent, WorldTransformComponent>().each(
                [&](entt::entity e, const CameraComponent& cam,
                    const WorldTransformComponent& wt)
                {
                    glm::mat4 proj = glm::perspective(cam.fovY, aspect,
                                                      cam.nearPlane, cam.farPlane);
                    proj[1][1] *= -1.f;
                    const glm::mat4 ivp = glm::inverse(proj * glm::inverse(wt.matrix));
                    const bool isSelected = (e == selected);
                    const glm::vec4 color = isSelected
                        ? glm::vec4{1.f,  0.9f, 0.2f, 1.f}   // selected: yellow
                        : glm::vec4{0.55f, 0.6f, 0.9f, 0.5f}; // others: dim blue
                    dd.DrawFrustum(ivp, color);
                });
        }
    }

    // ── Light wireframes ──────────────────────────────────────────────────────
    if (m_overlaySettings.drawPointLightRange) {
        const uint32_t sw = m_app->GetVulkanDevice().GetSwapchainWidth();
        const uint32_t sh = m_app->GetVulkanDevice().GetSwapchainHeight();
        if (sw > 0 && sh > 0) {
            const float     aspect   = static_cast<float>(sw) / static_cast<float>(sh);
            const glm::mat4 view     = m_camera.GetCameraData(aspect).view;
            const glm::vec3 camRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
            const glm::vec3 camUp    = glm::vec3(view[0][1], view[1][1], view[2][1]);
            const glm::vec4 color    = { 1.f, 0.7f, 0.2f, 0.6f };
            constexpr int   N        = 64;
            constexpr float kTwoPi   = 6.28318530f;
            scene.View<PointLightComponent, WorldTransformComponent>().each(
                [&](entt::entity, const PointLightComponent& pl, const WorldTransformComponent& wt) {
                    const glm::vec3 pos = glm::vec3(wt.matrix[3]);
                    for (int i = 0; i < N; ++i) {
                        const float     a0 = static_cast<float>(i)     / N * kTwoPi;
                        const float     a1 = static_cast<float>(i + 1) / N * kTwoPi;
                        const glm::vec3 p0 = pos + (camRight * std::cos(a0) + camUp * std::sin(a0)) * pl.range;
                        const glm::vec3 p1 = pos + (camRight * std::cos(a1) + camUp * std::sin(a1)) * pl.range;
                        dd.DrawLine(p0, p1, color);
                    }
                });
        }
    }

    if (m_overlaySettings.drawSpotLightCone) {
        const glm::vec4 color = { 1.f, 0.9f, 0.2f, 0.7f };
        scene.View<SpotLightComponent, WorldTransformComponent>().each(
            [&](entt::entity, const SpotLightComponent& sl, const WorldTransformComponent& wt) {
                const glm::vec3 pos = glm::vec3(wt.matrix[3]);
                const glm::vec3 fwd = glm::normalize(-glm::vec3(wt.matrix[2]));
                const glm::vec3 tmp = std::abs(fwd.y) < 0.99f
                                      ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
                const glm::vec3 right  = glm::normalize(glm::cross(fwd, tmp));
                const glm::vec3 up2    = glm::cross(right, fwd);
                const float     baseR  = sl.range * std::tan(sl.outerAngle);
                const glm::vec3 baseC  = pos + fwd * sl.range;
                constexpr int   N      = 32;
                constexpr float kTwoPi = 6.28318530f;
                for (int i = 0; i < N; ++i) {
                    const float a0 = static_cast<float>(i)     / N * kTwoPi;
                    const float a1 = static_cast<float>(i + 1) / N * kTwoPi;
                    const glm::vec3 p0 = baseC + (right * std::cos(a0) + up2 * std::sin(a0)) * baseR;
                    const glm::vec3 p1 = baseC + (right * std::cos(a1) + up2 * std::sin(a1)) * baseR;
                    dd.DrawLine(p0, p1, color);
                }
                for (int i = 0; i < 4; ++i) {
                    const float     a = static_cast<float>(i) * (kTwoPi / 4.f);
                    const glm::vec3 p = baseC + (right * std::cos(a) + up2 * std::sin(a)) * baseR;
                    dd.DrawLine(pos, p, color);
                }
            });
    }

    if (m_overlaySettings.drawAreaLightRect) {
        const glm::vec4 color = { 0.4f, 0.9f, 1.f, 0.7f };
        scene.View<AreaLightComponent, WorldTransformComponent>().each(
            [&](entt::entity, const AreaLightComponent& al, const WorldTransformComponent& wt) {
                const glm::vec3 pos = glm::vec3(wt.matrix[3]);
                const glm::vec3 hw  = glm::normalize(glm::vec3(wt.matrix[0])) * (al.size.x * 0.5f);
                const glm::vec3 hh  = glm::normalize(glm::vec3(wt.matrix[2])) * (al.size.y * 0.5f);
                const glm::vec3 corners[4] = {
                    pos - hw - hh, pos + hw - hh, pos + hw + hh, pos - hw + hh
                };
                for (int i = 0; i < 4; ++i)
                    dd.DrawLine(corners[i], corners[(i + 1) % 4], color);
                const glm::vec3 nrm = glm::normalize(glm::vec3(wt.matrix[1]));
                dd.DrawArrow(pos, pos + nrm * 0.5f, color, 0.08f);
            });
    }

    if (m_overlaySettings.drawDirectionalLightDir) {
        const glm::vec4 color = { 0.6f, 0.8f, 1.f, 0.9f };
        scene.View<DirectionalLightComponent, WorldTransformComponent>().each(
            [&](entt::entity, const DirectionalLightComponent&, const WorldTransformComponent& wt) {
                const glm::vec3 pos = glm::vec3(wt.matrix[3]);
                const glm::vec3 dir = glm::normalize(-glm::vec3(wt.matrix[2]));
                dd.DrawArrow(pos, pos + dir * 2.f, color, 0.15f);
            });
    }

    // ── Selection-dependent overlays ──────────────────────────────────────────

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
    m_currentScenePath = path;
    SA_LOG_INFO("EditorMode: loaded scene '{}'", path.string());
    m_app->GetRenderer().ApplyWorldSettings(scene.GetWorldSettings());
    m_app->PrepareAnimatedEntities();
    m_app->RebuildDrawList();
}

void EditorMode::LoadProject(const fs::path& saprojectPath) {
    if (m_app->GetPlayState() != EnginePlayState::Editing) {
        SA_LOG_WARN("EditorMode::LoadProject — cannot switch project while not in Editing state");
        return;
    }

    const fs::path projectDir = saprojectPath.parent_path();
    const fs::path cookCacheDir = projectDir / "cook_cache";

    // Clear scene first
    Scene& scene = m_app->GetScene();
    scene.Clear();
    m_currentScenePath.clear();

    // ── Cook project .saglsl shading models (filesystem phase) ───────────────
    // Runs before GPU teardown; outputs land in cookCacheDir/shaders/.
    std::string cookedShaderDir;
    {
        const fs::path assetsDir = projectDir / "assets";
        if (ShaderCook::HasSaglslFiles(assetsDir)) {
            const fs::path spvOut      = cookCacheDir / "shaders";
            const fs::path dispatchOut = cookCacheDir / "generated" / "shaders";

#ifdef _WIN32
            const std::string exeSuffix = ".exe";
#else
            const std::string exeSuffix;
#endif
            ShaderCook::CookConfig cookCfg;
            cookCfg.glslcPath    = StellarAliaApp::GLSLC_PATH;
            cookCfg.reflToolPath = std::string(StellarAliaApp::BIN_DIR) + "/ShaderReflectTool" + exeSuffix;
            cookCfg.includePaths = { StellarAliaApp::ENGINE_SHADER_SRC_DIR, dispatchOut.string() };

            const auto cookResult = ShaderCook::CookDirectory(assetsDir, spvOut, dispatchOut, cookCfg);
            if (!cookResult.failedModels.empty()) {
                SA_LOG_WARN("EditorMode: {} shading model(s) failed to cook",
                            cookResult.failedModels.size());
            }

            if (cookResult.modelCount > 0) {
                // Recompile deferred_lighting.frag with the project dispatch.
                const fs::path fragSrc = fs::path(StellarAliaApp::ENGINE_SHADER_SRC_DIR)
                                         / "deferred_lighting.frag";
                const fs::path outSpv  = spvOut / "deferred_lighting.frag.spv";
                ShaderCook::RecompileDeferredLighting(fragSrc, dispatchOut, outSpv,
                                                       cookCfg.glslcPath,
                                                       { StellarAliaApp::ENGINE_SHADER_SRC_DIR });
            }

            cookedShaderDir = spvOut.string();
        }
    }

    // Update Application paths
    m_app->UpdateProjectPaths(projectDir, cookCacheDir);

    m_app->GetResourceManager().ClearProjectAssets();
    m_app->GetMaterialManager().ClearProjectInstances();
    m_app->GetRenderer().ResetProjectIBL();

    // ── GPU hot-swap: replace project shader types (GPU is idle by now) ───────
    m_app->GetRenderer().ApplyProjectShaderTypes(cookedShaderDir);

    m_diagnostics.ClearSource(DiagSource::Runtime);

    // Rescan asset registry
    m_assetRegistry->Scan(projectDir / "assets", m_app->GetDesc().engineAssetsDir);
    SA_LOG_INFO("EditorMode: asset registry rescanned ({} assets)", m_assetRegistry->Count());

    // Update AssetsPanel root
    if (m_assetsPanel)
        m_assetsPanel->SetProjectDir(projectDir / "assets");

    // Load startup scene
    SaProject proj;
    if (LoadSaProject(saprojectPath, proj) && !proj.startupScene.empty()) {
        const fs::path scenePath = projectDir / proj.startupScene;
        if (SceneSerializer::LoadFromFile(scene, scenePath)) {
            m_currentScenePath = scenePath;
            SA_LOG_INFO("EditorMode: loaded startup scene '{}'", scenePath.string());
        } else {
            SA_LOG_WARN("EditorMode: could not load startup scene '{}'", scenePath.string());
        }
    }

    m_app->GetRenderer().ApplyWorldSettings(scene.GetWorldSettings());
    m_app->PrepareAnimatedEntities();
    m_app->RebuildDrawList();

    // Update recent projects
    m_projectManager.AddRecent(proj.name.empty() ? saprojectPath.stem().string() : proj.name,
                                saprojectPath);
    m_projectManager.SaveRecents(m_recentsConfigPath);

    // Warn if cook_cache is empty but project has uncooked assets
    {
        const fs::path assetsDir = projectDir / "assets";
        bool cookEmpty = true;
        {
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(cookCacheDir, ec))
                if (e.path().filename() != ".gitkeep") { cookEmpty = false; break; }
        }
        bool hasAssets = false;
        {
            std::error_code ec;
            for (const auto& e : fs::recursive_directory_iterator(assetsDir, ec))
                if (e.path().extension() == ".sameta") { hasAssets = true; break; }
        }
        if (cookEmpty && hasAssets) {
            SA_LOG_WARN("EditorMode: project cook cache is empty — run Reimport All to cook project assets");
            m_diagnostics.Push({DiagLevel::Warning, DiagSource::Runtime,
                "Project cook cache is empty — run \"Reimport All\" to cook project assets.", {}});
        }
    }

    SA_LOG_INFO("EditorMode: switched to project '{}'", projectDir.string());
}

void EditorMode::NewScene() {
    Scene& scene = m_app->GetScene();
    scene.Clear();
    m_currentScenePath.clear();
    const fs::path tmpl = m_templateRegistry.DefaultScenePath();
    if (!tmpl.empty())
        (void)SceneSerializer::LoadFromFile(scene, tmpl);
    m_app->GetRenderer().ApplyWorldSettings(scene.GetWorldSettings());
    m_app->PrepareAnimatedEntities();
    m_app->RebuildDrawList();
    SA_LOG_INFO("EditorMode: new scene");
}

void EditorMode::SaveScene() {
    if (m_currentScenePath.empty()) {
        SA_LOG_WARN("EditorMode: no scene path — use Save As (not yet implemented)");
        return;
    }
    if (SceneSerializer::SaveToFile(m_app->GetScene(), m_currentScenePath))
        SA_LOG_INFO("EditorMode: saved scene '{}'", m_currentScenePath.string());
    else
        SA_LOG_ERROR("EditorMode: failed to save scene '{}'", m_currentScenePath.string());
}

void EditorMode::CookProjectShaders() {
    const auto& desc = m_app->GetDesc();
    if (desc.projectDir.empty()) {
        SA_LOG_WARN("EditorMode::CookProjectShaders — no project dir set");
        return;
    }

    const fs::path binDir   = fs::path(StellarAliaApp::BIN_DIR);
#ifdef _WIN32
    const fs::path cookExe  = binDir / "StellarAliaShaderCook.exe";
    const fs::path reflExe  = binDir / "ShaderReflectTool.exe";
#else
    const fs::path cookExe  = binDir / "StellarAliaShaderCook";
    const fs::path reflExe  = binDir / "ShaderReflectTool";
#endif

    if (!fs::exists(cookExe)) {
        SA_LOG_WARN("EditorMode::CookProjectShaders — tool not found: '{}'",
                    cookExe.string());
        return;
    }

    const std::string scanDir     = (fs::path(desc.projectDir) / "assets").generic_string();
    const std::string spvOut      = fs::path(StellarAliaApp::BUILTIN_SHADER_DIR).generic_string();
    const std::string dispatchOut = fs::path(StellarAliaApp::SHADER_DISPATCH_DIR).generic_string();
    const std::string glslcPath   = fs::path(StellarAliaApp::GLSLC_PATH).generic_string();
    const std::string incEng      = (fs::path(StellarAliaApp::ASSETS_DIR) / "shaders").generic_string();

    std::string cmd =
        "\"" + cookExe.generic_string() + "\""
        " --scan-dir \""     + scanDir     + "\""
        " --spv-out \""      + spvOut      + "\""
        " --dispatch-out \"" + dispatchOut + "\""
        " --glslc \""        + glslcPath   + "\""
        " --reflect-tool \"" + reflExe.generic_string() + "\""
        " --include \""      + incEng      + "\""
        " --include \""      + dispatchOut + "\""
        " --force";

#ifdef _WIN32
    cmd = "\"" + cmd + "\""; // cmd.exe outer-quote trick
#endif

    SA_LOG_INFO("EditorMode: cooking project shaders...");
    m_diagnostics.ClearSource(DiagSource::ShaderCook);
    const int ret = std::system(cmd.c_str());

    // Read the per-shader error manifest written by the cook tool.
    // Each line is the generic path of a .saglsl that failed to compile.
    const fs::path errorManifest = fs::path(dispatchOut) / "cook_errors.txt";
    bool manifestRead = false;
    if (fs::exists(errorManifest)) {
        std::ifstream mf(errorManifest);
        std::string line;
        while (std::getline(mf, line)) {
            if (line.empty()) continue;
            fs::path shaderPath(line);
            m_diagnostics.Push({DiagLevel::Error, DiagSource::ShaderCook,
                "Shader compile error: " + shaderPath.filename().string()
                + " — check GLSL syntax and reimport",
                shaderPath});
            manifestRead = true;
        }
    }

    if (ret != 0) {
        SA_LOG_ERROR("EditorMode::CookProjectShaders — tool exited with {}", ret);
        if (!manifestRead) {
            // No manifest (parse error or tool crash before compile) — generic fallback.
            m_diagnostics.Push({DiagLevel::Error, DiagSource::ShaderCook,
                "Shader cook failed (exit " + std::to_string(ret) +
                ") — check engine log for details", {}});
        }
    } else {
        SA_LOG_INFO("EditorMode: shader cook complete");
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

void EditorMode::DrawBillboardIcons() {
    if (!m_overlaySettings.enabled || !m_overlaySettings.drawEntityIcons) return;
    if (!m_iconCache) return;

    ImFont* iconFont = m_ui.GetIconFont();
    if (!iconFont) return;

    const uint32_t sw = m_app->GetVulkanDevice().GetSwapchainWidth();
    const uint32_t sh = m_app->GetVulkanDevice().GetSwapchainHeight();
    if (sw == 0 || sh == 0) return;

    const float w    = static_cast<float>(sw);
    const float h    = static_cast<float>(sh);
    const float half = m_overlaySettings.billboardIconSize * 0.5f;

    const float aspect  = w / h;
    const auto camData  = m_camera.GetCameraData(aspect);
    const glm::mat4 vp  = camData.proj * camData.view;

    const entt::entity selected = m_hierarchyPanel
        ? static_cast<entt::entity>(m_hierarchyPanel->GetSelectedEntity())
        : entt::null;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    m_billboardHits.clear();
    auto project = [&](entt::entity e, glm::vec3 worldPos, const char* glyph, bool isSelected) {
        const glm::vec4 clip = vp * glm::vec4(worldPos, 1.f);
        if (clip.w <= 0.001f) return;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < 0.f || ndc.z > 1.f) return;
        const float sx = (ndc.x * 0.5f + 0.5f) * w;
        const float sy = (ndc.y * 0.5f + 0.5f) * h;
        const ImU32 col = isSelected
            ? IM_COL32(255, 230, 80, 255)
            : IM_COL32(220, 220, 220, 220);
        dl->AddText(iconFont, m_overlaySettings.billboardIconSize,
                    {sx - half, sy - half}, col, glyph);
        m_billboardHits.push_back(BillboardHit{e, ImVec2{sx, sy}});
    };

    auto& reg = m_app->GetScene().Registry();

    reg.view<WorldTransformComponent, DirectionalLightComponent>().each(
        [&](entt::entity e, const WorldTransformComponent& wt, const DirectionalLightComponent&) {
            project(e, glm::vec3(wt.matrix[3]), FA_ICON_LIGHT, e == selected);
        });
    reg.view<WorldTransformComponent, PointLightComponent>().each(
        [&](entt::entity e, const WorldTransformComponent& wt, const PointLightComponent&) {
            project(e, glm::vec3(wt.matrix[3]), FA_ICON_LIGHT, e == selected);
        });
    reg.view<WorldTransformComponent, SpotLightComponent>().each(
        [&](entt::entity e, const WorldTransformComponent& wt, const SpotLightComponent&) {
            project(e, glm::vec3(wt.matrix[3]), FA_ICON_LIGHT, e == selected);
        });
    reg.view<WorldTransformComponent, AreaLightComponent>().each(
        [&](entt::entity e, const WorldTransformComponent& wt, const AreaLightComponent&) {
            project(e, glm::vec3(wt.matrix[3]), FA_ICON_LIGHT, e == selected);
        });
    reg.view<WorldTransformComponent, CameraComponent>().each(
        [&](entt::entity e, const WorldTransformComponent& wt, const CameraComponent&) {
            project(e, glm::vec3(wt.matrix[3]), FA_ICON_CAMERA, e == selected);
        });
}

void EditorMode::DrawImGuizmo() {
#ifdef SA_HAS_IMGUIZMO
    if (!m_overlaySettings.enabled || !m_overlaySettings.drawGizmo) {
        m_gizmoIsUsing = false;
        return;
    }

    const entt::entity selected = m_hierarchyPanel
        ? static_cast<entt::entity>(m_hierarchyPanel->GetSelectedEntity())
        : entt::null;
    if (selected == entt::null) { m_gizmoIsUsing = false; return; }

    Scene& scene = m_app->GetScene();
    auto&  reg   = scene.Registry();

    const auto* tc  = reg.try_get<TransformComponent>(selected);
    const auto* wtc = reg.try_get<WorldTransformComponent>(selected);
    if (!tc || !wtc) { m_gizmoIsUsing = false; return; }

    const uint32_t sw = m_app->GetVulkanDevice().GetSwapchainWidth();
    const uint32_t sh = m_app->GetVulkanDevice().GetSwapchainHeight();
    if (sw == 0 || sh == 0) return;

    const float aspect = static_cast<float>(sw) / static_cast<float>(sh);

    // View matrix (same formula as renderer, no Vulkan Y-flip needed for ImGuizmo)
    const CameraData camData = m_camera.GetCameraData(aspect);
    const glm::mat4  proj    = glm::perspective(m_camera.fovY, aspect,
                                                m_camera.nearPlane, m_camera.farPlane);

    // Map GizmoMode → ImGuizmo operation; scale is always LOCAL
    ImGuizmo::OPERATION op;
    switch (m_overlaySettings.gizmoMode) {
        case GizmoMode::Translate: op = ImGuizmo::TRANSLATE; break;
        case GizmoMode::Rotate:    op = ImGuizmo::ROTATE;    break;
        case GizmoMode::Scale:     op = ImGuizmo::SCALE;     break;
    }
    const ImGuizmo::MODE mode = (m_overlaySettings.gizmoMode == GizmoMode::Scale)
        ? ImGuizmo::LOCAL
        : (m_overlaySettings.gizmoWorldSpace ? ImGuizmo::WORLD : ImGuizmo::LOCAL);

    float viewArr[16], projArr[16], matArr[16];
    std::memcpy(viewArr, glm::value_ptr(camData.view),  sizeof(viewArr));
    std::memcpy(projArr, glm::value_ptr(proj),          sizeof(projArr));
    std::memcpy(matArr,  glm::value_ptr(wtc->matrix),   sizeof(matArr));

    const ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.f, 0.f, io.DisplaySize.x, io.DisplaySize.y);

    const bool wasUsing = m_gizmoIsUsing;
    const bool changed  = ImGuizmo::Manipulate(viewArr, projArr, op, mode, matArr);
    m_gizmoIsUsing = ImGuizmo::IsUsing();
    if (wasUsing && !m_gizmoIsUsing)
        scene.MarkMaterialDirty();

    if (changed) {
        // Convert manipulated world matrix back to local space
        glm::mat4 newWorld;
        std::memcpy(glm::value_ptr(newWorld), matArr, sizeof(matArr));

        glm::mat4 newLocal = newWorld;
        const auto* hc = reg.try_get<HierarchyComponent>(selected);
        if (hc && hc->parent != entt::null) {
            const auto* parentWtc = reg.try_get<WorldTransformComponent>(hc->parent);
            if (parentWtc)
                newLocal = glm::inverse(parentWtc->matrix) * newWorld;
        }

        float t[3], r[3], s[3];
        float localArr[16];
        std::memcpy(localArr, glm::value_ptr(newLocal), sizeof(localArr));
        ImGuizmo::DecomposeMatrixToComponents(localArr, t, r, s);

        auto* mutableTc = reg.try_get<TransformComponent>(selected);
        mutableTc->position = { t[0], t[1], t[2] };
        mutableTc->rotation = glm::quat(glm::radians(glm::vec3(r[0], r[1], r[2])));
        mutableTc->scale    = { s[0], s[1], s[2] };
        scene.MarkDirty(selected);
    }
#else
    m_gizmoIsUsing = false;
#endif
}

// ── Viewport interaction (picking + asset drop) ───────────────────────────────

void EditorMode::HandleViewportInteraction() {
    if (m_app->GetPlayState() != EnginePlayState::Editing) return;

    const uint32_t sw = m_app->GetVulkanDevice().GetSwapchainWidth();
    const uint32_t sh = m_app->GetVulkanDevice().GetSwapchainHeight();
    if (sw == 0 || sh == 0) return;

    const ImGuiIO& io = ImGui::GetIO();

    // Transparent full-screen window behind all panels — receives drag-drop payloads
    // and left-click picking when no UI panel is under the cursor.
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize({static_cast<float>(sw), static_cast<float>(sh)});
    ImGui::SetNextWindowBgAlpha(0.f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration          |
        ImGuiWindowFlags_NoNav                 |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing    |
        ImGuiWindowFlags_NoDocking             |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##viewport_interact", nullptr, kFlags);

    // Tell ImGuizmo to treat this overlay as an acceptable hover target.
    // Without this, IsHoveringWindow() returns false (any non-gizmo window blocks it),
    // setting mbMouseOver=false and disabling all gizmo handle hit-tests.
#ifdef SA_HAS_IMGUIZMO
    ImGuizmo::SetAlternativeWindow(ImGui::GetCurrentWindow());
#endif

    // ── Asset drop from AssetsPanel ───────────────────────────────────────────
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAASSET")) {
            if (m_hierarchyPanel) {
                fs::path assetPath(static_cast<const char*>(p->Data));
                std::string ext = assetPath.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c){ return static_cast<char>(::tolower(c)); });
                if (ext == ".glb" || ext == ".gltf") {
                    glm::vec3 spawnPos(0.f);
                    const Core::Ray ray = ScreenToWorldRay(io.MousePos.x, io.MousePos.y);
                    if (!RayHitHorizontalPlane(ray, 0.f, spawnPos))
                        spawnPos = ray.origin + ray.dir * 10.f; // fallback: 10 units in front
                    m_hierarchyPanel->TriggerAssetDrop(assetPath, spawnPos);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // ── Left-click entity picking ─────────────────────────────────────────────
    if (!m_gizmoIsUsing &&
#ifdef SA_HAS_IMGUIZMO
        !ImGuizmo::IsOver() &&
#endif
        ImGui::IsWindowHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // Billboard icons (lights / cameras) are checked first — they have no mesh.
        entt::entity hit = entt::null;
        if (!m_billboardHits.empty()) {
            const float radius = m_overlaySettings.billboardIconSize * 0.5f;
            float bestDist = radius;
            for (const auto& bh : m_billboardHits) {
                const float dx = io.MousePos.x - bh.screenPos.x;
                const float dy = io.MousePos.y - bh.screenPos.y;
                const float d  = std::sqrt(dx * dx + dy * dy);
                if (d < bestDist) { bestDist = d; hit = bh.entity; }
            }
        }
        // Fall back to geometry raycast when no billboard was hit.
        if (hit == entt::null) {
            const Core::Ray ray = ScreenToWorldRay(io.MousePos.x, io.MousePos.y);
            hit = m_app->GetRenderer().RaycastScene(ray);
        }
        if (m_hierarchyPanel) {
            if (hit != entt::null)
                m_hierarchyPanel->SetSelection(hit);
            else
                m_hierarchyPanel->ClearSelection();
        }
    }

    ImGui::End();
}

Core::Ray EditorMode::ScreenToWorldRay(float sx, float sy) const {
    const uint32_t sw = m_app->GetVulkanDevice().GetSwapchainWidth();
    const uint32_t sh = m_app->GetVulkanDevice().GetSwapchainHeight();
    if (sw == 0 || sh == 0)
        return Core::Ray::FromOriginDir({}, {0.f, 0.f, -1.f});

    const float aspect = static_cast<float>(sw) / static_cast<float>(sh);
    const CameraData cam = m_camera.GetCameraData(aspect);

    // cam.proj has Vulkan Y-flip (proj[1][1]*=-1) already applied,
    // so ndcY = (sy/sh)*2−1 directly maps top→-1, bottom→+1.
    const float ndcX = (sx / static_cast<float>(sw)) * 2.f - 1.f;
    const float ndcY = (sy / static_cast<float>(sh)) * 2.f - 1.f;

    const glm::mat4 invProjView = glm::inverse(cam.proj * cam.view);
    const glm::vec4 near4 = invProjView * glm::vec4(ndcX, ndcY, 0.f, 1.f);
    const glm::vec4 far4  = invProjView * glm::vec4(ndcX, ndcY, 1.f, 1.f);
    const glm::vec3 nearPt = glm::vec3(near4) / near4.w;
    const glm::vec3 farPt  = glm::vec3(far4)  / far4.w;

    return Core::Ray::FromOriginDir(cam.worldPosition, glm::normalize(farPt - nearPt));
}

bool EditorMode::RayHitHorizontalPlane(const Core::Ray& ray, float planeY, glm::vec3& outHit) {
    if (std::abs(ray.dir.y) < 1e-6f) return false;
    const float t = (planeY - ray.origin.y) / ray.dir.y;
    if (t < 0.f) return false;
    outHit = ray.origin + t * ray.dir;
    return true;
}

} // namespace StellarAlia::Editor
