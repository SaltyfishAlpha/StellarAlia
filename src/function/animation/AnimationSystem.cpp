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

    const AssetID skelID = Resource::DeriveSkinID(meshComp->meshAsset, 0);

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
        const uint64_t matBufSize = static_cast<uint64_t>(boneCount) * sizeof(glm::mat4);
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
        std::vector<glm::mat4> identity(boneCount, glm::mat4(1.f));
        device->UploadBufferData(meshComp->skinMatricesBuffer,
                                 identity.data(),
                                 static_cast<uint64_t>(boneCount) * sizeof(glm::mat4), 0);
        device->UploadBufferData(meshComp->skinMatricesBufferPrev,
                                 identity.data(),
                                 static_cast<uint64_t>(boneCount) * sizeof(glm::mat4), 0);
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

        if (!entry.cachedClip) continue;

        const Resource::AnimClip& clip   = *entry.cachedClip;
        const uint32_t            nBones = static_cast<uint32_t>(entry.skeleton.size());
        const float               evalT  = std::clamp(t, 0.f, clip.duration);

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
            skinMats[bi] = globalPose[bi] * entry.skeleton[bi].inverseBindMatrix;

        device->UploadBufferData(meshComp->skinMatricesBuffer,
                                 skinMats.data(),
                                 static_cast<uint64_t>(nBones) * sizeof(glm::mat4), 0);
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

        // ── Clip hot-swap ─────────────────────────────────────────────────────
        if (animComp.clipAsset.IsValid() &&
            !(animComp.clipAsset == entry.currentClipAsset))
        {
            const Resource::CookedAnim* newAnim = resMgr.LoadAnimClip(animComp.clipAsset);
            if (newAnim) {
                entry.cachedClip       = &newAnim->clip;
                entry.currentClipAsset = animComp.clipAsset;
                animComp.time          = 0.f;
                SA_LOG_INFO("AnimationSystem: swapped clip → '{}'", newAnim->clip.name);
            } else {
                SA_LOG_WARN("AnimationSystem: clip {} not found — keeping previous",
                            animComp.clipAsset.ToString());
                entry.currentClipAsset = animComp.clipAsset;
            }
        }

        if (!animComp.playing || !entry.cachedClip || entry.skeleton.empty()) continue;

        const Resource::AnimClip& clip = *entry.cachedClip;

        animComp.time += dt * animComp.speed;
        if (animComp.looping && clip.duration > 0.f)
            animComp.time = std::fmod(animComp.time, clip.duration);
        else
            animComp.time = std::min(animComp.time, clip.duration);

        const float    t      = animComp.time;
        const uint32_t nBones = static_cast<uint32_t>(entry.skeleton.size());

        // ── Issue #84: pose double-buffer swap ────────────────────────────────
        // Normal frame → swap (prev ← last frame's curr) then write new curr.
        // First write or clip swap → skip swap; after writing curr, also write
        // it to prev (velocity = 0 that frame).
        const bool firstWrite = !meshComp.poseSeeded;
        const bool clipSwap   = !(animComp.clipAsset == meshComp.lastEvalClipId);
        if (!firstWrite && !clipSwap) {
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
            SampleChannel(ch, t,
                          localT[ch.boneIndex],
                          localR[ch.boneIndex],
                          localS[ch.boneIndex]);
        }

        for (uint32_t bi = 0; bi < nBones; ++bi) {
            glm::mat4 localMat = glm::mat4_cast(glm::normalize(localR[bi]));
            localMat[0] *= localS[bi].x;
            localMat[1] *= localS[bi].y;
            localMat[2] *= localS[bi].z;
            localMat[3]  = glm::vec4(localT[bi], 1.f);
            const int32_t parent = entry.skeleton[bi].parentIndex;
            globalPose[bi] = (parent >= 0)
                           ? (globalPose[parent] * localMat)
                           : localMat;
        }

        for (uint32_t bi = 0; bi < nBones; ++bi)
            skinMats[bi] = globalPose[bi] * entry.skeleton[bi].inverseBindMatrix;

        device->UploadBufferData(meshComp.skinMatricesBuffer,
                                 skinMats.data(),
                                 static_cast<uint64_t>(nBones) * sizeof(glm::mat4), 0);
        if (firstWrite || clipSwap) {
            device->UploadBufferData(meshComp.skinMatricesBufferPrev,
                                     skinMats.data(),
                                     static_cast<uint64_t>(nBones) * sizeof(glm::mat4), 0);
            meshComp.poseSeeded = true;
        }
        meshComp.lastEvalClipId = animComp.clipAsset;
    }
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
