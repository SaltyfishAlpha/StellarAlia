#pragma once

#include "function/script/ScriptApiExports.hpp"
#include "function/script/ScriptFieldSchema.hpp"
#include "function/script/ScriptSchemaCache.hpp"
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace StellarAlia {
class Scene;
class InputSystem;
class DebugDraw;
class PhysicsSystem;
struct ScriptComponent;
}
namespace StellarAlia::Resource {
class AssetRegistry;
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
        Scene*                   scene         = nullptr;
        InputSystem*             input         = nullptr;
        DebugDraw*               debug         = nullptr;
        PhysicsSystem*           physics       = nullptr;
        Resource::AssetRegistry* assetRegistry = nullptr;  // resolves ScriptComponent.scriptId → .cs path
        std::string              managedDir;   // path to bin/managed/ — Bridge + Runtime DLLs
        std::string              projectDir;   // project root
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
    bool IsPlaying()   const { return m_playing; }

    void SetProjectDir(const std::string& dir) { m_ctx.projectDir = dir; }

    // ── Script field reflection (#74) ─────────────────────────────────────────
    //
    // Returns the cached schema for className, fetching it via ScriptBridge on
    // first access. Returns nullptr when the bridge is unavailable, the class
    // is unknown, or no ALC has been loaded yet (i.e. Compile has not run).
    const ScriptClassSchema* GetSchemaFor(const std::string& className);

    // Inject every field in sc.fields into the C# instance bound to entityId.
    // No-op when m_playing == false (no instance exists yet) or the bridge call
    // fails. Used at OnPlayStart and from the Inspector for live edits during Play.
    void InjectFieldValues(uint64_t entityId, const ScriptComponent& sc);

    // Inject a single field delta — used by the Inspector during Play so each
    // DragFloat tick sends only the touched field instead of the full record set.
    // `kind` lets the system route EntityRef payloads (sceneLocalId → entt bits)
    // since the variant alternative alone is ambiguous (uint64_t covers other uses).
    void InjectSingleField(uint64_t entityId, const std::string& name,
                           const ScriptFieldValue& value,
                           ScriptFieldKind kind = ScriptFieldKind::Unsupported);

    // Capture current field values from the C# instance back into sc.fields.
    // Reserved for future PIE → Edit value sync; not used internally in #74.
    void CaptureFieldValues(uint64_t entityId, ScriptComponent& sc);

private:
    // hostfxr function pointer types (platform-specific char_t handled in .cpp)
    using fn_initialize_t    = int(*)(const void*, void*, void*);
    using fn_get_delegate_t  = int(*)(void*, int, void**);
    using fn_close_t         = int(*)(void*);

    // ScriptBridge entry-point delegates
    using InitializeDelegate           = void(*)(void*);
    using CompileDelegate              = int (*)(void*, int);
    using InstantiateDelegate          = void(*)(unsigned long long, void*);
    using InvokeLifecycleDelegate      = void(*)(unsigned long long, int, float);
    using RemoveInstanceDelegate       = void(*)(unsigned long long);
    using UnloadDelegate               = void(*)();
    // #74/#75 field reflection: two-step blob protocol (see ScriptBridgeEntry.cs).
    using GetClassSchemaBlobDelegate   = int (*)(void* classNameUtf8, void* outBuf, int capacity);
    using GetClassDefaultsBlobDelegate = int (*)(void* classNameUtf8, void* outBuf, int capacity);
    using ApplyFieldValuesDelegate     = int (*)(unsigned long long entityId, void* blob, int blobLen);
    using CaptureFieldValuesDelegate   = int (*)(unsigned long long entityId, void* outBuf, int capacity);

    void InvokeAll(entt::registry& reg, int method, float arg);
    bool LoadBridgeFunctions(void* loadAndGetFn);
    void OnScriptDestroyed(entt::registry& reg, entt::entity e);
    // Pull schema blob from bridge and decode into the cache. Returns true on success.
    bool FetchAndCacheSchema(const std::string& className);

    Context     m_ctx;
    bool        m_available = false;
    bool        m_playing   = false;

    void*       m_hostfxrHandle  = nullptr;  // platform DLL handle
    void*       m_hostfxrCtx     = nullptr;  // hostfxr context handle

    ScriptApiFunctionTable   m_functionTable{};  // must outlive managed NativeApi.s_table

    InitializeDelegate         m_fnInit            = nullptr;
    CompileDelegate            m_fnCompile         = nullptr;
    InstantiateDelegate        m_fnInstantiate     = nullptr;
    InvokeLifecycleDelegate    m_fnInvoke          = nullptr;
    RemoveInstanceDelegate     m_fnRemove          = nullptr;
    UnloadDelegate             m_fnUnload          = nullptr;
    GetClassSchemaBlobDelegate   m_fnGetSchemaBlob   = nullptr;
    GetClassDefaultsBlobDelegate m_fnGetDefaultsBlob = nullptr;
    ApplyFieldValuesDelegate     m_fnApplyFields     = nullptr;
    CaptureFieldValuesDelegate   m_fnCaptureFields   = nullptr;

    float                    m_totalTime = 0.f;

    ScriptSchemaCache m_schemaCache;

    // EnTT signal connection for on_destroy<ScriptComponent>
    entt::connection m_destroyConn;
};

} // namespace StellarAlia
