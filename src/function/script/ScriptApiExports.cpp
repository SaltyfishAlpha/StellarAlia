#include "function/script/ScriptApiExports.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "function/input/InputSystem.hpp"
#include "function/debug/DebugDraw.hpp"
#include "function/physics/PhysicsSystem.hpp"
#include "core/logs/Log.hpp"

#include <spdlog/spdlog.h>
#include <glm/gtc/quaternion.hpp>
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

// ── Entity — transform ────────────────────────────────────────────────────────

void SA_Entity_GetPosition(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 0.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e))
        { *x = t->position.x; *y = t->position.y; *z = t->position.z; }
}

void SA_Entity_SetPosition(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        t->position = { x, y, z };
        g_ctx.scene->MarkDirty(e);
    }
}

void SA_Entity_GetRotationEuler(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 0.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        glm::vec3 euler = glm::degrees(glm::eulerAngles(t->rotation));
        *x = euler.x; *y = euler.y; *z = euler.z;
    }
}

void SA_Entity_SetRotationEuler(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        t->rotation = glm::quat(glm::radians(glm::vec3(x, y, z)));
        g_ctx.scene->MarkDirty(e);
    }
}

void SA_Entity_GetRotationQuat(uint64_t id, float* w, float* x, float* y, float* z) {
    *w = 1.f; *x = *y = *z = 0.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e))
        { *w = t->rotation.w; *x = t->rotation.x; *y = t->rotation.y; *z = t->rotation.z; }
}

void SA_Entity_SetRotationQuat(uint64_t id, float w, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        t->rotation = glm::normalize(glm::quat(w, x, y, z));
        g_ctx.scene->MarkDirty(e);
    }
}

void SA_Entity_GetScale(uint64_t id, float* x, float* y, float* z) {
    *x = *y = *z = 1.f;
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e))
        { *x = t->scale.x; *y = t->scale.y; *z = t->scale.z; }
}

void SA_Entity_SetScale(uint64_t id, float x, float y, float z) {
    if (!g_ctx.scene) return;
    auto e = static_cast<entt::entity>(id);
    if (!g_ctx.scene->Registry().valid(e)) return;
    if (auto* t = g_ctx.scene->Registry().try_get<TransformComponent>(e)) {
        t->scale = { x, y, z };
        g_ctx.scene->MarkDirty(e);
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

} // extern "C"

// ── Function table ────────────────────────────────────────────────────────────

namespace StellarAlia {

ScriptApiFunctionTable SA_Script_BuildFunctionTable() {
    return {
        /* version              */ 2,
        /* Entity — transform   */
        SA_Entity_GetPosition,
        SA_Entity_SetPosition,
        SA_Entity_GetRotationEuler,
        SA_Entity_SetRotationEuler,
        SA_Entity_GetRotationQuat,
        SA_Entity_SetRotationQuat,
        SA_Entity_GetScale,
        SA_Entity_SetScale,
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
    };
}

} // namespace StellarAlia
