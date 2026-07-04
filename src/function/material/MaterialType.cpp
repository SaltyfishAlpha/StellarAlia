#include "function/material/MaterialType.hpp"
#include "function/material/MaterialInstance.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace StellarAlia {

const ParamDef* MaterialType::FindParam(std::string_view name) const noexcept {
    for (const auto& p : params)
        if (p.name == name) return &p;
    return nullptr;
}

const TextureDef* MaterialType::FindTexture(std::string_view name) const noexcept {
    for (const auto& t : textures)
        if (t.name == name) return &t;
    return nullptr;
}

std::unique_ptr<MaterialInstance>
MaterialType::CreateInstance(RHI::IRHIDevice*      device,
                              RHI::RHITextureHandle defaultTexture) {
    assert(device);

    auto inst = std::unique_ptr<MaterialInstance>(new MaterialInstance());
    inst->m_type   = this;
    inst->m_device = device;

    // CPU-side parameter blob — zero-initialized then defaults applied.
    // Applying defaults here ensures params absent from a .samat (e.g. newly
    // added fields like emissiveIntensity) get their annotated default value
    // rather than 0, without requiring every .samat to be re-cooked.
    inst->m_uboBlob.assign(uboSize, 0u);
    for (const auto& p : params) {
        if (p.offset + p.size <= uboSize)
            std::memcpy(inst->m_uboBlob.data() + p.offset, p.defaultValue,
                        std::min<size_t>(p.size, sizeof(p.defaultValue)));
    }

    // Allocate set=1 descriptor set.
    inst->m_descSet = device->AllocateDescriptorSet(shader->GetMaterialLayout());

    if (usesMaterialParamsSSBO) {
        // Issue #72 path: no per-instance UBO, no sampler writes. Texture slots
        // are bindless indices pack'd into m_uboBlob (initialised to 0 = default
        // white slot) so an unbound material samples white instead of garbage.
        // MaterialManager wires binding=0 → MaterialParamRing once the asset
        // loads (so all instances share one descriptor pointing at the ring).
        inst->m_texAssetIndices.assign(textures.size(), 0u);
        for (const auto& td : textures) {
            if (td.uboBlobOffset + sizeof(uint32_t) <= uboSize) {
                const uint32_t idx = 0u;
                std::memcpy(inst->m_uboBlob.data() + td.uboBlobOffset, &idx, sizeof(idx));
            }
        }
    } else {
        // Legacy UBO path: own UBO + per-binding sampler writes.
        if (uboSize > 0) {
            RHI::RHIBufferDesc bd{};
            bd.size       = uboSize;
            bd.usage      = RHI::RHIBufferUsage::Uniform;
            bd.cpuVisible = true;
            bd.debugName  = "MaterialParamsUBO";
            inst->m_ubo = device->CreateBuffer(bd);
            if (inst->m_ubo.IsValid())
                device->WriteDescriptorBuffer(inst->m_descSet, 0,
                                              inst->m_ubo, 0, uboSize);
        }
        inst->m_textures.resize(textures.size(), defaultTexture);
        for (const auto& td : textures) {
            if (defaultTexture.IsValid())
                device->WriteDescriptorTexture(inst->m_descSet, td.binding, defaultTexture);
        }
    }

    SA_LOG_DEBUG("MaterialType '{}': created instance ({})", name,
                 usesMaterialParamsSSBO ? "ssbo+bindless" : "legacy");
    return inst;
}

RHI::RHIPipelineHandle MaterialType::GetOrCreatePipeline(RHI::IRHIDevice*     device,
                                                           const AttachmentKey& key) {
    return shader->GetOrCreatePipeline(device, key, DefaultRenderState());
}

RHI::RHIPipelineHandle MaterialType::GetOrCreatePipeline(RHI::IRHIDevice*           device,
                                                           const AttachmentKey&       key,
                                                           const PipelineRenderState& state) {
    return shader->GetOrCreatePipeline(device, key, state);
}

PipelineRenderState MaterialType::DefaultRenderState() const noexcept {
    PipelineRenderState s{};
    s.cullMode      = defaultCullMode;
    s.blendMode     = defaultBlendMode;
    s.topology      = defaultTopology;
    s.depthTest     = defaultDepthTest;
    s.depthWrite    = defaultDepthWrite;
    s.noVertexInput = noVertexInput;
    return s;
}

} // namespace StellarAlia
