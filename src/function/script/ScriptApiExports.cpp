#include "function/script/ScriptApiExports.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "function/input/InputSystem.hpp"
#include "function/debug/DebugDraw.hpp"
#include "function/physics/PhysicsSystem.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "core/logs/Log.hpp"

#include <spdlog/spdlog.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace StellarAlia {

static ScriptApiContext g_ctx;

void SA_Script_SetContext(const ScriptApiContext& ctx) {
    g_ctx = ctx;
}

void SA_Script_SetTime(float dt, float totalTime) {
    g_ctx.dt        = dt;
    g_ctx.totalTime = totalTime;
}

} // namespace StellarAlia

using namespace StellarAlia;

extern "C" {

// ── Entity — transform (local, parent-relative) ───────────────────────────────

void SA_Entity_GetLocalPosition(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 0.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e))
        { *x = t->position.x; *y = t->position.y; *z = t->position.z; }
}

void SA_Entity_SetLocalPosition(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        t->position = { x, y, z };
        g_ctx.scene->MarkDirty(e);
    }
}

void SA_Entity_GetLocalRotationEuler(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 0.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        glm::vec3 euler = glm::degrees(glm::eulerAngles(t->rotation));
        *x = euler.x; *y = euler.y; *z = euler.z;
    }
}

void SA_Entity_SetLocalRotationEuler(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        t->rotation = glm::quat(glm::radians(glm::vec3(x, y, z)));
        g_ctx.scene->MarkDirty(e);
    }
}

void SA_Entity_GetLocalRotationQuat(uint64_t id, float* w, float* x, float* y, float* z) {
    *w = 1.f; *x = *y = *z = 0.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e))
        { *w = t->rotation.w; *x = t->rotation.x; *y = t->rotation.y; *z = t->rotation.z; }
}

void SA_Entity_SetLocalRotationQuat(uint64_t id, float w, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        t->rotation = glm::normalize(glm::quat(w, x, y, z));
        g_ctx.scene->MarkDirty(e);
    }
}

void SA_Entity_GetLocalScale(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 1.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e))
        { *x = t->scale.x; *y = t->scale.y; *z = t->scale.z; }
}

void SA_Entity_SetLocalScale(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        t->scale = { x, y, z };
        g_ctx.scene->MarkDirty(e);
    }
}

// ── Entity — transform (world-space) ──────────────────────────────────────────
//
// Scripts run before the per-frame Scene::UpdateTransforms, so we lazily refresh
// the relevant subtree via Scene::EnsureWorldUpToDate when world data is read.
// Writers refresh the parent (so the inverse is fresh), convert world→local,
// then MarkDirty self so the next read recomputes.

namespace {

inline glm::mat3 ExtractRotationScalePart(const glm::mat4& m) {
    return glm::mat3(m);
}

// Extracts the unit-rotation quaternion from a TRS matrix, removing the
// effect of any (possibly non-uniform) scale on the basis vectors. Pure
// rotation if the matrix decomposes cleanly; otherwise an approximation
// (parent non-uniform scale is a Unity-style accepted limitation).
inline glm::quat ExtractRotation(const glm::mat4& m) {
    glm::mat3 r = ExtractRotationScalePart(m);
    const float sx = glm::length(r[0]);
    const float sy = glm::length(r[1]);
    const float sz = glm::length(r[2]);
    if (sx > 1e-6f) r[0] /= sx;
    if (sy > 1e-6f) r[1] /= sy;
    if (sz > 1e-6f) r[2] /= sz;
    return glm::quat_cast(r);
}

inline glm::vec3 ExtractLossyScale(const glm::mat4& m) {
    return { glm::length(glm::vec3(m[0])),
             glm::length(glm::vec3(m[1])),
             glm::length(glm::vec3(m[2])) };
}

} // anon

void SA_Entity_GetWorldPosition(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 0.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    g_ctx.scene->EnsureWorldUpToDate(e);
    if (auto* w = g_ctx.scene->Registry().try_get<WorldTransformComponent>(e)) {
        *x = w->matrix[3][0]; *y = w->matrix[3][1]; *z = w->matrix[3][2];
    }
}

void SA_Entity_SetWorldPosition(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    auto& reg = g_ctx.scene->Registry();
    if (!reg.valid(e)) return;
    auto* t = reg.try_get<TransformComponent>(e);
    if (!t) return;

    glm::mat4 parentInv(1.f);
    if (auto* h = reg.try_get<HierarchyComponent>(e); h && h->parent != entt::null) {
        g_ctx.scene->EnsureWorldUpToDate(h->parent);
        if (auto* pw = reg.try_get<WorldTransformComponent>(h->parent))
            parentInv = glm::inverse(pw->matrix);
    }
    const glm::vec4 localPos = parentInv * glm::vec4(x, y, z, 1.f);
    t->position = glm::vec3(localPos);
    g_ctx.scene->MarkDirty(e);
}

void SA_Entity_GetWorldRotationQuat(uint64_t id, float* w, float* x, float* y, float* z) {
    *w = 1.f; *x = *y = *z = 0.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    g_ctx.scene->EnsureWorldUpToDate(e);
    if (auto* wt = g_ctx.scene->Registry().try_get<WorldTransformComponent>(e)) {
        glm::quat q = ExtractRotation(wt->matrix);
        *w = q.w; *x = q.x; *y = q.y; *z = q.z;
    }
}

void SA_Entity_SetWorldRotationQuat(uint64_t id, float w, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    auto& reg = g_ctx.scene->Registry();
    if (!reg.valid(e)) return;
    auto* t = reg.try_get<TransformComponent>(e);
    if (!t) return;

    glm::quat worldQ = glm::normalize(glm::quat(w, x, y, z));
    glm::quat parentInvQ(1.f, 0.f, 0.f, 0.f);
    if (auto* h = reg.try_get<HierarchyComponent>(e); h && h->parent != entt::null) {
        g_ctx.scene->EnsureWorldUpToDate(h->parent);
        if (auto* pw = reg.try_get<WorldTransformComponent>(h->parent))
            parentInvQ = glm::inverse(ExtractRotation(pw->matrix));
    }
    t->rotation = glm::normalize(parentInvQ * worldQ);
    g_ctx.scene->MarkDirty(e);
}

void SA_Entity_GetLossyWorldScale(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 1.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    g_ctx.scene->EnsureWorldUpToDate(e);
    if (auto* wt = g_ctx.scene->Registry().try_get<WorldTransformComponent>(e)) {
        glm::vec3 s = ExtractLossyScale(wt->matrix);
        *x = s.x; *y = s.y; *z = s.z;
    }
}

void SA_Entity_GetWorldMatrix(uint64_t id, float* out16) {
    // Column-major glm layout (matches System.Numerics.Matrix4x4 row-major
    // when used with Vector3.Transform — the conversion handles transposition).
    for (int i = 0; i < 16; ++i) out16[i] = 0.f;
    out16[0] = out16[5] = out16[10] = out16[15] = 1.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    g_ctx.scene->EnsureWorldUpToDate(e);
    if (auto* wt = g_ctx.scene->Registry().try_get<WorldTransformComponent>(e)) {
        const float* src = glm::value_ptr(wt->matrix);
        for (int i = 0; i < 16; ++i) out16[i] = src[i];
    }
}

// ── Entity — identity ─────────────────────────────────────────────────────────

void SA_Entity_GetName(uint64_t id, char* buf, int32_t bufLen) {
    if (!g_ctx.scene || bufLen <= 0) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) { buf[0] = '\0'; return; }
    if (auto* tag = g_ctx.scene->Registry().try_get<TagComponent>(e)) {
        std::strncpy(buf, tag->name.c_str(), static_cast<size_t>(bufLen - 1));
        buf[bufLen - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
}

int32_t SA_Entity_FindByName(const char* name, uint64_t* outId) {
    if (!g_ctx.scene || !name) return 0;
    auto view = g_ctx.scene->Registry().view<TagComponent>();
    for (auto e : view) {
        if (view.get<TagComponent>(e).name == name) {
            *outId = static_cast<uint64_t>(e);
            return 1;
        }
    }
    return 0;
}

int32_t SA_Entity_FindChild(uint64_t id, const char* childName, uint64_t* outId) {
    if (!g_ctx.scene || !childName) return 0;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 0;
    auto* hier = g_ctx.scene->Registry().try_get<HierarchyComponent>(e);
    if (!hier) return 0;
    auto& reg = g_ctx.scene->Registry();
    for (auto child : hier->children) {
        if (reg.valid(child)) {
            if (auto* tag = reg.try_get<TagComponent>(child); tag && tag->name == childName) {
                *outId = static_cast<uint64_t>(child);
                return 1;
            }
        }
    }
    return 0;
}

int32_t SA_Entity_IsValid(uint64_t id) {
    if (!g_ctx.scene) return 0;
    return g_ctx.scene->Registry().valid(static_cast<entt::entity>(id)) ? 1 : 0;
}

// ── Entity — lifecycle ────────────────────────────────────────────────────────

void SA_Entity_Destroy(uint64_t id) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (g_ctx.scene->Registry().valid(e))
        g_ctx.scene->DestroyEntity(e);
}

uint64_t SA_Entity_Create() {
    if (!g_ctx.scene) return static_cast<uint64_t>(entt::null);
    return static_cast<uint64_t>(g_ctx.scene->CreateEntity("Entity"));
}

// ── Animator ──────────────────────────────────────────────────────────────────

int32_t SA_Animator_IsPlaying(uint64_t id) {
    if (!g_ctx.scene) return 0;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 0;
    if (auto* a = g_ctx.scene->Registry().try_get<AnimatorComponent>(e))
        return a->playing ? 1 : 0;
    return 0;
}

void SA_Animator_SetPlaying(uint64_t id, int32_t playing) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* a = g_ctx.scene->Registry().try_get<AnimatorComponent>(e))
        a->playing = (playing != 0);
}

float SA_Animator_GetSpeed(uint64_t id) {
    if (!g_ctx.scene) return 1.f;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 1.f;
    if (auto* a = g_ctx.scene->Registry().try_get<AnimatorComponent>(e))
        return a->speed;
    return 1.f;
}

void SA_Animator_SetSpeed(uint64_t id, float speed) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* a = g_ctx.scene->Registry().try_get<AnimatorComponent>(e))
        a->speed = speed;
}

// #83 P2-5: only set clipAsset + the one-shot fade selector here; leave `time`
// untouched so AnimationSystem::Update can capture the outgoing clip's time as
// fadeFromTime. pendingFadeOverride: 0 = hard cut, >0 = fade seconds.
void SA_Animator_SetClip(uint64_t id, const char* uuid) {
    if (!g_ctx.scene || !uuid) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* a = g_ctx.scene->Registry().try_get<AnimatorComponent>(e)) {
        a->clipAsset           = AssetID::FromString(uuid);
        a->pendingFadeOverride = 0.f;
    }
}

void SA_Animator_CrossfadeTo(uint64_t id, const char* uuid, float fade) {
    if (!g_ctx.scene || !uuid) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* a = g_ctx.scene->Registry().try_get<AnimatorComponent>(e)) {
        a->clipAsset           = AssetID::FromString(uuid);
        a->pendingFadeOverride = fade < 0.f ? 0.f : fade;
    }
}

// ── RigidBody ─────────────────────────────────────────────────────────────────

void SA_RigidBody_GetLinearVelocity(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 0.f;
    if (!g_ctx.scene || !g_ctx.physics) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    auto* rb = g_ctx.scene->Registry().try_get<RigidBodyComponent>(e);
    if (!rb || rb->bodyId == ~0u) return;
    glm::vec3 v = g_ctx.physics->GetLinearVelocity(rb->bodyId);
    *x = v.x; *y = v.y; *z = v.z;
}

void SA_RigidBody_SetLinearVelocity(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene || !g_ctx.physics) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    auto* rb = g_ctx.scene->Registry().try_get<RigidBodyComponent>(e);
    if (rb && rb->bodyId != ~0u) g_ctx.physics->SetLinearVelocity(rb->bodyId, { x, y, z });
}

void SA_RigidBody_GetAngularVelocity(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 0.f;
    if (!g_ctx.scene || !g_ctx.physics) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    auto* rb = g_ctx.scene->Registry().try_get<RigidBodyComponent>(e);
    if (!rb || rb->bodyId == ~0u) return;
    glm::vec3 v = g_ctx.physics->GetAngularVelocity(rb->bodyId);
    *x = v.x; *y = v.y; *z = v.z;
}

void SA_RigidBody_SetAngularVelocity(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene || !g_ctx.physics) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    auto* rb = g_ctx.scene->Registry().try_get<RigidBodyComponent>(e);
    if (rb && rb->bodyId != ~0u) g_ctx.physics->SetAngularVelocity(rb->bodyId, { x, y, z });
}

void SA_RigidBody_AddForce(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene || !g_ctx.physics) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    auto* rb = g_ctx.scene->Registry().try_get<RigidBodyComponent>(e);
    if (rb && rb->bodyId != ~0u) g_ctx.physics->AddForce(rb->bodyId, { x, y, z });
}

void SA_RigidBody_AddImpulse(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene || !g_ctx.physics) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    auto* rb = g_ctx.scene->Registry().try_get<RigidBodyComponent>(e);
    if (rb && rb->bodyId != ~0u) g_ctx.physics->AddImpulse(rb->bodyId, { x, y, z });
}

// ── PointLight ────────────────────────────────────────────────────────────────

void SA_PointLight_GetColor(uint64_t id, float* r, float* g, float* b) {
    *r = *g = *b = 1.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* l = g_ctx.scene->Registry().try_get<PointLightComponent>(e))
        { *r = l->color.r; *g = l->color.g; *b = l->color.b; }
}

void SA_PointLight_SetColor(uint64_t id, float r, float g, float b) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* l = g_ctx.scene->Registry().try_get<PointLightComponent>(e))
        l->color = { r, g, b };
}

float SA_PointLight_GetIntensity(uint64_t id) {
    if (!g_ctx.scene) return 1.f;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 1.f;
    if (auto* l = g_ctx.scene->Registry().try_get<PointLightComponent>(e)) return l->intensity;
    return 1.f;
}

void SA_PointLight_SetIntensity(uint64_t id, float intensity) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* l = g_ctx.scene->Registry().try_get<PointLightComponent>(e)) l->intensity = intensity;
}

float SA_PointLight_GetRange(uint64_t id) {
    if (!g_ctx.scene) return 10.f;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 10.f;
    if (auto* l = g_ctx.scene->Registry().try_get<PointLightComponent>(e)) return l->range;
    return 10.f;
}

void SA_PointLight_SetRange(uint64_t id, float range) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* l = g_ctx.scene->Registry().try_get<PointLightComponent>(e)) l->range = range;
}

// ── Physics — Raycast ─────────────────────────────────────────────────────────

int32_t SA_Physics_Raycast(float ox, float oy, float oz,
                           float dx, float dy, float dz, float maxDist,
                           float* hitX, float* hitY, float* hitZ,
                           float* nrmX, float* nrmY, float* nrmZ,
                           uint64_t* hitEntity)
{
    *hitX = *hitY = *hitZ = 0.f;
    *nrmX = *nrmY = *nrmZ = 0.f;
    *hitEntity = ~uint64_t(0);  // ulong.MaxValue sentinel — mirrors Physics.cs check
    if (!g_ctx.scene || !g_ctx.physics) return 0;

    glm::vec3 hitPos{}, hitNormal{};
    entt::entity ent = entt::null;
    bool hit = g_ctx.physics->Raycast(
        { ox, oy, oz }, { dx, dy, dz }, maxDist,
        hitPos, hitNormal, ent, g_ctx.scene->Registry());
    if (!hit) return 0;

    *hitX = hitPos.x;  *hitY = hitPos.y;  *hitZ = hitPos.z;
    *nrmX = hitNormal.x; *nrmY = hitNormal.y; *nrmZ = hitNormal.z;
    *hitEntity = static_cast<uint64_t>(ent);
    return 1;
}

// ── Input ─────────────────────────────────────────────────────────────────────

float SA_Input_GetKey(const char* devicePath) {
    if (!g_ctx.input || !devicePath) return 0.f;
    return g_ctx.input->GetDeviceButton(devicePath);
}

void SA_Input_GetAxis2D(const char* devicePath, float* x, float* y) {
    *x = *y = 0.f;
    if (!g_ctx.input || !devicePath) return;
    glm::vec2 v = g_ctx.input->GetDeviceAxis2D(devicePath);
    *x = v.x; *y = v.y;
}

// ── InputAction (Block 3, Issue #71) ──────────────────────────────────────────

float SA_InputAction_ReadFloat(const char* action) {
    if (!g_ctx.input || !action) return 0.f;
    return g_ctx.input->ReadFloat(action);
}

void SA_InputAction_ReadVec2(const char* action, float* x, float* y) {
    *x = *y = 0.f;
    if (!g_ctx.input || !action) return;
    glm::vec2 v = g_ctx.input->ReadVec2(action);
    *x = v.x; *y = v.y;
}

int32_t SA_InputAction_IsActive(const char* action) {
    if (!g_ctx.input || !action) return 0;
    return g_ctx.input->IsActive(action) ? 1 : 0;
}

int32_t SA_InputAction_WasActivated(const char* action) {
    if (!g_ctx.input || !action) return 0;
    return g_ctx.input->WasActivated(action) ? 1 : 0;
}

int32_t SA_InputAction_WasDeactivated(const char* action) {
    if (!g_ctx.input || !action) return 0;
    return g_ctx.input->WasDeactivated(action) ? 1 : 0;
}

// ── StaticMesh / MeshRenderer (Block 3, Issue #71) ────────────────────────────

static void WriteUuidToBuf(const AssetID& id, char* buf, int32_t bufLen) {
    if (!buf || bufLen <= 0) return;
    const std::string s = id.IsValid() ? id.ToString() : std::string{};
    const size_t n = std::min<size_t>(s.size(), static_cast<size_t>(bufLen - 1));
    if (n) std::memcpy(buf, s.data(), n);
    buf[n] = '\0';
}

void SA_StaticMesh_GetAssetUUID(uint64_t id, char* buf, int32_t bufLen) {
    if (buf && bufLen > 0) buf[0] = '\0';
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* m = g_ctx.scene->Registry().try_get<StaticMeshComponent>(e))
        WriteUuidToBuf(m->meshAsset, buf, bufLen);
}

int32_t SA_MeshRenderer_GetSlotCount(uint64_t id) {
    if (!g_ctx.scene) return 0;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 0;
    if (auto* mr = g_ctx.scene->Registry().try_get<MeshRendererComponent>(e))
        return static_cast<int32_t>(mr->materialSlots.size());
    return 0;
}

void SA_MeshRenderer_GetSlotUUID(uint64_t id, int32_t slot, char* buf, int32_t bufLen) {
    if (buf && bufLen > 0) buf[0] = '\0';
    if (!g_ctx.scene || slot < 0) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    auto* mr = g_ctx.scene->Registry().try_get<MeshRendererComponent>(e);
    if (!mr || static_cast<size_t>(slot) >= mr->materialSlots.size()) return;
    WriteUuidToBuf(mr->materialSlots[slot], buf, bufLen);
}

int32_t SA_MeshRenderer_SetSlotUUID(uint64_t id, int32_t slot, const char* uuid) {
    if (!g_ctx.scene || slot < 0 || !uuid) return 0;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 0;
    auto* mr = g_ctx.scene->Registry().try_get<MeshRendererComponent>(e);
    if (!mr || static_cast<size_t>(slot) >= mr->materialSlots.size())
        return 0;
    mr->materialSlots[slot] = AssetID::FromString(uuid);
    return 1;
}

int32_t SA_MeshRenderer_GetCastShadow(uint64_t id) {
    if (!g_ctx.scene) return 0;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 0;
    if (auto* mr = g_ctx.scene->Registry().try_get<MeshRendererComponent>(e))
        return mr->castShadow ? 1 : 0;
    return 0;
}

void SA_MeshRenderer_SetCastShadow(uint64_t id, int32_t value) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* mr = g_ctx.scene->Registry().try_get<MeshRendererComponent>(e))
        mr->castShadow = (value != 0);
}

int32_t SA_MeshRenderer_GetReceiveShadow(uint64_t id) {
    if (!g_ctx.scene) return 0;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 0;
    if (auto* mr = g_ctx.scene->Registry().try_get<MeshRendererComponent>(e))
        return mr->receiveShadow ? 1 : 0;
    return 0;
}

void SA_MeshRenderer_SetReceiveShadow(uint64_t id, int32_t value) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* mr = g_ctx.scene->Registry().try_get<MeshRendererComponent>(e))
        mr->receiveShadow = (value != 0);
}

// ── MaterialOverride (Block 3, Issue #71) ─────────────────────────────────────

static MaterialOverrideComponent* GetMaterialOverride(uint64_t id) {
    if (!g_ctx.scene) return nullptr;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return nullptr;
    return g_ctx.scene->Registry().try_get<MaterialOverrideComponent>(e);
}

float SA_MaterialOverride_GetFloat(uint64_t id, const char* param) {
    if (!param) return 0.f;
    auto* mo = GetMaterialOverride(id);
    if (!mo) return 0.f;
    auto it = mo->scalars.find(param);
    if (it == mo->scalars.end()) return 0.f;
    if (const float* v = std::get_if<float>(&it->second)) return *v;
    return 0.f;
}

void SA_MaterialOverride_SetFloat(uint64_t id, const char* param, float value) {
    if (!param) return;
    auto* mo = GetMaterialOverride(id);
    if (!mo) return;
    mo->scalars[param] = value;
}

void SA_MaterialOverride_GetVec3(uint64_t id, const char* param, float* x, float* y, float* z) {
    *x = *y = *z = 0.f;
    if (!param) return;
    auto* mo = GetMaterialOverride(id);
    if (!mo) return;
    auto it = mo->scalars.find(param);
    if (it == mo->scalars.end()) return;
    if (const glm::vec3* v = std::get_if<glm::vec3>(&it->second)) {
        *x = v->x; *y = v->y; *z = v->z;
    }
}

void SA_MaterialOverride_SetVec3(uint64_t id, const char* param, float x, float y, float z) {
    if (!param) return;
    auto* mo = GetMaterialOverride(id);
    if (!mo) return;
    mo->scalars[param] = glm::vec3{ x, y, z };
}

void SA_MaterialOverride_GetVec4(uint64_t id, const char* param, float* x, float* y, float* z, float* w) {
    *x = *y = *z = *w = 0.f;
    if (!param) return;
    auto* mo = GetMaterialOverride(id);
    if (!mo) return;
    auto it = mo->scalars.find(param);
    if (it == mo->scalars.end()) return;
    if (const glm::vec4* v = std::get_if<glm::vec4>(&it->second)) {
        *x = v->x; *y = v->y; *z = v->z; *w = v->w;
    }
}

void SA_MaterialOverride_SetVec4(uint64_t id, const char* param, float x, float y, float z, float w) {
    if (!param) return;
    auto* mo = GetMaterialOverride(id);
    if (!mo) return;
    mo->scalars[param] = glm::vec4{ x, y, z, w };
}

// ── RigidBody diagnostics (Block 3 v4) ────────────────────────────────────────

int32_t SA_RigidBody_HasComponent(uint64_t id) {
    if (!g_ctx.scene) return 0;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return 0;
    return g_ctx.scene->Registry().try_get<RigidBodyComponent>(e) ? 1 : 0;
}

int32_t SA_RigidBody_GetType(uint64_t id) {
    if (!g_ctx.scene) return -1;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return -1;
    auto* rb = g_ctx.scene->Registry().try_get<RigidBodyComponent>(e);
    if (!rb) return -1;
    switch (rb->type) {
    case RigidBodyComponent::Type::Static:    return 0;
    case RigidBodyComponent::Type::Kinematic: return 1;
    case RigidBodyComponent::Type::Dynamic:   return 2;
    }
    return -1;
}

// ── InputMap (Block 3 v5, Issue #71 Phase 3a) ─────────────────────────────────

int32_t SA_InputMap_Push(const char* name) {
    if (!g_ctx.input || !name) return 0;
    return g_ctx.input->TryPushMap(name) ? 1 : 0;
}

void SA_InputMap_Pop() {
    if (!g_ctx.input) return;
    g_ctx.input->PopMap();
}

int32_t SA_InputMap_Replace(const char* name) {
    if (!g_ctx.input || !name) return 0;
    return g_ctx.input->TryReplaceMap(name) ? 1 : 0;
}

int32_t SA_InputMap_IsActive(const char* name) {
    if (!g_ctx.input || !name) return 0;
    return g_ctx.input->IsMapInStack(name) ? 1 : 0;
}

void SA_InputMap_GetActive(char* buf, int32_t bufLen) {
    if (!buf || bufLen <= 0) return;
    buf[0] = '\0';
    if (!g_ctx.input) return;
    const std::string_view name = g_ctx.input->GetTopMapName();
    if (name.empty()) return;
    const size_t n = std::min<size_t>(name.size(), static_cast<size_t>(bufLen - 1));
    if (n) std::memcpy(buf, name.data(), n);
    buf[n] = '\0';
}

// ── Debug ─────────────────────────────────────────────────────────────────────

void SA_Debug_DrawLine(float x0, float y0, float z0,
                       float x1, float y1, float z1,
                       float r,  float g,  float b,  float a)
{
    if (!g_ctx.debug) return;
    g_ctx.debug->DrawLine({ x0, y0, z0 }, { x1, y1, z1 }, { r, g, b, a });
}

// ── Logging ───────────────────────────────────────────────────────────────────

// Lazy-init "script" named logger sharing all sinks of the default logger.
// Initialized once; EditorLogCapture is guaranteed to be registered before
// Play mode starts (EditorMode constructs it at startup).
static spdlog::logger& ScriptLogger() {
    static auto s_logger = []() {
        auto& sinks = spdlog::default_logger()->sinks();
        auto l = std::make_shared<spdlog::logger>("script", sinks.begin(), sinks.end());
        l->set_level(spdlog::default_logger()->level());
        return l;
    }();
    return *s_logger;
}

void SA_Log_Info (const char* msg) { ScriptLogger().info (msg); }
void SA_Log_Warn (const char* msg) { ScriptLogger().warn (msg); }
void SA_Log_Error(const char* msg) { ScriptLogger().error(msg); }

// ── Time ──────────────────────────────────────────────────────────────────────

float SA_Time_GetDeltaTime() { return g_ctx.dt; }
float SA_Time_GetTotalTime() { return g_ctx.totalTime; }

// ── PostProcess — screen modifications (Issue #47) ───────────────────────────
// Writes ws.pp.<field> then live-applies to the bound renderer.  When either
// scene or renderer is unbound, setters no-op and getters return field defaults.

namespace {
PostProcessSettings* PpForRead() {
    return g_ctx.scene ? &g_ctx.scene->GetWorldSettings().pp : nullptr;
}
// Returns (pp, ws) for write path; both must be non-null before the caller
// touches `pp`.  Callers apply via g_ctx.renderer->ApplyWorldSettings(*ws, false).
struct PpWriteCtx { PostProcessSettings* pp; WorldSettings* ws; };
PpWriteCtx PpForWrite() {
    if (!g_ctx.scene) return {nullptr, nullptr};
    WorldSettings& ws = g_ctx.scene->GetWorldSettings();
    return { &ws.pp, &ws };
}
void ApplyPp(WorldSettings& ws) {
    if (g_ctx.renderer)
        g_ctx.renderer->ApplyWorldSettings(ws, /*updateIBL=*/false);
}
} // anonymous namespace

int32_t SA_PostProcess_GetVignetteEnabled() {
    auto* pp = PpForRead(); return pp ? (pp->vignetteEnabled ? 1 : 0) : 0;
}
void SA_PostProcess_SetVignetteEnabled(int32_t v) {
    auto c = PpForWrite(); if (!c.pp) return;
    c.pp->vignetteEnabled = (v != 0); ApplyPp(*c.ws);
}
float SA_PostProcess_GetVignetteIntensity() {
    auto* pp = PpForRead(); return pp ? pp->vignetteIntensity : 0.4f;
}
void SA_PostProcess_SetVignetteIntensity(float v) {
    auto c = PpForWrite(); if (!c.pp) return;
    c.pp->vignetteIntensity = v; ApplyPp(*c.ws);
}
float SA_PostProcess_GetVignetteSmoothness() {
    auto* pp = PpForRead(); return pp ? pp->vignetteSmoothness : 0.6f;
}
void SA_PostProcess_SetVignetteSmoothness(float v) {
    auto c = PpForWrite(); if (!c.pp) return;
    c.pp->vignetteSmoothness = v; ApplyPp(*c.ws);
}

int32_t SA_PostProcess_GetCAEnabled() {
    auto* pp = PpForRead(); return pp ? (pp->caEnabled ? 1 : 0) : 0;
}
void SA_PostProcess_SetCAEnabled(int32_t v) {
    auto c = PpForWrite(); if (!c.pp) return;
    c.pp->caEnabled = (v != 0); ApplyPp(*c.ws);
}
float SA_PostProcess_GetCAStrength() {
    auto* pp = PpForRead(); return pp ? pp->caStrength : 0.5f;
}
void SA_PostProcess_SetCAStrength(float v) {
    auto c = PpForWrite(); if (!c.pp) return;
    c.pp->caStrength = v; ApplyPp(*c.ws);
}

int32_t SA_PostProcess_GetFilmGrainEnabled() {
    auto* pp = PpForRead(); return pp ? (pp->filmGrainEnabled ? 1 : 0) : 0;
}
void SA_PostProcess_SetFilmGrainEnabled(int32_t v) {
    auto c = PpForWrite(); if (!c.pp) return;
    c.pp->filmGrainEnabled = (v != 0); ApplyPp(*c.ws);
}
float SA_PostProcess_GetFilmGrainIntensity() {
    auto* pp = PpForRead(); return pp ? pp->filmGrainIntensity : 0.1f;
}
void SA_PostProcess_SetFilmGrainIntensity(float v) {
    auto c = PpForWrite(); if (!c.pp) return;
    c.pp->filmGrainIntensity = v; ApplyPp(*c.ws);
}
float SA_PostProcess_GetFilmGrainSize() {
    auto* pp = PpForRead(); return pp ? pp->filmGrainSize : 1.6f;
}
void SA_PostProcess_SetFilmGrainSize(float v) {
    auto c = PpForWrite(); if (!c.pp) return;
    c.pp->filmGrainSize = v; ApplyPp(*c.ws);
}

} // extern "C"

// ── Function table ────────────────────────────────────────────────────────────

namespace StellarAlia {

ScriptApiFunctionTable SA_Script_BuildFunctionTable() {
    return {
        /* version              */ 8,
        /* Entity — local transform */
        SA_Entity_GetLocalPosition,
        SA_Entity_SetLocalPosition,
        SA_Entity_GetLocalRotationEuler,
        SA_Entity_SetLocalRotationEuler,
        SA_Entity_GetLocalRotationQuat,
        SA_Entity_SetLocalRotationQuat,
        SA_Entity_GetLocalScale,
        SA_Entity_SetLocalScale,
        /* Entity — lifecycle   */
        SA_Entity_Destroy,
        SA_Entity_Create,
        /* Entity — identity    */
        SA_Entity_GetName,
        SA_Entity_FindByName,
        SA_Entity_FindChild,
        SA_Entity_IsValid,
        /* Animator             */
        SA_Animator_IsPlaying,
        SA_Animator_SetPlaying,
        SA_Animator_GetSpeed,
        SA_Animator_SetSpeed,
        /* RigidBody            */
        SA_RigidBody_GetLinearVelocity,
        SA_RigidBody_SetLinearVelocity,
        SA_RigidBody_GetAngularVelocity,
        SA_RigidBody_SetAngularVelocity,
        SA_RigidBody_AddForce,
        SA_RigidBody_AddImpulse,
        /* PointLight           */
        SA_PointLight_GetColor,
        SA_PointLight_SetColor,
        SA_PointLight_GetIntensity,
        SA_PointLight_SetIntensity,
        SA_PointLight_GetRange,
        SA_PointLight_SetRange,
        /* Physics              */
        SA_Physics_Raycast,
        /* Input                */
        SA_Input_GetKey,
        SA_Input_GetAxis2D,
        /* Debug                */
        SA_Debug_DrawLine,
        /* Logging              */
        SA_Log_Info,
        SA_Log_Warn,
        SA_Log_Error,
        /* Time                 */
        SA_Time_GetDeltaTime,
        SA_Time_GetTotalTime,
        /* Block 3 — InputAction */
        SA_InputAction_ReadFloat,
        SA_InputAction_ReadVec2,
        SA_InputAction_IsActive,
        SA_InputAction_WasActivated,
        SA_InputAction_WasDeactivated,
        /* Block 3 — StaticMesh / MeshRenderer */
        SA_StaticMesh_GetAssetUUID,
        SA_MeshRenderer_GetSlotCount,
        SA_MeshRenderer_GetSlotUUID,
        SA_MeshRenderer_SetSlotUUID,
        SA_MeshRenderer_GetCastShadow,
        SA_MeshRenderer_SetCastShadow,
        SA_MeshRenderer_GetReceiveShadow,
        SA_MeshRenderer_SetReceiveShadow,
        /* Block 3 — MaterialOverride */
        SA_MaterialOverride_GetFloat,
        SA_MaterialOverride_SetFloat,
        SA_MaterialOverride_GetVec3,
        SA_MaterialOverride_SetVec3,
        SA_MaterialOverride_GetVec4,
        SA_MaterialOverride_SetVec4,
        /* v4 — RigidBody diagnostics */
        SA_RigidBody_HasComponent,
        SA_RigidBody_GetType,
        /* v5 — InputMap stack control */
        SA_InputMap_Push,
        SA_InputMap_Pop,
        SA_InputMap_Replace,
        SA_InputMap_IsActive,
        SA_InputMap_GetActive,
        /* v6 — World-space transform accessors */
        SA_Entity_GetWorldPosition,
        SA_Entity_SetWorldPosition,
        SA_Entity_GetWorldRotationQuat,
        SA_Entity_SetWorldRotationQuat,
        SA_Entity_GetLossyWorldScale,
        SA_Entity_GetWorldMatrix,
        /* v7 — PostProcess screen modifications (Issue #47) */
        SA_PostProcess_GetVignetteEnabled,
        SA_PostProcess_SetVignetteEnabled,
        SA_PostProcess_GetVignetteIntensity,
        SA_PostProcess_SetVignetteIntensity,
        SA_PostProcess_GetVignetteSmoothness,
        SA_PostProcess_SetVignetteSmoothness,
        SA_PostProcess_GetCAEnabled,
        SA_PostProcess_SetCAEnabled,
        SA_PostProcess_GetCAStrength,
        SA_PostProcess_SetCAStrength,
        SA_PostProcess_GetFilmGrainEnabled,
        SA_PostProcess_SetFilmGrainEnabled,
        SA_PostProcess_GetFilmGrainIntensity,
        SA_PostProcess_SetFilmGrainIntensity,
        SA_PostProcess_GetFilmGrainSize,
        SA_PostProcess_SetFilmGrainSize,
        /* v8 — Animator clip control (Issue #83 P2-5) */
        SA_Animator_SetClip,
        SA_Animator_CrossfadeTo,
    };
}

} // namespace StellarAlia
