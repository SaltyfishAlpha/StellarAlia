// InputDemo
//
// Demonstrates the three-layer action-mapped input system (Stage 7.1).
//
// Controls (Gameplay map):
//   WASD / Left stick      — Move camera (horizontal plane)
//   Mouse / Right stick    — Look (yaw + pitch)
//   Left Shift / L Bumper  — Sprint (3× speed)
//   Space / Button South   — Jump  (edge: WasActivated — logged once per press)
//   Escape / Start         — Push "UI" map (blocks Gameplay)
//
// Controls (UI map):
//   Arrow keys / Left stick / D-Pad  — Navigate
//   Return / Button South            — Submit (pops UI map, returns to Gameplay)
//   Escape / Button East             — Cancel (same)
//
// The active device family (KeyboardMouse / Gamepad) is printed whenever it
// changes. Camera movement is reflected in a rotating IBL skybox if cooked
// assets are available in the cook cache; otherwise the scene renders empty.

#include "core/logs/Log.hpp"
#include "core/asset/AssetID.hpp"
#include "function/input/InputSystem.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/EntityFactory.hpp"
#include "function/scene/Scene.hpp"
#include "platform/input/GLFWInputProvider.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "camera/EditorCamera.hpp"
#include "input/EditorInputMaps.hpp"
#include "InputDemoPath.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/glm.hpp>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;
using namespace StellarAlia::Editor;
using Clock = std::chrono::steady_clock;

// ── IBL asset UUIDs (grasslands_sunset_4k — shared cook cache) ───────────────

namespace IBL {
    const AssetID BoomBoxWithAxes = AssetID::FromString("92b09700-4b40-4ca7-8502-627b2b53a4af");
    
    const AssetID SkyboxHdr      = AssetID::FromString("7a735f5c-b3c4-4361-b342-699c816fa64d");
    const AssetID SH9            = AssetID::FromString("79735f5c-b3c4-4362-b342-699c816fa64e");
    const AssetID PrefilteredEnv = AssetID::FromString("78735f5c-b3c4-4363-b142-699c816fa64f");
    const AssetID BrdfLut        = AssetID::FromString("c5b06992-5a8f-4dc9-9d11-406e12b969d4");
    const AssetID SkyboxCubemap  = AssetID::FromString("7e735f5c-b3c4-4365-b342-699c816fa649");
}


// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("InputDemo: starting");

    const fs::path cookDir      = InputDemo::COOK_CACHE_DIR;
    const std::string shaderDir = InputDemo::BUILTIN_SHADER_DIR;
    fs::create_directories(cookDir);

    // ── Window ────────────────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "Input Demo — WASD + Mouse"});

    // ── Input system ──────────────────────────────────────────────────────────
    GLFWInputProvider provider{ static_cast<GLFWwindow*>(window->GetNativeHandle()) };
    InputSystem input;
    input.Init(&provider);
    input.RegisterMaps(MakeViewportMaps());
    input.PushMap("Viewport");
    SA_LOG_INFO("InputDemo: Viewport map active");
    SA_LOG_INFO("  WASD/LeftStick = Move    RMB+Mouse/RightStick = Look");
    SA_LOG_INFO("  LeftShift/LB = Sprint    Escape/Start = open UI map");

    // ── Vulkan device ─────────────────────────────────────────────────────────
    RHIDeviceDesc devDesc{};
    devDesc.windowHandle    = NativeWindowHandle{ window->GetNativeHandle() };
    devDesc.swapchainWidth  = window->GetWidth();
    devDesc.swapchainHeight = window->GetHeight();
    devDesc.vsync           = true;
    devDesc.enableValidation = false;  // quieter output while testing input
    auto device = VulkanDevice::Create(devDesc);

    // ── Resource / Material managers ──────────────────────────────────────────
    ResourceManager resMgr;
    resMgr.Init(cookDir.string(), device.get());

    MaterialManager matMgr;
    matMgr.Init(device.get(), &resMgr);

    // ── SceneRenderer (shadow and bloom off — nothing to shadow/bloom) ────────
    SceneRenderer renderer;
    SceneRenderer::Desc rendDesc{};
    rendDesc.device       = device.get();
    rendDesc.matMgr       = &matMgr;
    rendDesc.resMgr       = &resMgr;
    rendDesc.shaderDir    = shaderDir;
    rendDesc.cookCacheDir = cookDir.string();
    rendDesc.config.shadowEnabled  = false;

    if (!renderer.Init(rendDesc)) {
        SA_LOG_CRITICAL("InputDemo: renderer init failed");
        return 1;
    }

    // ── Scene: sun only — camera is driven by EditorCamera, not a scene entity ──
    Scene scene("InputDemo");
    scene.GetWorldSettings().pp.bloomEnabled = false;

    EntityFactory::CreateDirectionalLight(scene, "Sun",
        { 1.f, 0.95f, 0.88f }, 1.5f,
        glm::normalize(
            glm::angleAxis(glm::radians(30.f),  glm::vec3{0, 1, 0}) *
            glm::angleAxis(glm::radians(-50.f), glm::vec3{1, 0, 0})),
        /*castShadow=*/false);


    // BoomBoxWithAxes — cooked from assets/models/BoomBoxWithAxes/BoomBoxWithAxes.gltf
    // The model is real-world scale (~0.1 m); scale up for visibility.
    // materialSlots is empty: submesh defaultMaterialIDs from the cook are used.
    EntityFactory::CreateStaticMesh(scene, "BoomBoxWithAxes",
        IBL::BoomBoxWithAxes,
        /*position*/ {0.f, 0.f, 0.f},
        /*rotation*/ glm::angleAxis(glm::radians(20.f), glm::vec3{0.f, 1.f, 0.f}),
        /*scale*/    {50.f, 50.f, 50.f});

    // ── IBL (optional — skips gracefully if cook cache empty) ────────────────
    {
        auto& ws          = scene.GetWorldSettings();
        ws.skyboxHdr      = IBL::SkyboxHdr;
        ws.sh9            = IBL::SH9;
        ws.prefilteredEnv = IBL::PrefilteredEnv;
        ws.brdfLut        = IBL::BrdfLut;
        ws.skyboxCubemap  = IBL::SkyboxCubemap;

        if (!renderer.SetIBL(scene.GetWorldSettings()))
            SA_LOG_WARN("InputDemo: IBL unavailable — rendering without skybox");
    }

    scene.UpdateTransforms();
    renderer.BuildDrawList(scene);

    // ── State ─────────────────────────────────────────────────────────────────
    EditorCamera    cam;
    DeviceFamily    lastFamily  = DeviceFamily::KeyboardMouse;
    bool            uiOpen      = false;
    auto            lastTime    = Clock::now();

    // ── Render loop ───────────────────────────────────────────────────────────
    while (!window->ShouldClose()) {
        // Time delta
        const auto  now  = Clock::now();
        const float dt   = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // Input — order: PollEvents first (fires scroll/callbacks), then Poll().
        window->PollEvents();
        input.Poll();

        // ── Device family change notification ─────────────────────────────────
        const DeviceFamily family = input.ActiveFamily();
        if (family != lastFamily) {
            lastFamily = family;
            SA_LOG_INFO("InputDemo: active device → {}",
                family == DeviceFamily::Gamepad ? "Gamepad" : "Keyboard + Mouse");
        }

        // ── Map switching ─────────────────────────────────────────────────────
        if (!uiOpen && input.WasActivated("ToggleUI")) {
            input.PushMap("UI");
            uiOpen = true;
            SA_LOG_INFO("InputDemo: UI map pushed  (Submit/Cancel to return)");
        }
        if (uiOpen) {
            if (input.WasActivated("Submit") || input.WasActivated("Cancel")) {
                input.PopMap();
                uiOpen = false;
                SA_LOG_INFO("InputDemo: UI map popped  (Gameplay resumed)");
            }
            const auto nav = input.ReadVec2("Navigate");
            if (glm::length(nav) > 0.1f)
                SA_LOG_INFO("  Navigate: ({:.2f}, {:.2f})", nav.x, nav.y);
        }

        // ── Gameplay actions ──────────────────────────────────────────────────
        if (!uiOpen) {
            if (input.WasActivated("Jump"))
                SA_LOG_INFO("InputDemo: Jump!");

            // Right-mouse-button activates look + cursor wrap (Blender style).
            // Gamepad: always in look mode (no button required).
            const bool mouseLook = input.IsActive("MouseLook") ||
                                   input.ActiveFamily() == DeviceFamily::Gamepad;
            if (input.WasActivated("MouseLook"))
                provider.SetCursorCapture(true);
            else if (input.WasDeactivated("MouseLook"))
                provider.SetCursorCapture(false);

            cam.Update(input, dt, mouseLook);
        }

        // ── Render ────────────────────────────────────────────────────────────
        const int w = static_cast<int>(device->GetSwapchainWidth());
        const int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        scene.UpdateTransforms();
        renderer.RenderFrame(scene, cam.GetCameraData(aspect),
                             static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    device->WaitIdle();
    renderer.Shutdown();
    input.Shutdown();
    matMgr.Shutdown();
    resMgr.Shutdown();
    device.reset();
    window.reset();

    SA_LOG_INFO("InputDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
