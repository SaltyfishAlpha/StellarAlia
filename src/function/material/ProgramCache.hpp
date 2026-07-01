#pragma once

#include <memory>
#include <span>
#include <string>
#include <unordered_map>

#include "function/material/ComputeProgram.hpp"
#include "function/material/ShaderProgram.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/ShaderReflection.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// ProgramCache (Issue #86)
//
// Central owner of all GPU programs — graphics `ShaderProgram` (vert+frag) and
// `ComputeProgram`. Resource-layer service, owned by SceneRenderer and injected
// via FeatureInitContext::programs. Holders (MaterialType, RenderFeature) keep
// raw pointers into the cache; the cache owns program lifetime.
//
// Not a dedup layer: programs are keyed per holder (no cross-holder sharing), so
// each MaterialType / feature variant keeps its own program instance — preserving
// the existing per-program pipeline-cache (AttachmentKey-keyed) safety. Value is
// central ownership + uniform loading + uniform hot-reload + engine/project scope.
// ─────────────────────────────────────────────────────────────────────────────
class ProgramCache {
public:
    // device/frameLayout/bindlessLayout/shaderDir captured for subsequent loads.
    // shaderDir is the engine builtin shader dir (compute + builtin graphics).
    void Init(RHI::IRHIDevice*         device,
              RHI::RHIDescLayoutHandle frameLayout,
              RHI::RHIDescLayoutHandle bindlessLayout,
              std::string              shaderDir);
    void Shutdown();

    // Compute: get-or-load by shader stem ("ssr" → <shaderDir>/ssr.comp.spv/.refl).
    // useFrameLayout=true wires the per-frame layout at set=1 (engine convention).
    // primaryDir/fallbackDir (Issue #91): when primaryDir is non-empty the .comp.spv/
    // .refl are resolved there first then fallbackDir (for project-cooked compute
    // .saeffect in cook_cache/shaders); empty primaryDir keeps the engine builtin dir.
    // Returns nullptr on load failure.
    ComputeProgram* GetCompute(const std::string& stem,
                               bool useFrameLayout = true,
                               bool projectScope   = false,
                               const std::string& primaryDir  = {},
                               const std::string& fallbackDir = {});

    // Graphics: get-or-load by holder key (unique per MaterialType name /
    // "feature:variant"). vert/frag stems resolved in primaryDir, then fallbackDir.
    // Each holder owns its own ShaderProgram (no cross-holder sharing). nullptr on fail.
    ShaderProgram* GetGraphics(const std::string& key,
                               const std::string& vertStem,
                               const std::string& fragStem,
                               const std::string& primaryDir,
                               const std::string& fallbackDir = {},
                               bool               projectScope = false);

    // Hot-reload a graphics program's fragment shader by key. Device must be idle.
    bool ReloadGraphicsFrag(const std::string&           key,
                            std::span<const uint8_t>     fragSpv,
                            const RHI::ShaderReflection& fragRefl);

    // Drop all project-scoped programs on project switch. GPU must be idle.
    void ClearProjectPrograms();

private:
    template <class Prog>
    struct Entry { std::unique_ptr<Prog> prog; bool project = false; };

    RHI::IRHIDevice*         m_device = nullptr;
    RHI::RHIDescLayoutHandle m_frameLayout;
    RHI::RHIDescLayoutHandle m_bindlessLayout;
    std::string              m_shaderDir;

    std::unordered_map<std::string, Entry<ComputeProgram>> m_compute;   // key = stem
    std::unordered_map<std::string, Entry<ShaderProgram>>  m_graphics;  // key = holder
};

} // namespace StellarAlia
