#include "function/FrameUniformsBuffer.hpp"
#include "core/logs/Log.hpp"

#include <cassert>
#include <cstring>

namespace StellarAlia {

void FrameUniformsBuffer::Init(RHI::IRHIDevice* device) {
    assert(device);
    m_device = device;

    // Build a minimal ShaderReflection for set=0:
    //   binding=0  FrameData   (UniformBuffer, all stages)
    //   binding=1  LightData   (UniformBuffer, all stages)
    RHI::ShaderReflection refl;
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 1; bd.binding = 0;
        bd.type    = RHI::RHIDescriptorType::UniformBuffer;
        bd.stages  = RHI::RHIShaderStage::All;
        bd.name    = "FrameData";
        refl.bindings.push_back(bd);
    }
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 1; bd.binding = 1;
        bd.type    = RHI::RHIDescriptorType::UniformBuffer;
        bd.stages  = RHI::RHIShaderStage::All;
        bd.name    = "LightData";
        refl.bindings.push_back(bd);
    }
    // Global frame samplers are visible to ALL stages (incl. compute): screen-space
    // compute passes (SSR #48, future volumetric fog / GI) sample these IBL/sky/LTC
    // LUTs. RHIShaderStage::All = Vertex|Fragment|Compute; pure layout metadata, no
    // runtime cost, and graphics access (Fragment) is unaffected.
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 1; bd.binding = 2;
        bd.type    = RHI::RHIDescriptorType::Texture2D;
        bd.stages  = RHI::RHIShaderStage::All;
        bd.name    = "t_BrdfLut";
        refl.bindings.push_back(bd);
    }
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 1; bd.binding = 3;
        bd.type    = RHI::RHIDescriptorType::TextureCube;
        bd.stages  = RHI::RHIShaderStage::All;
        bd.name    = "t_PrefilteredEnv";
        refl.bindings.push_back(bd);
    }
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 1; bd.binding = 4;
        bd.type    = RHI::RHIDescriptorType::TextureCube;
        bd.stages  = RHI::RHIShaderStage::All;
        bd.name    = "t_SkyboxMap";
        refl.bindings.push_back(bd);
    }
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 1; bd.binding = 5;
        bd.type    = RHI::RHIDescriptorType::Texture2D;
        bd.stages  = RHI::RHIShaderStage::All;
        bd.name    = "t_LtcMat";
        refl.bindings.push_back(bd);
    }
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 1; bd.binding = 6;
        bd.type    = RHI::RHIDescriptorType::Texture2D;
        bd.stages  = RHI::RHIShaderStage::All;
        bd.name    = "t_LtcAmp";
        refl.bindings.push_back(bd);
    }

    // Issue #72 Step 6.5: FrameUniforms is set=1 in the new layout (set=0 reserved
    // for BindlessTextureHeap so it can stay bound for the whole command buffer).
    m_layout = device->CreateDescriptorSetLayout(refl, 1);

    // Placeholder 1×1 BLACK texture for binding=2 (BRDF LUT, sampler2D).
    // Black → brdfSS=(0,0) → iblSpecular=0 when no IBL is loaded.
    {
        RHI::RHITextureDesc td{};
        td.width = td.height = 1;
        td.format    = RHI::RHIFormat::RGBA8_UNORM;
        td.usage     = RHI::RHITextureUsage::Sampled;
        td.debugName = "IBLPlaceholder";
        m_iblPlaceholder = device->CreateTexture(td);
        const uint32_t black = 0x00000000u;
        device->UploadTextureData(m_iblPlaceholder, &black, 4);
    }

    // Placeholder 1×1 BLACK cubemap for bindings 3/4 (samplerCube).
    // Black → prefilteredColor=(0,0,0) → iblSpecular=0 when no IBL is loaded.
    {
        RHI::RHITextureDesc td{};
        td.width = td.height = 1;
        td.cubemap   = true;
        td.format    = RHI::RHIFormat::RGBA8_UNORM;
        td.usage     = RHI::RHITextureUsage::Sampled;
        td.debugName = "IBLCubePlaceholder";
        m_iblCubePlaceholder = device->CreateTexture(td);
        const uint8_t black6[24] = {};   // zero-init = black for all 6 faces
        device->UploadTextureData(m_iblCubePlaceholder, black6, sizeof(black6));
    }

    for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
        // FrameData UBO — CPU-visible (updated every frame)
        RHI::RHIBufferDesc frameDesc{};
        frameDesc.size       = sizeof(FrameUniforms);
        frameDesc.usage      = RHI::RHIBufferUsage::Uniform;
        frameDesc.cpuVisible = true;
        frameDesc.debugName  = "FrameDataUBO";
        m_frameUBOs[i] = device->CreateBuffer(frameDesc);

        // LightData UBO
        RHI::RHIBufferDesc lightDesc{};
        lightDesc.size       = sizeof(LightUniforms);
        lightDesc.usage      = RHI::RHIBufferUsage::Uniform;
        lightDesc.cpuVisible = true;
        lightDesc.debugName  = "LightDataUBO";
        m_lightUBOs[i] = device->CreateBuffer(lightDesc);

        // Allocate + write descriptor set
        m_descSets[i] = device->AllocateDescriptorSet(m_layout);
        device->WriteDescriptorBuffer(m_descSets[i], 0, m_frameUBOs[i],
                                      0, sizeof(FrameUniforms));
        device->WriteDescriptorBuffer(m_descSets[i], 1, m_lightUBOs[i],
                                      0, sizeof(LightUniforms));
        device->WriteDescriptorTexture(m_descSets[i], 2, m_iblPlaceholder);      // brdfLut (2D)
        device->WriteDescriptorTexture(m_descSets[i], 3, m_iblCubePlaceholder); // prefilteredEnv (cube)
        device->WriteDescriptorTexture(m_descSets[i], 4, m_iblCubePlaceholder); // skyboxMap (cube)
        device->WriteDescriptorTexture(m_descSets[i], 5, m_iblPlaceholder);     // t_LtcMat (2D)
        device->WriteDescriptorTexture(m_descSets[i], 6, m_iblPlaceholder);     // t_LtcAmp (2D)
    }

    SA_LOG_INFO("FrameUniformsBuffer: initialized ({} frames)", MAX_FRAMES);
}

void FrameUniformsBuffer::Shutdown() {
    if (!m_device) return;
    for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
        m_device->DestroyBuffer(m_frameUBOs[i]);
        m_device->DestroyBuffer(m_lightUBOs[i]);
        // DescSets are freed when the device pool is destroyed.
    }
    m_device->DestroyTexture(m_iblPlaceholder);
    m_device->DestroyTexture(m_iblCubePlaceholder);
    m_device = nullptr;
}

void FrameUniformsBuffer::SetIBLTextures(RHI::RHITextureHandle brdfLut,
                                          RHI::RHITextureHandle prefilteredEnv,
                                          RHI::RHITextureHandle skyboxMap) {
    // Bindings 3 and 4 are samplerCube — fall back to the cube placeholder if
    // the caller did not supply a valid cubemap (e.g. cache miss on first run).
    const auto safeCube2D = brdfLut.IsValid()        ? brdfLut        : m_iblPlaceholder;
    const auto safeCube3  = prefilteredEnv.IsValid() ? prefilteredEnv : m_iblCubePlaceholder;
    const auto safeCube4  = skyboxMap.IsValid()      ? skyboxMap      : m_iblCubePlaceholder;
    for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
        m_device->WriteDescriptorTexture(m_descSets[i], 2, safeCube2D);
        m_device->WriteDescriptorTexture(m_descSets[i], 3, safeCube3);
        m_device->WriteDescriptorTexture(m_descSets[i], 4, safeCube4);
    }
}

void FrameUniformsBuffer::SetLtcTextures(RHI::RHITextureHandle ltcMat,
                                          RHI::RHITextureHandle ltcAmp) {
    const auto safeMat = ltcMat.IsValid() ? ltcMat : m_iblPlaceholder;
    const auto safeAmp = ltcAmp.IsValid() ? ltcAmp : m_iblPlaceholder;
    for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
        m_device->WriteDescriptorTexture(m_descSets[i], 5, safeMat);
        m_device->WriteDescriptorTexture(m_descSets[i], 6, safeAmp);
    }
}

void FrameUniformsBuffer::Upload(uint32_t             frameIndex,
                                  const FrameUniforms& frame,
                                  const LightUniforms& light) {
    assert(frameIndex < MAX_FRAMES);
    m_device->UploadBufferData(m_frameUBOs[frameIndex],
                               &frame, sizeof(frame));
    m_device->UploadBufferData(m_lightUBOs[frameIndex],
                               &light, sizeof(light));
}

RHI::RHIDescSetHandle FrameUniformsBuffer::GetDescriptorSet(uint32_t frameIndex) const {
    assert(frameIndex < MAX_FRAMES);
    return m_descSets[frameIndex];
}

} // namespace StellarAlia
