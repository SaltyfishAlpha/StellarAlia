#pragma once

#include <string>

#include "function/material/ComputeProgram.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "resource/types/ImageData.hpp"

#include <glm/glm.hpp>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// GpuIblBake
//
// Computes IBL data needed by the PBR shader from an HDR equirect panorama.
// Runs once at startup via IRHIDevice::ImmediateCompute — completely outside
// the per-frame RenderGraph.
//
// Diffuse irradiance is returned as L0+L1+L2 Spherical Harmonic coefficients
// (9 RGB coefficients, pre-multiplied by the Lambertian convolution kernel).
// This replaces a 256×128 irradiance texture with 27 floats.
//
// Maps produced:
//   brdfLut        — 512×512  RGBA32F, 1 mip  (split-sum scale/bias)
//   prefilteredEnv — 512×256  RGBA32F, 5 mips (specular, mip ↔ roughness)
//   shCoeffs[9]    — glm::vec4[9], RGB SH for diffuse irradiance (cpu-side)
// ─────────────────────────────────────────────────────────────────────────────
class GpuIblBake {
public:
    struct Result {
        RHI::RHITextureHandle brdfLut;
        RHI::RHITextureHandle prefilteredEnv;
        // L0+L1+L2 SH coefficients, Lambertian-convolved, std140-padded (w=0).
        glm::vec4             shCoeffs[9];

        [[nodiscard]] bool IsValid() const {
            return brdfLut.IsValid() && prefilteredEnv.IsValid();
        }
    };

    bool Init(RHI::IRHIDevice* device, const std::string& shaderDir);
    void Shutdown(RHI::IRHIDevice* device);

    [[nodiscard]] Result Bake(RHI::IRHIDevice* device, const Resource::ImageData& hdr);

    [[nodiscard]] bool IsInitialized() const { return m_brdfProg.IsLoaded(); }

private:
    ComputeProgram m_brdfProg;   // ibl_brdf_lut.comp
    ComputeProgram m_prefProg;   // ibl_prefilter.comp
};

} // namespace StellarAlia
