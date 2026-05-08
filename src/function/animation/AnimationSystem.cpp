#include "function/animation/AnimationSystem.hpp"

#include "function/scene/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "resource/cook/CookedAnim.hpp"
#include "resource/cook/CookedSkeleton.hpp"
#include "core/logs/Log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstring>
#include <algorithm>

namespace StellarAlia {

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

    // Skeleton UUID is always derived from the mesh asset (skin index 0).
    const AssetID skelID = Resource::DeriveSkinID(meshComp->meshAsset, 0);

    // ── Load skeleton (required) ──────────────────────────────────────────────
    const Resource::CookedSkeleton* skel = resMgr.LoadSkeleton(skelID);
    if (!skel) {
        SA_LOG_ERROR("AnimationSystem: skeleton {} not found (derived from mesh {})",
                     skelID.ToString(), meshComp->meshAsset.ToString());
        return;
    }

    // ── Load animation clip (optional — bind pose used when absent) ───────────
    const auto* animComp = registry.try_get<AnimatorComponent>(entity);
    const Resource::CookedAnim* cookedAnim = nullptr;
    if (animComp && animComp->clipAsset.IsValid()) {
        cookedAnim = resMgr.LoadAnimClip(animComp->clipAsset);
        if (!cookedAnim)
            SA_LOG_WARN("AnimationSystem: anim clip {} not found — using bind pose",
                        animComp->clipAsset.ToString());
    }

    // ── Load mesh data (CPU side) for rest-pose vertices + skin data ──────────
    const Resource::CookedMesh* mesh = resMgr.LoadMeshData(meshComp->meshAsset);
    if (!mesh || !mesh->IsSkinned()) {
        SA_LOG_ERROR("AnimationSystem: mesh {} not found or not skinned",
                     meshComp->meshAsset.ToString());
        return;
    }

    const uint32_t vertCount = mesh->vertexCount;

    // ── Build SkinEntry ───────────────────────────────────────────────────────
    SkinEntry entry;
    entry.entity     = entity;
    entry.skeleton   = skel->bones;
    entry.cachedClip = cookedAnim ? &cookedAnim->clip : nullptr;
    entry.restPos.resize(vertCount);
    entry.restNorm.resize(vertCount);
    entry.restTang.resize(vertCount);
    entry.restUV.resize(vertCount);
    entry.skinData.resize(vertCount);
    entry.deformedVerts.resize(vertCount * 48u);

    const uint8_t* vbPtr = mesh->vertexData.data();
    for (uint32_t vi = 0; vi < vertCount; ++vi) {
        const uint8_t* v = vbPtr + vi * 48u;
        memcpy(&entry.restPos[vi],  v,      12);
        memcpy(&entry.restNorm[vi], v + 12, 12);
        memcpy(&entry.restTang[vi], v + 24, 16);
        memcpy(&entry.restUV[vi],   v + 40,  8);
    }
    memcpy(entry.skinData.data(), mesh->skinData.data(),
           vertCount * sizeof(Resource::SkinVertex));

    // ── Populate SkinnedMeshComponent draw metadata ───────────────────────────
    meshComp->vertexCount = vertCount;
    meshComp->subMeshes.clear();
    meshComp->subMeshes.reserve(mesh->subMeshes.size());
    for (const auto& sm : mesh->subMeshes) {
        SkinnedSubMeshInfo info;
        info.firstIndex      = sm.indexOffset;
        info.indexCount      = sm.indexCount;
        info.vertexOffset    = static_cast<int32_t>(sm.vertexOffset);
        info.materialAssetID = sm.defaultMaterialID;
        meshComp->subMeshes.push_back(info);
    }

    // ── Allocate CPU-visible dynamic vertex buffer ────────────────────────────
    RHI::RHIBufferDesc vbDesc{};
    vbDesc.size       = static_cast<uint64_t>(vertCount) * 48u;
    vbDesc.usage      = RHI::RHIBufferUsage::Vertex;
    vbDesc.cpuVisible = true;
    vbDesc.debugName  = "SkinnedVB";
    meshComp->dynVertexBuffer = device->CreateBuffer(vbDesc);
    if (!meshComp->dynVertexBuffer.IsValid()) {
        SA_LOG_ERROR("AnimationSystem: failed to allocate dynVertexBuffer");
        return;
    }

    // ── Reuse static index buffer from GPU mesh ───────────────────────────────
    const Resource::GPUMesh* gpuMesh = resMgr.LoadMesh(meshComp->meshAsset);
    if (!gpuMesh) {
        SA_LOG_ERROR("AnimationSystem: failed to load GPU mesh for index buffer");
        return;
    }
    meshComp->indexBuffer = gpuMesh->indexBuffer;

    // ── Upload bind-pose as initial frame content ─────────────────────────────
    const uint32_t boneCount = static_cast<uint32_t>(skel->bones.size());
    std::vector<glm::mat4> identity(boneCount, glm::mat4(1.f));
    SkinVertices(entry, identity);
    device->UploadBufferData(meshComp->dynVertexBuffer,
                             entry.deformedVerts.data(),
                             entry.deformedVerts.size(), 0);

    if (animComp) entry.currentClipAsset = animComp->clipAsset;
    meshComp->ready = true;

    const uint32_t entId = static_cast<uint32_t>(entt::to_integral(entity));
    m_entries[entId] = std::move(entry);

    SA_LOG_INFO("AnimationSystem: prepared entity {} ({} verts, {} bones, clip='{}')",
                entId, vertCount, boneCount,
                cookedAnim ? cookedAnim->clip.name : "(bind pose)");
}

// ─────────────────────────────────────────────────────────────────────────────
// EvaluateAll — one-shot evaluation at explicit time t (ignores playing flag)
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::EvaluateAll(float t,
                                   entt::registry& registry,
                                   Resource::ResourceManager& resMgr,
                                   RHI::IRHIDevice* device) {
    for (auto& [entId, entry] : m_entries) {
        const auto* meshComp = registry.try_get<SkinnedMeshComponent>(entry.entity);
        if (!meshComp || !meshComp->ready || entry.skeleton.empty()) continue;

        // Hot-swap clip if changed in inspector while in editor mode.
        const auto* animComp = registry.try_get<AnimatorComponent>(entry.entity);
        if (animComp && animComp->clipAsset.IsValid() &&
            !(animComp->clipAsset == entry.currentClipAsset))
        {
            const Resource::CookedAnim* newAnim = resMgr.LoadAnimClip(animComp->clipAsset);
            if (newAnim) {
                entry.cachedClip       = &newAnim->clip;
                entry.currentClipAsset = animComp->clipAsset;
            } else {
                entry.currentClipAsset = animComp->clipAsset; // don't retry every call
            }
        }

        if (!entry.cachedClip) continue;

        const Resource::AnimClip& clip    = *entry.cachedClip;
        const uint32_t            nBones  = static_cast<uint32_t>(entry.skeleton.size());
        const float               evalT   = std::clamp(t, 0.f, clip.duration);

        std::vector<glm::vec3> localT(nBones, {0.f, 0.f, 0.f});
        std::vector<glm::quat> localR(nBones, glm::quat(1.f, 0.f, 0.f, 0.f));
        std::vector<glm::vec3> localS(nBones, {1.f, 1.f, 1.f});

        for (const auto& ch : clip.channels) {
            if (ch.boneIndex < 0 || ch.boneIndex >= (int32_t)nBones) continue;
            SampleChannel(ch, evalT, localT[ch.boneIndex], localR[ch.boneIndex], localS[ch.boneIndex]);
        }

        std::vector<glm::mat4> globalPose(nBones);
        for (uint32_t bi = 0; bi < nBones; ++bi) {
            const glm::mat4 localMat =
                glm::translate(glm::mat4(1.f), localT[bi]) *
                glm::mat4_cast(glm::normalize(localR[bi])) *
                glm::scale(glm::mat4(1.f), localS[bi]);
            const int32_t parent = entry.skeleton[bi].parentIndex;
            globalPose[bi] = (parent >= 0) ? (globalPose[parent] * localMat) : localMat;
        }

        std::vector<glm::mat4> skinMats(nBones);
        for (uint32_t bi = 0; bi < nBones; ++bi)
            skinMats[bi] = globalPose[bi] * entry.skeleton[bi].inverseBindMatrix;

        entry.lastGlobalPose = globalPose;

        SkinVertices(entry, skinMats);
        device->UploadBufferData(meshComp->dynVertexBuffer,
                                 entry.deformedVerts.data(),
                                 entry.deformedVerts.size(), 0);
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
                entry.currentClipAsset = animComp.clipAsset; // don't retry every frame
            }
        }

        if (!animComp.playing || !entry.cachedClip || entry.skeleton.empty()) continue;

        const Resource::AnimClip& clip = *entry.cachedClip;

        // ── Advance time ──────────────────────────────────────────────────────
        animComp.time += dt * animComp.speed;
        if (animComp.looping && clip.duration > 0.f)
            animComp.time = std::fmod(animComp.time, clip.duration);
        else
            animComp.time = std::min(animComp.time, clip.duration);

        const float    t      = animComp.time;
        const uint32_t nBones = static_cast<uint32_t>(entry.skeleton.size());

        // ── Sample keyframes → local TRS per bone ─────────────────────────────
        std::vector<glm::vec3> localT(nBones, {0.f,0.f,0.f});
        std::vector<glm::quat> localR(nBones, glm::quat(1.f,0.f,0.f,0.f));
        std::vector<glm::vec3> localS(nBones, {1.f,1.f,1.f});

        for (const auto& ch : clip.channels) {
            if (ch.boneIndex < 0 || ch.boneIndex >= (int32_t)nBones) continue;
            SampleChannel(ch, t,
                          localT[ch.boneIndex],
                          localR[ch.boneIndex],
                          localS[ch.boneIndex]);
        }

        // ── FK: global pose per bone ──────────────────────────────────────────
        std::vector<glm::mat4> globalPose(nBones);
        for (uint32_t bi = 0; bi < nBones; ++bi) {
            const glm::mat4 localMat =
                glm::translate(glm::mat4(1.f), localT[bi]) *
                glm::mat4_cast(glm::normalize(localR[bi])) *
                glm::scale(glm::mat4(1.f), localS[bi]);

            const int32_t parent = entry.skeleton[bi].parentIndex;
            globalPose[bi] = (parent >= 0)
                           ? (globalPose[parent] * localMat)
                           : localMat;
        }

        // ── Skin matrices = globalPose × inverseBindMatrix ────────────────────
        std::vector<glm::mat4> skinMats(nBones);
        for (uint32_t bi = 0; bi < nBones; ++bi)
            skinMats[bi] = globalPose[bi] * entry.skeleton[bi].inverseBindMatrix;

        entry.lastGlobalPose = globalPose;

        // ── Deform and upload ─────────────────────────────────────────────────
        SkinVertices(entry, skinMats);
        device->UploadBufferData(meshComp.dynVertexBuffer,
                                 entry.deformedVerts.data(),
                                 entry.deformedVerts.size(), 0);
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
// SkinVertices
// ─────────────────────────────────────────────────────────────────────────────
void AnimationSystem::SkinVertices(SkinEntry& e,
                                    const std::vector<glm::mat4>& skinMats) {
    const uint32_t vertCount = static_cast<uint32_t>(e.restPos.size());
    const uint32_t boneCount = static_cast<uint32_t>(skinMats.size());
    uint8_t* out = e.deformedVerts.data();

    for (uint32_t vi = 0; vi < vertCount; ++vi) {
        const Resource::SkinVertex& sv = e.skinData[vi];
        const glm::vec3& rp = e.restPos[vi];
        const glm::vec3& rn = e.restNorm[vi];
        const glm::vec4& rt = e.restTang[vi];

        glm::vec3 dPos  = {0.f,0.f,0.f};
        glm::vec3 dNorm = {0.f,0.f,0.f};
        glm::vec3 dTang = {0.f,0.f,0.f};

        for (int j = 0; j < 4; ++j) {
            const float    w  = sv.weights[j];
            const uint32_t bi = sv.joints[j];
            if (w < 1e-6f || bi >= boneCount) continue;
            const glm::mat4& sm = skinMats[bi];
            dPos  += w * glm::vec3(sm * glm::vec4(rp, 1.f));
            const glm::mat3 m3(sm);
            dNorm += w * (m3 * rn);
            dTang += w * (m3 * glm::vec3(rt));
        }

        const float dNormLen = glm::length(dNorm);
        const float dTangLen = glm::length(dTang);
        if (dNormLen > 1e-6f) dNorm /= dNormLen;
        if (dTangLen > 1e-6f) dTang /= dTangLen;

        uint8_t* v = out + vi * 48u;
        memcpy(v,      &dPos,         12);
        memcpy(v + 12, &dNorm,        12);
        memcpy(v + 24, &dTang,        12);
        memcpy(v + 36, &rt.w,          4);  // handedness unchanged
        memcpy(v + 40, &e.restUV[vi],  8);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bone pose accessors
// ─────────────────────────────────────────────────────────────────────────────

std::span<const glm::mat4> AnimationSystem::GetBoneGlobalPoses(entt::entity entity) const {
    const auto it = m_entries.find(static_cast<uint32_t>(entt::to_integral(entity)));
    if (it == m_entries.end()) return {};
    return it->second.lastGlobalPose;
}

std::span<const Resource::BoneInfo> AnimationSystem::GetBoneSkeleton(entt::entity entity) const {
    const auto it = m_entries.find(static_cast<uint32_t>(entt::to_integral(entity)));
    if (it == m_entries.end()) return {};
    return it->second.skeleton;
}

} // namespace StellarAlia
