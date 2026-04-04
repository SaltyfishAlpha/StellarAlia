#include "function/material/MaterialType.hpp"
#include "function/material/MaterialInstance.hpp"
#include "core/logs/Log.hpp"

#include <cassert>

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

    // CPU-side parameter blob — zero-initialized
    inst->m_uboBlob.assign(uboSize, 0u);

    // GPU-side UBO (cpu-visible for per-frame updates)
    if (uboSize > 0) {
        RHI::RHIBufferDesc bd{};
        bd.size       = uboSize;
        bd.usage      = RHI::RHIBufferUsage::Uniform;
        bd.cpuVisible = true;
        bd.debugName  = "MaterialParamsUBO";
        inst->m_ubo = device->CreateBuffer(bd);
    }

    // Allocate set=1 descriptor set
    inst->m_descSet = device->AllocateDescriptorSet(shader.GetMaterialLayout());

    // Write UBO at binding=0 (if any)
    if (uboSize > 0 && inst->m_ubo.IsValid())
        device->WriteDescriptorBuffer(inst->m_descSet, 0,
                                      inst->m_ubo, 0, uboSize);

    // Fill texture slots with the default texture
    inst->m_textures.resize(textures.size(), defaultTexture);
    for (const auto& td : textures) {
        if (defaultTexture.IsValid())
            device->WriteDescriptorTexture(inst->m_descSet, td.binding, defaultTexture);
    }

    SA_LOG_DEBUG("MaterialType '{}': created instance", name);
    return inst;
}

RHI::RHIPipelineHandle MaterialType::GetOrCreatePipeline(RHI::IRHIDevice*     device,
                                                           const AttachmentKey& key) {
    return shader.GetOrCreatePipeline(device, key,
                                       defaultCullMode,
                                       defaultBlendMode,
                                       defaultDepthTest,
                                       defaultDepthWrite,
                                       noVertexInput);
}

} // namespace StellarAlia
