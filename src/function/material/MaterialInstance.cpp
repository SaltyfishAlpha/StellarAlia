#include "function/material/MaterialInstance.hpp"
#include "function/material/MaterialType.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>
#include <cstring>

namespace StellarAlia {

// ─── Parameter setters ────────────────────────────────────────────────────────

void MaterialInstance::SetRawParam(std::string_view name,
                                   const void*      src,
                                   uint32_t         byteCount) {
    const ParamDef* p = m_type->FindParam(name);
    if (!p) { SA_LOG_WARN("MaterialInstance::SetParam: unknown param '{}'", name); return; }
    const uint32_t copyBytes = std::min(byteCount, p->size);
    std::memcpy(m_uboBlob.data() + p->offset, src, copyBytes);
    m_paramDirty = true;
}

void MaterialInstance::SetFloat(std::string_view name, float value) {
    SetRawParam(name, &value, sizeof(float));
}

void MaterialInstance::SetVec3(std::string_view name, glm::vec3 value) {
    SetRawParam(name, &value, sizeof(glm::vec3));
}

void MaterialInstance::SetVec4(std::string_view name, glm::vec4 value) {
    SetRawParam(name, &value, sizeof(glm::vec4));
}

void MaterialInstance::SetTexture(std::string_view name, RHI::RHITextureHandle tex) {
    const TextureDef* td = m_type->FindTexture(name);
    if (!td) { SA_LOG_WARN("MaterialInstance::SetTexture: unknown slot '{}'", name); return; }
    m_textures[td->slotIndex] = tex;
    m_device->WriteDescriptorTexture(m_descSet, td->binding, tex);
}

// ─── Pipeline ─────────────────────────────────────────────────────────────────

RHI::RHIPipelineHandle MaterialInstance::GetPipeline(RHI::IRHIDevice*     device,
                                                       const AttachmentKey& key) {
    return m_type->GetOrCreatePipeline(device, key);
}

// ─── Bind ─────────────────────────────────────────────────────────────────────

void MaterialInstance::Bind(RHI::IRHICommandList* cmd) {
    if (m_paramDirty) FlushParams();
    cmd->SetDescriptorSet(1, m_descSet);
}

void MaterialInstance::FlushParams() {
    if (!m_uboBlob.empty())
        m_device->UploadBufferData(m_ubo, m_uboBlob.data(),
                                   static_cast<uint64_t>(m_uboBlob.size()));
    m_paramDirty = false;
}

} // namespace StellarAlia
