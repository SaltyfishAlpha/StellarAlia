#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "function/material/MaterialType.hpp"  // ParamDef (reused for @Param)
#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia {

class ComputeProgram;
class ShaderProgram;

// ─────────────────────────────────────────────────────────────────────────────
// ScreenEffectType (Issue #88) — the post-processing-side peer of MaterialType.
//
// User authoring (.saeffect) → cook (.spv + .refl with metadata) → registry
// auto-builds a ScreenEffectType → the generic ScreenEffectFeature runs it at the
// declared injection point. Mirrors .saglsl → MaterialType, but each effect is its
// OWN pass at an injection point (not a dispatch into a shared pass).
// ─────────────────────────────────────────────────────────────────────────────

// Frame stage an effect injects at. Anchor ScreenEffectFeatures are pre-placed at
// these points in SceneRenderer's feature list.
enum class EffectInject : uint8_t {
    AfterLighting,   // hdr lit, pre-TAA
    AfterTAA,        // hdr anti-aliased
    BeforeTonemap,   // hdr, just before tonemap
    AfterTonemap,    // ldr (post-tonemap)
};

[[nodiscard]] const char* EffectInjectName(EffectInject) noexcept;
[[nodiscard]] bool        ParseEffectInject(std::string_view, EffectInject& out) noexcept;

// One declared @In / @Out resource, referencing the engine handle vocabulary by
// name (e.g. "hdr", "depth"). `storage` = written as UAV (compute) / produced as
// the effect output; false = sampled input.
struct ScreenEffectResource {
    std::string name;
    bool        storage = false;
};

// A registered effect: description (from .refl metadata) + runtime resources.
struct ScreenEffectType {
    std::string  name;                 // @Effect (unique id / display)
    EffectInject inject = EffectInject::AfterTonemap;
    bool         isCompute = false;    // @Stage compute | fragment
    std::vector<ScreenEffectResource> ins;   // @In  (sampled)
    std::vector<ScreenEffectResource> outs;  // @Out (engine allocates transient + redirect)

    std::vector<ParamDef> params;      // @Param, set=2 binding=0 EffectParams UBO
    uint32_t              paramUboSize = 0;

    bool enabled       = true;
    bool isProjectType = false;        // cooked from a project; cleared on project switch

    // Runtime, populated at registration:
    ComputeProgram*       computeProg  = nullptr;  // when isCompute
    ShaderProgram*        graphicsProg = nullptr;  // when fragment
    RHI::RHIDescSetHandle descSet;                 // set=2 resources (rebound per frame)
    RHI::RHIBufferHandle  paramUbo;                // set=2 binding=0 EffectParams UBO (cpu-visible)
    std::vector<uint8_t>  paramBlob;               // current param values (global "instance")
};

} // namespace StellarAlia
