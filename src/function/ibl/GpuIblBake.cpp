#include "function/ibl/GpuIblBake.hpp"

#include "core/logs/Log.hpp"
#include "platform/rhi/IRHICommandList.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
    if (brdfSpv.empty() || prefSpv.empty()) return false;

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

    SA_LOG_INFO("GpuIblBake: shaders loaded");
    return true;
}

void GpuIblBake::Shutdown(RHI::IRHIDevice* device) {
    m_brdfProg.Unload(device);
    m_prefProg.Unload(device);
}

// ─── CPU SH projection ────────────────────────────────────────────────────────
//
// Projects an HDR equirectangular panorama into L0+L1+L2 real spherical harmonics
// (9 RGB coefficients), then pre-multiplies each band by the Lambertian convolution
// kernel (Ramamoorthi & Hanrahan 2001, Table 1):
//   l=0: A0 = π      l=1: A1 = 2π/3      l=2: A2 = π/4
//
// This allows the shader to evaluate irradiance(N) as a simple linear combination
// of the 9 SH basis functions Y_i(N), with no texture sample required.

static void ProjectHDRtoSH(const Resource::ImageData& hdr, glm::vec4 outSH[9]) {
    constexpr float kPI = 3.14159265359f;

    // Lambertian convolution factors per SH band
    const float kConv[9] = {
        kPI,                        // l=0 (1 coeff)
        2.0f * kPI / 3.0f,          // l=1 (3 coeffs)
        2.0f * kPI / 3.0f,
        2.0f * kPI / 3.0f,
        kPI / 4.0f,                 // l=2 (5 coeffs)
        kPI / 4.0f,
        kPI / 4.0f,
        kPI / 4.0f,
        kPI / 4.0f,
    };

    glm::vec3 accum[9] = {};
    double    weightSum = 0.0;

    const uint32_t W = hdr.width;
    const uint32_t H = hdr.height;

    for (uint32_t y = 0; y < H; ++y) {
        // Elevation angle: v=0 → top (+90°), v=1 → bottom (−90°)
        const float v    = (y + 0.5f) / static_cast<float>(H);
        const float elev = (0.5f - v) * kPI;          // −π/2 .. +π/2
        const float cosE = std::cos(elev);
        const float sinE = std::sin(elev);
        // Solid-angle weight for equirectangular: dΩ ∝ cos(elevation)
        const float latW = cosE;

        for (uint32_t x = 0; x < W; ++x) {
            const float u   = (x + 0.5f) / static_cast<float>(W);
            const float phi = (u * 2.0f - 1.0f) * kPI;   // −π .. +π

            // World direction (same convention as EquirectToDir in shaders)
            const glm::vec3 d(cosE * std::cos(phi), sinE, cosE * std::sin(phi));

            // Pixel radiance (RGBA32F, 4 floats per pixel)
            const float* p = hdr.pixelsHDR.data() + (y * W + x) * 4;
            const glm::vec3 L(p[0], p[1], p[2]);

            // Real SH basis Y_i(d), l=0,1,2
            const float Y[9] = {
                0.282095f,
                0.488603f * d.y,
                0.488603f * d.z,
                0.488603f * d.x,
                1.092548f * d.x * d.y,
                1.092548f * d.y * d.z,
                0.315392f * (3.0f * d.z * d.z - 1.0f),
                1.092548f * d.x * d.z,
                0.546274f * (d.x * d.x - d.y * d.y),
            };

            weightSum += static_cast<double>(latW);
            for (int i = 0; i < 9; ++i)
                accum[i] += L * (Y[i] * latW);
        }
    }

    // Normalise: scale factor = 4π / Σweights, then apply Lambertian convolution
    const float scale = (weightSum > 0.0) ?
        static_cast<float>(4.0 * static_cast<double>(kPI) / weightSum) : 0.0f;

    for (int i = 0; i < 9; ++i)
        outSH[i] = glm::vec4(accum[i] * (scale * kConv[i]), 0.0f);
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
    // Done before GPU upload so we only iterate the CPU buffer once.
    ProjectHDRtoSH(hdr, result.shCoeffs);
    SA_LOG_INFO("GpuIblBake: SH projection complete");

    // ── Upload HDR to GPU ─────────────────────────────────────────────────────
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
    constexpr uint32_t kBrdfSize  = 512u;
    constexpr uint32_t kPrefW     = 512u, kPrefH = 256u;
    constexpr uint32_t kPrefMips  = 5u;

    auto makeStorage = [&](uint32_t w, uint32_t h, uint32_t mips, const char* name) {
        RHI::RHITextureDesc d{};
        d.width     = w;
        d.height    = h;
        d.mipLevels = mips;
        d.format    = RHI::RHIFormat::RGBA32F;
        d.usage     = RHI::RHITextureUsage::UnorderedAccess | RHI::RHITextureUsage::Sampled;
        d.debugName = name;
        return device->CreateTexture(d);
    };

    result.brdfLut        = makeStorage(kBrdfSize, kBrdfSize, 1,         "IBL_BrdfLut");
    result.prefilteredEnv = makeStorage(kPrefW,    kPrefH,    kPrefMips, "IBL_PrefilteredEnv");

    // ── Pipelines ─────────────────────────────────────────────────────────────
    auto brdfPipeline = m_brdfProg.GetPipeline(device);
    auto prefPipeline = m_prefProg.GetPipeline(device);
    if (!brdfPipeline.IsValid() || !prefPipeline.IsValid()) {
        SA_LOG_ERROR("GpuIblBake: pipeline creation failed");
        device->DestroyTexture(hdrTex);
        device->DestroyTexture(result.brdfLut);
        device->DestroyTexture(result.prefilteredEnv);
        return {};
    }

    // ── Descriptor sets ───────────────────────────────────────────────────────
    auto brdfDs = device->AllocateDescriptorSet(m_brdfProg.GetLayout(0));
    device->WriteDescriptorStorageImage(brdfDs, 0, result.brdfLut);

    std::array<RHI::RHIDescSetHandle, kPrefMips> prefDs{};
    for (uint32_t m = 0; m < kPrefMips; ++m) {
        prefDs[m] = device->AllocateDescriptorSet(m_prefProg.GetLayout(0));
        device->WriteDescriptorStorageImageMip(prefDs[m], 0, result.prefilteredEnv, m);
        device->WriteDescriptorTexture(prefDs[m], 1, hdrTex);
    }

    // ── Dispatch ─────────────────────────────────────────────────────────────
    using RS = RHI::RHIResourceState;

    device->ImmediateCompute([&](RHI::IRHICommandList* cmd) {
        cmd->TransitionTexture(result.brdfLut,        RS::Undefined, RS::UnorderedAccess);
        cmd->TransitionTexture(result.prefilteredEnv, RS::Undefined, RS::UnorderedAccess);

        // BRDF LUT — 512×512
        cmd->SetComputePipeline(brdfPipeline);
        cmd->SetDescriptorSet(0, brdfDs);
        cmd->Dispatch((kBrdfSize + 7) / 8, (kBrdfSize + 7) / 8, 1);

        // Prefiltered specular — one dispatch per roughness mip
        constexpr float kRoughnesses[kPrefMips] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        cmd->SetComputePipeline(prefPipeline);
        for (uint32_t m = 0; m < kPrefMips; ++m) {
            const uint32_t mW = std::max(1u, kPrefW >> m);
            const uint32_t mH = std::max(1u, kPrefH >> m);
            cmd->SetDescriptorSet(0, prefDs[m]);
            cmd->SetPushConstants(&kRoughnesses[m], sizeof(float), RHI::RHIShaderStage::Compute);
            cmd->Dispatch((mW + 7) / 8, (mH + 7) / 8, 1);
        }

        cmd->TransitionTexture(result.brdfLut,        RS::UnorderedAccess, RS::ShaderRead);
        cmd->TransitionTexture(result.prefilteredEnv, RS::UnorderedAccess, RS::ShaderRead);
    });

    device->DestroyTexture(hdrTex);

    SA_LOG_INFO("GpuIblBake: bake complete — BRDF LUT + prefiltered env (5 mips) + SH");
    return result;
}

} // namespace StellarAlia
