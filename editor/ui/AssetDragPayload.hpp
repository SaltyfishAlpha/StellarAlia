#pragma once

#include "core/asset/AssetID.hpp"

#include <cstddef>
#include <type_traits>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// AssetDragPayload — ImGui drag-drop payload for asset drags from AssetsPanel.
//
// Carried under the "SAASSET" payload type. Receivers can use either:
//   - id     : preferred (stable, type-filterable via the type[] tag)
//   - relPath: legacy/back-compat (path-based dispatch — e.g. Hierarchy spawn
//              flow that distinguishes by extension)
//
// Layout note: `path` is FIRST so that pre-#73 receivers that read
// `static_cast<const char*>(payload->Data)` as a null-terminated path string
// continue to work unchanged during the migration window (#73 Step 5 → Step 7).
//
// `path` is the absolute disk path of the asset (preserved verbatim from
// AssetsPanel's directory iterator). Receivers that need a project-relative
// path should compute it themselves; this struct does NOT promise relative.
//
// POD so ImGui::SetDragDropPayload can memcpy it as bytes.
// ─────────────────────────────────────────────────────────────────────────────
struct AssetDragPayload {
    char    path[260];      // absolute, null-terminated, POSIX or native separators
    char    type[32];       // AssetEntry::type — "Mesh"/"Texture"/"Material"/"Script"/...
    AssetID id;
};

static_assert(std::is_trivially_copyable_v<AssetDragPayload>,
              "AssetDragPayload must be trivially copyable for ImGui payload memcpy");
static_assert(sizeof(AssetDragPayload) < 512,
              "AssetDragPayload size budget exceeded");

} // namespace StellarAlia::Editor
