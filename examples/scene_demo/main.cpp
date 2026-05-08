// Scene Demo
//
// Stages exercised:
//   1. ECS unit tests — entity creation, hierarchy, transform propagation,
//      MaterialOverrideComponent, SceneSerializer round-trip
//   2. Full render — loads assets/scenes/default.sascene, cooks assets via
//      the pre-build CookAssets target, binds meshes + default PBR materials,
//      and renders with SceneRenderer.

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
#include "core/Random.hpp"
#include "SceneDemoPath.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cassert>
#include <filesystem>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

// ─── Default PBR asset UUIDs ─────────────────────────────────────────────────

namespace DefaultAssets {
    const AssetID Cube = AssetID::FromString("c0be0000-0000-4000-0000-000000000001");
} // namespace DefaultAssets

// ─── Procedural unit cube ─────────────────────────────────────────────────────

static Resource::CookedMesh MakeProcCubeMesh(const AssetID& id) {
    struct Vtx { float px,py,pz, nx,ny,nz, tx,ty,tz,tw, u,v; };
    static_assert(sizeof(Vtx) == 48);

    // clang-format off
    static const Vtx verts[24] = {
        // +X
        { 0.5f,-0.5f,-0.5f,  1,0,0,  0, 0,-1,1,  0,1 },
        { 0.5f, 0.5f,-0.5f,  1,0,0,  0, 0,-1,1,  0,0 },
        { 0.5f, 0.5f, 0.5f,  1,0,0,  0, 0,-1,1,  1,0 },
        { 0.5f,-0.5f, 0.5f,  1,0,0,  0, 0,-1,1,  1,1 },
        // -X
        {-0.5f,-0.5f, 0.5f, -1,0,0,  0, 0, 1,1,  0,1 },
        {-0.5f, 0.5f, 0.5f, -1,0,0,  0, 0, 1,1,  0,0 },
        {-0.5f, 0.5f,-0.5f, -1,0,0,  0, 0, 1,1,  1,0 },
        {-0.5f,-0.5f,-0.5f, -1,0,0,  0, 0, 1,1,  1,1 },
        // +Y
        {-0.5f, 0.5f,-0.5f,  0,1,0,  1, 0, 0,1,  0,1 },
        {-0.5f, 0.5f, 0.5f,  0,1,0,  1, 0, 0,1,  0,0 },
        { 0.5f, 0.5f, 0.5f,  0,1,0,  1, 0, 0,1,  1,0 },
        { 0.5f, 0.5f,-0.5f,  0,1,0,  1, 0, 0,1,  1,1 },
        // -Y
        {-0.5f,-0.5f, 0.5f,  0,-1,0, 1, 0, 0,1,  0,1 },
        {-0.5f,-0.5f,-0.5f,  0,-1,0, 1, 0, 0,1,  0,0 },
        { 0.5f,-0.5f,-0.5f,  0,-1,0, 1, 0, 0,1,  1,0 },
        { 0.5f,-0.5f, 0.5f,  0,-1,0, 1, 0, 0,1,  1,1 },
        // +Z
        { 0.5f,-0.5f, 0.5f,  0,0,1,  1, 0, 0,1,  0,1 },
        { 0.5f, 0.5f, 0.5f,  0,0,1,  1, 0, 0,1,  0,0 },
        {-0.5f, 0.5f, 0.5f,  0,0,1,  1, 0, 0,1,  1,0 },
        {-0.5f,-0.5f, 0.5f,  0,0,1,  1, 0, 0,1,  1,1 },
        // -Z
        {-0.5f,-0.5f,-0.5f,  0,0,-1,-1, 0, 0,1,  0,1 },
        {-0.5f, 0.5f,-0.5f,  0,0,-1,-1, 0, 0,1,  0,0 },
        { 0.5f, 0.5f,-0.5f,  0,0,-1,-1, 0, 0,1,  1,0 },
        { 0.5f,-0.5f,-0.5f,  0,0,-1,-1, 0, 0,1,  1,1 },
    };
    static const uint32_t indices[36] = {
         0, 1, 2,  0, 2, 3,
         4, 5, 6,  4, 6, 7,
         8, 9,10,  8,10,11,
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23,
    };
    // clang-format on

    Resource::CookedMesh mesh;
    mesh.id           = id;
    mesh.vertexCount  = 24;
    mesh.indexCount   = 36;
    mesh.vertexStride = 48;
    mesh.indexStride  = 4;

    Resource::CookedSubMesh sm;
    sm.vertexOffset   = 0;
    sm.vertexCount    = 24;
    sm.indexOffset    = 0;
    sm.indexCount     = 36;
    sm.materialIndex  = -1;
    sm.localTransform = glm::mat4(1.0f);
    mesh.subMeshes.push_back(sm);

    mesh.vertexData.resize(24 * 48);
    std::memcpy(mesh.vertexData.data(), verts, mesh.vertexData.size());
    mesh.indexData.resize(36 * 4);
    std::memcpy(mesh.indexData.data(), indices, mesh.indexData.size());

    return mesh;
}

// ─── Unit tests (no GPU required) ────────────────────────────────────────────

static void CHECK(bool cond, const char* msg) {
    if (!cond) { SA_LOG_CRITICAL("FAIL: {}", msg); std::abort(); }
    SA_LOG_INFO("  OK: {}", msg);
}

static void RunUnitTests(const fs::path& tmpDir) {
    SA_LOG_INFO("=== ECS unit tests ===");

    // Entity creation
    {
        Scene s("T");
        auto e = s.CreateEntity("Cube");
        CHECK(s.Registry().all_of<TagComponent, TransformComponent, WorldTransformComponent>(e),
              "CreateEntity attaches built-in components");
    }

    // Hierarchy + transform propagation
    {
        Scene s("T");
        auto parent = s.CreateEntity("P");
        auto child  = s.CreateEntity("C");
        s.Registry().get<TransformComponent>(parent).position = {2.f, 0.f, 0.f};
        s.MarkDirty(parent);
        s.Registry().get<TransformComponent>(child).position  = {0.f, 1.f, 0.f};
        s.SetParent(child, parent);
        s.UpdateTransforms();
        glm::vec3 wp = glm::vec3(s.Registry().get<WorldTransformComponent>(child).matrix[3]);
        CHECK(glm::length(wp - glm::vec3(2.f, 1.f, 0.f)) < 1e-4f,
              "child world position = parent + local");
    }

    // DestroyEntity orphans children
    {
        Scene s("T");
        auto p = s.CreateEntity("P"), c = s.CreateEntity("C");
        s.SetParent(c, p);
        s.DestroyEntity(p);
        CHECK(!s.Registry().valid(p), "parent destroyed");
        CHECK(s.Registry().valid(c),  "child still valid");
        auto* h = s.Registry().try_get<HierarchyComponent>(c);
        CHECK(!h || h->parent == entt::null, "child orphaned");
    }

    // MaterialOverrideComponent
    {
        Scene s("T");
        auto e = s.CreateEntity("M");
        auto& mo = s.Registry().emplace<MaterialOverrideComponent>(e);
        mo.scalars["roughnessFactor"] = 0.1f;
        mo.scalars["metallicFactor"]  = 1.0f;
        CHECK(s.Registry().try_get<MaterialOverrideComponent>(e) != nullptr,
              "MaterialOverrideComponent attached");

        mo.scalars["customBlend"] = 0.75f;
        const auto& got = std::get<float>(mo.scalars.at("customBlend"));
        CHECK(std::abs(got - 0.75f) < 1e-6f, "MaterialOverrideComponent scalar round-trip");
    }

    // SceneSerializer round-trip
    {
        Scene src("Saved");
        auto cam = src.CreateEntity("Camera");
        src.Registry().emplace<CameraComponent>(cam, CameraComponent{glm::radians(60.f), 0.1f, 500.f, 1});

        auto mesh = src.CreateEntity("Mesh");
        StaticMeshComponent smc;
        smc.meshAsset = AssetID::Generate();
        src.Registry().emplace<StaticMeshComponent>(mesh, smc);
        src.SetParent(mesh, cam);  // hierarchy test

        const fs::path p = tmpDir / "rt.sascene";
        CHECK(SceneSerializer::SaveToFile(src, p), "save");

        Scene dst("?");
        CHECK(SceneSerializer::LoadFromFile(dst, p), "load");
        CHECK(dst.GetName() == "Saved", "name preserved");

        uint32_t n = 0;
        dst.Registry().view<TagComponent>().each([&](auto, auto&){ ++n; });
        CHECK(n == 2, "entity count preserved");

        bool hierarchyOk = false;
        dst.Registry().view<TagComponent, HierarchyComponent>().each(
            [&](auto, const TagComponent& t, const HierarchyComponent& h) {
                if (t.name == "Mesh" && h.parent != entt::null) {
                    hierarchyOk = (dst.Registry().get<TagComponent>(h.parent).name == "Camera");
                }
            });
        CHECK(hierarchyOk, "hierarchy preserved");
    }

    SA_LOG_INFO("=== All ECS unit tests passed ===");
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();

    // ── Unit tests (no GPU) ──────────────────────────────────────────────────
    const fs::path tmpDir = fs::temp_directory_path() / "StellarAlia_SceneDemo";
    fs::create_directories(tmpDir);
    RunUnitTests(tmpDir);

    // ── Window + device ──────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "SceneDemo"});

    RHIDeviceDesc devDesc{};
    devDesc.windowHandle     = NativeWindowHandle{window->GetNativeHandle()};
    devDesc.swapchainWidth   = window->GetWidth();
    devDesc.swapchainHeight  = window->GetHeight();
    devDesc.vsync            = true;
    devDesc.enableValidation = true;
    auto device = VulkanDevice::Create(devDesc);

    // ── ResourceManager + MaterialManager ────────────────────────────────────
    ResourceManager resMgr;
    resMgr.Init(SceneDemo::COOK_CACHE_DIR, device.get());

    MaterialManager matMgr;
    matMgr.Init(device.get(), &resMgr);

    // ── SceneRenderer ─────────────────────────────────────────────────────────
    SceneRenderer renderer;
    SceneRenderer::Desc rendDesc{};
    rendDesc.device       = device.get();
    rendDesc.matMgr       = &matMgr;
    rendDesc.resMgr       = &resMgr;
    rendDesc.shaderDir    = SceneDemo::BUILTIN_SHADER_DIR;
    rendDesc.cookCacheDir = SceneDemo::COOK_CACHE_DIR;

    if (!renderer.Init(rendDesc)) {
        SA_LOG_CRITICAL("SceneDemo: renderer init failed");
        return 1;
    }

    // ── Ensure procedural cube mesh in cook cache ─────────────────────────────
    {
        const fs::path cubePath =
            fs::path(SceneDemo::COOK_CACHE_DIR) /
            (DefaultAssets::Cube.ToString() + ".samesh");
        if (!fs::exists(cubePath)) {
            Resource::CookedMesh cubeMesh = MakeProcCubeMesh(DefaultAssets::Cube);
            if (Resource::SaveCookedMesh(cubeMesh, cubePath.string()))
                SA_LOG_INFO("SceneDemo: wrote procedural cube to cook cache");
            else
                SA_LOG_WARN("SceneDemo: failed to write procedural cube");
        }
    }

    // ── Load scene ────────────────────────────────────────────────────────────
    const fs::path scenePath =
        fs::path(SceneDemo::SA_ASSETS_DIR) / "scenes" / "default.sascene";

    Scene scene("DefaultScene");
    if (!SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_CRITICAL("SceneDemo: failed to load scene '{}'", scenePath.string());
        return 1;
    }
    SA_LOG_INFO("SceneDemo: loaded scene '{}' with {} entities",
                scene.GetName(),
                [&]{ uint32_t n=0;
                     scene.View<TagComponent>().each([&](auto,auto&){++n;});
                     return n; }());

    // ── IBL (offline-first; GPU bake + cache on miss) ─────────────────────────
    if (!renderer.SetIBL(scene.GetWorldSettings()))
        SA_LOG_WARN("SceneDemo: no IBL source — scene renders without IBL");

    // ── Add randomly placed/rotated cube entity ───────────────────────────────
    {
        Core::Random rng;
        const glm::vec3 pos = {
            rng.Float(-5.f, 5.f),
            rng.Float(-5.f, 5.f),
            rng.Float(-5.f, 5.f)
        };
        glm::vec3 axis = {
            rng.Float(-1.f, 1.f),
            rng.Float(-1.f, 1.f),
            rng.Float(-1.f, 1.f)
        };
        if (glm::length(axis) < 1e-4f) axis = {0.f, 1.f, 0.f};
        axis = glm::normalize(axis);
        const glm::quat rot = glm::angleAxis(rng.Float(0.f, 6.28318530718f), axis);

        entt::entity cubeEnt = scene.CreateEntity("RandomCube");
        auto& t      = scene.Registry().get<TransformComponent>(cubeEnt);
        t.position   = pos;
        t.rotation   = rot;
        scene.MarkDirty(cubeEnt);

        scene.Registry().emplace<StaticMeshComponent>(cubeEnt,
            StaticMeshComponent{DefaultAssets::Cube});
        scene.Registry().emplace<MeshRendererComponent>(cubeEnt);

        SA_LOG_INFO("SceneDemo: added RandomCube at ({:.2f}, {:.2f}, {:.2f})",
                    pos.x, pos.y, pos.z);
    }

    // ── First transform update + draw list ────────────────────────────────────
    scene.UpdateTransforms();
    renderer.BuildDrawList(scene);

    // ── Render loop ──────────────────────────────────────────────────────────
    while (!window->ShouldClose()) {
        window->PollEvents();

        const int w = static_cast<int>(device->GetSwapchainWidth());
        const int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        scene.UpdateTransforms();
        renderer.RenderFrame(scene, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }

    // ── Save scene on exit ────────────────────────────────────────────────────
    if (!SceneSerializer::SaveToFile(scene, scenePath))
        SA_LOG_WARN("SceneDemo: failed to save scene on exit");

    // ── Cleanup ───────────────────────────────────────────────────────────────
    device->WaitIdle();
    renderer.Shutdown();
    matMgr.Shutdown();
    resMgr.Shutdown();
    device.reset();
    window.reset();

    SA_LOG_INFO("SceneDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
