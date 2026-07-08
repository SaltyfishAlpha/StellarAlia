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

struct BonePoseOverrideComponent;
struct SkinnedMeshComponent;

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
    // AnimatorComponent::playing flag.  t < 0 evaluates each entity at its own
    // AnimatorComponent::time — the Editing-state pose convention: assign-clip /
    // scrub / PIE-stop all display clip@time, so Play→Edit restores exactly the
    // pre-play visual. Also hot-swaps the clip when clipAsset changed.
    void EvaluateAll(float t,
                     entt::registry& registry,
                     Resource::ResourceManager& resMgr,
                     RHI::IRHIDevice* device);

    // Recompute + upload ONE entity's pose immediately (#83 bone editing).
    // Editor hook for override edits while the simulation is not running —
    // Update() only runs in Play. With a cached clip the pose re-samples at
    // the Animator's frozen time (an edit must not snap the body to bind
    // pose); otherwise bind locals + overrides via EvaluateStaticPose.
    void RefreshPose(entt::entity entity,
                     entt::registry& registry,
                     RHI::IRHIDevice* device);

    // #83 P2 editor crossfade test hook — Update only runs in Play, so the
    // editor drives fades itself: request a fade to toClip, then call
    // TickEditor each frame (timeline panel preview loop) to advance time +
    // fade and re-upload. No velocity double-buffer (editor has none).
    void RequestEditorCrossfade(entt::entity entity, const AssetID& toClip,
                                float fadeSeconds,
                                entt::registry& registry,
                                Resource::ResourceManager& resMgr);
    void TickEditor(entt::entity entity, float dt,
                    entt::registry& registry, RHI::IRHIDevice* device);

    // Returns the most-recently computed mesh-local bone transforms for an entity.
    // Multiply each column-3 position by WorldTransformComponent.matrix to get world space.
    // Returns an empty span if the entity is not prepared or has no skeleton.
    std::span<const glm::mat4>           GetBoneGlobalPoses(entt::entity entity) const;
    std::span<const Resource::BoneInfo>  GetBoneSkeleton   (entt::entity entity) const;

    // #83 P1 observability — palette upload accounting since the last
    // BeginFrameStats() (Application calls it once per frame). "skipped" =
    // Static Pose Skip hits; verify the skip works by pausing a clip and
    // watching bytes drop to 0 while skipped counts up.
    struct UploadStats {
        uint32_t evaluated = 0;   // entities whose pose was recomputed
        uint32_t uploaded  = 0;   // entities that wrote GPU palettes
        uint32_t skipped   = 0;   // Static Pose Skip hits
        uint64_t bytes     = 0;   // palette bytes written
    };
    void BeginFrameStats() { m_uploadStats = {}; }
    [[nodiscard]] const UploadStats& GetUploadStats() const { return m_uploadStats; }

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
        // #83 P1: packed palette — mat3x4 rows of (global · IBM), 48 B/bone.
        std::vector<glm::mat3x4> workSkinMats;

        // Bind-pose local TRS (derived from the inverse-bind matrices at
        // PrepareEntity) — the sampling base when no clip plays (#83 bone edit).
        std::vector<glm::vec3> bindLocalT;
        std::vector<glm::quat> bindLocalR;
        std::vector<glm::vec3> bindLocalS;

        // #83 P2 crossfade — second sample buffer for the outgoing clip.
        std::vector<glm::vec3> workLocalT2;
        std::vector<glm::quat> workLocalR2;
        std::vector<glm::vec3> workLocalS2;
        const Resource::AnimClip* fadeFromClip = nullptr;   // null = not fading
        float                     fadeFromTime = 0.f;        // outgoing clip time at fade start
        float                     fadeElapsed  = 0.f;
        float                     fadeDurationSnap = 0.f;    // captured at trigger
        // True while pose overrides were applied on the no-clip path — one more
        // recompute after the last override is removed restores the bind pose.
        bool overrideWasActive = false;

        // #83 P1 Static Pose Skip: hash of the last uploaded palette. Upload
        // is skipped when the pose is byte-identical AND prev already equals
        // curr (prevSynced) — the transition frame still swaps once so the
        // velocity pass sees prev == curr before skipping starts.
        uint64_t lastUploadedHash = 0;
        bool     prevSynced       = false;
    };

    RHI::RHIDescLayoutHandle                 m_skinDescLayout;
    RHI::RHIDescLayoutHandle                 m_velocityDescLayout;   // Issue #84
    std::unordered_map<uint32_t, SkinEntry>  m_entries;
    UploadStats                              m_uploadStats;

    static void SampleChannel(const Resource::AnimChannel& ch, float t,
                               glm::vec3& out_T, glm::quat& out_R, glm::vec3& out_S);

    // #83 P2: sample one clip at time t into a local-TRS triple (reset first).
    static void SampleClipLocals(const Resource::AnimClip& clip, float t, uint32_t nBones,
                                 std::vector<glm::vec3>& T,
                                 std::vector<glm::quat>& R,
                                 std::vector<glm::vec3>& S);

    // #83 P2: sample main (+ optional crossfade-from at weight w) → apply bone
    // overrides → globalPose → packed workSkinMats. Does NOT upload; caller
    // owns the swap/upload policy (Update vs editor/EvaluateAll). w = to-clip
    // weight (1 = only main). Shared by Update, EvaluateAll, editor test hook.
    void EvaluateClipPose(SkinEntry& entry,
                          const Resource::AnimClip& main, float mainT,
                          const Resource::AnimClip* from, float fromT, float w,
                          const BonePoseOverrideComponent* ovr);

    // Recompute + upload a pose from bind locals + optional overrides — the
    // no-active-clip path (#83 bone editing). Uploads to BOTH matrix buffers
    // (discrete pose change → zero velocity).
    void EvaluateStaticPose(SkinEntry&                      entry,
                            const BonePoseOverrideComponent* ovr,
                            SkinnedMeshComponent&            meshComp,
                            RHI::IRHIDevice*                 device);
};

} // namespace StellarAlia
