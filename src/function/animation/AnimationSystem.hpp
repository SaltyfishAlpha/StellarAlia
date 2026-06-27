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

    // Must be called once before PrepareEntity; stores the set=3 layouts used
    // to allocate per-entity descriptor sets.
    //   skinDescLayout      — bindings 0/1 (curr matrices + skinData); from
    //                         SceneRenderer::GetSkinDescLayout().
    //   velocityDescLayout  — bindings 0/1/2 (curr + skinData + prev); from
    //                         SceneRenderer::GetVelocityDescLayout() (Issue #84).
    //                         May be invalid if VelocityPrepass shader unavailable;
    //                         in that case velocityDescSet stays {} per entity.
    void Init(RHI::IRHIDevice* device,
              RHI::RHIDescLayoutHandle skinDescLayout,
              RHI::RHIDescLayoutHandle velocityDescLayout = {});

    // Releases all per-entity GPU resources (skinMatricesBuffer, skinDescSet).
    // skinDataBuffer and vertexBuffer are owned by ResourceManager and not freed here.
    void Shutdown(RHI::IRHIDevice* device, entt::registry& registry);

private:
    struct SkinEntry {
        entt::entity entity = entt::null;

        std::vector<Resource::BoneInfo>     skeleton;
        const Resource::AnimClip*           cachedClip = nullptr;
        AssetID                             currentClipAsset;  // tracks last-loaded clip

        // Pre-allocated per-bone work buffers — resized once in PrepareEntity to avoid
        // per-frame heap allocation in Update()/EvaluateAll().
        // workGlobalPose doubles as lastGlobalPose for bone gizmo queries.
        std::vector<glm::vec3> workLocalT;
        std::vector<glm::quat> workLocalR;
        std::vector<glm::vec3> workLocalS;
        std::vector<glm::mat4> workGlobalPose;
        std::vector<glm::mat4> workSkinMats;
    };

    RHI::RHIDescLayoutHandle                 m_skinDescLayout;
    RHI::RHIDescLayoutHandle                 m_velocityDescLayout;   // Issue #84
    std::unordered_map<uint32_t, SkinEntry>  m_entries;

    static void SampleChannel(const Resource::AnimChannel& ch, float t,
                               glm::vec3& out_T, glm::quat& out_R, glm::vec3& out_S);
};

} // namespace StellarAlia
