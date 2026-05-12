#include "function/material/ShaderProgram.hpp"

#include <algorithm>

#include "core/logs/Log.hpp"

namespace StellarAlia {

bool ShaderProgram::Load(RHI::IRHIDevice* device, const Desc& desc) {
    m_frameLayout = desc.frameLayout;
    m_merged      = RHI::MergeReflections(desc.vertRefl, desc.fragRefl);

    m_vertShader = device->CreateShader(desc.vertSpv, desc.vertRefl);
    m_fragShader = device->CreateShader(desc.fragSpv, desc.fragRefl);

    if (!m_vertShader.IsValid() || !m_fragShader.IsValid()) {
        SA_LOG_ERROR("ShaderProgram::Load — shader module creation failed");
        return false;
    }

    m_materialLayout = device->CreateDescriptorSetLayout(m_merged, 1);

    bool hasSet2 = std::any_of(m_merged.bindings.begin(), m_merged.bindings.end(),
                               [](const RHI::ShaderBindingDesc& b){ return b.set == 2; });
    if (hasSet2)
        m_set2Layout = device->CreateDescriptorSetLayout(m_merged, 2);

    SA_LOG_INFO("ShaderProgram: loaded ({} merged bindings, pc={}B)",
                m_merged.bindings.size(), m_merged.pushConstantSize);
    return true;
}

void ShaderProgram::Unload(RHI::IRHIDevice* device) {
    for (auto& [key, pipeline] : m_pipelineCache)
        device->DestroyPipeline(pipeline);
    m_pipelineCache.clear();

    device->DestroyShader(m_vertShader);
    device->DestroyShader(m_fragShader);
    m_vertShader     = {};
    m_fragShader     = {};
    m_materialLayout = {};
    m_set2Layout     = {};
    m_frameLayout    = {};
}

bool ShaderProgram::ReloadFragShader(RHI::IRHIDevice* device,
                                      std::span<const uint8_t>     fragSpv,
                                      const RHI::ShaderReflection& fragRefl) {
    // Clear the pipeline cache — all pipelines bake in the frag shader and are now stale.
    for (auto& [key, pipeline] : m_pipelineCache)
        device->DestroyPipeline(pipeline);
    m_pipelineCache.clear();

    device->DestroyShader(m_fragShader);
    m_fragShader = {};

    m_fragShader = device->CreateShader(fragSpv, fragRefl);
    if (!m_fragShader.IsValid()) {
        SA_LOG_ERROR("ShaderProgram::ReloadFragShader — CreateShader failed");
        return false;
    }

    SA_LOG_INFO("ShaderProgram: frag shader reloaded");
    return true;
}

RHI::RHIPipelineHandle ShaderProgram::GetOrCreatePipeline(
    RHI::IRHIDevice*     device,
    const AttachmentKey& key,
    RHI::RHICullMode     cullMode,
    RHI::RHIBlendMode    blendMode,
    RHI::RHITopology     topology,
    bool                 depthTest,
    bool                 depthWrite,
    bool                 noVertexInput)
{
    auto it = m_pipelineCache.find(key);
    if (it != m_pipelineCache.end()) return it->second;

    RHI::RHIPipelineDesc pipeDesc{};
    pipeDesc.vertShader  = m_vertShader;
    pipeDesc.fragShader  = m_fragShader;

    // Fixed slot assignment: index == set index, always.
    // VulkanDevice::CreatePipeline substitutes m_emptyDescLayout for invalid handles,
    // so materialLayout is always at set=1, never accidentally promoted to set=0 when
    // frameLayout is absent.
    pipeDesc.descriptorLayouts[0] = m_frameLayout;
    pipeDesc.descriptorLayouts[1] = m_materialLayout;
    pipeDesc.descriptorLayouts[2] = m_set2Layout;
    pipeDesc.descriptorLayoutCount =
        m_set2Layout.IsValid()     ? 3u :
        m_materialLayout.IsValid() ? 2u :
        m_frameLayout.IsValid()    ? 1u : 0u;

    pipeDesc.pushConstantSize   = m_merged.pushConstantSize;
    pipeDesc.pushConstantStages = m_merged.pushConstantStages;

    // Forward reflection-driven vertex inputs (empty for noVertexInput pipelines,
    // and falls back to the backend's legacy 4-attrib layout for v3-v5 .refl files
    // that predate the vertexInputs field).
    const uint32_t viCount = std::min<uint32_t>(
        static_cast<uint32_t>(m_merged.vertexInputs.size()),
        RHI::RHIPipelineDesc::kMaxVertexAttribs);
    for (uint32_t i = 0; i < viCount; ++i)
        pipeDesc.vertexInputs[i] = m_merged.vertexInputs[i];
    pipeDesc.vertexInputCount = viCount;

    pipeDesc.colorFormatCount = key.colorCount;
    for (uint32_t i = 0; i < key.colorCount; ++i)
        pipeDesc.colorFormats[i] = key.colorFormats[i];
    pipeDesc.depthFormat = key.depthFormat;

    pipeDesc.cullMode      = cullMode;
    pipeDesc.blendMode     = blendMode;
    pipeDesc.topology      = topology;
    pipeDesc.depthTest     = depthTest;
    pipeDesc.depthWrite    = depthWrite;
    pipeDesc.noVertexInput = noVertexInput;
    pipeDesc.debugName  = "ShaderProgram::Pipeline";

    auto pipeline = device->CreatePipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        SA_LOG_ERROR("ShaderProgram::GetOrCreatePipeline — CreatePipeline failed");
        return {};
    }

    m_pipelineCache[key] = pipeline;
    return pipeline;
}

} // namespace StellarAlia
