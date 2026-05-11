#include "function/script/ScriptApiExports.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "function/input/InputSystem.hpp"
#include "function/debug/DebugDraw.hpp"
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
        SA_Entity_GetPosition,
        SA_Entity_SetPosition,
        SA_Entity_GetRotationEuler,
        SA_Entity_SetRotationEuler,
        SA_Entity_GetScale,
        SA_Entity_SetScale,
        SA_Entity_GetName,
        SA_Entity_FindByName,
        SA_Entity_FindChild,
        SA_Entity_IsValid,
        SA_Animator_IsPlaying,
        SA_Animator_SetPlaying,
        SA_Animator_GetSpeed,
        SA_Animator_SetSpeed,
        SA_Input_GetKey,
        SA_Input_GetAxis2D,
        SA_Debug_DrawLine,
        SA_Log_Info,
        SA_Log_Warn,
        SA_Log_Error,
        SA_Time_GetDeltaTime,
        SA_Time_GetTotalTime,
    };
}

} // namespace StellarAlia
