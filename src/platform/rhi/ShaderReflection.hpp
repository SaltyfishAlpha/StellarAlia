#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
// Vertex shader input attribute format. Parallel to RHIFormat but limited to
// the subset valid as a VkVertexInputAttributeDescription::format.
// ─────────────────────────────────────────────────────────────────────────────
enum class RHIVertexFormat : uint32_t {
    Undefined = 0,
    R32_SFLOAT,           // float
    R32G32_SFLOAT,        // vec2
    R32G32B32_SFLOAT,     // vec3
    R32G32B32A32_SFLOAT,  // vec4
};

// One vertex shader stage input. Populated only when reflecting a vertex stage;
// empty for fragment/compute. Drives VkPipelineVertexInputStateCreateInfo so
// pipelines declare only the locations the SPIR-V actually consumes.
struct ShaderVertexInputDesc {
    uint32_t        location;
    RHIVertexFormat format = RHIVertexFormat::Undefined;
};

// ─────────────────────────────────────────────────────────────────────────────
// Full reflection data for one compiled shader stage.
// Produced by spirv-reflect at build time; loaded as a binary blob at runtime.
// ─────────────────────────────────────────────────────────────────────────────
struct ShaderReflection {
    std::vector<ShaderBindingDesc> bindings;
    uint32_t       pushConstantSize   = 0;
    RHIShaderStage pushConstantStages = RHIShaderStage::None;

    // Vertex stage inputs (sorted by location). Non-empty only for vertex
    // reflections; consumed by VulkanDevice::CreatePipeline to emit one
    // VkVertexInputAttributeDescription per actually-used location.
    std::vector<ShaderVertexInputDesc> vertexInputs;

    // Generic shader-level metadata, written by the cook tool from @-annotations
    // (Issue #86/#88 generalization — replaces the ad-hoc shadingModel/vertShader
    // fields). Keys are tool-defined strings; consumers look up what they need:
    //   material:     "shadingModel"="SimpleAlbedo", "vertShader"="deferred_geometry"
    //   ScreenEffect: "stage", "inject", "in", "out"
    std::vector<std::pair<std::string, std::string>> metadata;

    // Look up a metadata value by key; returns empty string if absent.
    [[nodiscard]] std::string GetMeta(std::string_view key) const {
        for (const auto& [k, v] : metadata)
            if (k == key) return v;
        return {};
    }
    // Update-or-append a metadata key.
    void SetMeta(std::string key, std::string value) {
        for (auto& [k, v] : metadata)
            if (k == key) { v = std::move(value); return; }
        metadata.emplace_back(std::move(key), std::move(value));
    }

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
