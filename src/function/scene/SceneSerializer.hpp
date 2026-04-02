#pragma once

#include <filesystem>
#include <string>

namespace StellarAlia {

class Scene;

// ─────────────────────────────────────────────────────────────────────────────
// SceneSerializer
//
// Reads and writes .sascene files (UTF-8 JSON).
//
// Format overview — see SceneSerializer.cpp for the full schema.
// Entity identity across load/save is preserved via a per-entity "id" field
// that maps to the serialized integer, NOT the runtime entt::entity value
// (which is opaque and may change between runs).
// ─────────────────────────────────────────────────────────────────────────────
struct SceneSerializer {
    // Serialize 'scene' to JSON and write to 'path'.
    // Returns false and logs on failure.
    [[nodiscard]] static bool SaveToFile(const Scene&                 scene,
                                         const std::filesystem::path& path);

    // Parse JSON at 'path' into 'scene' (appends entities — does not clear first).
    // Returns false and leaves 'scene' unchanged on failure.
    [[nodiscard]] static bool LoadFromFile(Scene&                       scene,
                                           const std::filesystem::path& path);
};

} // namespace StellarAlia
