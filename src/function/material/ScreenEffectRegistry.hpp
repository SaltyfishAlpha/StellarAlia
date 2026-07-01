#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "function/material/ScreenEffectType.hpp"

namespace StellarAlia {

struct FeatureInitContext;

// ─────────────────────────────────────────────────────────────────────────────
// ScreenEffectRegistry (Issue #88) — the post-processing-side peer of
// MaterialManager: scans cooked *.saeffect.refl, owns ScreenEffectTypes, loads
// their programs via ProgramCache, and supports project-scoped clearing.
// Owned by SceneRenderer.
// ─────────────────────────────────────────────────────────────────────────────
class ScreenEffectRegistry {
public:
    // Scan a cooked shader dir for *.saeffect.refl, register each effect.
    // Loads its program (via ctx.programs), allocates its set=2 descriptor set,
    // extracts @Param layout. isProjectType marks effects for ClearProjectEffects.
    void Scan(const std::string& cookDir, const FeatureInitContext& ctx, bool isProjectType);

    // All enabled effects declared at the given injection point (registration order).
    [[nodiscard]] std::vector<ScreenEffectType*> GetByInject(EffectInject inject);

    [[nodiscard]] const std::vector<std::unique_ptr<ScreenEffectType>>& All() const { return m_effects; }
    [[nodiscard]] ScreenEffectType* Find(std::string_view name);

    // Drop project-scoped effects on project switch (programs freed by ProgramCache).
    // Frees each dropped effect's param UBO + descriptor set via `device`.
    void ClearProjectEffects(RHI::IRHIDevice* device);

private:
    std::vector<std::unique_ptr<ScreenEffectType>> m_effects;
};

} // namespace StellarAlia
