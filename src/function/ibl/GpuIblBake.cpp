#include "function/ibl/GpuIblBake.hpp"
#include "function/ibl/SHProjection.hpp"

#include "core/logs/Log.hpp"
#include "platform/rhi/IRHICommandList.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"

#include <algorithm>
#include <array>
#include <fstream>

namespace StellarAlia {

// ─── Internal helpers ─────────────────────────────────────────────────────────

static std::vector<uint8_t> LoadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { SA_LOG_ERROR("GpuIblBake: cannot open '{}'", path); return {}; }
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return data;
}

static RHI::ShaderReflection LoadRefl(const std::string& path) {
    RHI::ShaderReflection r;
    if (!RHI::ShaderReflectionIO::LoadFromFile(path, r))
        SA_LOG_ERROR("GpuIblBake: cannot open '{}'", path);
    return r;
}

// ─── Init / Shutdown ──────────────────────────────────────────────────────────

bool GpuIblBake::Init(RHI::IRHIDevice* device, const std::string& shaderDir) {
    const auto brdfSpv = LoadSpv(shaderDir + "/ibl_brdf_lut.comp.spv");
    const auto prefSpv = LoadSpv(shaderDir + "/ibl_prefilter.comp.spv");
    const auto cubeSpv = LoadSpv(shaderDir + "/equirect_to_cube.comp.spv");
    if (brdfSpv.empty() || prefSpv.empty() || cubeSpv.empty()) return false;

    if (!m_brdfProg.Load(device, {brdfSpv,
                                  LoadRefl(shaderDir + "/ibl_brdf_lut.comp.refl")})) {
        SA_LOG_ERROR("GpuIblBake: BRDF LUT program load failed");
        return false;
    }
    if (!m_prefProg.Load(device, {prefSpv,
                                   LoadRefl(shaderDir + "/ibl_prefilter.comp.refl")})) {
        SA_LOG_ERROR("GpuIblBake: prefilter program load failed");
        return false;
    }
    if (!m_equirectCubeProg.Load(device, {cubeSpv,
                                          LoadRefl(shaderDir + "/equirect_to_cube.comp.refl")})) {
        SA_LOG_ERROR("GpuIblBake: equirect_to_cube program load failed");
        return false;
    }

    SA_LOG_INFO("GpuIblBake: shaders loaded");
    return true;
}

void GpuIblBake::Shutdown(RHI::IRHIDevice* device) {
    m_brdfProg.Unload(device);
    m_prefProg.Unload(device);
    m_equirectCubeProg.Unload(device);
}

// ─── BakeBrdfLut ──────────────────────────────────────────────────────────────

RHI::RHITextureHandle GpuIblBake::BakeBrdfLut(RHI::IRHIDevice* device)
{
    constexpr uint32_t kBrdfSize = 512u;

    RHI::RHITextureDesc d{};
    d.width     = kBrdfSize;
    d.height    = kBrdfSize;
    d.format    = RHI::RHIFormat::RGBA32F;
    d.usage     = RHI::RHITextureUsage::UnorderedAccess
                | RHI::RHITextureUsage::Sampled
                | RHI::RHITextureUsage::CopySrc;
    d.debugName = "IBL_BrdfLut";
    RHI::RHITextureHandle lut = device->CreateTexture(d);

    auto pipeline = m_brdfProg.GetPipeline(device);
    if (!pipeline.IsValid()) {
        SA_LOG_ERROR("GpuIblBake::BakeBrdfLut: pipeline creation failed");
        device->DestroyTexture(lut);
        return {};
    }

    auto ds = device->AllocateDescriptorSet(m_brdfProg.GetLayout(0));
    device->WriteDescriptorStorageImage(ds, 0, lut);

    using RS = RHI::RHIResourceState;
    device->ImmediateCompute([&](RHI::IRHICommandList* cmd) {
        cmd->TransitionTexture(lut, RS::Undefined, RS::UnorderedAccess);
        cmd->SetComputePipeline(pipeline);
        cmd->SetDescriptorSet(0, ds);
        cmd->Dispatch((kBrdfSize + 7) / 8, (kBrdfSize + 7) / 8, 1);
        cmd->TransitionTexture(lut, RS::UnorderedAccess, RS::ShaderRead);
    });

    SA_LOG_INFO("GpuIblBake: BRDF LUT baked (standalone)");
    return lut;
}

// ─── Bake ─────────────────────────────────────────────────────────────────────

GpuIblBake::Result GpuIblBake::Bake(RHI::IRHIDevice* device,
                                     const Resource::ImageData& hdr) {
    Result result{};

    if (!hdr.isHDR || hdr.pixelsHDR.empty()) {
        SA_LOG_ERROR("GpuIblBake::Bake — input is not a valid HDR image");
        return result;
    }

    // ── CPU: project HDR into SH coefficients ────────────────────────────────
    IBL::ProjectHDRtoSH(hdr, result.shCoeffs);
    SA_LOG_INFO("GpuIblBake: SH projection complete");

    // ── Upload equirect HDR to GPU ────────────────────────────────────────────
    RHI::RHITextureDesc hdrDesc{};
    hdrDesc.width     = hdr.width;
    hdrDesc.height    = hdr.height;
    hdrDesc.format    = RHI::RHIFormat::RGBA32F;
    hdrDesc.usage     = RHI::RHITextureUsage::Sampled;
    hdrDesc.debugName = "IBL_HDR_Input";
    RHI::RHITextureHandle hdrTex = device->CreateTexture(hdrDesc);
    const uint64_t hdrSz = static_cast<uint64_t>(hdr.width) * hdr.height * 4 * sizeof(float);
    device->UploadTextureData(hdrTex, hdr.pixelsHDR.data(), hdrSz);

    // ── Create output textures ────────────────────────────────────────────────
    constexpr uint32_t kBrdfSize    = 512u;
    constexpr uint32_t kPrefSize    = 512u;
    constexpr uint32_t kPrefMips    = 5u;
    constexpr uint32_t kSkyboxSize  = 1024u;
    // Intermediate cubemap: full mip chain so prefilter can use PDF-based LOD selection.
    // For 512×512: floor(log2(512)) + 1 = 10 mip levels.
    constexpr uint32_t kIntermMips  = 10u;

    // brdfLut is a 2D lookup table (NdotV × roughness) — NOT a cubemap.
    {
        RHI::RHITextureDesc d{};
        d.width     = kBrdfSize;
        d.height    = kBrdfSize;
        d.format    = RHI::RHIFormat::RGBA32F;
        d.usage     = RHI::RHITextureUsage::UnorderedAccess
                    | RHI::RHITextureUsage::Sampled
                    | RHI::RHITextureUsage::CopySrc;
        d.debugName = "IBL_BrdfLut";
        result.brdfLut = device->CreateTexture(d);
    }

    // Helper: create a cubemap storage texture (6 faces)
    auto makeCubemap = [&](uint32_t size, uint32_t mips, const char* name) {
        RHI::RHITextureDesc d{};
        d.width     = size;
        d.height    = size;
        d.mipLevels = mips;
        d.cubemap   = true;     // arrayLayers forced to 6 in VulkanDevice
        d.format    = RHI::RHIFormat::RGBA32F;
        d.usage     = RHI::RHITextureUsage::UnorderedAccess
                    | RHI::RHITextureUsage::Sampled
                    | RHI::RHITextureUsage::CopySrc;
        d.debugName = name;
        return device->CreateTexture(d);
    };

    // Intermediate cubemap: equirect converted to cube, then mipmapped.
    // The prefilter shader samples from this with LOD selection to suppress fireflies.
    RHI::RHITextureHandle intermCube  = makeCubemap(kPrefSize,   kIntermMips, "IBL_IntermCubemap");
    result.prefilteredEnv             = makeCubemap(kPrefSize,   kPrefMips,   "IBL_PrefilteredEnv");
    result.skyboxCubemap              = makeCubemap(kSkyboxSize, 1,           "IBL_SkyboxCubemap");

    // ── Pipelines ─────────────────────────────────────────────────────────────
    auto brdfPipeline = m_brdfProg.GetPipeline(device);
    auto prefPipeline = m_prefProg.GetPipeline(device);
    auto cubePipeline = m_equirectCubeProg.GetPipeline(device);
    if (!brdfPipeline.IsValid() || !prefPipeline.IsValid() || !cubePipeline.IsValid()) {
        SA_LOG_ERROR("GpuIblBake: pipeline creation failed");
        device->DestroyTexture(hdrTex);
        device->DestroyTexture(intermCube);
        device->DestroyTexture(result.brdfLut);
        device->DestroyTexture(result.prefilteredEnv);
        device->DestroyTexture(result.skyboxCubemap);
        return {};
    }

    // ── Descriptor sets ───────────────────────────────────────────────────────

    // BRDF LUT: binding=0 storage image
    auto brdfDs = device->AllocateDescriptorSet(m_brdfProg.GetLayout(0));
    device->WriteDescriptorStorageImage(brdfDs, 0, result.brdfLut);

    // equirect→cube for intermediate cubemap: binding=0=output, binding=1=equirect input
    auto intermCubeDs = device->AllocateDescriptorSet(m_equirectCubeProg.GetLayout(0));
    device->WriteDescriptorStorageImage(intermCubeDs, 0, intermCube);
    device->WriteDescriptorTexture(intermCubeDs, 1, hdrTex);

    // equirect→cube for skybox: binding=0=output, binding=1=equirect input
    auto skyboxDs = device->AllocateDescriptorSet(m_equirectCubeProg.GetLayout(0));
    device->WriteDescriptorStorageImage(skyboxDs, 0, result.skyboxCubemap);
    device->WriteDescriptorTexture(skyboxDs, 1, hdrTex);

    // Prefiltered env: one DS per mip.
    // binding=0 = output mip (image2DArray), binding=1 = input cubemap (samplerCube)
    std::array<RHI::RHIDescSetHandle, kPrefMips> prefDs{};
    for (uint32_t m = 0; m < kPrefMips; ++m) {
        prefDs[m] = device->AllocateDescriptorSet(m_prefProg.GetLayout(0));
        device->WriteDescriptorStorageImageMip(prefDs[m], 0, result.prefilteredEnv, m);
        device->WriteDescriptorTexture(prefDs[m], 1, intermCube);  // samplerCube
    }

    // ── Dispatch ─────────────────────────────────────────────────────────────
    using RS = RHI::RHIResourceState;

    device->ImmediateCompute([&](RHI::IRHICommandList* cmd) {
        // Transition all outputs to UAV for compute writes
        cmd->TransitionTexture(result.brdfLut,        RS::Undefined, RS::UnorderedAccess);
        cmd->TransitionTexture(intermCube,             RS::Undefined, RS::UnorderedAccess);
        cmd->TransitionTexture(result.skyboxCubemap,  RS::Undefined, RS::UnorderedAccess);
        cmd->TransitionTexture(result.prefilteredEnv, RS::Undefined, RS::UnorderedAccess);

        // Step 1: Convert equirect HDR → intermediate cubemap (mip 0 only)
        cmd->SetComputePipeline(cubePipeline);
        cmd->SetDescriptorSet(0, intermCubeDs);
        cmd->Dispatch((kPrefSize + 7) / 8, (kPrefSize + 7) / 8, 6);

        // Step 2: Convert equirect HDR → skybox cubemap
        cmd->SetDescriptorSet(0, skyboxDs);
        cmd->Dispatch((kSkyboxSize + 7) / 8, (kSkyboxSize + 7) / 8, 6);

        // Step 3: Build full mip chain on the intermediate cubemap for PDF LOD selection.
        // GenerateMipmaps expects mip 0 in ShaderRead on entry; all mips in ShaderRead on return.
        cmd->TransitionTexture(intermCube, RS::UnorderedAccess, RS::ShaderRead);
        cmd->GenerateMipmaps(intermCube);

        // Step 4: BRDF LUT — 512×512, 2D
        cmd->SetComputePipeline(brdfPipeline);
        cmd->SetDescriptorSet(0, brdfDs);
        cmd->Dispatch((kBrdfSize + 7) / 8, (kBrdfSize + 7) / 8, 1);

        // Step 5: Prefiltered specular cubemap — 5 mips, 6 faces per mip.
        // Push constants: { roughness (f32), envFaceSize (f32) }
        struct PrefPC { float roughness; float envFaceSize; };
        constexpr float kRoughnesses[kPrefMips] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        cmd->SetComputePipeline(prefPipeline);
        for (uint32_t m = 0; m < kPrefMips; ++m) {
            const uint32_t mSize = std::max(1u, kPrefSize >> m);
            const PrefPC pc = { kRoughnesses[m], static_cast<float>(kPrefSize) };
            cmd->SetDescriptorSet(0, prefDs[m]);
            cmd->SetPushConstants(&pc, sizeof(PrefPC), RHI::RHIShaderStage::Compute);
            cmd->Dispatch((mSize + 7) / 8, (mSize + 7) / 8, 6);
        }

        cmd->TransitionTexture(result.brdfLut,        RS::UnorderedAccess, RS::ShaderRead);
        cmd->TransitionTexture(result.prefilteredEnv, RS::UnorderedAccess, RS::ShaderRead);
        cmd->TransitionTexture(result.skyboxCubemap,  RS::UnorderedAccess, RS::ShaderRead);
    });

    device->DestroyTexture(hdrTex);
    device->DestroyTexture(intermCube);

    SA_LOG_INFO("GpuIblBake: bake complete — BRDF LUT + prefiltered env (5 mips, PDF LOD) + skybox cubemap + SH");
    return result;
}

} // namespace StellarAlia
