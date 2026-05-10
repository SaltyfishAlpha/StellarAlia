// AreaLightDemo
//
// Visual test for the area light (LTC) implementation.
//
// Scene: area_light_test.sascene
//   GroundPlane      — 10×8 grey PBR floor
//   CubeGray         — neutral grey PBR, roughness=0.7
//   CubeBlue         — metallic blue PBR, roughness=0.25
//   CubeEmissive     — near-black PBR + emissiveFactor=(2, 1, 0.1) orange glow
//
//   AreaLightLeft    — warm orange (1.0, 0.55, 0.2), 2×3m, left side
//   AreaLightRight   — cool blue  (0.3, 0.55, 1.0), 2×3m, right side
//   SpotTop          — white, from directly above, tight cone
//   PointBetween     — warm yellow, between cube 1 and 2
//   Sun              — directional, low-intensity fill
//
// No HDR / IBL source — purely direct-light driven.
//
// NOTE: Area light shading requires LTC lookup table data in
//       src/function/ibl/LtcLut.cpp (LTC1 and LTC2 arrays).
//       Until the data is pasted there, area lights render black.

#include "core/logs/Log.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "function/material/MaterialManager.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "AreaLightDemoPath.hpp"

#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

// ─── Builtin asset UUIDs ──────────────────────────────────────────────────────
namespace BuiltinAssets {
    const AssetID Cube  = AssetID::FromString("c0be0000-0000-4000-0000-000000000001");
    const AssetID Plane = AssetID::FromString("c0be0000-0000-4000-0000-000000000002");
}

// ─── Procedural mesh helpers ──────────────────────────────────────────────────
// Used as runtime fallback if the cook pipeline hasn't produced the .samesh yet.

struct Vtx { float px,py,pz, nx,ny,nz, tx,ty,tz,tw, u,v; };
static_assert(sizeof(Vtx) == 48);

static CookedMesh MakeProcCube(const AssetID& id) {
    // clang-format off
    static const Vtx verts[24] = {
        { 0.5f,-0.5f,-0.5f, 1,0,0, 0, 0,-1,1, 0,1 }, { 0.5f, 0.5f,-0.5f, 1,0,0, 0, 0,-1,1, 0,0 },
        { 0.5f, 0.5f, 0.5f, 1,0,0, 0, 0,-1,1, 1,0 }, { 0.5f,-0.5f, 0.5f, 1,0,0, 0, 0,-1,1, 1,1 },
        {-0.5f,-0.5f, 0.5f,-1,0,0, 0, 0, 1,1, 0,1 }, {-0.5f, 0.5f, 0.5f,-1,0,0, 0, 0, 1,1, 0,0 },
        {-0.5f, 0.5f,-0.5f,-1,0,0, 0, 0, 1,1, 1,0 }, {-0.5f,-0.5f,-0.5f,-1,0,0, 0, 0, 1,1, 1,1 },
        {-0.5f, 0.5f,-0.5f, 0,1,0, 1, 0, 0,1, 0,1 }, {-0.5f, 0.5f, 0.5f, 0,1,0, 1, 0, 0,1, 0,0 },
        { 0.5f, 0.5f, 0.5f, 0,1,0, 1, 0, 0,1, 1,0 }, { 0.5f, 0.5f,-0.5f, 0,1,0, 1, 0, 0,1, 1,1 },
        {-0.5f,-0.5f, 0.5f, 0,-1,0,1, 0, 0,1, 0,1 }, {-0.5f,-0.5f,-0.5f, 0,-1,0,1, 0, 0,1, 0,0 },
        { 0.5f,-0.5f,-0.5f, 0,-1,0,1, 0, 0,1, 1,0 }, { 0.5f,-0.5f, 0.5f, 0,-1,0,1, 0, 0,1, 1,1 },
        { 0.5f,-0.5f, 0.5f, 0,0,1, 1, 0, 0,1, 0,1 }, { 0.5f, 0.5f, 0.5f, 0,0,1, 1, 0, 0,1, 0,0 },
        {-0.5f, 0.5f, 0.5f, 0,0,1, 1, 0, 0,1, 1,0 }, {-0.5f,-0.5f, 0.5f, 0,0,1, 1, 0, 0,1, 1,1 },
        {-0.5f,-0.5f,-0.5f, 0,0,-1,-1,0, 0,1, 0,1 }, {-0.5f, 0.5f,-0.5f, 0,0,-1,-1,0, 0,1, 0,0 },
        { 0.5f, 0.5f,-0.5f, 0,0,-1,-1,0, 0,1, 1,0 }, { 0.5f,-0.5f,-0.5f, 0,0,-1,-1,0, 0,1, 1,1 },
    };
    static const uint32_t indices[36] = {
         0, 1, 2,  0, 2, 3,  4, 5, 6,  4, 6, 7,
         8, 9,10,  8,10,11, 12,13,14, 12,14,15,
        16,17,18, 16,18,19, 20,21,22, 20,22,23,
    };
    // clang-format on
    CookedMesh m; m.id = id; m.vertexCount = 24; m.indexCount = 36;
    m.vertexStride = 48; m.indexStride = 4;
    CookedSubMesh sm; sm.vertexOffset = 0; sm.vertexCount = 24;
    sm.indexOffset = 0; sm.indexCount = 36; sm.materialIndex = -1;
    sm.localTransform = glm::mat4(1.f); m.subMeshes.push_back(sm);
    m.vertexData.resize(24 * 48); std::memcpy(m.vertexData.data(), verts, m.vertexData.size());
    m.indexData.resize(36 * 4);   std::memcpy(m.indexData.data(), indices, m.indexData.size());
    return m;
}

static CookedMesh MakeProcPlane(const AssetID& id) {
    // 1×1 plane, XZ, Y-up normal, tangent=(1,0,0,1)
    // clang-format off
    static const Vtx verts[4] = {
        {-0.5f,0,-0.5f, 0,1,0, 1,0,0,1, 0,0},
        { 0.5f,0,-0.5f, 0,1,0, 1,0,0,1, 1,0},
        { 0.5f,0, 0.5f, 0,1,0, 1,0,0,1, 1,1},
        {-0.5f,0, 0.5f, 0,1,0, 1,0,0,1, 0,1},
    };
    static const uint32_t indices[6] = { 0,2,1, 0,3,2 };
    // clang-format on
    CookedMesh m; m.id = id; m.vertexCount = 4; m.indexCount = 6;
    m.vertexStride = 48; m.indexStride = 4;
    CookedSubMesh sm; sm.vertexOffset = 0; sm.vertexCount = 4;
    sm.indexOffset = 0; sm.indexCount = 6; sm.materialIndex = -1;
    sm.localTransform = glm::mat4(1.f); m.subMeshes.push_back(sm);
    m.vertexData.resize(4 * 48); std::memcpy(m.vertexData.data(), verts, m.vertexData.size());
    m.indexData.resize(6 * 4);   std::memcpy(m.indexData.data(), indices, m.indexData.size());
    return m;
}

static void EnsureInCache(const fs::path& cookDir, const AssetID& id,
                           CookedMesh (*makeFn)(const AssetID&), const char* label) {
    const fs::path path = cookDir / (id.ToString() + ".samesh");
    if (!fs::exists(path)) {
        CookedMesh cm = makeFn(id);
        if (SaveCookedMesh(cm, path.string()))
            SA_LOG_INFO("AreaLightDemo: wrote procedural {} to cook cache", label);
        else
            SA_LOG_ERROR("AreaLightDemo: failed to write {} to cook cache", label);
    }
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("AreaLightDemo: starting");

    const fs::path assetsDir    = AreaLightDemo::SA_ASSETS_DIR;
    const fs::path cookDir      = AreaLightDemo::COOK_CACHE_DIR;
    const std::string shaderDir = AreaLightDemo::BUILTIN_SHADER_DIR;

    fs::create_directories(cookDir);

    // ── Window + device ───────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "AreaLightDemo — LTC Area Lights"});

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
    rendDesc.config.bloomMipCount  = 6;

    if (!renderer.Init(rendDesc)) {
        SA_LOG_CRITICAL("AreaLightDemo: renderer init failed");
        return 1;
    }

    // ── Ensure builtin meshes in cook cache (fallback if cook didn't run) ─────
    EnsureInCache(cookDir, BuiltinAssets::Cube,  MakeProcCube,  "cube");
    EnsureInCache(cookDir, BuiltinAssets::Plane, MakeProcPlane, "plane");

    // ── Load scene ────────────────────────────────────────────────────────────
    Scene scene("AreaLightTest");
    const fs::path scenePath = assetsDir / "scenes" / "area_light_test.sascene";
    if (!SceneSerializer::LoadFromFile(scene, scenePath))
        SA_LOG_WARN("AreaLightDemo: failed to load scene '{}'", scenePath.string());

    // ── No IBL — purely direct lighting demo ──────────────────────────────────
    // SetIBL will return false (no HDR source in WorldSettings) — that's expected.
    renderer.SetIBL(scene.GetWorldSettings());

    // ── Find animated entity (CubeBlue — shadow test) ────────────────────────
    entt::entity cubeBlue = entt::null;
    for (auto [e, tag] : scene.View<TagComponent>().each()) {
        if (tag.name == "CubeBlue") { cubeBlue = e; break; }
    }
    if (cubeBlue == entt::null)
        SA_LOG_WARN("AreaLightDemo: 'CubeBlue' entity not found — shadow animation disabled");

    // ── Initial transform update + draw list ──────────────────────────────────
    scene.UpdateTransforms();
    renderer.BuildDrawList(scene);

    const auto startTime = std::chrono::steady_clock::now();

    // ─────────────────────────────────────────────────────────────────────────
    // Render loop
    // ─────────────────────────────────────────────────────────────────────────
    while (!window->ShouldClose()) {
        window->PollEvents();

        const int w = static_cast<int>(device->GetSwapchainWidth());
        const int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        // ── Animate CubeBlue: bob up/down + spin around Y ─────────────────────
        if (cubeBlue != entt::null) {
            const float t = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - startTime).count();

            auto& anim = scene.Registry().get_or_emplace<AnimatedTransformComponent>(cubeBlue);
            anim.position = { 0.0f, 0.5f + std::sin(t * 1.5f) * 0.8f, 0.0f };
            anim.rotation = glm::angleAxis(t * 2.0f, glm::vec3(0.f, 1.f, 0.f));
            anim.scale    = { 1.0f, 1.0f, 1.0f };
            scene.MarkDirty(cubeBlue);
        }

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

    SA_LOG_INFO("AreaLightDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
