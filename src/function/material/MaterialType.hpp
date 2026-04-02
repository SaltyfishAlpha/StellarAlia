#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "function/material/AttachmentKey.hpp"
#include "function/material/ShaderProgram.hpp"
#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia {

class MaterialInstance;

// ─────────────────────────────────────────────────────────────────────────────
// Parameter entry — describes one field in the MaterialParams UBO.
// ─────────────────────────────────────────────────────────────────────────────
struct ParamDef {
    std::string name;
    uint32_t    offset;  // byte offset in MaterialParams UBO
    uint32_t    size;    // byte size (4, 8, 12, 16)
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture slot — one sampler2D binding in set=1.
// ─────────────────────────────────────────────────────────────────────────────
struct TextureDef {
    std::string name;
    uint32_t    binding;    // set=1 binding index
    uint32_t    slotIndex;  // index into MaterialInstance::m_textures
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

    // Shader (variant 0 = base; extension point for #defines later)
    ShaderProgram         shader;

    // UBO layout for set=1, binding=0 (MaterialParams)
    uint32_t              uboSize = 0;
    std::vector<ParamDef> params;

    // Sampler bindings for set=1, bindings 1..N
    std::vector<TextureDef> textures;

    // Default render state
    RHI::RHICullMode  defaultCullMode  = RHI::RHICullMode::Back;
    RHI::RHIBlendMode defaultBlendMode = RHI::RHIBlendMode::Opaque;
    bool              defaultDepthTest  = true;
    bool              defaultDepthWrite = true;

    // Find a param by name; returns nullptr if not found.
    [[nodiscard]] const ParamDef*  FindParam  (std::string_view name) const noexcept;
    [[nodiscard]] const TextureDef* FindTexture(std::string_view name) const noexcept;

    // Create a new MaterialInstance backed by this type.
    // defaultTexture: a 1×1 white/flat texture used for unset slots.
    [[nodiscard]] std::unique_ptr<MaterialInstance>
    CreateInstance(RHI::IRHIDevice*     device,
                   RHI::RHITextureHandle defaultTexture);

    // Get (or create) a pipeline for the given attachment config.
    RHI::RHIPipelineHandle GetOrCreatePipeline(RHI::IRHIDevice*      device,
                                                const AttachmentKey&  key);
};

} // namespace StellarAlia
