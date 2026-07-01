#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "function/material/AttachmentKey.hpp"
#include "function/material/ShaderProgram.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/ShaderReflection.hpp"

namespace StellarAlia {

class MaterialInstance;

// ─────────────────────────────────────────────────────────────────────────────
// Parameter entry — describes one field in the MaterialParams UBO.
// ─────────────────────────────────────────────────────────────────────────────
struct ParamDef {
    std::string          name;
    uint32_t             offset = 0;       // byte offset in MaterialParams UBO
    uint32_t             size   = 0;       // byte size (4, 8, 12, 16)
    // Populated from GLSL @Type("Display Name") annotations:
    RHI::ParamUIType     uiType       = RHI::ParamUIType::Inferred;
    std::string          displayName;      // human-readable label; empty → use name
    float                minValue     = 0.f;
    float                maxValue     = 1.f;
    float                defaultValue[4] = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture slot — one sampler2D binding in set=1.
// ─────────────────────────────────────────────────────────────────────────────
struct TextureDef {
    std::string name;
    uint32_t    binding;       // legacy UBO path: set=1 sampler binding index. Unused in SSBO path.
    uint32_t    slotIndex;     // index into MaterialInstance::m_textures / m_texAssetIndices
    // Issue #72 (SSBO path): byte offset of this texture's bindless index (uint)
    // inside the MaterialParams SSBO blob. ~0u in legacy UBO path.
    uint32_t    uboBlobOffset = 0xFFFFFFFFu;
    std::string displayName;
};

// ─────────────────────────────────────────────────────────────────────────────
// MaterialType
//
// Shared, immutable description of a shader + parameter layout.
// Multiple MaterialInstances can reference the same type.
// ─────────────────────────────────────────────────────────────────────────────
class MaterialType {
public:
    std::string           name;

    // Shader program — owned by ProgramCache (Issue #86), keyed by this type's
    // name. MaterialType references it; does not own its lifetime.
    ShaderProgram*        shader = nullptr;

    // UBO layout for set=1, binding=0 (MaterialParams)
    uint32_t              uboSize = 0;
    std::vector<ParamDef> params;

    // Sampler bindings for set=1, bindings 1..N
    std::vector<TextureDef> textures;

    // True for types registered from a project's cook cache (custom .saglsl models).
    // Cleared by MaterialManager::ClearProjectTypes() on project switch.
    bool isProjectType = false;

    // Issue #72: when true, set=1 binding=0 is a STORAGE_BUFFER (named "MaterialParams")
    // and textures are referenced as bindless indices inside the SSBO blob. Such types
    // share a single descriptor per shader (binding 0 → MaterialParamRing) and bind
    // set=3 to MaterialManager's BindlessTextureHeap. False = legacy UBO path with
    // per-instance descriptor sets.
    bool usesMaterialParamsSSBO = false;

    // Default render state
    RHI::RHICullMode  defaultCullMode  = RHI::RHICullMode::Back;
    RHI::RHIBlendMode defaultBlendMode = RHI::RHIBlendMode::Opaque;
    RHI::RHITopology  defaultTopology  = RHI::RHITopology::TriangleList;
    bool              defaultDepthTest  = true;
    bool              defaultDepthWrite = true;
    bool              noVertexInput     = false;

    // Find a param by name; returns nullptr if not found.
    [[nodiscard]] const ParamDef*  FindParam  (std::string_view name) const noexcept;
    [[nodiscard]] const TextureDef* FindTexture(std::string_view name) const noexcept;

    // Create a new MaterialInstance backed by this type.
    // defaultTexture: a 1×1 white/flat texture used for unset slots.
    [[nodiscard]] std::unique_ptr<MaterialInstance>
    CreateInstance(RHI::IRHIDevice*     device,
                   RHI::RHITextureHandle defaultTexture);

    // Get (or create) a pipeline using the type's stored default render state.
    // Use this from RenderFeature::AddPasses — no need to repeat cull/blend/depth flags.
    RHI::RHIPipelineHandle GetOrCreatePipeline(RHI::IRHIDevice*     device,
                                                const AttachmentKey& key);
};

} // namespace StellarAlia
