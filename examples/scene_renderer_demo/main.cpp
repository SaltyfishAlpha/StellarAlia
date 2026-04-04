// SceneRendererDemo
//
// Demonstrates the SceneRenderer encapsulation (Stage 6.1).
// Loads assets/scenes/default.sascene (random cubes + Chinese dragon),
// adds a directional light via EntityFactory, and renders with full IBL.
//
// Render loop — single call per frame:
//   scene.UpdateTransforms();
//   renderer.RenderFrame(scene, w, h);
//
// All of BeginFrame/EndFrame/Present, frame-index bookkeeping, uniform upload,
// and RenderGraph assembly are internal to SceneRenderer.

#include "core/logs/Log.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/EntityFactory.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "SceneRendererDemoPath.hpp"

#include <filesystem>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

// ─── Procedural cube (fallback if not in cook cache) ─────────────────────────

static const AssetID kCubeID = AssetID::FromString("c0be0000-0000-4000-0000-000000000001");

static CookedMesh MakeProcCube() {
    struct Vtx { float px,py,pz, nx,ny,nz, tx,ty,tz,tw, u,v; };
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
    CookedMesh m; m.id = kCubeID;
    m.vertexCount = 24; m.indexCount = 36;
    m.vertexStride = 48; m.indexStride = 4;
    CookedSubMesh sm; sm.vertexOffset = 0; sm.vertexCount = 24;
    sm.indexOffset = 0; sm.indexCount = 36; sm.materialIndex = -1;
    sm.localTransform = glm::mat4(1.f);
    m.subMeshes.push_back(sm);
    m.vertexData.resize(24 * 48); std::memcpy(m.vertexData.data(), verts, m.vertexData.size());
    m.indexData.resize(36 * 4);   std::memcpy(m.indexData.data(), indices, m.indexData.size());
    return m;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("SceneRendererDemo: starting");

    const fs::path assetsDir    = SceneRendererDemo::SA_ASSETS_DIR;
    const fs::path cookDir      = SceneRendererDemo::COOK_CACHE_DIR;
    const std::string shaderDir = SceneRendererDemo::BUILTIN_SHADER_DIR;

    fs::create_directories(cookDir);

    // ── Window + device ───────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "SceneRendererDemo"});

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
        SA_LOG_CRITICAL("SceneRendererDemo: renderer init failed");
        return 1;
    }

    // ── Ensure cube mesh in cook cache ────────────────────────────────────────
    {
        const fs::path cubePath = cookDir / (kCubeID.ToString() + ".samesh");
        if (!fs::exists(cubePath)) {
            CookedMesh cm = MakeProcCube();
            if (SaveCookedMesh(cm, cubePath.string()))
                SA_LOG_INFO("SceneRendererDemo: wrote procedural cube to cook cache");
        }
    }

    // ── Load scene ────────────────────────────────────────────────────────────
    Scene scene("DefaultScene");
    const fs::path scenePath = assetsDir / "scenes" / "default.sascene";
    if (!SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_CRITICAL("SceneRendererDemo: failed to load '{}'", scenePath.string());
        return 1;
    }
    SA_LOG_INFO("SceneRendererDemo: scene '{}' loaded", scene.GetName());

    // ── Add directional light ─────────────────────────────────────────────────
    EntityFactory::CreateDirectionalLight(scene, "Sun",
        {1.0f, 0.95f, 0.85f}, 2.0f,
        glm::normalize(glm::angleAxis(glm::radians(-45.f), glm::vec3(1,0,0))
                     * glm::angleAxis(glm::radians( 30.f), glm::vec3(0,1,0))));

    // ── IBL (offline-first; GPU bake + cache on miss) ─────────────────────────
    if (!renderer.SetIBL(scene.GetWorldSettings()))
        SA_LOG_WARN("SceneRendererDemo: no IBL source — scene renders without IBL");

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

    SA_LOG_INFO("SceneRendererDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
