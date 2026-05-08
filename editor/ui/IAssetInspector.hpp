#pragma once

#include <filesystem>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// IAssetInspector — one instance per asset type, registered with InspectorPanel.
//
// Draw() is called every frame while an asset of this type is selected.
// Implementations cache file content keyed on path to avoid re-reading every frame.
// ─────────────────────────────────────────────────────────────────────────────
class IAssetInspector {
public:
    virtual ~IAssetInspector() = default;
    virtual void Draw(const std::filesystem::path& path) = 0;
};

} // namespace StellarAlia::Editor
