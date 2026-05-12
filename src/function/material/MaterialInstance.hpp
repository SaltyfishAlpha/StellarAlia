#pragma once

#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "function/material/AttachmentKey.hpp"
#include "platform/rhi/IRHICommandList.hpp"
#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia {

class MaterialType;

// ─────────────────────────────────────────────────────────────────────────────
// MaterialInstance
//
// Per-object material state: a CPU-side parameter blob + GPU-side UBO + a
// descriptor set at set=1.
//
// Typical render-loop usage:
//   mat->SetTexture("t_BaseColor", goldTex);
//   mat->SetFloat("roughnessFactor", 0.3f);
//   ...
//   RHIPipelineHandle pipe = mat->GetPipeline(device, attachmentKey);
//   cmd->SetPipeline(pipe);
//   cmd->SetDescriptorSet(0, frameUniforms.GetDescriptorSet(frameIdx));
//   mat->Bind(cmd);
//   cmd->SetPushConstants(&modelMatrix, 64, RHIShaderStage::Vertex);
//   cmd->DrawIndexed(...);
// ─────────────────────────────────────────────────────────────────────────────
class MaterialInstance {
public:
    ~MaterialInstance();

    // ── Parameter setters (mark dirty → flushed on next Bind) ────────────────

    // Generic setter: copies sizeof(T) bytes from value into the UBO blob at the
    // offset specified by the named ParamDef.  Works for any scalar/vector/matrix.
    template<typename T>
    void SetParam(std::string_view name, const T& value) {
        SetRawParam(name, &value, static_cast<uint32_t>(sizeof(T)));
    }

    // Type-specific convenience wrappers (delegate to SetRawParam).
    void SetFloat(std::string_view name, float value);
    void SetVec3 (std::string_view name, glm::vec3 value);
    void SetVec4 (std::string_view name, glm::vec4 value);

    // Raw bytes setter — copies 'byteCount' bytes from 'src' into the named param.
    void SetRawParam(std::string_view name, const void* src, uint32_t byteCount);

    // Set a texture for the named sampler slot.
    // Immediately updates the descriptor set (no dirty flag needed for textures).
    void SetTexture(std::string_view      name,
                    RHI::RHITextureHandle tex);

    // Get or create the pipeline for the given attachment configuration.
    [[nodiscard]] RHI::RHIPipelineHandle
    GetPipeline(RHI::IRHIDevice* device, const AttachmentKey& key);

    // Bind the material descriptor set (set=1).
    // Flushes the parameter UBO to the GPU if dirty.
    void Bind(RHI::IRHICommandList* cmd);

    [[nodiscard]] const MaterialType* GetType() const { return m_type; }
    [[nodiscard]]       MaterialType* GetType()       { return m_type; }

    // Exposed for the SSBO + bindless render path (Issue #72) where BuildDrawList
    // needs to issue cmd.SetDescriptorSet(1, descSet, dynamicOffset).
    [[nodiscard]] RHI::RHIDescSetHandle GetDescSet() const { return m_descSet; }

    // CPU-side blob (post-default + post-SetParam/SetTexture). BuildDrawList copies
    // this as the baseline before applying per-entity overrides in SSBO path.
    [[nodiscard]] const std::vector<uint8_t>& GetParamBlob() const { return m_uboBlob; }

private:
    friend class MaterialType;
    friend class MaterialManager;

    MaterialType*                  m_type   = nullptr;
    RHI::IRHIDevice*               m_device = nullptr;
    // Issue #72: back-pointer used by SSBO path to register textures into the
    // shared BindlessTextureHeap when SetTexture() is called. nullptr in legacy
    // path or for instances created outside MaterialManager.
    class MaterialManager*         m_mgr    = nullptr;

    // Issue #72: in SSBO path (usesMaterialParamsSSBO), m_paramBlob contains the
    // packed MaterialParams blob — ParamDef fields + TextureDef _Idx fields ready
    // for ring upload. In legacy UBO path, it mirrors only the param UBO contents.
    std::vector<uint8_t>           m_uboBlob;
    bool                           m_paramDirty = true;
    RHI::RHIBufferHandle           m_ubo;            // legacy UBO path only; empty in SSBO path
    RHI::RHIDescSetHandle          m_descSet;        // set=1
    // Legacy path: per-slot RHITextureHandle (written to set=1 binding>=1 sampler).
    // SSBO path: unused — texture bindings live in m_texAssetIndices below.
    std::vector<RHI::RHITextureHandle> m_textures;
    // Issue #72 SSBO path: per-slot bindless heap index, parallel to MaterialType::textures.
    // Default-initialised to BindlessTextureHeap::kDefaultSlot (0) so unbound slots
    // sample the engine's white 1×1 texture rather than garbage.
    std::vector<uint32_t>          m_texAssetIndices;

    void FlushParams();
};

} // namespace StellarAlia
