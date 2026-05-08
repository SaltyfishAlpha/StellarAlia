#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platform/rhi/RHITypes.hpp"

namespace StellarAlia::RHI {

// ─────────────────────────────────────────────────────────────────────────────
// Editor UI type hint for a UBO member, set by @Type(...) GLSL annotations.
// ─────────────────────────────────────────────────────────────────────────────
enum class ParamUIType : uint8_t {
    Inferred = 0,  // deduced from member size: 4→Float, 8→Vec2, 12→Vec3, 16→Vec4
    Float    = 1,
    Vec2     = 2,
    Vec3     = 3,
    Vec4     = 4,
    Color3   = 5,
    Color4   = 6,
    Range    = 7,
};

// ─────────────────────────────────────────────────────────────────────────────
// One member field inside a UBO or SSBO struct.
// ─────────────────────────────────────────────────────────────────────────────
struct ShaderMemberDesc {
    std::string  name;           // GLSL member name, e.g. "baseColorFactor"
    uint32_t     offset  = 0;    // byte offset within the struct
    uint32_t     size    = 0;    // byte size of the member
    // Fields populated from GLSL @Type("Display Name") annotations:
    ParamUIType  uiType       = ParamUIType::Inferred;
    std::string  displayName;    // human-readable label; empty → use name
    float        minValue     = 0.f;
    float        maxValue     = 1.f;
    float        defaultValue[4] = {};
};

struct ShaderBindingDesc {
    uint32_t          set;
    uint32_t          binding;
    RHIDescriptorType type;
    RHIShaderStage    stages;      // Which stages reference this binding
    std::string       name;        // GLSL/HLSL variable name for name-based lookup
    std::string       displayName; // from @Texture("Display Name") annotation; empty → use name
    uint32_t          arraySize  = 1;
    uint32_t          blockSize  = 0;  // UBO/SSBO declared struct size in bytes (0 for non-buffer types)

    // Per-member layout for UniformBuffer / StorageBuffer (empty for other types).
    std::vector<ShaderMemberDesc> members;
};

// ─────────────────────────────────────────────────────────────────────────────
// Full reflection data for one compiled shader stage.
// Produced by spirv-reflect at build time; loaded as a binary blob at runtime.
// ─────────────────────────────────────────────────────────────────────────────
struct ShaderReflection {
    std::vector<ShaderBindingDesc> bindings;
    uint32_t       pushConstantSize   = 0;
    RHIShaderStage pushConstantStages = RHIShaderStage::None;

    // Set by ShaderCookTool for .saglsl-compiled shaders; empty for builtin shaders.
    // Used by MaterialManager::RegisterTypesFromShaderDir to auto-register types.
    std::string shadingModel;  // e.g. "SimpleAlbedo"
    std::string vertShader;    // e.g. "deferred_geometry"

    // Convenience: find a binding by variable name (e.g. "u_AlbedoMap")
    [[nodiscard]] std::optional<ShaderBindingDesc>
    FindBinding(std::string_view name) const noexcept;

    // Convenience: find a binding by set + binding index
    [[nodiscard]] std::optional<ShaderBindingDesc>
    FindBinding(uint32_t set, uint32_t binding) const noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// Merge vertex + fragment (or any two stage) reflections into a single layout.
// Bindings at the same (set, binding) pair have their stage flags OR-ed together.
// The larger push constant range wins (must be compatible across stages).
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] ShaderReflection MergeReflections(const ShaderReflection& a,
                                                 const ShaderReflection& b);

} // namespace StellarAlia::RHI
