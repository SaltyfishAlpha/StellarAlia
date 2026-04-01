#include "platform/rhi/ShaderReflection.hpp"

#include <algorithm>

namespace StellarAlia::RHI {

std::optional<ShaderBindingDesc>
ShaderReflection::FindBinding(std::string_view name) const noexcept {
    for (const auto& b : bindings)
        if (b.name == name)
            return b;
    return std::nullopt;
}

std::optional<ShaderBindingDesc>
ShaderReflection::FindBinding(uint32_t set, uint32_t binding) const noexcept {
    for (const auto& b : bindings)
        if (b.set == set && b.binding == binding)
            return b;
    return std::nullopt;
}

ShaderReflection MergeReflections(const ShaderReflection& a,
                                   const ShaderReflection& b) {
    ShaderReflection merged;
    merged.bindings = a.bindings;

    // Push constant: take the larger size, union the stages
    merged.pushConstantSize   = std::max(a.pushConstantSize, b.pushConstantSize);
    merged.pushConstantStages = a.pushConstantStages | b.pushConstantStages;

    for (const auto& bBinding : b.bindings) {
        auto it = std::find_if(
            merged.bindings.begin(), merged.bindings.end(),
            [&](const ShaderBindingDesc& existing) {
                return existing.set == bBinding.set &&
                       existing.binding == bBinding.binding;
            });

        if (it != merged.bindings.end()) {
            // Same slot used in both stages — union the stage flags
            it->stages |= bBinding.stages;
        } else {
            merged.bindings.push_back(bBinding);
        }
    }

    return merged;
}

} // namespace StellarAlia::RHI
