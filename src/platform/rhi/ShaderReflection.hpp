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
struct ShaderBindingDesc {
    uint32_t          set;
    uint32_t          binding;
    RHIDescriptorType type;
    RHIShaderStage    stages;      // Which stages reference this binding
    std::string       name;        // GLSL/HLSL variable name for name-based lookup
    uint32_t          arraySize = 1;
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
