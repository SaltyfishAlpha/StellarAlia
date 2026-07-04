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

    // Issue #72 Step 6.5: set layout aligned with UE5 / Unity HDRP.
    //   set=0 → bindless heap (passed in via desc.bindlessLayout)
    //   set=1 → frame uniforms (passed in)
    //   set=2 → material (reflected)
    //   set=3 → skin / per-object (reflected)
    m_materialLayout = device->CreateDescriptorSetLayout(m_merged, 2);

    bool hasSet3 = std::any_of(m_merged.bindings.begin(), m_merged.bindings.end(),
                               [](const RHI::ShaderBindingDesc& b){ return b.set == 3; });
    if (hasSet3)
        m_set3Layout = device->CreateDescriptorSetLayout(m_merged, 3);

    // Issue #72 Step 6.5: ALWAYS wire bindless layout into slot 0 (even when the
    // shader doesn't sample from set=0). The bindless heap layout is engine-wide
    // — making every pipeline share it keeps set=0 layout-compatible across
    // pipeline changes, which in turn preserves the bindings for sets 1..3
    // (frame, material, skin) when transitioning between e.g. PBR (uses bindless)
    // and SimpleAlbedo (doesn't). Shaders that don't access set=0 incur no
    // runtime cost — Vulkan only requires the set to be bound if a shader
    // statically uses it.
    if (desc.bindlessLayout.IsValid())
        m_bindlessLayout = desc.bindlessLayout;

    bool usesBindless = std::any_of(m_merged.bindings.begin(), m_merged.bindings.end(),
                                    [](const RHI::ShaderBindingDesc& b){ return b.set == 0; });
    SA_LOG_INFO("ShaderProgram: loaded ({} merged bindings, pc={}B, bindless-slot={}, samples-bindless={})",
                m_merged.bindings.size(), m_merged.pushConstantSize,
                m_bindlessLayout.IsValid(), usesBindless);
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
    m_bindlessLayout = {};
    m_frameLayout    = {};
    m_materialLayout = {};
    m_set3Layout     = {};
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
    PipelineRenderState state{};
    state.cullMode      = cullMode;
    state.blendMode     = blendMode;
    state.topology      = topology;
    state.depthTest     = depthTest;
    state.depthWrite    = depthWrite;
    state.noVertexInput = noVertexInput;
    return GetOrCreatePipeline(device, key, state);
}

RHI::RHIPipelineHandle ShaderProgram::GetOrCreatePipeline(
    RHI::IRHIDevice*           device,
    const AttachmentKey&       key,
    const PipelineRenderState& state)
{
    const PipelineStateKey cacheKey{key, state};
    auto it = m_pipelineCache.find(cacheKey);
    if (it != m_pipelineCache.end()) return it->second;

    RHI::RHIPipelineDesc pipeDesc{};
    pipeDesc.vertShader  = m_vertShader;
    pipeDesc.fragShader  = m_fragShader;

    // Issue #72 Step 6.5: slot index == set index.
    //   0 = bindless heap (UE5/Unity HDRP convention — most stable resource)
    //   1 = frame uniforms
    //   2 = material (per-shader)
    //   3 = skin / per-object (per-shader)
    // VulkanDevice::CreatePipeline substitutes m_emptyDescLayout for invalid handles.
    pipeDesc.descriptorLayouts[0] = m_bindlessLayout;
    pipeDesc.descriptorLayouts[1] = m_frameLayout;
    pipeDesc.descriptorLayouts[2] = m_materialLayout;
    pipeDesc.descriptorLayouts[3] = m_set3Layout;
    pipeDesc.descriptorLayoutCount =
        m_set3Layout.IsValid()     ? 4u :
        m_materialLayout.IsValid() ? 3u :
        m_frameLayout.IsValid()    ? 2u :
        m_bindlessLayout.IsValid() ? 1u : 0u;

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

    pipeDesc.cullMode           = state.cullMode;
    pipeDesc.blendMode          = state.blendMode;
    pipeDesc.topology           = state.topology;
    pipeDesc.depthTest          = state.depthTest;
    pipeDesc.depthWrite         = state.depthWrite;
    pipeDesc.depthCompareOp     = state.depthCompareOp;
    pipeDesc.noVertexInput      = state.noVertexInput;
    pipeDesc.stencilTestEnable  = state.stencilTestEnable;
    pipeDesc.stencilWriteEnable = state.stencilWriteEnable;
    pipeDesc.stencilFront       = state.stencilFront;
    pipeDesc.stencilBack        = state.stencilBack;
    pipeDesc.debugName  = "ShaderProgram::Pipeline";

    auto pipeline = device->CreatePipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        SA_LOG_ERROR("ShaderProgram::GetOrCreatePipeline — CreatePipeline failed");
        return {};
    }

    m_pipelineCache[cacheKey] = pipeline;
    return pipeline;
}

} // namespace StellarAlia
