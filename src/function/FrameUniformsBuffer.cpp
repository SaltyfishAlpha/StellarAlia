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
        bd.set     = 0; bd.binding = 0;
        bd.type    = RHI::RHIDescriptorType::UniformBuffer;
        bd.stages  = RHI::RHIShaderStage::All;
        bd.name    = "FrameData";
        refl.bindings.push_back(bd);
    }
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 0; bd.binding = 1;
        bd.type    = RHI::RHIDescriptorType::UniformBuffer;
        bd.stages  = RHI::RHIShaderStage::All;
        bd.name    = "LightData";
        refl.bindings.push_back(bd);
    }
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 0; bd.binding = 2;
        bd.type    = RHI::RHIDescriptorType::Texture2D;
        bd.stages  = RHI::RHIShaderStage::Fragment;
        bd.name    = "t_BrdfLut";
        refl.bindings.push_back(bd);
    }
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 0; bd.binding = 3;
        bd.type    = RHI::RHIDescriptorType::TextureCube;
        bd.stages  = RHI::RHIShaderStage::Fragment;
        bd.name    = "t_PrefilteredEnv";
        refl.bindings.push_back(bd);
    }
    {
        RHI::ShaderBindingDesc bd;
        bd.set     = 0; bd.binding = 4;
        bd.type    = RHI::RHIDescriptorType::TextureCube;
        bd.stages  = RHI::RHIShaderStage::Fragment;
        bd.name    = "t_SkyboxMap";
        refl.bindings.push_back(bd);
    }

    m_layout = device->CreateDescriptorSetLayout(refl, 0);

    // Placeholder 1×1 white texture for binding=2 (BRDF LUT, sampler2D).
    {
        RHI::RHITextureDesc td{};
        td.width = td.height = 1;
        td.format    = RHI::RHIFormat::RGBA8_UNORM;
        td.usage     = RHI::RHITextureUsage::Sampled;
        td.debugName = "IBLPlaceholder";
        m_iblPlaceholder = device->CreateTexture(td);
        const uint32_t white = 0xFFFFFFFFu;
        device->UploadTextureData(m_iblPlaceholder, &white, 4);
    }

    // Placeholder 1×1 white cubemap for bindings 3/4 (samplerCube).
    // Provides a valid cube image view until SetIBLTextures() is called.
    {
        RHI::RHITextureDesc td{};
        td.width = td.height = 1;
        td.cubemap   = true;
        td.format    = RHI::RHIFormat::RGBA8_UNORM;
        td.usage     = RHI::RHITextureUsage::Sampled;
        td.debugName = "IBLCubePlaceholder";
        m_iblCubePlaceholder = device->CreateTexture(td);
        // 6 faces × 1×1 × RGBA8 = 24 bytes of white
        const uint8_t white6[24] = {
            0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF
        };
        device->UploadTextureData(m_iblCubePlaceholder, white6, sizeof(white6));
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
    for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
        m_device->WriteDescriptorTexture(m_descSets[i], 2, brdfLut);
        m_device->WriteDescriptorTexture(m_descSets[i], 3, prefilteredEnv);
        m_device->WriteDescriptorTexture(m_descSets[i], 4, skyboxMap);
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
