#include "function/material/MaterialInstance.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/MaterialType.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>
#include <cstring>

namespace StellarAlia {

// ─── Destructor ───────────────────────────────────────────────────────────────

MaterialInstance::~MaterialInstance() {
    if (m_device) {
        if (m_descSet.IsValid()) m_device->FreeDescriptorSet(m_descSet);
        // In SSBO path m_ubo is never allocated (binding=0 points at the
        // shared MaterialParamRing); only legacy instances own a UBO.
        if (m_ubo.IsValid())     m_device->DestroyBuffer(m_ubo);
    }
}

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

    if (m_type->usesMaterialParamsSSBO) {
        // Issue #72: SSBO path — register the texture into the bindless heap and
        // pack the resulting index into m_uboBlob at the field's offset.
        uint32_t slot = 0u;
        if (m_mgr && tex.IsValid())
            slot = m_mgr->GetTextureHeap().Register(tex);
        if (td->slotIndex < m_texAssetIndices.size())
            m_texAssetIndices[td->slotIndex] = slot;
        if (td->uboBlobOffset + sizeof(uint32_t) <= m_uboBlob.size())
            std::memcpy(m_uboBlob.data() + td->uboBlobOffset, &slot, sizeof(slot));
        m_paramDirty = true;
    } else {
        m_textures[td->slotIndex] = tex;
        m_device->WriteDescriptorTexture(m_descSet, td->binding, tex);
    }
}

// ─── Pipeline ─────────────────────────────────────────────────────────────────

RHI::RHIPipelineHandle MaterialInstance::GetPipeline(RHI::IRHIDevice*     device,
                                                       const AttachmentKey& key) {
    return m_type->GetOrCreatePipeline(device, key);
}

// ─── Bind ─────────────────────────────────────────────────────────────────────

void MaterialInstance::Bind(RHI::IRHICommandList* cmd) {
    // SSBO+bindless path: bind site must use SetDescriptorSet(1, descSet,
    // dynamic offset) directly — see SceneRenderer's GBuffer execute closure.
    // Calling Bind() here would emit set=1 without the required dynamic offset
    // and trip VUID-vkCmdBindDescriptorSets-dynamicOffsetCount-00359.
    if (m_type && m_type->usesMaterialParamsSSBO) {
        SA_LOG_WARN("MaterialInstance::Bind: legacy Bind() called on SSBO instance ({})",
                    m_type->name);
        return;
    }
    if (m_paramDirty) FlushParams();
    // Issue #72 Step 6.5: material lives at set=2.
    cmd->SetDescriptorSet(2, m_descSet);
}

void MaterialInstance::FlushParams() {
    // SSBO path has no per-instance UBO — blob is uploaded into the ring by
    // SceneRenderer::BuildDrawList each frame.
    if (m_type && m_type->usesMaterialParamsSSBO) {
        m_paramDirty = false;
        return;
    }
    if (!m_uboBlob.empty() && m_ubo.IsValid())
        m_device->UploadBufferData(m_ubo, m_uboBlob.data(),
                                   static_cast<uint64_t>(m_uboBlob.size()));
    m_paramDirty = false;
}

} // namespace StellarAlia
