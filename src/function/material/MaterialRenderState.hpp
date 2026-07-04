#pragma once

#include <cstdint>

namespace StellarAlia {

// Issue #56 — per-material-asset transparency classification (glTF alphaMode).
enum class AlphaMode : uint8_t { Opaque = 0, Mask = 1, Blend = 2 };

// Per-instance pipeline-state overrides carried by the material asset
// (.samatc top-level fields). Shader params (e.g. alphaCutoff) are NOT here —
// they flow through the reflected MaterialParams blob.
struct MaterialRenderState {
    AlphaMode alphaMode   = AlphaMode::Opaque;
    bool      doubleSided = false;
};

} // namespace StellarAlia
