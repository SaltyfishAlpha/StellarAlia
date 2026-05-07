#include "function/material/ShaderProgram.hpp"
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

    // Build set=1 descriptor layout from bindings with set index 1.
    // If the shader has no set=1 bindings (e.g. a simple unlit pass), the layout
    // will be empty but still valid.
    m_materialLayout = device->CreateDescriptorSetLayout(m_merged, 1);

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
    m_frameLayout    = {};
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

    // set=0 = frame uniforms, set=1 = material params
    uint32_t layoutCount = 0;
    if (m_frameLayout.IsValid())    pipeDesc.descriptorLayouts[layoutCount++] = m_frameLayout;
    if (m_materialLayout.IsValid()) pipeDesc.descriptorLayouts[layoutCount++] = m_materialLayout;
    pipeDesc.descriptorLayoutCount = layoutCount;

    pipeDesc.pushConstantSize   = m_merged.pushConstantSize;
    pipeDesc.pushConstantStages = m_merged.pushConstantStages;

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
