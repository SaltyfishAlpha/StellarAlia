#pragma once

#include "function/script/ScriptApiExports.hpp"
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace StellarAlia {
class Scene;
class InputSystem;
class DebugDraw;
class PhysicsSystem;
}

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// ScriptSystem — drives C# script lifecycle for all ScriptComponent entities.
//
// Lifecycle (called by Application):
//   Init()                 — load hostfxr + ScriptBridge.dll once at startup
//   OnPlayStart(reg)       — compile → load ALC → OnAttach all → OnStart all
//   FixedUpdate(dt, reg)   — inside physics accumulator loop (1/60 s)
//   Update(dt, reg)        — variable-rate per frame
//   LateUpdate(dt, reg)    — after all Update, before UpdateTransforms
//   OnPlayStop(reg)        — OnStop all → OnDetach all → unload ALC
//   OnSceneAboutToChange   — call before scene.Clear() when switching scenes
//   Shutdown(reg)          — release CLR resources
// ─────────────────────────────────────────────────────────────────────────────
class ScriptSystem {
public:
    struct Context {
        Scene*         scene          = nullptr;
        InputSystem*   input          = nullptr;
        DebugDraw*     debug          = nullptr;
        PhysicsSystem* physics        = nullptr;
        std::string    managedDir;    // path to bin/managed/ — Bridge + Runtime DLLs
        std::string    projectDir;    // project root — .cs files resolved relative to this
    };

    ScriptSystem()  = default;
    ~ScriptSystem() = default;

    ScriptSystem(const ScriptSystem&)            = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    bool Init(const Context& ctx);
    void Shutdown(entt::registry& reg);

    void OnPlayStart          (Scene& gameScene);
    void OnPlayStop           (entt::registry& reg);
    void OnSceneAboutToChange (entt::registry& reg);

    // Compile all scripts in the editor scene (diagnostics only, no Instantiate).
    // Safe to call while not playing. Returns true on compile success.
    bool RecompileEditing     (entt::registry& reg);

    void FixedUpdate(float fixedDt, entt::registry& reg);
    void Update     (float dt,      entt::registry& reg);
    void LateUpdate (float dt,      entt::registry& reg);

    bool IsAvailable() const { return m_available; }

    void SetProjectDir(const std::string& dir) { m_ctx.projectDir = dir; }

private:
    // hostfxr function pointer types (platform-specific char_t handled in .cpp)
    using fn_initialize_t    = int(*)(const void*, void*, void*);
    using fn_get_delegate_t  = int(*)(void*, int, void**);
    using fn_close_t         = int(*)(void*);

    // ScriptBridge entry-point delegates
    using InitializeDelegate     = void(*)(void*);
    using CompileDelegate        = int (*)(void*, int);
    using InstantiateDelegate    = void(*)(unsigned long long, void*);
    using InvokeLifecycleDelegate= void(*)(unsigned long long, int, float);
    using RemoveInstanceDelegate = void(*)(unsigned long long);
    using UnloadDelegate         = void(*)();

    void InvokeAll(entt::registry& reg, int method, float arg);
    bool LoadBridgeFunctions(void* loadAndGetFn);
    void OnScriptDestroyed(entt::registry& reg, entt::entity e);

    Context     m_ctx;
    bool        m_available = false;
    bool        m_playing   = false;

    void*       m_hostfxrHandle  = nullptr;  // platform DLL handle
    void*       m_hostfxrCtx     = nullptr;  // hostfxr context handle

    ScriptApiFunctionTable   m_functionTable{};  // must outlive managed NativeApi.s_table

    InitializeDelegate      m_fnInit       = nullptr;
    CompileDelegate         m_fnCompile    = nullptr;
    InstantiateDelegate     m_fnInstantiate= nullptr;
    InvokeLifecycleDelegate m_fnInvoke     = nullptr;
    RemoveInstanceDelegate  m_fnRemove     = nullptr;
    UnloadDelegate          m_fnUnload     = nullptr;

    float                    m_totalTime = 0.f;

    // EnTT signal connection for on_destroy<ScriptComponent>
    entt::connection m_destroyConn;
};

} // namespace StellarAlia
