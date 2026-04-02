#pragma once

#include "function/FrameUniforms.hpp"
#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// FrameUniformsBuffer
//
// Manages double-buffered GPU UBOs for set=0 frame globals (FrameUniforms +
// LightUniforms) and their pre-built DescriptorSets.
//
// set=0 layout:
//   binding=0  FrameData        (UniformBuffer)
//   binding=1  LightData        (UniformBuffer)
//   binding=2  t_BrdfLut        (sampler2D — split-sum BRDF LUT)
//   binding=3  t_IrradianceMap  (sampler2D — precomputed diffuse irradiance equirect)
//   binding=4  t_PrefilteredEnv (sampler2D — prefiltered specular env equirect, mip chain)
//
// Usage per frame:
//   fub.Upload(frameIndex, frameData, lightData);
//   cmd->SetDescriptorSet(0, fub.GetDescriptorSet(frameIndex));
// ─────────────────────────────────────────────────────────────────────────────
class FrameUniformsBuffer {
public:
    static constexpr uint32_t MAX_FRAMES = 2;

    // Allocates UBOs, creates descriptor layout, and writes placeholder 1×1
    // white textures to IBL bindings so descriptors are always valid.
    void Init(RHI::IRHIDevice* device);

    // Destroys all GPU resources. Call before device destruction.
    void Shutdown();

    // Upload CPU-side uniforms into the UBO for the given in-flight frame slot.
    void Upload(uint32_t            frameIndex,
                const FrameUniforms& frame,
                const LightUniforms& light);

    // Update IBL/skybox texture bindings on all frame descriptor sets.
    // Call once after the baked IBL assets are ready.
    //   binding=2  brdfLut        — BRDF split-sum LUT
    //   binding=3  prefilteredEnv — GGX prefiltered specular (mip chain)
    //   binding=4  skyboxMap      — original HDR equirect for skybox background
    void SetIBLTextures(RHI::RHITextureHandle brdfLut,
                        RHI::RHITextureHandle prefilteredEnv,
                        RHI::RHITextureHandle skyboxMap);

    [[nodiscard]] RHI::RHIDescSetHandle    GetDescriptorSet(uint32_t frameIndex) const;
    [[nodiscard]] RHI::RHIDescLayoutHandle GetLayout() const { return m_layout; }

private:
    RHI::IRHIDevice*         m_device = nullptr;
    RHI::RHIDescLayoutHandle m_layout;
    RHI::RHIBufferHandle     m_frameUBOs[MAX_FRAMES];
    RHI::RHIBufferHandle     m_lightUBOs[MAX_FRAMES];
    RHI::RHIDescSetHandle    m_descSets[MAX_FRAMES];
    RHI::RHITextureHandle    m_iblPlaceholder;  // 1×1 white used until SetIBLTextures()
};

} // namespace StellarAlia
