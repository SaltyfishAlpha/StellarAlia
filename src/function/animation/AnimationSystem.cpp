#include "function/animation/AnimationSystem.hpp"

#include "function/scene/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedAnim.hpp"
#include "resource/cook/CookedSkeleton.hpp"
#include "core/logs/Log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

namespace StellarAlia {

// FNV-1a over the packed palette (#83 P1 Static Pose Skip).
static uint64_t HashBytes(const void* data, size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::Init(RHI::IRHIDevice* /*device*/,
                            RHI::RHIDescLayoutHandle skinDescLayout,
                            RHI::RHIDescLayoutHandle velocityDescLayout) {
    m_skinDescLayout     = skinDescLayout;
    m_velocityDescLayout = velocityDescLayout;
}

void AnimationSystem::Shutdown(RHI::IRHIDevice* device, entt::registry& registry) {
    for (auto& [entId, entry] : m_entries) {
        auto* meshComp = registry.try_get<SkinnedMeshComponent>(entry.entity);
        if (meshComp) {
            if (meshComp->skinDescSet.IsValid())
                device->FreeDescriptorSet(meshComp->skinDescSet);
            if (meshComp->velocityDescSet.IsValid())
                device->FreeDescriptorSet(meshComp->velocityDescSet);
            if (meshComp->skinMatricesBuffer.IsValid())
                device->DestroyBuffer(meshComp->skinMatricesBuffer);
            if (meshComp->skinMatricesBufferPrev.IsValid())
                device->DestroyBuffer(meshComp->skinMatricesBufferPrev);
            meshComp->skinDescSet            = {};
            meshComp->velocityDescSet        = {};
            meshComp->skinMatricesBuffer     = {};
            meshComp->skinMatricesBufferPrev = {};
            meshComp->ready                  = false;
            meshComp->poseSeeded             = false;
        }
    }
    m_entries.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// PrepareEntity
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::PrepareEntity(entt::entity entity,
                                     entt::registry& registry,
                                     Resource::ResourceManager& resMgr,
                                     RHI::IRHIDevice* device) {
    auto* meshComp = registry.try_get<SkinnedMeshComponent>(entity);
    if (!meshComp || !meshComp->meshAsset.IsValid()) return;
    if (meshComp->ready) return;

    // #83 P1: explicit skeleton asset wins; derived ID is the legacy fallback.
    const AssetID skelID = meshComp->skeletonAsset.IsValid()
        ? meshComp->skeletonAsset
        : Resource::DeriveSkinID(meshComp->meshAsset, 0);

    const Resource::CookedSkeleton* skel = resMgr.LoadSkeleton(skelID);
    if (!skel) {
        SA_LOG_ERROR("AnimationSystem: skeleton {} not found (derived from mesh {})",
                     skelID.ToString(), meshComp->meshAsset.ToString());
        return;
    }

    const auto* animComp = registry.try_get<AnimatorComponent>(entity);
    const Resource::CookedAnim* cookedAnim = nullptr;
    if (animComp && animComp->clipAsset.IsValid()) {
        cookedAnim = resMgr.LoadAnimClip(animComp->clipAsset);
        if (!cookedAnim)
            SA_LOG_WARN("AnimationSystem: anim clip {} not found — using bind pose",
                        animComp->clipAsset.ToString());
    }

    const Resource::GPUMesh* gpuMesh = resMgr.LoadMesh(meshComp->meshAsset);
    if (!gpuMesh || !gpuMesh->IsSkinned()) {
        SA_LOG_ERROR("AnimationSystem: mesh {} not found or not skinned",
                     meshComp->meshAsset.ToString());
        return;
    }

    const uint32_t boneCount = static_cast<uint32_t>(skel->bones.size());

    // ── Bone matrices buffers — curr + prev double buffer (Issue #84) ─────────
    {
        const uint64_t matBufSize = static_cast<uint64_t>(boneCount) * sizeof(glm::mat3x4);
        RHI::RHIBufferDesc d{};
        d.size       = matBufSize;
        d.usage      = RHI::RHIBufferUsage::Storage;
        d.cpuVisible = true;
        d.debugName  = "SkinMatricesBuffer";
        meshComp->skinMatricesBuffer = device->CreateBuffer(d);
        d.debugName  = "SkinMatricesBufferPrev";
        meshComp->skinMatricesBufferPrev = device->CreateBuffer(d);
        if (!meshComp->skinMatricesBuffer.IsValid() || !meshComp->skinMatricesBufferPrev.IsValid()) {
            SA_LOG_ERROR("AnimationSystem: failed to allocate skinMatrices double buffer");
            return;
        }
        std::vector<glm::mat3x4> identity(boneCount, glm::mat3x4(1.f));
        device->UploadBufferData(meshComp->skinMatricesBuffer,
                                 identity.data(),
                                 static_cast<uint64_t>(boneCount) * sizeof(glm::mat3x4), 0);
        device->UploadBufferData(meshComp->skinMatricesBufferPrev,
                                 identity.data(),
                                 static_cast<uint64_t>(boneCount) * sizeof(glm::mat3x4), 0);
    }

    // ── Allocate descriptor sets ──────────────────────────────────────────────
    // skinDescSet (set=3, bindings 0/1) — used by deferred geometry / shadow / mask
    //   binding 0 = curr skinMatrices    binding 1 = GPUMesh::skinDataBuffer (shared)
    // velocityDescSet (set=3, bindings 0/1/2) — Issue #84, used by VelocityPrepass
    //   binding 2 = prev skinMatrices (additional slot)
    if (m_skinDescLayout.IsValid()) {
        meshComp->skinDescSet = device->AllocateDescriptorSet(m_skinDescLayout);
        if (!meshComp->skinDescSet.IsValid()) {
            SA_LOG_ERROR("AnimationSystem: failed to allocate skinDescSet");
            return;
        }
        device->WriteDescriptorBuffer(meshComp->skinDescSet, 0, meshComp->skinMatricesBuffer);
        device->WriteDescriptorBuffer(meshComp->skinDescSet, 1, gpuMesh->skinDataBuffer);
    } else {
        SA_LOG_WARN("AnimationSystem: no skinDescLayout — skinDescSet not allocated");
    }
    if (m_velocityDescLayout.IsValid()) {
        meshComp->velocityDescSet = device->AllocateDescriptorSet(m_velocityDescLayout);
        if (meshComp->velocityDescSet.IsValid()) {
            device->WriteDescriptorBuffer(meshComp->velocityDescSet, 0, meshComp->skinMatricesBuffer);
            device->WriteDescriptorBuffer(meshComp->velocityDescSet, 1, gpuMesh->skinDataBuffer);
            device->WriteDescriptorBuffer(meshComp->velocityDescSet, 2, meshComp->skinMatricesBufferPrev);
        }
    }
    meshComp->poseSeeded = false;

    meshComp->boneCount = boneCount;

    // ── Build SkinEntry ───────────────────────────────────────────────────────
    SkinEntry entry;
    entry.entity       = entity;
    entry.skeleton     = skel->bones;
    entry.cachedClip   = cookedAnim ? &cookedAnim->clip : nullptr;
    entry.workLocalT.assign(boneCount, glm::vec3{0.f, 0.f, 0.f});
    entry.workLocalR.assign(boneCount, glm::quat{1.f, 0.f, 0.f, 0.f});
    entry.workLocalS.assign(boneCount, glm::vec3{1.f, 1.f, 1.f});
    entry.workGlobalPose.resize(boneCount);
    entry.workSkinMats.resize(boneCount);
    entry.workLocalT2.assign(boneCount, glm::vec3{0.f});         // #83 P2 crossfade
    entry.workLocalR2.assign(boneCount, glm::quat{1.f, 0.f, 0.f, 0.f});
    entry.workLocalS2.assign(boneCount, glm::vec3{1.f});

    // Bind-pose globals — global bind matrix = inverse(inverseBindMatrix).
    // Keeps the skeleton gizmo (and any pose reader) meaningful for entities
    // without a clip (Issue #108: VRM ships no animations), where EvaluateAll
    // never runs and workGlobalPose would stay uninitialized.
    for (uint32_t bi = 0; bi < boneCount; ++bi)
        entry.workGlobalPose[bi] = glm::inverse(skel->bones[bi].inverseBindMatrix);

    // Bind-pose LOCALS (#83 bone editing) — the sampling base for the no-clip
    // path: local = parentGlobal⁻¹ · global, decomposed to TRS.
    entry.bindLocalT.resize(boneCount);
    entry.bindLocalR.resize(boneCount);
    entry.bindLocalS.resize(boneCount);
    for (uint32_t bi = 0; bi < boneCount; ++bi) {
        const int32_t parent = skel->bones[bi].parentIndex;
        const glm::mat4 local = parent >= 0
            ? glm::inverse(entry.workGlobalPose[parent]) * entry.workGlobalPose[bi]
            : entry.workGlobalPose[bi];
        entry.bindLocalT[bi] = glm::vec3(local[3]);
        glm::vec3 s{glm::length(glm::vec3(local[0])),
                    glm::length(glm::vec3(local[1])),
                    glm::length(glm::vec3(local[2]))};
        s = glm::max(s, glm::vec3(1e-7f));
        entry.bindLocalS[bi] = s;
        entry.bindLocalR[bi] = glm::quat_cast(glm::mat3(
            glm::vec3(local[0]) / s.x,
            glm::vec3(local[1]) / s.y,
            glm::vec3(local[2]) / s.z));
    }

    if (animComp) entry.currentClipAsset = animComp->clipAsset;
    if (animComp) meshComp->lastEvalClipId = animComp->clipAsset;
    meshComp->ready = true;

    const uint32_t entId = static_cast<uint32_t>(entt::to_integral(entity));
    m_entries[entId] = std::move(entry);

    SA_LOG_INFO("AnimationSystem: prepared entity {} ({} bones, clip='{}')",
                entId, boneCount,
                cookedAnim ? cookedAnim->clip.name : "(bind pose)");
}

// ─────────────────────────────────────────────────────────────────────────────
// EvaluateAll
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::EvaluateAll(float t,
                                   entt::registry& registry,
                                   Resource::ResourceManager& resMgr,
                                   RHI::IRHIDevice* device) {
    for (auto& [entId, entry] : m_entries) {
        auto* meshComp = registry.try_get<SkinnedMeshComponent>(entry.entity);
        if (!meshComp || !meshComp->ready || entry.skeleton.empty()) continue;

        const auto* animComp = registry.try_get<AnimatorComponent>(entry.entity);
        if (animComp && animComp->clipAsset.IsValid() &&
            !(animComp->clipAsset == entry.currentClipAsset))
        {
            const Resource::CookedAnim* newAnim = resMgr.LoadAnimClip(animComp->clipAsset);
            if (newAnim) {
                entry.cachedClip       = &newAnim->clip;
                entry.currentClipAsset = animComp->clipAsset;
            } else {
                entry.currentClipAsset = animComp->clipAsset;
            }
        }

        if (!entry.cachedClip) {
            // #83: clip-less entities must still honor pose overrides here —
            // EvaluateAll runs on PIE stop / scene load, and skipping them
            // snaps an edited pose back to bind until the next manual edit.
            const auto* ovr = registry.try_get<BonePoseOverrideComponent>(entry.entity);
            if (ovr && !ovr->bones.empty()) {
                EvaluateStaticPose(entry, ovr, *meshComp, device);
                entry.overrideWasActive = true;
            }
            continue;
        }

        const Resource::AnimClip& clip   = *entry.cachedClip;
        const uint32_t            nBones = static_cast<uint32_t>(entry.skeleton.size());
        // t < 0 → per-entity Animator time (Editing-state pose convention).
        const float               reqT   = t >= 0.f ? t : (animComp ? animComp->time : 0.f);
        const float               evalT  = std::clamp(reqT, 0.f, clip.duration);

        auto& localT     = entry.workLocalT;
        auto& localR     = entry.workLocalR;
        auto& localS     = entry.workLocalS;
        auto& globalPose = entry.workGlobalPose;
        auto& skinMats   = entry.workSkinMats;
        std::fill(localT.begin(), localT.end(), glm::vec3{0.f, 0.f, 0.f});
        std::fill(localR.begin(), localR.end(), glm::quat{1.f, 0.f, 0.f, 0.f});
        std::fill(localS.begin(), localS.end(), glm::vec3{1.f, 1.f, 1.f});

        for (const auto& ch : clip.channels) {
            if (ch.boneIndex < 0 || ch.boneIndex >= (int32_t)nBones) continue;
            SampleChannel(ch, evalT, localT[ch.boneIndex], localR[ch.boneIndex], localS[ch.boneIndex]);
        }

        // #83 bone editing: overrides pin bones during scrubbing too.
        if (const auto* ovr = registry.try_get<BonePoseOverrideComponent>(entry.entity))
            for (const auto& [bi, trs] : ovr->bones)
                if (bi >= 0 && bi < static_cast<int32_t>(nBones)) {
                    localT[bi] = trs.position;
                    localR[bi] = trs.rotation;
                    localS[bi] = trs.scale;
                }

        for (uint32_t bi = 0; bi < nBones; ++bi) {
            glm::mat4 localMat = glm::mat4_cast(glm::normalize(localR[bi]));
            localMat[0] *= localS[bi].x;
            localMat[1] *= localS[bi].y;
            localMat[2] *= localS[bi].z;
            localMat[3]  = glm::vec4(localT[bi], 1.f);
            const int32_t parent = entry.skeleton[bi].parentIndex;
            globalPose[bi] = (parent >= 0) ? (globalPose[parent] * localMat) : localMat;
        }

        for (uint32_t bi = 0; bi < nBones; ++bi)
            skinMats[bi] = glm::mat3x4(glm::transpose(globalPose[bi] * entry.skeleton[bi].inverseBindMatrix));

        device->UploadBufferData(meshComp->skinMatricesBuffer,
                                 skinMats.data(),
                                 static_cast<uint64_t>(nBones) * sizeof(glm::mat3x4), 0);
        // curr-only write — invalidate the skip state so the next Update
        // re-uploads and re-syncs prev (#83 P1).
        entry.lastUploadedHash = 0;
        entry.prevSynced       = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Update
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::Update(float dt,
                              entt::registry& registry,
                              Resource::ResourceManager& resMgr,
                              RHI::IRHIDevice* device) {
    auto view = registry.view<AnimatorComponent, SkinnedMeshComponent>();

    for (auto entity : view) {
        auto& animComp = view.get<AnimatorComponent>(entity);
        auto& meshComp = view.get<SkinnedMeshComponent>(entity);
        if (!meshComp.ready) continue;

        const uint32_t entId = static_cast<uint32_t>(entt::to_integral(entity));
        auto it = m_entries.find(entId);
        if (it == m_entries.end()) continue;
        SkinEntry& entry = it->second;

        // ── Clip hot-swap (crossfade when eligible, #83 P2) ───────────────────
        bool enteredFadeThisFrame = false;
        if (animComp.clipAsset.IsValid() &&
            !(animComp.clipAsset == entry.currentClipAsset))
        {
            const Resource::CookedAnim* newAnim = resMgr.LoadAnimClip(animComp.clipAsset);
            // #83 P2-5: SetClip forces a hard cut (pendingFadeOverride==0),
            // CrossfadeTo forces a specific fade (>0); <0 falls back to fadeDuration.
            const float effFade = animComp.pendingFadeOverride >= 0.f
                                  ? animComp.pendingFadeOverride : animComp.fadeDuration;
            animComp.pendingFadeOverride = -1.f;  // one-shot: consume on swap detection
            if (newAnim) {
                // Fade from the outgoing clip when one is playing and fade > 0;
                // else hard cut. Capture the outgoing clip + its time first.
                if (entry.cachedClip && animComp.playing && effFade > 0.f) {
                    entry.fadeFromClip     = entry.cachedClip;
                    entry.fadeFromTime     = animComp.time;
                    entry.fadeElapsed      = 0.f;
                    entry.fadeDurationSnap = effFade;
                    enteredFadeThisFrame   = true;
                }
                entry.cachedClip       = &newAnim->clip;
                entry.currentClipAsset = animComp.clipAsset;
                animComp.time          = 0.f;
                SA_LOG_INFO("AnimationSystem: {} → '{}'",
                            enteredFadeThisFrame ? "crossfade" : "swap", newAnim->clip.name);
            } else {
                SA_LOG_WARN("AnimationSystem: clip {} not found — keeping previous",
                            animComp.clipAsset.ToString());
                entry.currentClipAsset = animComp.clipAsset;
            }
        }

        if (!animComp.playing || !entry.cachedClip || entry.skeleton.empty()) {
            // #83 bone editing: without an active clip, pose overrides drive a
            // bind-pose recompute — plus one restore pass after the last
            // override is removed.
            const auto* ovr = registry.try_get<BonePoseOverrideComponent>(entity);
            const bool hasOvr = ovr && !ovr->bones.empty();
            if ((hasOvr || entry.overrideWasActive) && !entry.skeleton.empty())
                EvaluateStaticPose(entry, hasOvr ? ovr : nullptr, meshComp, device);
            entry.overrideWasActive = hasOvr;
            continue;
        }

        const Resource::AnimClip& clip = *entry.cachedClip;

        animComp.time += dt * animComp.speed;
        if (animComp.looping && clip.duration > 0.f)
            animComp.time = std::fmod(animComp.time, clip.duration);
        else
            animComp.time = std::min(animComp.time, clip.duration);

        const uint32_t nBones = static_cast<uint32_t>(entry.skeleton.size());

        const bool firstWrite = !meshComp.poseSeeded;
        const bool clipSwap   = !(animComp.clipAsset == meshComp.lastEvalClipId);

        // ── Crossfade advance (#83 P2) ────────────────────────────────────────
        // Both clips keep playing during the blend (double-advance — a frozen
        // outgoing clip reads stiff). w = to-clip weight; hits 1 → fade ends.
        const Resource::AnimClip* fromClip = entry.fadeFromClip;
        float fadeW = 1.f, fromT = 0.f;
        if (fromClip) {
            entry.fadeElapsed += dt * animComp.speed;
            fadeW = entry.fadeDurationSnap > 0.f
                    ? glm::clamp(entry.fadeElapsed / entry.fadeDurationSnap, 0.f, 1.f) : 1.f;
            fromT = entry.fadeFromTime + entry.fadeElapsed;
            if (fromClip->duration > 0.f) fromT = std::fmod(fromT, fromClip->duration);
            if (fadeW >= 1.f) { fromClip = nullptr; entry.fadeFromClip = nullptr; }
        }

        const auto* ovr = registry.try_get<BonePoseOverrideComponent>(entity);
        EvaluateClipPose(entry, clip, animComp.time, fromClip, fromT, fadeW,
                         (ovr && !ovr->bones.empty()) ? ovr : nullptr);
        auto& skinMats = entry.workSkinMats;

        // Crossfade keeps the pose continuous across a clip switch → treat the
        // fade-trigger frame as a normal frame (swap, no prev reseed) so
        // velocity stays valid; only a true hard cut reseeds prev = curr.
        const bool hardCut = clipSwap && !enteredFadeThisFrame;

        // ── #83 P1 Static Pose Skip ───────────────────────────────────────────
        // Byte-identical palette + prev already synced → nothing to swap or
        // upload (paused clips / static poses cost zero bandwidth). The first
        // identical frame still swaps+uploads once so prev catches up to curr
        // (velocity settles to zero) before skipping begins.
        const uint64_t poseHash = HashBytes(
            skinMats.data(), static_cast<size_t>(nBones) * sizeof(glm::mat3x4));
        m_uploadStats.evaluated++;
        const bool sameAsUploaded = poseHash == entry.lastUploadedHash;
        if (sameAsUploaded && entry.prevSynced && !firstWrite && !hardCut) {
            m_uploadStats.skipped++;
            meshComp.lastEvalClipId = animComp.clipAsset;
            continue;
        }

        // ── Issue #84: pose double-buffer swap ────────────────────────────────
        // Normal frame → swap (prev ← last frame's curr) then write new curr.
        // First write or hard cut → skip swap; after writing curr, also write
        // it to prev (velocity = 0 that frame). Crossfade is NOT a hard cut.
        if (!firstWrite && !hardCut) {
            std::swap(meshComp.skinMatricesBuffer, meshComp.skinMatricesBufferPrev);
            // Re-bind both desc sets to reflect the swapped handles
            // (UPDATE_AFTER_BIND on all desc sets makes mid-frame writes safe).
            if (meshComp.skinDescSet.IsValid())
                device->WriteDescriptorBuffer(meshComp.skinDescSet, 0, meshComp.skinMatricesBuffer);
            if (meshComp.velocityDescSet.IsValid()) {
                device->WriteDescriptorBuffer(meshComp.velocityDescSet, 0, meshComp.skinMatricesBuffer);
                device->WriteDescriptorBuffer(meshComp.velocityDescSet, 2, meshComp.skinMatricesBufferPrev);
            }
        }

        device->UploadBufferData(meshComp.skinMatricesBuffer,
                                 skinMats.data(),
                                 static_cast<uint64_t>(nBones) * sizeof(glm::mat3x4), 0);
        m_uploadStats.uploaded++;
        m_uploadStats.bytes += static_cast<uint64_t>(nBones) * sizeof(glm::mat3x4);
        if (firstWrite || hardCut) {
            device->UploadBufferData(meshComp.skinMatricesBufferPrev,
                                     skinMats.data(),
                                     static_cast<uint64_t>(nBones) * sizeof(glm::mat3x4), 0);
            m_uploadStats.bytes += static_cast<uint64_t>(nBones) * sizeof(glm::mat3x4);
            meshComp.poseSeeded = true;
        }
        entry.prevSynced       = firstWrite || clipSwap || sameAsUploaded;
        entry.lastUploadedHash = poseHash;
        meshComp.lastEvalClipId = animComp.clipAsset;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// RefreshPose (#83 bone editing — editor immediate hook)
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::RefreshPose(entt::entity entity,
                                   entt::registry& registry,
                                   RHI::IRHIDevice* device) {
    const auto it = m_entries.find(static_cast<uint32_t>(entt::to_integral(entity)));
    if (it == m_entries.end()) return;
    SkinEntry& entry = it->second;

    auto* meshComp = registry.try_get<SkinnedMeshComponent>(entity);
    if (!meshComp || !meshComp->ready || entry.skeleton.empty()) return;

    const auto* ovr    = registry.try_get<BonePoseOverrideComponent>(entity);
    const bool  hasOvr = ovr && !ovr->bones.empty();

    if (entry.cachedClip) {
        // Re-sample the frozen clip time, then let the overrides pin bones.
        const auto* animComp = registry.try_get<AnimatorComponent>(entity);
        const float t = animComp
            ? std::clamp(animComp->time, 0.f, entry.cachedClip->duration) : 0.f;
        const auto nBones = static_cast<uint32_t>(entry.skeleton.size());

        std::fill(entry.workLocalT.begin(), entry.workLocalT.end(), glm::vec3{0.f});
        std::fill(entry.workLocalR.begin(), entry.workLocalR.end(),
                  glm::quat{1.f, 0.f, 0.f, 0.f});
        std::fill(entry.workLocalS.begin(), entry.workLocalS.end(), glm::vec3{1.f});
        for (const auto& ch : entry.cachedClip->channels) {
            if (ch.boneIndex < 0 || ch.boneIndex >= static_cast<int32_t>(nBones)) continue;
            SampleChannel(ch, t, entry.workLocalT[ch.boneIndex],
                          entry.workLocalR[ch.boneIndex], entry.workLocalS[ch.boneIndex]);
        }
        if (hasOvr)
            for (const auto& [bi, trs] : ovr->bones)
                if (bi >= 0 && bi < static_cast<int32_t>(nBones)) {
                    entry.workLocalT[bi] = trs.position;
                    entry.workLocalR[bi] = trs.rotation;
                    entry.workLocalS[bi] = trs.scale;
                }

        for (uint32_t bi = 0; bi < nBones; ++bi) {
            glm::mat4 localMat = glm::mat4_cast(glm::normalize(entry.workLocalR[bi]));
            localMat[0] *= entry.workLocalS[bi].x;
            localMat[1] *= entry.workLocalS[bi].y;
            localMat[2] *= entry.workLocalS[bi].z;
            localMat[3]  = glm::vec4(entry.workLocalT[bi], 1.f);
            const int32_t parent = entry.skeleton[bi].parentIndex;
            entry.workGlobalPose[bi] = (parent >= 0)
                ? (entry.workGlobalPose[parent] * localMat) : localMat;
        }
        for (uint32_t bi = 0; bi < nBones; ++bi)
            entry.workSkinMats[bi] = glm::mat3x4(glm::transpose(
                entry.workGlobalPose[bi] * entry.skeleton[bi].inverseBindMatrix));

        const auto bytes = static_cast<uint64_t>(nBones) * sizeof(glm::mat3x4);
        device->UploadBufferData(meshComp->skinMatricesBuffer,
                                 entry.workSkinMats.data(), bytes, 0);
        device->UploadBufferData(meshComp->skinMatricesBufferPrev,
                                 entry.workSkinMats.data(), bytes, 0);
        meshComp->poseSeeded = true;
        entry.lastUploadedHash = HashBytes(entry.workSkinMats.data(), bytes);
        entry.prevSynced       = true;
    } else {
        EvaluateStaticPose(entry, hasOvr ? ovr : nullptr, *meshComp, device);
    }
    entry.overrideWasActive = hasOvr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Editor crossfade test hook (#83 P2-4b)
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::RequestEditorCrossfade(entt::entity entity, const AssetID& toClip,
                                              float fadeSeconds,
                                              entt::registry& registry,
                                              Resource::ResourceManager& resMgr) {
    const auto it = m_entries.find(static_cast<uint32_t>(entt::to_integral(entity)));
    if (it == m_entries.end()) return;
    SkinEntry& e = it->second;
    auto* animComp = registry.try_get<AnimatorComponent>(entity);
    if (!animComp) return;

    const Resource::CookedAnim* newAnim = resMgr.LoadAnimClip(toClip);
    if (!newAnim) return;

    if (e.cachedClip && fadeSeconds > 0.f) {
        e.fadeFromClip     = e.cachedClip;
        e.fadeFromTime     = animComp->time;
        e.fadeElapsed      = 0.f;
        e.fadeDurationSnap = fadeSeconds;
    }
    e.cachedClip           = &newAnim->clip;
    e.currentClipAsset     = toClip;
    animComp->clipAsset    = toClip;   // keep component in sync (avoids Update re-swap)
    animComp->time         = 0.f;
}

void AnimationSystem::TickEditor(entt::entity entity, float dt,
                                  entt::registry& registry, RHI::IRHIDevice* device) {
    const auto it = m_entries.find(static_cast<uint32_t>(entt::to_integral(entity)));
    if (it == m_entries.end()) return;
    SkinEntry& e = it->second;
    auto* meshComp = registry.try_get<SkinnedMeshComponent>(entity);
    auto* animComp = registry.try_get<AnimatorComponent>(entity);
    if (!meshComp || !meshComp->ready || !animComp ||
        !e.cachedClip || e.skeleton.empty()) return;

    const Resource::AnimClip& clip = *e.cachedClip;
    animComp->time += dt * animComp->speed;
    if (animComp->looping && clip.duration > 0.f)
        animComp->time = std::fmod(animComp->time, clip.duration);
    else
        animComp->time = std::min(animComp->time, clip.duration);

    const Resource::AnimClip* fromClip = e.fadeFromClip;
    float w = 1.f, fromT = 0.f;
    if (fromClip) {
        e.fadeElapsed += dt * animComp->speed;
        w = e.fadeDurationSnap > 0.f
            ? glm::clamp(e.fadeElapsed / e.fadeDurationSnap, 0.f, 1.f) : 1.f;
        fromT = e.fadeFromTime + e.fadeElapsed;
        if (fromClip->duration > 0.f) fromT = std::fmod(fromT, fromClip->duration);
        if (w >= 1.f) { fromClip = nullptr; e.fadeFromClip = nullptr; }
    }

    const auto* ovr = registry.try_get<BonePoseOverrideComponent>(entity);
    EvaluateClipPose(e, clip, animComp->time, fromClip, fromT, w,
                     (ovr && !ovr->bones.empty()) ? ovr : nullptr);

    const auto bytes = static_cast<uint64_t>(e.skeleton.size()) * sizeof(glm::mat3x4);
    device->UploadBufferData(meshComp->skinMatricesBuffer,     e.workSkinMats.data(), bytes, 0);
    device->UploadBufferData(meshComp->skinMatricesBufferPrev, e.workSkinMats.data(), bytes, 0);
    meshComp->poseSeeded    = true;
    e.lastUploadedHash      = HashBytes(e.workSkinMats.data(), bytes);
    e.prevSynced            = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// EvaluateStaticPose (#83 bone editing)
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::EvaluateStaticPose(SkinEntry&                       entry,
                                          const BonePoseOverrideComponent* ovr,
                                          SkinnedMeshComponent&            meshComp,
                                          RHI::IRHIDevice*                 device) {
    const auto nBones = static_cast<uint32_t>(entry.skeleton.size());

    entry.workLocalT = entry.bindLocalT;
    entry.workLocalR = entry.bindLocalR;
    entry.workLocalS = entry.bindLocalS;

    if (ovr)
        for (const auto& [bi, trs] : ovr->bones)
            if (bi >= 0 && bi < static_cast<int32_t>(nBones)) {
                entry.workLocalT[bi] = trs.position;
                entry.workLocalR[bi] = trs.rotation;
                entry.workLocalS[bi] = trs.scale;
            }

    for (uint32_t bi = 0; bi < nBones; ++bi) {
        glm::mat4 localMat = glm::mat4_cast(glm::normalize(entry.workLocalR[bi]));
        localMat[0] *= entry.workLocalS[bi].x;
        localMat[1] *= entry.workLocalS[bi].y;
        localMat[2] *= entry.workLocalS[bi].z;
        localMat[3]  = glm::vec4(entry.workLocalT[bi], 1.f);
        const int32_t parent = entry.skeleton[bi].parentIndex;
        entry.workGlobalPose[bi] = (parent >= 0)
            ? (entry.workGlobalPose[parent] * localMat) : localMat;
    }

    for (uint32_t bi = 0; bi < nBones; ++bi)
        entry.workSkinMats[bi] = glm::mat3x4(glm::transpose(
            entry.workGlobalPose[bi] * entry.skeleton[bi].inverseBindMatrix));

    // Both buffers — a discrete pose edit has no meaningful velocity.
    const auto bytes = static_cast<uint64_t>(nBones) * sizeof(glm::mat3x4);
    device->UploadBufferData(meshComp.skinMatricesBuffer,     entry.workSkinMats.data(), bytes, 0);
    device->UploadBufferData(meshComp.skinMatricesBufferPrev, entry.workSkinMats.data(), bytes, 0);
    meshComp.poseSeeded = true;
    entry.lastUploadedHash = HashBytes(entry.workSkinMats.data(), bytes);
    entry.prevSynced       = true;
    m_uploadStats.evaluated++;
    m_uploadStats.uploaded++;
    m_uploadStats.bytes += bytes * 2;
}

// ─────────────────────────────────────────────────────────────────────────────
// SampleClipLocals / EvaluateClipPose (#83 P2)
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::SampleClipLocals(const Resource::AnimClip& clip, float t,
                                        uint32_t nBones,
                                        std::vector<glm::vec3>& T,
                                        std::vector<glm::quat>& R,
                                        std::vector<glm::vec3>& S) {
    std::fill(T.begin(), T.end(), glm::vec3{0.f, 0.f, 0.f});
    std::fill(R.begin(), R.end(), glm::quat{1.f, 0.f, 0.f, 0.f});
    std::fill(S.begin(), S.end(), glm::vec3{1.f, 1.f, 1.f});
    for (const auto& ch : clip.channels) {
        if (ch.boneIndex < 0 || ch.boneIndex >= static_cast<int32_t>(nBones)) continue;
        SampleChannel(ch, t, T[ch.boneIndex], R[ch.boneIndex], S[ch.boneIndex]);
    }
}

void AnimationSystem::EvaluateClipPose(SkinEntry& entry,
                                        const Resource::AnimClip& main, float mainT,
                                        const Resource::AnimClip* from, float fromT, float w,
                                        const BonePoseOverrideComponent* ovr) {
    const auto nBones = static_cast<uint32_t>(entry.skeleton.size());

    SampleClipLocals(main, mainT, nBones,
                     entry.workLocalT, entry.workLocalR, entry.workLocalS);

    // Crossfade: blend the outgoing clip in at weight (1 - w).
    if (from && w < 0.9999f) {
        SampleClipLocals(*from, fromT, nBones,
                         entry.workLocalT2, entry.workLocalR2, entry.workLocalS2);
        for (uint32_t bi = 0; bi < nBones; ++bi) {
            entry.workLocalT[bi] = glm::mix(entry.workLocalT2[bi], entry.workLocalT[bi], w);
            entry.workLocalS[bi] = glm::mix(entry.workLocalS2[bi], entry.workLocalS[bi], w);
            entry.workLocalR[bi] = glm::normalize(
                glm::slerp(glm::normalize(entry.workLocalR2[bi]),
                           glm::normalize(entry.workLocalR[bi]), w));
        }
    }

    // Bone overrides win (applied after blend — pinned bone stays pinned).
    if (ovr)
        for (const auto& [bi, trs] : ovr->bones)
            if (bi >= 0 && bi < static_cast<int32_t>(nBones)) {
                entry.workLocalT[bi] = trs.position;
                entry.workLocalR[bi] = trs.rotation;
                entry.workLocalS[bi] = trs.scale;
            }

    for (uint32_t bi = 0; bi < nBones; ++bi) {
        glm::mat4 localMat = glm::mat4_cast(glm::normalize(entry.workLocalR[bi]));
        localMat[0] *= entry.workLocalS[bi].x;
        localMat[1] *= entry.workLocalS[bi].y;
        localMat[2] *= entry.workLocalS[bi].z;
        localMat[3]  = glm::vec4(entry.workLocalT[bi], 1.f);
        const int32_t parent = entry.skeleton[bi].parentIndex;
        entry.workGlobalPose[bi] = (parent >= 0)
            ? (entry.workGlobalPose[parent] * localMat) : localMat;
    }
    for (uint32_t bi = 0; bi < nBones; ++bi)
        entry.workSkinMats[bi] = glm::mat3x4(glm::transpose(
            entry.workGlobalPose[bi] * entry.skeleton[bi].inverseBindMatrix));
}

// ─────────────────────────────────────────────────────────────────────────────
// SampleChannel
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::SampleChannel(const Resource::AnimChannel& ch, float t,
                                     glm::vec3& out_T,
                                     glm::quat& out_R,
                                     glm::vec3& out_S) {
    if (ch.times.empty()) return;

    auto setVal = [&](const glm::vec4& v) {
        switch (ch.target) {
            case Resource::AnimChannel::Target::Translation: out_T = {v.x,v.y,v.z}; break;
            case Resource::AnimChannel::Target::Scale:       out_S = {v.x,v.y,v.z}; break;
            case Resource::AnimChannel::Target::Rotation:
                out_R = glm::normalize(glm::quat{v.w, v.x, v.y, v.z}); break;
        }
    };

    if (t <= ch.times.front()) { setVal(ch.values.front()); return; }
    if (t >= ch.times.back())  { setVal(ch.values.back());  return; }

    const auto   upper = std::upper_bound(ch.times.begin(), ch.times.end(), t);
    const size_t i1    = static_cast<size_t>(upper - ch.times.begin());
    const size_t i0    = i1 - 1;
    const float  a     = (t - ch.times[i0]) / (ch.times[i1] - ch.times[i0]);

    if (ch.interp == Resource::AnimChannel::Interp::Step) {
        setVal(ch.values[i0]); return;
    }

    const auto& v0 = ch.values[i0];
    const auto& v1 = ch.values[i1];

    switch (ch.target) {
        case Resource::AnimChannel::Target::Translation:
            out_T = glm::mix(glm::vec3{v0.x,v0.y,v0.z}, glm::vec3{v1.x,v1.y,v1.z}, a);
            break;
        case Resource::AnimChannel::Target::Scale:
            out_S = glm::mix(glm::vec3{v0.x,v0.y,v0.z}, glm::vec3{v1.x,v1.y,v1.z}, a);
            break;
        case Resource::AnimChannel::Target::Rotation: {
            const glm::quat q0 = glm::normalize(glm::quat{v0.w, v0.x, v0.y, v0.z});
            const glm::quat q1 = glm::normalize(glm::quat{v1.w, v1.x, v1.y, v1.z});
            out_R = glm::normalize(glm::slerp(q0, q1, a));
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bone pose accessors
// ─────────────────────────────────────────────────────────────────────────────

std::span<const glm::mat4> AnimationSystem::GetBoneGlobalPoses(entt::entity entity) const {
    const auto it = m_entries.find(static_cast<uint32_t>(entt::to_integral(entity)));
    if (it == m_entries.end()) return {};
    return it->second.workGlobalPose;
}

std::span<const Resource::BoneInfo> AnimationSystem::GetBoneSkeleton(entt::entity entity) const {
    const auto it = m_entries.find(static_cast<uint32_t>(entt::to_integral(entity)));
    if (it == m_entries.end()) return {};
    return it->second.skeleton;
}

} // namespace StellarAlia
