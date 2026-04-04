#include "core/logs/Log.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/ShaderReflection.hpp"
#include "function/FrameUniforms.hpp"
#include "resource/ResourceManager.hpp"
#include "function/FrameUniformsBuffer.hpp"
#include "function/material/AttachmentKey.hpp"
#include "function/material/ShaderProgram.hpp"
#include "function/material/MaterialType.hpp"
#include "function/material/MaterialInstance.hpp"
#include "function/material/MaterialManager.hpp"
#include "MaterialDemoPath.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;

// ─── helpers ─────────────────────────────────────────────────────────────────

static std::vector<uint8_t> LoadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { SA_LOG_ERROR("LoadSpv: cannot open '{}'", path); return {}; }
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return data;
}

// ─── Cube geometry — 24 verts (4/face), 36 indices ───────────────────────────
// Each vertex: pos(3) + normal(3) + tangent(4, w=handedness) + uv(2) = 12 floats = 48 bytes

struct Vert {
    float p[3]; float n[3]; float t[4]; float uv[2];
};

// clang-format off
static const Vert kCubeVerts[24] = {
    // +Z (front)
    {{-.5f,-.5f, .5f},{0,0,1},{1,0,0,1},{0,1}}, {{ .5f,-.5f, .5f},{0,0,1},{1,0,0,1},{1,1}},
    {{ .5f, .5f, .5f},{0,0,1},{1,0,0,1},{1,0}}, {{-.5f, .5f, .5f},{0,0,1},{1,0,0,1},{0,0}},
    // -Z (back)
    {{ .5f,-.5f,-.5f},{0,0,-1},{-1,0,0,1},{0,1}}, {{-.5f,-.5f,-.5f},{0,0,-1},{-1,0,0,1},{1,1}},
    {{-.5f, .5f,-.5f},{0,0,-1},{-1,0,0,1},{1,0}}, {{ .5f, .5f,-.5f},{0,0,-1},{-1,0,0,1},{0,0}},
    // +X (right)
    {{ .5f,-.5f, .5f},{1,0,0},{0,0,-1,1},{0,1}}, {{ .5f,-.5f,-.5f},{1,0,0},{0,0,-1,1},{1,1}},
    {{ .5f, .5f,-.5f},{1,0,0},{0,0,-1,1},{1,0}}, {{ .5f, .5f, .5f},{1,0,0},{0,0,-1,1},{0,0}},
    // -X (left)
    {{-.5f,-.5f,-.5f},{-1,0,0},{0,0,1,1},{0,1}}, {{-.5f,-.5f, .5f},{-1,0,0},{0,0,1,1},{1,1}},
    {{-.5f, .5f, .5f},{-1,0,0},{0,0,1,1},{1,0}}, {{-.5f, .5f,-.5f},{-1,0,0},{0,0,1,1},{0,0}},
    // +Y (top)
    {{-.5f, .5f, .5f},{0,1,0},{1,0,0,1},{0,1}}, {{ .5f, .5f, .5f},{0,1,0},{1,0,0,1},{1,1}},
    {{ .5f, .5f,-.5f},{0,1,0},{1,0,0,1},{1,0}}, {{-.5f, .5f,-.5f},{0,1,0},{1,0,0,1},{0,0}},
    // -Y (bottom)
    {{-.5f,-.5f,-.5f},{0,-1,0},{1,0,0,1},{0,1}}, {{ .5f,-.5f,-.5f},{0,-1,0},{1,0,0,1},{1,1}},
    {{ .5f,-.5f, .5f},{0,-1,0},{1,0,0,1},{1,0}}, {{-.5f,-.5f, .5f},{0,-1,0},{1,0,0,1},{0,0}},
};

static const uint16_t kCubeIdx[36] = {
     0, 1, 2,  0, 2, 3,   // front
     4, 5, 6,  4, 6, 7,   // back
     8, 9,10,  8,10,11,   // right
    12,13,14, 12,14,15,   // left
    16,17,18, 16,18,19,   // top
    20,21,22, 20,22,23,   // bottom
};
// clang-format on

// ─── Build PBR ShaderReflection (matches pbr.vert / pbr.frag) ────────────────
// Since we have no .refl cook step yet, we hand-author the bindings.

static ShaderReflection MakePbrVertRefl() {
    ShaderReflection r;
    // frame_uniforms.glsl — set=0, binding=0 (u_Frame) accessed in vert
    { ShaderBindingDesc b; b.set=0; b.binding=0; b.type=RHIDescriptorType::UniformBuffer;
      b.stages=RHIShaderStage::Vertex; b.name="FrameData"; r.bindings.push_back(b); }
    // push constant: mat4 model (64B)
    r.pushConstantSize   = 64;
    r.pushConstantStages = RHIShaderStage::Vertex;
    return r;
}

static ShaderReflection MakePbrFragRefl() {
    ShaderReflection r;
    // frame_uniforms.glsl — binding=0 and binding=1 used in frag
    { ShaderBindingDesc b; b.set=0; b.binding=0; b.type=RHIDescriptorType::UniformBuffer;
      b.stages=RHIShaderStage::Fragment; b.name="FrameData"; r.bindings.push_back(b); }
    { ShaderBindingDesc b; b.set=0; b.binding=1; b.type=RHIDescriptorType::UniformBuffer;
      b.stages=RHIShaderStage::Fragment; b.name="LightData"; r.bindings.push_back(b); }
    // set=1 — MaterialParams UBO
    { ShaderBindingDesc b; b.set=1; b.binding=0; b.type=RHIDescriptorType::UniformBuffer;
      b.stages=RHIShaderStage::Fragment; b.name="MaterialParams"; r.bindings.push_back(b); }
    // set=1 — five samplers
    const char* sampNames[] = {"t_BaseColor","t_Normal","t_MetallicRoughness","t_Occlusion","t_Emissive"};
    for (uint32_t i = 0; i < 5; ++i) {
        ShaderBindingDesc b; b.set=1; b.binding=i+1; b.type=RHIDescriptorType::Texture2D;
        b.stages=RHIShaderStage::Fragment; b.name=sampNames[i]; r.bindings.push_back(b);
    }
    return r;
}

// ─── Build PBR MaterialType ───────────────────────────────────────────────────

// MaterialParams layout (must match pbr.frag set=1 binding=0):
//   vec4  baseColorFactor    : offset=0,  size=16
//   float roughnessFactor    : offset=16, size=4
//   float metallicFactor     : offset=20, size=4
//   float normalScale        : offset=24, size=4
//   float occlusionStrength  : offset=28, size=4
//   vec3  emissiveFactor     : offset=32, size=12
//   float _pad               : offset=44, size=4
// Total: 48 bytes

static std::unique_ptr<MaterialType> BuildPbrType(IRHIDevice*        device,
                                                    RHIDescLayoutHandle frameLayout,
                                                    const std::vector<uint8_t>& vertSpv,
                                                    const std::vector<uint8_t>& fragSpv) {
    auto type      = std::make_unique<MaterialType>();
    type->name     = "PBR";
    type->uboSize  = 48;

    type->params = {
        {"baseColorFactor",   0, 16},
        {"roughnessFactor",  16,  4},
        {"metallicFactor",   20,  4},
        {"normalScale",      24,  4},
        {"occlusionStrength",28,  4},
        {"emissiveFactor",   32, 12},
    };

    type->textures = {
        {"t_BaseColor",          1, 0},
        {"t_Normal",             2, 1},
        {"t_MetallicRoughness",  3, 2},
        {"t_Occlusion",          4, 3},
        {"t_Emissive",           5, 4},
    };

    type->defaultCullMode  = RHICullMode::Back;
    type->defaultDepthTest  = true;
    type->defaultDepthWrite = true;

    ShaderProgram::Desc pd;
    pd.vertSpv    = vertSpv;
    pd.vertRefl   = MakePbrVertRefl();
    pd.fragSpv    = fragSpv;
    pd.fragRefl   = MakePbrFragRefl();
    pd.frameLayout = frameLayout;

    if (!type->shader.Load(device, pd)) return nullptr;
    return type;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== Material Demo (Stage 3.5) ===");

    // ── Window ────────────────────────────────────────────────────────────────
    auto window = GLFWWindow::Create({
        .width=1280, .height=720,
        .title="StellarAlia - Material Demo",
        .resizable=true,
    });
    if (!window) { SA_LOG_CRITICAL("Window creation failed"); return 1; }

    // ── Device ────────────────────────────────────────────────────────────────
    auto device = VulkanDevice::Create({
        .windowHandle      = {window->GetNativeHandle()},
        .swapchainWidth    = window->GetWidth(),
        .swapchainHeight   = window->GetHeight(),
        .swapchainImageCount = 2,
        .vsync             = true,
        .enableValidation  = true,
    });
    if (!device) { SA_LOG_CRITICAL("Device creation failed"); return 1; }

    // ── Frame uniforms (set=0) ────────────────────────────────────────────────
    FrameUniformsBuffer frameUniforms;
    frameUniforms.Init(device.get());

    // ── Depth texture ─────────────────────────────────────────────────────────
    auto createDepthTex = [&](uint32_t w, uint32_t h) {
        RHITextureDesc dd{};
        dd.width    = w; dd.height = h;
        dd.format   = RHIFormat::D32F;
        dd.usage    = RHITextureUsage::DepthStencil;
        dd.debugName = "SceneDepth";
        return device->CreateTexture(dd);
    };
    RHITextureHandle depthTex = createDepthTex(window->GetWidth(), window->GetHeight());

    // ── Shaders ───────────────────────────────────────────────────────────────
    const std::string shaderDir = MatDemo::BUILTIN_SHADER_DIR;
    auto vertSpv = LoadSpv(shaderDir + "/pbr.vert.spv");
    auto fragSpv = LoadSpv(shaderDir + "/pbr.frag.spv");
    if (vertSpv.empty() || fragSpv.empty()) {
        SA_LOG_CRITICAL("Failed to load pbr shader .spv — build the project first");
        frameUniforms.Shutdown();
        device->DestroyTexture(depthTex);
        device->WaitIdle();
        return 1;
    }

    // ── ResourceManager + MaterialManager ────────────────────────────────────
    Resource::ResourceManager resMgr;
    resMgr.Init(MatDemo::PROJECT_COOK_CACHE, device.get());

    MaterialManager matMgr;
    matMgr.Init(device.get(), &resMgr);

    auto pbrType = BuildPbrType(device.get(), frameUniforms.GetLayout(), vertSpv, fragSpv);
    if (!pbrType) {
        SA_LOG_CRITICAL("Failed to build PBR material type");
        return 1;
    }
    matMgr.RegisterType(std::move(pbrType));

    // ── Gold material instance ────────────────────────────────────────────────
    auto goldMat = matMgr.CreateInstance("PBR");
    // Gold: warm yellow base, high metallic, low roughness
    goldMat->SetVec4 ("baseColorFactor",  {1.0f, 0.84f, 0.0f, 1.0f});
    goldMat->SetFloat ("roughnessFactor",  0.2f);
    goldMat->SetFloat ("metallicFactor",   1.0f);
    goldMat->SetFloat ("normalScale",      1.0f);
    goldMat->SetFloat ("occlusionStrength",1.0f);

    // ── Cube mesh ─────────────────────────────────────────────────────────────
    RHIBufferHandle vb, ib;
    {
        RHIBufferDesc vbd{};
        vbd.size = sizeof(kCubeVerts);
        vbd.usage = RHIBufferUsage::Vertex;
        vbd.debugName = "CubeVB";
        vb = device->CreateBuffer(vbd);
        device->UploadBufferData(vb, kCubeVerts, sizeof(kCubeVerts));

        RHIBufferDesc ibd{};
        ibd.size = sizeof(kCubeIdx);
        ibd.usage = RHIBufferUsage::Index;
        ibd.debugName = "CubeIB";
        ib = device->CreateBuffer(ibd);
        device->UploadBufferData(ib, kCubeIdx, sizeof(kCubeIdx));
    }

    SA_LOG_INFO("Material Demo: entering render loop");

    // ── Render loop ───────────────────────────────────────────────────────────
    float    time = 0.f;
    uint32_t lastW = window->GetWidth(), lastH = window->GetHeight();

    while (!window->ShouldClose()) {
        window->PollEvents();

        const uint32_t W = window->GetWidth(), H = window->GetHeight();
        if (W != lastW || H != lastH) {
            lastW = W; lastH = H;
            device->WaitIdle();
            device->DestroyTexture(depthTex);
            device->ResizeSwapchain(lastW, lastH);
            depthTex = createDepthTex(lastW, lastH);
        }

        IRHICommandList* cmd = device->BeginFrame();
        if (!cmd) continue;

        const uint32_t w  = device->GetSwapchainWidth();
        const uint32_t h  = device->GetSwapchainHeight();
        const uint32_t fi = device->GetCurrentFrameIndex();

        // ── Upload frame uniforms ─────────────────────────────────────────────
        time += 0.016f;

        FrameUniforms fu{};
        fu.view       = glm::lookAt(glm::vec3(0.f, 1.5f, 3.f),
                                     glm::vec3(0.f),
                                     glm::vec3(0.f, 1.f, 0.f));
        fu.proj       = glm::perspective(glm::radians(60.f),
                                          static_cast<float>(w) / static_cast<float>(h),
                                          0.01f, 100.f);
        fu.proj[1][1] *= -1.f;  // flip Y for Vulkan NDC
        fu.viewProj    = fu.proj * fu.view;
        fu.invViewProj = glm::inverse(fu.viewProj);
        fu.cameraPos   = glm::vec3(0.f, 1.5f, 3.f);
        fu.time        = time;
        fu.resolution  = glm::vec2(static_cast<float>(w), static_cast<float>(h));
        fu.deltaTime   = 0.016f;

        LightUniforms lu{};
        lu.lights[0].direction = glm::normalize(glm::vec3(1.f, -1.f, 0.5f));
        lu.lights[0].intensity = 3.f;
        lu.lights[0].color     = glm::vec3(1.f, 0.98f, 0.95f);
        lu.lights[0].type      = 0;
        lu.lightCount          = 1;

        frameUniforms.Upload(fi, fu, lu);

        // ── Transition depth ──────────────────────────────────────────────────
        cmd->TransitionTexture(depthTex,
                               RHIResourceState::Undefined,
                               RHIResourceState::DepthWrite);

        // ── Attachment key (for pipeline cache) ───────────────────────────────
        AttachmentKey key;
        key.colorFormats[0] = device->GetSwapchainFormat();
        key.colorCount      = 1;
        key.depthFormat     = RHIFormat::D32F;

        // ── Render pass ───────────────────────────────────────────────────────
        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachments[0].texture     = device->GetSwapchainTexture();
        passDesc.colorAttachments[0].clearOnLoad = true;
        passDesc.colorAttachments[0].clearColor[0] = 0.05f;
        passDesc.colorAttachments[0].clearColor[1] = 0.05f;
        passDesc.colorAttachments[0].clearColor[2] = 0.08f;
        passDesc.colorAttachments[0].clearColor[3] = 1.f;
        passDesc.colorAttachmentCount  = 1;
        passDesc.hasDepth              = true;
        passDesc.depthAttachment.texture    = depthTex;
        passDesc.depthAttachment.clearOnLoad = true;
        passDesc.depthAttachment.clearDepth  = 1.f;
        passDesc.width  = w;
        passDesc.height = h;

        cmd->BeginRenderPass(passDesc);
        cmd->SetViewport({0.f, 0.f, static_cast<float>(w), static_cast<float>(h)});
        cmd->SetScissor({0, 0, w, h});

        // ── Draw cube with gold PBR material ─────────────────────────────────
        RHIPipelineHandle pipeline = goldMat->GetPipeline(device.get(), key);
        if (pipeline.IsValid()) {
            cmd->SetPipeline(pipeline);
            cmd->SetDescriptorSet(0, frameUniforms.GetDescriptorSet(
                fi % FrameUniformsBuffer::MAX_FRAMES));
            goldMat->Bind(cmd);

            // Model matrix: spin around Y
            glm::mat4 model = glm::rotate(glm::mat4(1.f),
                                           time * 0.8f,
                                           glm::vec3(0.f, 1.f, 0.f));
            cmd->SetPushConstants(&model, 64, RHIShaderStage::Vertex);

            cmd->SetVertexBuffer(0, vb, 0);
            cmd->SetIndexBuffer(ib, 0, true);  // uint16
            cmd->DrawIndexed(36, 1, 0, 0, 0);
        }

        cmd->EndRenderPass();

        device->EndFrame();
        device->Present();
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    device->WaitIdle();

    device->DestroyBuffer(vb);
    device->DestroyBuffer(ib);
    device->DestroyTexture(depthTex);
    matMgr.Shutdown();
    frameUniforms.Shutdown();
    resMgr.Shutdown();

    device.reset();
    window.reset();

    SA_LOG_INFO("Material Demo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
