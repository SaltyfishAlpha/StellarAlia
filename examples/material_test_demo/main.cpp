// MaterialTestDemo
//
// Visual test for the material override system (MaterialOverrideComponent).
//
// Scene: material_test.sascene — two cubes + camera + directional light:
//   Left  (PbrCube):          PBR material (default_pbr.mat) — textured PBR shading
//   Right (SimpleAlbedoCube): SimpleAlbedo base material + MaterialOverrideComponent
//                             orange override (baseColorFactor = {1, 0.5, 0, 1})
//
// SimpleAlbedo MaterialType is registered by SimpleAlbedoFeature, which is added
// to SceneRenderer before Init().  The geometry pass handles both material types.
//
// Render loop — single call per frame:
//   scene.UpdateTransforms();
//   renderer.RenderFrame(scene, w, h);

#include "core/logs/Log.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/renderer/RenderFeature.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "MaterialTestDemoPath.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

// ─── Asset UUIDs ─────────────────────────────────────────────────────────────

namespace BuiltinAssets {
    const AssetID Cube              = AssetID::FromString("c0be0000-0000-4000-0000-000000000001");
    const AssetID SimpleAlbedoWhite = AssetID::FromString("e1a0be00-0000-4000-0000-000000000001");
}

// ─── SimpleAlbedoFeature ──────────────────────────────────────────────────────
//
// Registers the "SimpleAlbedo" MaterialType with the MaterialManager.
// No passes are added — the built-in geometry pass renders SimpleAlbedo meshes.

class SimpleAlbedoFeature final : public RenderFeature {
public:
    void OnInit(const FeatureInitContext& ctx) override
    {
        ctx.matMgr->RegisterTypeFromShaders(
            {"SimpleAlbedo", "deferred_geometry", "simple_albedo.gbuffer"}, ctx);
    }

    // No additional passes — built-in geometry pass handles SimpleAlbedo meshes.
    void AddPasses(SceneRenderer&, const FrameContext&, const RendererHandles&,
                   const entt::registry&, uint32_t, uint32_t) override {}
};

// ─── Procedural cube ──────────────────────────────────────────────────────────

static CookedMesh MakeProcCube(const AssetID& id) {
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
    CookedMesh m; m.id = id; m.vertexCount = 24; m.indexCount = 36;
    m.vertexStride = 48; m.indexStride = 4;
    CookedSubMesh sm; sm.vertexOffset = 0; sm.vertexCount = 24;
    sm.indexOffset = 0; sm.indexCount = 36; sm.materialIndex = -1;
    sm.localTransform = glm::mat4(1.f); m.subMeshes.push_back(sm);
    m.vertexData.resize(24 * 48); std::memcpy(m.vertexData.data(), verts, m.vertexData.size());
    m.indexData.resize(36 * 4);   std::memcpy(m.indexData.data(), indices, m.indexData.size());
    return m;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("MaterialTestDemo: starting");

    const fs::path assetsDir    = MatTestDemo::SA_ASSETS_DIR;
    const fs::path cookDir      = MatTestDemo::COOK_CACHE_DIR;
    const std::string shaderDir = MatTestDemo::BUILTIN_SHADER_DIR;

    fs::create_directories(cookDir);

    // ── Window + device ───────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "MaterialTestDemo"});

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

    // ── SceneRenderer — register SimpleAlbedoFeature before Init ─────────────
    SceneRenderer renderer;
    renderer.AddFeature(std::make_unique<SimpleAlbedoFeature>());

    SceneRenderer::Desc rendDesc{};
    rendDesc.device       = device.get();
    rendDesc.matMgr       = &matMgr;
    rendDesc.resMgr       = &resMgr;
    rendDesc.shaderDir    = shaderDir;
    rendDesc.cookCacheDir = cookDir.string();
    rendDesc.config.shadowEnabled  = true;
    rendDesc.config.bloomEnabled   = true;
    rendDesc.config.bloomMipCount  = 6;
    rendDesc.config.builtinTonemap = true;

    if (!renderer.Init(rendDesc)) {
        SA_LOG_CRITICAL("MaterialTestDemo: renderer init failed");
        return 1;
    }

    // ── Ensure cube mesh in cook cache ────────────────────────────────────────
    {
        const fs::path cubePath = cookDir / (BuiltinAssets::Cube.ToString() + ".samesh");
        if (!fs::exists(cubePath)) {
            CookedMesh cm = MakeProcCube(BuiltinAssets::Cube);
            if (SaveCookedMesh(cm, cubePath.string()))
                SA_LOG_INFO("MaterialTestDemo: wrote procedural cube to cook cache");
        }
    }

    // ── Ensure SimpleAlbedoWhite .samat in cook cache ─────────────────────────
    {
        const fs::path matPath =
            cookDir / (BuiltinAssets::SimpleAlbedoWhite.ToString() + ".samatc");
        if (!fs::exists(matPath)) {
            std::ofstream f(matPath);
            f << R"({
  "type": "SimpleAlbedo",
  "version": 1,
  "params": { "baseColorFactor": [1.0, 1.0, 1.0, 1.0] },
  "textures": {}
})";
            SA_LOG_INFO("MaterialTestDemo: wrote SimpleAlbedoWhite .samat to cook cache");
        }
    }

    // ── Load scene ────────────────────────────────────────────────────────────
    Scene scene("MaterialTest");
    const fs::path scenePath = assetsDir / "scenes" / "material_test.sascene";
    if (!SceneSerializer::LoadFromFile(scene, scenePath))
        SA_LOG_WARN("MaterialTestDemo: failed to load scene '{}'", scenePath.string());

    // ── IBL (offline-first; GPU bake + cache on miss) ─────────────────────────
    if (!renderer.SetIBL(scene.GetWorldSettings()))
        SA_LOG_WARN("MaterialTestDemo: no IBL source — scene renders without IBL");

    // ── First transform update + draw list ────────────────────────────────────
    scene.UpdateTransforms();
    renderer.BuildDrawList(scene);

    // ─────────────────────────────────────────────────────────────────────────
    // Render loop — single call per frame
    // ─────────────────────────────────────────────────────────────────────────
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

    SA_LOG_INFO("MaterialTestDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
