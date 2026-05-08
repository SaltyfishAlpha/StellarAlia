#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <entt/entt.hpp>

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

    // Like LoadFromFile but does NOT overwrite WorldSettings or the scene name.
    // Returns the newly added root entities (those with no parent), or empty on failure.
    // Use this to instantiate entity templates from a .sascene that contains a single
    // prototype entity, without disturbing the current scene's global settings.
    [[nodiscard]] static std::vector<entt::entity> SpawnFromTemplate(
        Scene&                       scene,
        const std::filesystem::path& path);
};

} // namespace StellarAlia
