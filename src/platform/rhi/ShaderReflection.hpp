#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platform/rhi/RHITypes.hpp"

namespace StellarAlia::RHI {

// ─────────────────────────────────────────────────────────────────────────────
// One descriptor binding slot as extracted from SPIR-V reflection.
// Deserialized from a pre-compiled .refl file at runtime.
// ─────────────────────────────────────────────────────────────────────────────
// One member field inside a UBO or SSBO struct.
struct ShaderMemberDesc {
    std::string name;    // GLSL member name, e.g. "baseColorFactor"
    uint32_t    offset;  // byte offset within the struct (from SPIR-V Offset decoration)
    uint32_t    size;    // byte size of the member (not including std140 tail padding)
};

struct ShaderBindingDesc {
    uint32_t          set;
    uint32_t          binding;
    RHIDescriptorType type;
    RHIShaderStage    stages;      // Which stages reference this binding
    std::string       name;        // GLSL/HLSL variable name for name-based lookup
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
