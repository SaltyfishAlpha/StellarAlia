// AssetLoadDemo — imports MetalRoughSpheres.glb into metal_rough_spheres.sascene
//
// Steps:
//   1. Load the existing .sascene (preserves WorldSettings, camera, sun).
//   2. Recursively destroy the previous import by tag so re-running is idempotent.
//   3. Read the model UUID from its .sameta sidecar.
//   4. Create a root pivot entity, expand the GLB node tree under it.
//   5. Collapse a single-child pivot: if the GLB has one root node (Node_0),
//      absorb its transform into the pivot and re-parent its children directly,
//      eliminating the otherwise-redundant pivot → Node_0 → meshes chain.
//   6. Save the updated .sascene back to disk.

#include "core/logs/Log.hpp"
#include "core/asset/AssetID.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "function/scene/Components.hpp"
#include "resource/GltfSceneInstantiator.hpp"
#include "AssetsPath.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace StellarAlia;

// Read the uuid= line from a .sameta sidecar (plain key=value text).
static AssetID ReadMetaUuid(const fs::path& metaPath) {
    std::ifstream f(metaPath);
    if (!f) return AssetID::Invalid();
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.substr(0, 5) == "uuid=")
            return AssetID::FromString(line.substr(5));
    }
    return AssetID::Invalid();
}

// Recursively destroy entity and all its descendants.
static void DestroyRecursive(Scene& scene, entt::entity entity) {
    if (!scene.Registry().valid(entity)) return;
    auto* h = scene.Registry().try_get<HierarchyComponent>(entity);
    if (h) {
        std::vector<entt::entity> children(h->children.begin(), h->children.end());
        for (entt::entity child : children)
            DestroyRecursive(scene, child);
    }
    scene.DestroyEntity(entity);
}

int main() {
    Core::Log::Initialize();

    const fs::path assetsDir = ASSETS_SOURCE_DIR;
    const fs::path glbPath   = assetsDir / "models" / "builtin" / "MetalRoughSpheres.glb";
    const fs::path metaPath  = fs::path(glbPath.string() + ".sameta");
    const fs::path scenePath = assetsDir / "scenes" / "metal_rough_spheres.sascene";

    // ── Read model UUID ───────────────────────────────────────────────────────
    const AssetID glbId = ReadMetaUuid(metaPath);
    if (!glbId.IsValid()) {
        SA_LOG_ERROR("Cannot read UUID from '{}'", metaPath.string());
        Core::Log::Shutdown();
        return 1;
    }
    SA_LOG_INFO("Model UUID: {}", glbId.ToString());

    // ── Load existing scene (preserves WorldSettings, camera, sun) ─────────
    Scene scene("MetalRoughSpheres");
    if (!SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_ERROR("Failed to load '{}'", scenePath.string());
        Core::Log::Shutdown();
        return 1;
    }

    // ── Remove any previous import (tag-only search, recursive destroy) ───────
    {
        entt::entity old = entt::null;
        scene.View<TagComponent>().each(
            [&](entt::entity e, const TagComponent& tag) {
                if (tag.name == "MetalRoughSpheres") old = e;
            });
        if (old != entt::null) {
            DestroyRecursive(scene, old);
            SA_LOG_INFO("Removed previous MetalRoughSpheres import");
        }
    }

    // ── Create root pivot and expand GLB node tree under it ───────────────────
    const entt::entity root = scene.CreateEntity("MetalRoughSpheres");
    if (!GltfSceneInstantiator::Expand(scene, root, glbPath.string(), glbId)) {
        SA_LOG_ERROR("GltfSceneInstantiator::Expand failed");
        Core::Log::Shutdown();
        return 1;
    }

    // ── Collapse single-child pivot ───────────────────────────────────────────
    // Most GLBs have one root node that carries the model-level transform (scale,
    // rotation, etc.).  Absorb that node's transform into the pivot entity and
    // re-parent its children directly, removing the redundant level.
    {
        auto& reg = scene.Registry();
        auto* h   = reg.try_get<HierarchyComponent>(root);
        if (h && h->children.size() == 1) {
            const entt::entity glbRoot = *h->children.begin();

            // Copy GLB root's local transform into our pivot.
            auto& pivotTc = reg.get<TransformComponent>(root);
            pivotTc       = reg.get<TransformComponent>(glbRoot);
            scene.MarkDirty(root);

            // Re-parent GLB root's children directly to the pivot.
            auto* glbH = reg.try_get<HierarchyComponent>(glbRoot);
            if (glbH) {
                std::vector<entt::entity> children(
                    glbH->children.begin(), glbH->children.end());
                for (entt::entity child : children)
                    scene.SetParent(child, root);
            }

            // Destroy the now-empty intermediate node.
            scene.DestroyEntity(glbRoot);
            SA_LOG_INFO("Collapsed single GLB root node into pivot");
        }
    }

    // ── Save ──────────────────────────────────────────────────────────────────
    if (!SceneSerializer::SaveToFile(scene, scenePath)) {
        SA_LOG_ERROR("Failed to save '{}'", scenePath.string());
        Core::Log::Shutdown();
        return 1;
    }
    SA_LOG_INFO("Saved '{}'", scenePath.string());

    Core::Log::Shutdown();
    return 0;
}
