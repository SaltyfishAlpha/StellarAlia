// Scene Demo
//
// Stages exercised:
//   1. ECS unit tests — entity creation, hierarchy, transform propagation,
//      MaterialOverrideComponent, SceneSerializer round-trip
//   2. Full render — loads assets/scenes/default.sascene, cooks assets via
//      the pre-build CookAssets target, binds meshes + default PBR materials,
//      and renders with RenderGraph.

#include "core/logs/Log.hpp"
#include "function/FrameUniforms.hpp"
#include "function/FrameUniformsBuffer.hpp"
#include "function/material/AttachmentKey.hpp"
#include "function/material/MaterialInstance.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/MaterialType.hpp"
#include "function/material/ShaderProgram.hpp"
#include "function/render_graph/RenderGraph.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "platform/rhi/ShaderReflection.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "core/Random.hpp"
#include "SceneDemoPath.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

// ─── Default PBR asset UUIDs ─────────────────────────────────────────────────
// These match assets/textures/builtin/*.sameta

namespace DefaultAssets {
    // textures
    const AssetID Albedo = AssetID::FromString("5fe2fd09-f465-435c-abdf-b25066db1ccf");
    const AssetID MR     = AssetID::FromString("6752a4f5-098e-41cc-ac04-9e251bfff65f");
    const AssetID Normal = AssetID::FromString("bbf1b547-3330-4dd6-95d5-7e22f67f8265");
    // IBL
    const AssetID EnvMap  = AssetID::FromString("7a735f5c-b3c4-4361-b342-699c816fa64d");
    const AssetID BrdfLut = AssetID::FromString("c5b06992-5a8f-4dc9-9d11-406e12b969d4");
    // Procedural geometry
    const AssetID Cube    = AssetID::FromString("c0be0000-0000-4000-0000-000000000001");
} // namespace DefaultAssets

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::vector<uint8_t> LoadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { SA_LOG_ERROR("LoadSpv: cannot open '{}'", path); return {}; }
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return data;
}

// Procedural unit cube: 24 vertices (4 per face × 6 faces), 36 indices.
// Vertex layout matches MeshData::Vertex (48 bytes):
//   vec3 position, vec3 normal, vec4 tangent (w=handedness), vec2 texCoord0
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

static std::unique_ptr<MaterialType> BuildDefaultPbrType(
    IRHIDevice*         device,
    RHIDescLayoutHandle frameLayout,
    const std::string&  shaderDir)
{
    const auto vertSpv = LoadSpv(shaderDir + "/pbr.vert.spv");
    const auto fragSpv = LoadSpv(shaderDir + "/pbr.frag.spv");
    if (vertSpv.empty() || fragSpv.empty()) return nullptr;

    ShaderReflection vertRefl, fragRefl;
    if (!ShaderReflectionIO::LoadFromFile(shaderDir + "/pbr.vert.refl", vertRefl) ||
        !ShaderReflectionIO::LoadFromFile(shaderDir + "/pbr.frag.refl", fragRefl)) {
        SA_LOG_ERROR("BuildDefaultPbrType: failed to load .refl files");
        return nullptr;
    }

    const ShaderReflection merged = MergeReflections(vertRefl, fragRefl);

    auto type  = std::make_unique<MaterialType>();
    type->name = "PBR";

    // uboSize from set=1 binding=0 UBO block size
    if (auto ubo = merged.FindBinding(1, 0))
        type->uboSize = ubo->blockSize;

    // params from UBO member reflection
    if (auto ubo = merged.FindBinding(1, 0))
        for (const auto& m : ubo->members)
            type->params.push_back({m.name, m.offset, m.size});

    // textures from set=1 texture/sampler bindings sorted by binding
    {
        std::vector<const ShaderBindingDesc*> texBindings;
        for (const auto& b : merged.bindings) {
            if (b.set != 1) continue;
            if (b.type == RHIDescriptorType::Texture2D  ||
                b.type == RHIDescriptorType::TextureCube ||
                b.type == RHIDescriptorType::Sampler)
                texBindings.push_back(&b);
        }
        std::sort(texBindings.begin(), texBindings.end(),
            [](const ShaderBindingDesc* a, const ShaderBindingDesc* b){
                return a->binding < b->binding; });
        uint32_t slotIdx = 0;
        for (const auto* b : texBindings)
            type->textures.push_back({b->name, b->binding, slotIdx++});
    }

    type->defaultCullMode   = RHICullMode::Back;
    type->defaultDepthTest  = true;
    type->defaultDepthWrite = true;

    ShaderProgram::Desc pd;
    pd.vertSpv     = vertSpv;
    pd.vertRefl    = vertRefl;
    pd.fragSpv     = fragSpv;
    pd.fragRefl    = fragRefl;
    pd.frameLayout = frameLayout;
    if (!type->shader.Load(device, pd)) return nullptr;

    return type;
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

    // MaterialOverrideComponent Set<T> upsert
    {
        Scene s("T");
        auto e  = s.CreateEntity("M");
        auto& ov = s.Registry().emplace<MaterialOverrideComponent>(e);
        MaterialOverrideComponent::Set(ov.params, "roughnessFactor", 0.5f);
        MaterialOverrideComponent::Set(ov.params, "roughnessFactor", 0.1f);
        float v = 0.f;
        std::memcpy(&v, ov.params[0].value.data(), 4);
        CHECK(ov.params.size() == 1 && std::abs(v - 0.1f) < 1e-6f,
              "MaterialOverride upsert works");
    }

    // SceneSerializer round-trip
    {
        Scene src("Saved");
        auto cam = src.CreateEntity("Camera");
        src.Registry().emplace<CameraComponent>(cam, CameraComponent{glm::radians(60.f), 0.1f, 500.f});
        src.Registry().emplace<ActiveCameraTag>(cam);

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

// ─── Per-frame draw item ──────────────────────────────────────────────────────

struct DrawItem {
    RHIBufferHandle   vertexBuffer;
    RHIBufferHandle   indexBuffer;
    uint32_t          firstIndex;
    uint32_t          indexCount;
    int32_t           vertexOffset;
    MaterialInstance* material;     // raw pointer; owned by matInstances vector
    RHIPipelineHandle pipeline;     // pre-warmed before the render loop
    uint32_t          pushConstantSize;
    glm::mat4         worldMatrix;
};

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
    devDesc.windowHandle    = NativeWindowHandle{window->GetNativeHandle()};
    devDesc.swapchainWidth  = window->GetWidth();
    devDesc.swapchainHeight = window->GetHeight();
    devDesc.vsync           = true;
    devDesc.enableValidation = true;
    auto device = VulkanDevice::Create(devDesc);

    // ── Frame uniforms ───────────────────────────────────────────────────────
    FrameUniformsBuffer frameUniforms;
    frameUniforms.Init(device.get());

    // ── Depth texture ────────────────────────────────────────────────────────
    RHITextureDesc depthDesc{};
    depthDesc.width     = 1280;
    depthDesc.height    = 720;
    depthDesc.format    = RHIFormat::D32F;
    depthDesc.usage     = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "Depth";
    RHITextureHandle depthTex = device->CreateTexture(depthDesc);

    // 1×1 white texture used as placeholder for unset material slots
    RHITextureDesc whiteDesc{};
    whiteDesc.width     = 1;
    whiteDesc.height    = 1;
    whiteDesc.format    = RHIFormat::RGBA8_UNORM;
    whiteDesc.usage     = RHITextureUsage::Sampled;
    whiteDesc.debugName = "White1x1";
    RHITextureHandle whiteTex = device->CreateTexture(whiteDesc);
    const uint32_t whitePixel = 0xFFFFFFFFu;
    device->UploadTextureData(whiteTex, &whitePixel, sizeof(whitePixel));

    // ── ResourceManager ──────────────────────────────────────────────────────
    ResourceManager resMgr;
    resMgr.Init(SceneDemo::COOK_CACHE_DIR, device.get());

    // ── PBR material type ────────────────────────────────────────────────────
    MaterialManager matMgr;
    matMgr.Init(device.get(), whiteTex);

    auto pbrType = BuildDefaultPbrType(device.get(), frameUniforms.GetLayout(),
                                        SceneDemo::BUILTIN_SHADER_DIR);
    if (!pbrType) {
        SA_LOG_CRITICAL("SceneDemo: failed to build PBR material type");
        return 1;
    }
    matMgr.RegisterType(std::move(pbrType));

    // ── Load scene ───────────────────────────────────────────────────────────
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

    // ── Ensure procedural cube mesh in cook cache ────────────────────────────
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
        auto& t       = scene.Registry().get<TransformComponent>(cubeEnt);
        t.position    = pos;
        t.rotation    = rot;
        scene.MarkDirty(cubeEnt);

        StaticMeshComponent smc;
        smc.meshAsset      = DefaultAssets::Cube;
        smc.castShadow     = true;
        smc.receiveShadow  = true;
        scene.Registry().emplace<StaticMeshComponent>(cubeEnt, std::move(smc));

        SA_LOG_INFO("SceneDemo: added RandomCube at ({:.2f}, {:.2f}, {:.2f})",
                    pos.x, pos.y, pos.z);
    }

    // ── Load IBL textures from IBLComponent ──────────────────────────────────
    {
        RHITextureHandle brdfLut, irradianceMap, prefilteredEnv;
        scene.View<IBLComponent>().each([&](entt::entity, const IBLComponent& ibl) {
            if (!brdfLut.IsValid()       && ibl.brdfLut.IsValid())
                brdfLut       = resMgr.LoadTexture(ibl.brdfLut);
            if (!irradianceMap.IsValid() && ibl.irradianceMap.IsValid())
                irradianceMap = resMgr.LoadTexture(ibl.irradianceMap);
            if (!prefilteredEnv.IsValid() && ibl.prefilteredEnvMap.IsValid())
                prefilteredEnv = resMgr.LoadTexture(ibl.prefilteredEnvMap);
        });
        if (brdfLut.IsValid() || irradianceMap.IsValid() || prefilteredEnv.IsValid()) {
            frameUniforms.SetIBLTextures(
                brdfLut.IsValid()       ? brdfLut       : whiteTex,
                prefilteredEnv.IsValid()? prefilteredEnv: whiteTex,
                prefilteredEnv.IsValid()? prefilteredEnv: whiteTex);
            SA_LOG_INFO("SceneDemo: IBL textures bound");
        }
    }

    // Flush dirty transforms (including the newly added cube) before building draw items.
    scene.UpdateTransforms();

    // ── Resolve meshes + create per-submesh MaterialInstances ────────────────
    // matInstances owns the unique_ptrs; drawItems holds raw pointers into it.
    std::vector<std::unique_ptr<MaterialInstance>> matInstances;
    std::vector<DrawItem> drawItems;

    scene.View<StaticMeshComponent, WorldTransformComponent>().each(
        [&](entt::entity e,
            const StaticMeshComponent&     meshComp,
            const WorldTransformComponent& world)
    {
        if (!meshComp.meshAsset.IsValid()) return;

        const GPUMesh* gpuMesh = resMgr.LoadMesh(meshComp.meshAsset);
        if (!gpuMesh) {
            SA_LOG_WARN("SceneDemo: mesh {} not found in cook cache",
                        meshComp.meshAsset.ToString());
            return;
        }

        for (size_t si = 0; si < gpuMesh->subMeshes.size(); ++si) {
            const auto& sub = gpuMesh->subMeshes[si];
            (void)si;  // matID from slot[si] reserved for Stage 3.9 .samat loading

            auto inst = matMgr.CreateInstance("PBR");
            if (!inst) continue;

            // Default PBR scalar params (UBO blob is zero-init; must set factors explicitly)
            inst->SetParam<glm::vec4>("baseColorFactor",  {1.f, 1.f, 1.f, 1.f});
            inst->SetParam<float>    ("roughnessFactor",   1.f);
            inst->SetParam<float>    ("metallicFactor",    0.f);
            inst->SetParam<float>    ("normalScale",       1.f);
            inst->SetParam<float>    ("occlusionStrength", 1.f);
            inst->SetParam<glm::vec3>("emissiveFactor",   {0.f, 0.f, 0.f});

            // Default PBR textures — overridden if slot has explicit AssetID
            auto loadOrWhite = [&](const AssetID& id) -> RHITextureHandle {
                auto h = resMgr.LoadTexture(id);
                return h.IsValid() ? h : whiteTex;
            };
            inst->SetTexture("t_BaseColor",         loadOrWhite(DefaultAssets::Albedo));
            inst->SetTexture("t_Normal",            loadOrWhite(DefaultAssets::Normal));
            inst->SetTexture("t_MetallicRoughness", loadOrWhite(DefaultAssets::MR));
            inst->SetTexture("t_Occlusion",         whiteTex);
            inst->SetTexture("t_Emissive",          whiteTex);

            // Apply per-entity parameter overrides if present
            if (const auto* ov = scene.Registry().try_get<MaterialOverrideComponent>(e)) {
                for (const auto& p : ov->params)
                    inst->SetRawParam(p.name, p.value.data(),
                                      static_cast<uint32_t>(p.value.size()));
            }

            DrawItem item{};
            item.vertexBuffer    = gpuMesh->vertexBuffer;
            item.indexBuffer     = gpuMesh->indexBuffer;
            item.firstIndex      = sub.firstIndex;
            item.indexCount      = sub.indexCount;
            item.vertexOffset    = sub.vertexOffset;
            item.material        = inst.get();
            item.worldMatrix     = world.matrix * sub.localTransform;
            item.pushConstantSize = inst->GetType()->shader
                                       .GetMergedReflection().pushConstantSize;

            matInstances.push_back(std::move(inst));
            drawItems.push_back(item);
        }
    });

    SA_LOG_INFO("SceneDemo: {} draw items prepared", drawItems.size());

    // ── Attachment keys ───────────────────────────────────────────────────────
    AttachmentKey skyboxKey{};
    skyboxKey.colorFormats[0] = device->GetSwapchainFormat();
    skyboxKey.colorCount      = 1;
    skyboxKey.depthFormat     = RHIFormat::Undefined;  // no depth (avoids Intel iGPU issues)

    AttachmentKey geoKey{};
    geoKey.colorCount      = 1;
    geoKey.colorFormats[0] = device->GetSwapchainFormat();
    geoKey.depthFormat     = RHIFormat::D32F;

    // Pre-warm PBR pipelines so no device call is needed inside the geometry lambda
    for (auto& item : drawItems)
        item.pipeline = item.material->GetPipeline(device.get(), geoKey);

    // ── Skybox ShaderProgram (same pattern as gltf_material_demo) ────────────
    ShaderProgram skyboxProgram;
    {
        const std::string shaderDir = SceneDemo::BUILTIN_SHADER_DIR;
        auto skyboxVertSpv = LoadSpv(shaderDir + "/skybox.vert.spv");
        auto skyboxFragSpv = LoadSpv(shaderDir + "/skybox.frag.spv");
        ShaderReflection skyboxVertRefl, skyboxFragRefl;
        const bool ok =
            !skyboxVertSpv.empty() && !skyboxFragSpv.empty() &&
            ShaderReflectionIO::LoadFromFile(shaderDir + "/skybox.vert.refl", skyboxVertRefl) &&
            ShaderReflectionIO::LoadFromFile(shaderDir + "/skybox.frag.refl", skyboxFragRefl);
        if (ok) {
            ShaderProgram::Desc pd;
            pd.vertSpv     = skyboxVertSpv;
            pd.vertRefl    = skyboxVertRefl;
            pd.fragSpv     = skyboxFragSpv;
            pd.fragRefl    = skyboxFragRefl;
            pd.frameLayout = frameUniforms.GetLayout();
            if (skyboxProgram.Load(device.get(), pd))
                SA_LOG_INFO("SceneDemo: skybox ShaderProgram loaded");
            else
                SA_LOG_ERROR("SceneDemo: skybox ShaderProgram load FAILED");
        }
    }

    // ── Render loop ──────────────────────────────────────────────────────────
    uint32_t frameIndex = 0;

    while (!window->ShouldClose()) {
        window->PollEvents();

        // Use swapchain dimensions (updated by ResizeSwapchain callback if resized)
        int w = static_cast<int>(device->GetSwapchainWidth());
        int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        // ── Update world transforms ──────────────────────────────────────
        scene.UpdateTransforms();

        // ── Extract camera from ECS ──────────────────────────────────────
        FrameUniforms fu{};
        fu.resolution = { static_cast<float>(w), static_cast<float>(h) };
        fu.time       = static_cast<float>(frameIndex) / 60.f;

        scene.View<CameraComponent, ActiveCameraTag, WorldTransformComponent>().each(
            [&](auto, const CameraComponent& cam, const WorldTransformComponent& wt)
        {
            const float aspect = static_cast<float>(w) / static_cast<float>(h);
            fu.view          = glm::inverse(wt.matrix);
            fu.proj          = glm::perspective(cam.fovY, aspect, cam.nearPlane, cam.farPlane);
            fu.proj[1][1]   *= -1.f;  // Vulkan Y-flip
            fu.viewProj      = fu.proj * fu.view;
            fu.invViewProj   = glm::inverse(fu.viewProj);
            fu.cameraPos     = glm::vec3(wt.matrix[3]);
        });

        // ── Extract light from ECS ───────────────────────────────────────
        LightUniforms lu{};
        lu.ambientColor     = {0.05f, 0.05f, 0.08f};
        lu.ambientIntensity = 1.f;

        scene.View<DirectionalLightComponent, TransformComponent>().each(
            [&](auto, const DirectionalLightComponent& light,
                const TransformComponent& t)
        {
            // Forward vector (-Z) rotated by entity's orientation
            lu.direction = glm::normalize(t.rotation * glm::vec3(0.f, 0.f, -1.f));
            lu.color     = light.color;
            lu.intensity = light.intensity;
        });

        // ── Begin frame ──────────────────────────────────────────────────
        // fi must come AFTER BeginFrame so it matches the device's in-flight slot.
        auto* cmd = device->BeginFrame();
        if (!cmd) continue;

        const uint32_t fi = device->GetCurrentFrameIndex();
        frameUniforms.Upload(fi, fu, lu);

        // ── RenderGraph ──────────────────────────────────────────────────
        RenderGraph rg;
        rg.Reset();

        auto rgSwapchain = rg.ImportTexture("Swapchain",
            device->GetSwapchainTexture(),
            RHIResourceState::RenderTarget,
            RHIResourceState::RenderTarget);
        auto rgDepth = rg.ImportTexture("Depth",
            depthTex,
            RHIResourceState::Undefined,
            RHIResourceState::Undefined);

        const auto frameDescSet = frameUniforms.GetDescriptorSet(fi);
        const int  localW = w, localH = h;

        // ── Skybox pass ───────────────────────────────────────────────────
        // Fullscreen triangle, no depth attachment (avoids Intel iGPU issues).
        if (skyboxProgram.IsLoaded()) {
            RHIPipelineHandle skyboxPipeline = skyboxProgram.GetOrCreatePipeline(
                device.get(), skyboxKey,
                RHICullMode::None, RHIBlendMode::Opaque,
                /*depthTest*/false, /*depthWrite*/false, /*noVertexInput*/true);

            rg.AddPass("Skybox",
                [rgSwapchain](RGPassBuilder& b) { b.Write(rgSwapchain); },
                [skyboxPipeline, frameDescSet, localW, localH, rgSwapchain]
                (IRHICommandList& cmd, const RGResources& res)
            {
                RHIRenderPassDesc rpDesc{};
                rpDesc.colorAttachmentCount            = 1;
                rpDesc.colorAttachments[0].texture     = res.Get(rgSwapchain);
                rpDesc.colorAttachments[0].clearOnLoad = false;  // sky covers all pixels
                rpDesc.hasDepth                        = false;
                rpDesc.width  = static_cast<uint32_t>(localW);
                rpDesc.height = static_cast<uint32_t>(localH);

                cmd.BeginRenderPass(rpDesc);
                cmd.SetViewport(RHIViewport{0.f, 0.f, float(localW), float(localH)});
                cmd.SetScissor(RHIScissor{0, 0, uint32_t(localW), uint32_t(localH)});
                cmd.SetPipeline(skyboxPipeline);
                cmd.SetDescriptorSet(0, frameDescSet);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
        }

        // ── Geometry pass ─────────────────────────────────────────────────
        // Loads skybox color, clears depth fresh each frame.
        rg.AddPass("Geometry",
            [rgSwapchain, rgDepth](RGPassBuilder& b) {
                b.Write(rgSwapchain);
                b.WriteDepth(rgDepth);
            },
            [&drawItems, frameDescSet, localW, localH,
             rgSwapchain, rgDepth]
            (IRHICommandList& cmd, const RGResources& res)
        {
            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount              = 1;
            rpDesc.colorAttachments[0].texture       = res.Get(rgSwapchain);
            rpDesc.colorAttachments[0].clearOnLoad   = false;  // load skybox pixels
            rpDesc.depthAttachment.texture           = res.Get(rgDepth);
            rpDesc.depthAttachment.clearOnLoad       = true;   // clear depth each frame
            rpDesc.depthAttachment.clearDepth        = 1.f;
            rpDesc.hasDepth                          = true;
            rpDesc.width  = static_cast<uint32_t>(localW);
            rpDesc.height = static_cast<uint32_t>(localH);

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHIViewport{0.f, 0.f, float(localW), float(localH)});
            cmd.SetScissor(RHIScissor{0, 0, uint32_t(localW), uint32_t(localH)});

            for (const auto& item : drawItems) {
                if (!item.pipeline.IsValid()) continue;
                cmd.SetPipeline(item.pipeline);
                cmd.SetDescriptorSet(0, frameDescSet);
                item.material->Bind(&cmd);
                cmd.SetVertexBuffer(0, item.vertexBuffer);
                cmd.SetIndexBuffer(item.indexBuffer);
                if (item.pushConstantSize > 0)
                    cmd.SetPushConstants(&item.worldMatrix, item.pushConstantSize,
                                         RHIShaderStage::Vertex);
                cmd.DrawIndexed(item.indexCount, 1, item.firstIndex,
                                item.vertexOffset, 0);
            }
            cmd.EndRenderPass();
        });

        rg.Compile();
        rg.Execute(*device, *cmd);

        device->EndFrame();
        device->Present();
        ++frameIndex;
    }

    // ── Save scene on exit ────────────────────────────────────────────────────
    if (!SceneSerializer::SaveToFile(scene, scenePath))
        SA_LOG_WARN("SceneDemo: failed to save scene on exit");

    // ── Cleanup (order mirrors gltf_material_demo) ────────────────────────────
    device->WaitIdle();
    skyboxProgram.Unload(device.get());
    drawItems.clear();
    matInstances.clear();       // destroy MaterialInstances before matMgr.Shutdown()
    matMgr.Shutdown();
    device->DestroyTexture(depthTex);
    device->DestroyTexture(whiteTex);
    frameUniforms.Shutdown();
    resMgr.Shutdown();
    device.reset();
    window.reset();
    SA_LOG_INFO("SceneDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
