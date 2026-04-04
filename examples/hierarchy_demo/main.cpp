// HierarchyDemo
//
// Tests Scene::UpdateTransforms() with a parent-child hierarchy and
// per-frame AnimatedTransformComponent overrides.
//
// Scene (hierarchy_test.sascene):
//   Root      — silver metallic cube at origin       (metallic=0.9 rough=0.1)
//   ├─ ChildL — rough grey cube at local x=−3        (metallic=0.2 rough=0.7)
//   │   └─ Moon — small dull cube orbiting ChildL   (metallic=0.0 rough=0.9)
//   └─ ChildR — semi-glossy cube at local x=+3      (metallic=0.8 rough=0.3)
//
// Hardcoded animation (set via AnimatedTransformComponent each frame):
//   Root   — slow Y rotation
//   ChildL — fast spin + vertical bob
//   Moon   — circular orbit in ChildL's local space, scale 0.35
//   ChildR — counter-spin
//
// Run ibl_demo at least once to populate the IBL cook cache.
//
// Render loop — single call per frame (after animation update):
//   scene.UpdateTransforms();
//   renderer.RenderFrame(scene, w, h);

#include "core/logs/Log.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "HierarchyDemoPath.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

// ─── Built-in UUIDs ──────────────────────────────────────────────────────────

namespace BuiltinAssets {
    const AssetID Cube = AssetID::FromString("c0be0000-0000-4000-0000-000000000001");
}

// ─── Procedural cube ─────────────────────────────────────────────────────────

static CookedMesh MakeProcCubeMesh(const AssetID& id) {
    struct Vtx { float px,py,pz, nx,ny,nz, tx,ty,tz,tw, u,v; };
    static_assert(sizeof(Vtx) == 48);
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
    CookedMesh mesh;
    mesh.id = id; mesh.vertexCount = 24; mesh.indexCount = 36;
    mesh.vertexStride = 48; mesh.indexStride = 4;
    CookedSubMesh sm; sm.vertexOffset = 0; sm.vertexCount = 24;
    sm.indexOffset = 0; sm.indexCount = 36; sm.materialIndex = -1;
    sm.localTransform = glm::mat4(1.f); mesh.subMeshes.push_back(sm);
    mesh.vertexData.resize(24 * 48); std::memcpy(mesh.vertexData.data(), verts, mesh.vertexData.size());
    mesh.indexData.resize(36 * 4);   std::memcpy(mesh.indexData.data(), indices, mesh.indexData.size());
    return mesh;
}

// ─── Utility ─────────────────────────────────────────────────────────────────

static entt::entity FindByTag(const entt::registry& reg, std::string_view name) {
    for (auto [e, tag] : reg.view<TagComponent>().each())
        if (tag.name == name) return e;
    return entt::null;
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("HierarchyDemo: starting");

    const fs::path    assetsDir = HierarchyDemo::SA_ASSETS_DIR;
    const fs::path    cookDir   = HierarchyDemo::COOK_CACHE_DIR;
    const std::string shaderDir = HierarchyDemo::BUILTIN_SHADER_DIR;

    fs::create_directories(cookDir);

    // ── Window + device ───────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "HierarchyDemo"});

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
        SA_LOG_CRITICAL("HierarchyDemo: renderer init failed");
        return 1;
    }

    // ── Ensure cube in cook cache ─────────────────────────────────────────────
    {
        const fs::path p = cookDir / (BuiltinAssets::Cube.ToString() + ".samesh");
        if (!fs::exists(p)) {
            CookedMesh m = MakeProcCubeMesh(BuiltinAssets::Cube);
            if (SaveCookedMesh(m, p.string()))
                SA_LOG_INFO("HierarchyDemo: wrote procedural cube to cook cache");
        }
    }

    // ── Load scene ────────────────────────────────────────────────────────────
    Scene scene("HierarchyTest");
    const fs::path scenePath = assetsDir / "scenes" / "hierarchy_test.sascene";
    if (!SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_CRITICAL("HierarchyDemo: failed to load {}", scenePath.string());
        return 1;
    }

    // ── IBL (offline-first; GPU bake + cache on miss) ─────────────────────────
    if (!renderer.SetIBL(scene.GetWorldSettings()))
        SA_LOG_WARN("HierarchyDemo: no IBL source — scene renders without IBL");

    // ── Attach AnimatedTransformComponent to the four animated entities ────────
    auto& reg = scene.Registry();
    entt::entity eRoot   = FindByTag(reg, "Root");
    entt::entity eChildL = FindByTag(reg, "ChildL");
    entt::entity eMoon   = FindByTag(reg, "Moon");
    entt::entity eChildR = FindByTag(reg, "ChildR");

    for (entt::entity e : {eRoot, eChildL, eMoon, eChildR}) {
        if (e == entt::null) {
            SA_LOG_CRITICAL("HierarchyDemo: entity not found in scene");
            return 1;
        }
        const auto& t = reg.get<TransformComponent>(e);
        reg.emplace<AnimatedTransformComponent>(e,
            AnimatedTransformComponent{t.position, t.rotation, t.scale});
    }

    // ── First transform update + draw list ────────────────────────────────────
    scene.UpdateTransforms();
    renderer.BuildDrawList(scene);

    // ─────────────────────────────────────────────────────────────────────────
    // Render loop — animate then render each frame
    // ─────────────────────────────────────────────────────────────────────────
    uint32_t frameIndex = 0;

    while (!window->ShouldClose()) {
        window->PollEvents();

        const int w = static_cast<int>(device->GetSwapchainWidth());
        const int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        // ── Animate ───────────────────────────────────────────────────────────
        const float t = static_cast<float>(frameIndex) / 60.f;

        reg.get<AnimatedTransformComponent>(eRoot) = {
            {0.f, 0.f, 0.f},
            glm::angleAxis(t * 0.5f, glm::vec3(0, 1, 0)),
            {1.f, 1.f, 1.f}};

        reg.get<AnimatedTransformComponent>(eChildL) = {
            {-3.f, 0.4f * std::sin(t * 2.f), 0.f},
            glm::angleAxis(t * 2.0f, glm::vec3(0, 1, 0)),
            {1.f, 1.f, 1.f}};

        reg.get<AnimatedTransformComponent>(eMoon) = {
            {1.0f * std::cos(t * 3.f), 0.f, 1.0f * std::sin(t * 3.f)},
            glm::angleAxis(t * 5.f, glm::vec3(0, 1, 0)),
            {0.35f, 0.35f, 0.35f}};

        reg.get<AnimatedTransformComponent>(eChildR) = {
            {3.f, 0.f, 0.f},
            glm::angleAxis(-t * 1.5f, glm::vec3(0, 1, 0)),
            {1.f, 1.f, 1.f}};

        for (entt::entity e : {eRoot, eChildL, eMoon, eChildR})
            scene.MarkDirty(e);

        scene.UpdateTransforms();
        renderer.RenderFrame(scene, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        ++frameIndex;
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    device->WaitIdle();
    renderer.Shutdown();
    matMgr.Shutdown();
    resMgr.Shutdown();
    device.reset();
    window.reset();

    SA_LOG_INFO("HierarchyDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
