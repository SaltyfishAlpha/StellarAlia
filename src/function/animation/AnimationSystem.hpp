#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <unordered_map>
#include <vector>

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

    void Update(float dt,
                entt::registry& registry,
                RHI::IRHIDevice* device);

    // Evaluate all prepared entities at an explicit time t, ignoring the
    // AnimatorComponent::playing flag.  Use this for Stop (t=0) or scrubbing.
    void EvaluateAll(float t,
                     entt::registry& registry,
                     RHI::IRHIDevice* device);

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
        std::vector<uint8_t>                deformedVerts;  // vertexCount × 48 bytes
    };

    std::unordered_map<uint32_t, SkinEntry> m_entries;

    static void SampleChannel(const Resource::AnimChannel& ch, float t,
                               glm::vec3& out_T, glm::quat& out_R, glm::vec3& out_S);

    static void SkinVertices(SkinEntry& e, const std::vector<glm::mat4>& skinMats);
};

} // namespace StellarAlia
