// AnimDemo
//
// Loads anim_test.sascene (camera + directional light + IBL world settings),
// then programmatically spawns a CesiumMan entity with CPU-skinned animation.
//
// Prerequisites:
//   1. Run cook_demo (or any target that invokes CookAssets) so that
//      CesiumMan.glb is cooked to .samesh v5 + .saskel + .saanim.
//   2. Run ibl_demo at least once to populate the IBL cook cache.
//
// Render loop:
//   animSystem.Update(dt)   — sample keyframes, CPU deform, upload dynVB
//   renderer.RenderFrame()  — GBuffer pass reads dynVertexBuffer as skinned mesh

#include "core/logs/Log.hpp"
#include "function/animation/AnimationSystem.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedAnim.hpp"
#include "resource/cook/CookedSkeleton.hpp"
#include "AnimDemoPath.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("AnimDemo: starting");

    const fs::path    assetsDir = AnimDemo::SA_ASSETS_DIR;
    const fs::path    cookDir   = AnimDemo::COOK_CACHE_DIR;
    const std::string shaderDir = AnimDemo::BUILTIN_SHADER_DIR;

    fs::create_directories(cookDir);

    // ── Window + device ───────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "AnimDemo"});

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

    if (!renderer.Init(rendDesc)) {
        SA_LOG_CRITICAL("AnimDemo: renderer init failed");
        return 1;
    }

    // ── Load scene (camera + directional light + IBL world settings) ──────────
    Scene scene("AnimTest");
    const fs::path scenePath = assetsDir / "scenes" / "anim_test.sascene";
    if (!SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_CRITICAL("AnimDemo: failed to load {}", scenePath.string());
        return 1;
    }

    if (!renderer.SetIBL(scene.GetWorldSettings()))
        SA_LOG_WARN("AnimDemo: no IBL source — scene renders without IBL");

    // ── Spawn CesiumMan entity ────────────────────────────────────────────────
    const AssetID meshUUID  = AssetID::FromString("ce510000-0000-4000-8000-000000000001");
    const AssetID animAsset = DeriveAnimID(meshUUID, 0);

    entt::entity cesiumMan = scene.CreateEntity("CesiumMan");
    auto& reg = scene.Registry();

    // TransformComponent is already attached by CreateEntity; set desired pose.
    // CesiumMan.glb root node rotation (Z-up → Y-up correction) was dropped
    // during cooking; compensate here with a -90° X rotation.
    reg.get<TransformComponent>(cesiumMan) = TransformComponent{
        {0.f, 0.f, 0.f},
        glm::angleAxis(glm::radians(-90.f), glm::vec3(1.f, 0.f, 0.f)),
        {1.f, 1.f, 1.f}
    };

    // Skeleton is derived internally by AnimationSystem::PrepareEntity from the
    // SkinnedMeshComponent.meshAsset (DeriveSkinID) — no separate component needed.
    reg.emplace<AnimatorComponent>(cesiumMan, AnimatorComponent{animAsset});
    {
        auto& smc   = reg.emplace<SkinnedMeshComponent>(cesiumMan);
        smc.meshAsset = meshUUID;
    }

    // ── Prepare animation: allocates dynVB, caches clip, uploads bind pose ────
    AnimationSystem animSystem;
    animSystem.PrepareEntity(cesiumMan, reg, resMgr, device.get());

    if (!reg.get<SkinnedMeshComponent>(cesiumMan).ready) {
        SA_LOG_CRITICAL("AnimDemo: PrepareEntity failed — check cook cache");
        return 1;
    }

    // ── Initial transform propagation + draw list ─────────────────────────────
    scene.UpdateTransforms();
    renderer.BuildDrawList(scene);

    // ── Render loop ───────────────────────────────────────────────────────────
    using Clock = std::chrono::steady_clock;
    auto lastTime = Clock::now();

    while (!window->ShouldClose()) {
        window->PollEvents();

        const int w = static_cast<int>(device->GetSwapchainWidth());
        const int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        animSystem.Update(dt, reg, resMgr, device.get());
        renderer.RenderFrame(scene, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    device->WaitIdle();
    renderer.Shutdown();
    matMgr.Shutdown();
    resMgr.Shutdown();
    device.reset();
    window.reset();

    SA_LOG_INFO("AnimDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
