#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <span>
#include <unordered_map>
#include <vector>

#include "core/asset/AssetID.hpp"
#include "resource/types/AnimData.hpp"
#include "resource/cook/CookedAnim.hpp"
#include "resource/cook/CookedSkeleton.hpp"
#include "resource/ResourceManager.hpp"
#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// AnimationSystem — CPU-skinning keyframe animation.
//
// Usage:
//   1. PrepareEntity() — call once per animated entity after scene load.
//      Populates SkinnedMeshComponent (dynVB, indexBuffer, subMeshes, ready).
//
//   2. Update(dt) — call each frame to evaluate FK + CPU skinning + GPU upload.
// ─────────────────────────────────────────────────────────────────────────────
class AnimationSystem {
public:
    void PrepareEntity(entt::entity entity,
                       entt::registry& registry,
                       Resource::ResourceManager& resMgr,
                       RHI::IRHIDevice* device);

    // resMgr is used to hot-swap the animation clip when clipAsset changes at runtime.
    void Update(float dt,
                entt::registry& registry,
                Resource::ResourceManager& resMgr,
                RHI::IRHIDevice* device);

    // Evaluate all prepared entities at an explicit time t, ignoring the
    // AnimatorComponent::playing flag.  Use this for Stop (t=0) or scrubbing.
    // Also hot-swaps the clip if AnimatorComponent::clipAsset changed in the editor.
    void EvaluateAll(float t,
                     entt::registry& registry,
                     Resource::ResourceManager& resMgr,
                     RHI::IRHIDevice* device);

    // Returns the most-recently computed mesh-local bone transforms for an entity.
    // Multiply each column-3 position by WorldTransformComponent.matrix to get world space.
    // Returns an empty span if the entity is not prepared or has no skeleton.
    std::span<const glm::mat4>           GetBoneGlobalPoses(entt::entity entity) const;
    std::span<const Resource::BoneInfo>  GetBoneSkeleton   (entt::entity entity) const;

private:
    struct SkinEntry {
        entt::entity entity = entt::null;

        std::vector<glm::vec3>              restPos;
        std::vector<glm::vec3>              restNorm;
        std::vector<glm::vec4>              restTang;
        std::vector<glm::vec2>              restUV;
        std::vector<Resource::SkinVertex>   skinData;
        std::vector<Resource::BoneInfo>     skeleton;
        const Resource::AnimClip*           cachedClip = nullptr;
        AssetID                             currentClipAsset;  // tracks last-loaded clip
        std::vector<uint8_t>                deformedVerts;  // vertexCount × 48 bytes
        std::vector<glm::mat4>              lastGlobalPose; // mesh-local FK result, updated each frame
    };

    std::unordered_map<uint32_t, SkinEntry> m_entries;

    static void SampleChannel(const Resource::AnimChannel& ch, float t,
                               glm::vec3& out_T, glm::quat& out_R, glm::vec3& out_S);

    static void SkinVertices(SkinEntry& e, const std::vector<glm::mat4>& skinMats);
};

} // namespace StellarAlia
