#include "core/logs/Log.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "resource/ResourceManager.hpp"
#include "function/FrameUniformsBuffer.hpp"
#include "function/material/AttachmentKey.hpp"
#include "function/material/MaterialType.hpp"
#include "function/material/MaterialInstance.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/ProgramCache.hpp"
#include "function/renderer/RenderFeature.hpp"   // FeatureInitContext
#include "MaterialDemoPath.hpp"

using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;

// ─────────────────────────────────────────────────────────────────────────────
// MaterialDemo — unit test for the material + shader COOK and DISPATCH paths.
//
//   1. shader cook   — RegisterTypeFromShaders("pbr") loads the cooked pbr.vert /
//                      pbr.frag .spv + .refl and builds the MaterialType's param /
//                      texture layout entirely from the cooked reflection (this demo
//                      used to hand-author the bindings — it now exercises the real
//                      cook output instead).
//   2. material cook — LoadMaterial() reads the cooked default_pbr.samat (baked
//                      params + texture references) into a MaterialInstance.
//   3. dispatch      — GetPipeline() assembles a GPU pipeline from the cooked shader,
//                      proving the SPIR-V + reflection yield a valid pipeline layout
//                      (i.e. the material is dispatch-ready).
//
// This is an interface unit test: it verifies the cook→load→pipeline chain and exits
// PASS/FAIL. Rendering an actual lit frame is a full-rendering concern that belongs
// in the demo_project test project, not here.
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== MaterialDemo — material/shader cook + dispatch test ===");

    // A window/device is required only to create GPU resources (shaders, pipelines).
    auto window = GLFWWindow::Create({
        .width = 256, .height = 256,
        .title = "MaterialDemo (cook+dispatch test)",
        .resizable = false,
    });
    if (!window) { SA_LOG_CRITICAL("Window creation failed"); return 1; }

    auto device = VulkanDevice::Create({
        .windowHandle        = {window->GetNativeHandle()},
        .swapchainWidth      = window->GetWidth(),
        .swapchainHeight     = window->GetHeight(),
        .swapchainImageCount = 2,
        .vsync               = true,
        .enableValidation    = true,   // surfaces any pipeline/layout mismatch
    });
    if (!device) { SA_LOG_CRITICAL("Device creation failed"); return 1; }

    const std::string shaderDir = MatDemo::BUILTIN_SHADER_DIR;

    Resource::ResourceManager resMgr;
    resMgr.Init(MatDemo::PROJECT_COOK_CACHE, device.get());

    FrameUniformsBuffer frameUniforms;
    frameUniforms.Init(device.get());              // provides set=1 frame layout

    MaterialManager matMgr;
    matMgr.Init(device.get(), &resMgr);

    ProgramCache programCache;
    programCache.Init(device.get(), frameUniforms.GetLayout(),
                      matMgr.GetTextureHeap().GetLayout(), shaderDir);

    const FeatureInitContext ctx{ device.get(), &matMgr, &resMgr,
                                  frameUniforms.GetLayout(), shaderDir, shaderDir,
                                  &programCache };

    int failures = 0;

    // ── TEST 1: shader cook → reflection → MaterialType ──────────────────────
    MaterialTypeDesc pbrDesc{};
    pbrDesc.name       = "PBR";
    pbrDesc.vertShader = "pbr";
    pbrDesc.fragShader = "pbr";
    pbrDesc.cullMode   = RHICullMode::Back;
    if (!matMgr.RegisterTypeFromShaders(pbrDesc, ctx)) {
        SA_LOG_ERROR("[FAIL] shader cook: RegisterTypeFromShaders(pbr) failed — build shaders first");
        ++failures;
    }
    MaterialType* pbrType = matMgr.GetType("PBR");
    if (!pbrType) {
        SA_LOG_ERROR("[FAIL] shader cook: GetType(\"PBR\") returned null");
        ++failures;
    } else {
        SA_LOG_INFO("[OK]   shader cook: PBR type from cooked reflection — uboSize={} params={} textures={}",
                    pbrType->uboSize, pbrType->params.size(), pbrType->textures.size());
        if (pbrType->params.empty() || pbrType->textures.empty() ||
            !pbrType->FindTexture("t_BaseColor") || !pbrType->FindParam("baseColorFactor")) {
            SA_LOG_ERROR("[FAIL] cooked reflection missing expected params/textures");
            ++failures;
        }
    }

    // ── TEST 2: material cook → MaterialInstance (default_pbr.samat) ──────────
    // The cooked .samat carries "type":"PBR" + baked params; LoadMaterial reads it.
    const AssetID kDefaultPbr = AssetID::FromString("d3f4abb0-0000-4000-0000-000000000001");
    if (matMgr.LoadMaterial(kDefaultPbr, resMgr)) {
        SA_LOG_INFO("[OK]   material cook: default_pbr.samat loaded from cook cache");
    } else {
        SA_LOG_ERROR("[FAIL] material cook: default_pbr.samat failed to load (uuid {})",
                     kDefaultPbr.ToString());
        ++failures;
    }

    // ── TEST 3: dispatch-readiness — cooked shader → GPU pipeline ────────────
    if (auto goldMat = matMgr.CreateInstance("PBR")) {
        goldMat->SetVec4 ("baseColorFactor",  {1.0f, 0.84f, 0.0f, 1.0f});
        goldMat->SetFloat("roughnessFactor",  0.2f);
        goldMat->SetFloat("metallicFactor",   1.0f);

        AttachmentKey key{};
        key.colorFormats[0] = device->GetSwapchainFormat();
        key.colorCount      = 1;
        key.depthFormat     = RHIFormat::D32F;

        if (goldMat->GetPipeline(device.get(), key).IsValid()) {
            SA_LOG_INFO("[OK]   dispatch: pipeline created from cooked pbr shader (dispatch-ready)");
        } else {
            SA_LOG_ERROR("[FAIL] dispatch: pipeline creation from cooked shader failed");
            ++failures;
        }
        device->WaitIdle();
        goldMat.reset();   // free instance GPU resources before device teardown
    } else {
        SA_LOG_ERROR("[FAIL] dispatch: CreateInstance(\"PBR\") returned null");
        ++failures;
    }

    // ── Result ────────────────────────────────────────────────────────────────
    if (failures == 0)
        SA_LOG_INFO("=== Material cook + dispatch test: PASS ===");
    else
        SA_LOG_CRITICAL("=== Material cook + dispatch test: FAIL ({} failure(s)) ===", failures);

    // ── Shutdown ──────────────────────────────────────────────────────────────
    device->WaitIdle();
    matMgr.Shutdown();
    programCache.Shutdown();
    frameUniforms.Shutdown();
    resMgr.Shutdown();
    device.reset();
    window.reset();

    SA_LOG_INFO("MaterialDemo: clean shutdown");
    Core::Log::Shutdown();
    return failures == 0 ? 0 : 1;
}
