#include "function/material/ComputeProgram.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>
#include <set>

namespace StellarAlia {

bool ComputeProgram::Load(RHI::IRHIDevice* device, const Desc& desc) {
    m_refl        = desc.refl;
    m_frameLayout = desc.frameLayout;

    m_shader = device->CreateShader(desc.spv, desc.refl);
    if (!m_shader.IsValid()) {
        SA_LOG_ERROR("ComputeProgram::Load — shader module creation failed");
        return false;
    }

    // Build one descriptor set layout per unique set index found in reflection.
    // If a frameLayout was supplied externally it overrides the auto-derived
    // layout for set=1 (engine-wide per-frame set convention, see
    // frame_uniforms.glsl), so skip that set to avoid creating an unused layout.
    std::set<uint32_t> sets;
    for (const auto& b : desc.refl.bindings)
        sets.insert(b.set);

    for (uint32_t setIdx : sets) {
        if (setIdx == 1 && m_frameLayout.IsValid()) continue;
        m_layouts[setIdx] = device->CreateDescriptorSetLayout(desc.refl, setIdx);
    }

    SA_LOG_INFO("ComputeProgram: loaded ({} bindings, {} layouts, pc={}B)",
                desc.refl.bindings.size(), m_layouts.size(),
                desc.refl.pushConstantSize);
    return true;
}

void ComputeProgram::Unload(RHI::IRHIDevice* device) {
    if (m_pipeline.IsValid()) {
        device->DestroyPipeline(m_pipeline);
        m_pipeline = {};
    }
    if (m_shader.IsValid()) {
        device->DestroyShader(m_shader);
        m_shader = {};
    }
    m_layouts.clear();
    m_frameLayout = {};
    m_refl        = {};
}

RHI::RHIPipelineHandle ComputeProgram::GetPipeline(RHI::IRHIDevice* device) {
    if (m_pipeline.IsValid()) return m_pipeline;

    // Build the descriptor layout array, placed at their set-index positions.
    // Slot i in descriptorLayouts[] maps to set=i in the shader.
    RHI::RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader    = m_shader;
    pipeDesc.pushConstantSize = m_refl.pushConstantSize;

    // External frame layout occupies set=1 (engine-wide per-frame set convention).
    if (m_frameLayout.IsValid())
        pipeDesc.descriptorLayouts[1] = m_frameLayout;

    // Place auto-derived layouts at their respective set positions.
    for (const auto& [setIdx, layout] : m_layouts) {
        if (setIdx < 4)
            pipeDesc.descriptorLayouts[setIdx] = layout;
    }

    // Count = highest occupied slot index + 1.
    uint32_t count = 0;
    for (uint32_t i = 0; i < 4; ++i)
        if (pipeDesc.descriptorLayouts[i].IsValid()) count = i + 1;
    pipeDesc.descriptorLayoutCount = count;

    m_pipeline = device->CreateComputePipeline(pipeDesc);
    if (!m_pipeline.IsValid())
        SA_LOG_ERROR("ComputeProgram::GetPipeline — CreateComputePipeline failed");
    return m_pipeline;
}

RHI::RHIDescLayoutHandle ComputeProgram::GetLayout(uint32_t setIndex) const {
    if (setIndex == 1 && m_frameLayout.IsValid()) return m_frameLayout;
    auto it = m_layouts.find(setIndex);
    return (it != m_layouts.end()) ? it->second : RHI::RHIDescLayoutHandle{};
}

} // namespace StellarAlia
