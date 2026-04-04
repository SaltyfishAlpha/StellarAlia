// Sandbox
//
// Scene built entirely in code via EntityFactory (no .sascene file):
//   MainCamera      — perspective, z=0.6 looking at origin
//   Sun             — directional light, warm, castShadow=true
//   BoomBoxWithAxes — PBR glTF model (assets/models/BoomBoxWithAxes/)
//
// IBL: assets/hdri/grasslands_sunset_4k.hdr
//   Offline products are read from the cook cache on subsequent runs;
//   on first run the renderer GPU-bakes and caches them automatically.
//
// Asset UUIDs are stable — assigned by the cook importer and stored in
// *.sameta sidecar files next to each source asset.

#include "core/logs/Log.hpp"
#include "core/asset/AssetID.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/EntityFactory.hpp"
#include "function/scene/Scene.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "SandboxPath.hpp"

#include <glm/gtc/quaternion.hpp>

#include <filesystem>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

// ─── Asset UUIDs (from *.sameta sidecar files) ───────────────────────────────

namespace Assets {
    // assets/models/BoomBoxWithAxes/BoomBoxWithAxes.gltf.sameta
    const AssetID BoomBoxWithAxes = AssetID::FromString("92b09700-4b40-4ca7-8502-627b2b53a4af");
    const AssetID EmissiveStrengthTest = AssetID::FromString("45af8e08-fbf7-4fc0-b28f-c66be7f435a3");

    // IBL — assets/hdri/grasslands_sunset_4k.hdr.sameta + bake-product UUIDs
    // (same UUIDs as metal_rough_spheres.sascene; bake cache is shared)
    const AssetID SkyboxHdr      = AssetID::FromString("7a735f5c-b3c4-4361-b342-699c816fa64d");
    const AssetID SH9            = AssetID::FromString("79735f5c-b3c4-4362-b342-699c816fa64e");
    const AssetID PrefilteredEnv = AssetID::FromString("78735f5c-b3c4-4363-b142-699c816fa64f");
    const AssetID BrdfLut        = AssetID::FromString("c5b06992-5a8f-4dc9-9d11-406e12b969d4");
    const AssetID SkyboxCubemap  = AssetID::FromString("7e735f5c-b3c4-4365-b342-699c816fa649");
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("Sandbox: starting");

    const fs::path cookDir      = Sandbox::COOK_CACHE_DIR;
    const std::string shaderDir = Sandbox::BUILTIN_SHADER_DIR;

    fs::create_directories(cookDir);

    // ── Window + device ───────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "Sandbox — BoomBoxWithAxes"});

    RHIDeviceDesc devDesc{};
    devDesc.windowHandle     = NativeWindowHandle{window->GetNativeHandle()};
    devDesc.swapchainWidth   = window->GetWidth();
    devDesc.swapchainHeight  = window->GetHeight();
    devDesc.vsync            = true;
    devDesc.enableValidation = true;
    auto device = VulkanDevice::Create(devDesc);

    // ── ResourceManager + MaterialManager ────────────────────────────────────
    ResourceManager resMgr;
    resMgr.Init(cookDir.string(), device.get());

    MaterialManager matMgr;
    matMgr.Init(device.get(), &resMgr);

    // ── SceneRenderer ─────────────────────────────────────────────────────────
    SceneRenderer renderer;

    SceneRenderer::Desc rendDesc{};
    rendDesc.device       = device.get();
    rendDesc.matMgr       = &matMgr;
    rendDesc.resMgr       = &resMgr;
    rendDesc.shaderDir    = shaderDir;
    rendDesc.cookCacheDir = cookDir.string();
    rendDesc.config.shadowEnabled  = true;
    rendDesc.config.shadowMapSize  = 2048;
    rendDesc.config.bloomEnabled   = true;
    rendDesc.config.bloomMipCount  = 3;
    rendDesc.config.builtinTonemap = true;

    if (!renderer.Init(rendDesc)) {
        SA_LOG_CRITICAL("Sandbox: renderer init failed");
        return 1;
    }

    // ── Scene ─────────────────────────────────────────────────────────────────
    Scene scene("Sandbox");

    // Camera — slightly elevated, looking toward -Z (model at origin)
    EntityFactory::CreateCamera(scene, "MainCamera",
        glm::radians(60.f), /*near*/ 0.01f, /*far*/ 500.f,
        /*position*/ {0.f, 0.15f, 15.0f});

    // Directional light — warm sunlight pitched from above, rotated to the left
    EntityFactory::CreateDirectionalLight(scene, "Sun",
        /*color*/      {1.f, 0.92f, 0.78f},
        /*intensity*/  2.5f,
        /*rotation*/   glm::normalize(
            glm::angleAxis(glm::radians(40.f),  glm::vec3{0.f, 1.f, 0.f}) *
            glm::angleAxis(glm::radians(-50.f), glm::vec3{1.f, 0.f, 0.f})),
        /*castShadow*/ true);

    // BoomBoxWithAxes — cooked from assets/models/BoomBoxWithAxes/BoomBoxWithAxes.gltf
    // The model is real-world scale (~0.1 m); scale up for visibility.
    // materialSlots is empty: submesh defaultMaterialIDs from the cook are used.
    // EntityFactory::CreateStaticMesh(scene, "BoomBoxWithAxes",
    //     Assets::BoomBoxWithAxes,
    //     /*position*/ {0.f, 0.f, 0.f},
    //     /*rotation*/ glm::angleAxis(glm::radians(20.f), glm::vec3{0.f, 1.f, 0.f}),
    //     /*scale*/    {50.f, 50.f, 50.f});

    EntityFactory::CreateStaticMesh(scene, "EmissiveStrengthTest",
        Assets::EmissiveStrengthTest,
        /*position*/ {0.f, 0.f, 0.f},
        /*rotation*/ {0.f, 0.f, 0.f, 0.f},
        /*scale*/    {1.f, 1.f, 1.f});

    // ── IBL ───────────────────────────────────────────────────────────────────
    // Provide all five UUIDs: the renderer reads from the cook cache directly,
    // or GPU-bakes and writes them on the first run.
    // auto& ws          = scene.GetWorldSettings();
    // ws.skyboxHdr      = Assets::SkyboxHdr;
    // ws.sh9            = Assets::SH9;
    // ws.prefilteredEnv = Assets::PrefilteredEnv;
    // ws.brdfLut        = Assets::BrdfLut;
    // ws.skyboxCubemap  = Assets::SkyboxCubemap;

    if (!renderer.SetIBL(scene.GetWorldSettings()))
        SA_LOG_WARN("Sandbox: IBL setup failed — rendering without ambient");

    // ── First transform update + draw list ────────────────────────────────────
    scene.UpdateTransforms();
    renderer.BuildDrawList(scene);

    // ── Render loop ───────────────────────────────────────────────────────────
    while (!window->ShouldClose()) {
        window->PollEvents();

        const int w = static_cast<int>(device->GetSwapchainWidth());
        const int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        scene.UpdateTransforms();
        renderer.RenderFrame(scene, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    device->WaitIdle();
    renderer.Shutdown();
    matMgr.Shutdown();
    resMgr.Shutdown();
    device.reset();
    window.reset();

    SA_LOG_INFO("Sandbox: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
