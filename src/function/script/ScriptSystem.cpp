#include "function/script/ScriptSystem.hpp"
#include "function/script/ScriptApiExports.hpp"
#include "function/script/ScriptFieldBlob.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "resource/AssetRegistry.hpp"
#include "core/logs/Log.hpp"

#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#  include <Windows.h>
#  define SA_LOAD_LIB(p)   (void*)LoadLibraryW(p)
#  define SA_GET_PROC(h,n)  GetProcAddress((HMODULE)(h), n)
#  define SA_FREE_LIB(h)    FreeLibrary((HMODULE)(h))
   using path_char_t = wchar_t;
#  define SA_STR(s) L##s
#else
#  include <dlfcn.h>
#  define SA_LOAD_LIB(p)   dlopen(p, RTLD_LAZY | RTLD_LOCAL)
#  define SA_GET_PROC(h,n)  dlsym(h, n)
#  define SA_FREE_LIB(h)    dlclose(h)
   using path_char_t = char;
#  define SA_STR(s) s
#endif

namespace fs = std::filesystem;

namespace StellarAlia {

// ── helpers ──────────────────────────────────────────────────────────────────

#ifdef _WIN32
static std::wstring ToPathStr(const std::string& s) {
    std::wstring out(s.size(), L'\0');
    std::transform(s.begin(), s.end(), out.begin(),
        [](char c){ return static_cast<wchar_t>(c); });
    return out;
}
#else
static const std::string& ToPathStr(const std::string& s) { return s; }
#endif

static std::string ClassNameFromPath(const fs::path& scriptPath) {
    return scriptPath.stem().string();
}

// Default-constructed value matching a schema kind — used when migrating
// sc.fields across a recompile that introduced new fields or changed types.
static ScriptFieldValue MakeDefaultForKind(ScriptFieldKind k) {
    switch (k) {
        case ScriptFieldKind::Bool:      return false;
        case ScriptFieldKind::Int32:
        case ScriptFieldKind::Enum:      return int32_t{0};
        case ScriptFieldKind::Float:     return 0.f;
        case ScriptFieldKind::String:    return std::string{};
        case ScriptFieldKind::Vec2:      return glm::vec2{};
        case ScriptFieldKind::Vec3:
        case ScriptFieldKind::Color:     return glm::vec3{};
        case ScriptFieldKind::Vec4:      return glm::vec4{};
        case ScriptFieldKind::AssetRef:  return AssetID{};
        case ScriptFieldKind::EntityRef: return uint64_t{0};
        default:                          return false;
    }
}

// True when the variant alt matches the schema's declared kind. Color is
// permissive — its payload is whichever of vec3/vec4 the C# field used.
static bool VariantMatchesKind(const ScriptFieldValue& v, ScriptFieldKind k) {
    switch (k) {
        case ScriptFieldKind::Bool:      return std::holds_alternative<bool>(v);
        case ScriptFieldKind::Int32:
        case ScriptFieldKind::Enum:      return std::holds_alternative<int32_t>(v);
        case ScriptFieldKind::Float:     return std::holds_alternative<float>(v);
        case ScriptFieldKind::String:    return std::holds_alternative<std::string>(v);
        case ScriptFieldKind::Vec2:      return std::holds_alternative<glm::vec2>(v);
        case ScriptFieldKind::Vec3:      return std::holds_alternative<glm::vec3>(v);
        case ScriptFieldKind::Vec4:      return std::holds_alternative<glm::vec4>(v);
        case ScriptFieldKind::Color:     return std::holds_alternative<glm::vec3>(v) ||
                                                std::holds_alternative<glm::vec4>(v);
        case ScriptFieldKind::AssetRef:  return std::holds_alternative<AssetID>(v);
        case ScriptFieldKind::EntityRef: return std::holds_alternative<uint64_t>(v);
        default:                          return false;
    }
}

// Resolve ScriptComponent.scriptId → absolute .cs path via AssetRegistry.
// Returns empty path when the registry / entry / sourcePath is unavailable.
static fs::path ResolveScriptPath(const Resource::AssetRegistry* reg, const AssetID& id) {
    if (!reg || !id.IsValid()) return {};
    const auto* entry = reg->FindByID(id);
    if (!entry) return {};
    return entry->sourcePath;
}

// ── Init ─────────────────────────────────────────────────────────────────────

bool ScriptSystem::Init(const Context& ctx) {
    m_ctx = ctx;
    SA_LOG_INFO("[ScriptSystem] Init — managedDir='{}'  projectDir='{}'",
                ctx.managedDir, ctx.projectDir);

    // 1. Find hostfxr
    path_char_t hostfxrPath[1024];
    size_t      hostfxrPathLen = sizeof(hostfxrPath) / sizeof(path_char_t);
    if (get_hostfxr_path(hostfxrPath, &hostfxrPathLen, nullptr) != 0) {
        SA_LOG_ERROR("[ScriptSystem] .NET 8 Runtime not found — C# scripting disabled.\n"
                     "  Download: https://dotnet.microsoft.com/download/dotnet/8.0");
        return false;
    }
    SA_LOG_INFO("[ScriptSystem] hostfxr found");

    // 2. Load hostfxr DLL
    m_hostfxrHandle = SA_LOAD_LIB(hostfxrPath);
    if (!m_hostfxrHandle) {
        SA_LOG_ERROR("[ScriptSystem] Failed to load hostfxr DLL");
        return false;
    }

    // GCC warns on function-pointer casts from void*; cast via uintptr_t as workaround.
    auto hfxr_init  = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        reinterpret_cast<uintptr_t>(SA_GET_PROC(m_hostfxrHandle, "hostfxr_initialize_for_runtime_config")));
    auto hfxr_get   = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        reinterpret_cast<uintptr_t>(SA_GET_PROC(m_hostfxrHandle, "hostfxr_get_runtime_delegate")));
    auto hfxr_close = reinterpret_cast<hostfxr_close_fn>(
        reinterpret_cast<uintptr_t>(SA_GET_PROC(m_hostfxrHandle, "hostfxr_close")));

    if (!hfxr_init || !hfxr_get || !hfxr_close) {
        SA_LOG_ERROR("[ScriptSystem] hostfxr exports not found");
        return false;
    }

    // 3. Initialize CLR via runtimeconfig.json
    std::string rcPath = (fs::path(ctx.managedDir) /
                          "StellarAlia.ScriptBridge.runtimeconfig.json").string();
    SA_LOG_INFO("[ScriptSystem] runtimeconfig path: '{}'  exists={}",
                rcPath, fs::exists(rcPath));
    auto rcPathW = ToPathStr(rcPath);
    int rc = hfxr_init(rcPathW.c_str(), nullptr, &m_hostfxrCtx);
    if (rc != 0) {
        SA_LOG_ERROR("[ScriptSystem] hostfxr_initialize_for_runtime_config failed: 0x{:x}", rc);
        return false;
    }
    SA_LOG_INFO("[ScriptSystem] CLR initialized");

    // 4. Get load_assembly_and_get_function_pointer delegate
    load_assembly_and_get_function_pointer_fn loadAndGet = nullptr;
    rc = hfxr_get(m_hostfxrCtx, hdt_load_assembly_and_get_function_pointer,
                  reinterpret_cast<void**>(&loadAndGet));
    if (rc != 0 || !loadAndGet) {
        SA_LOG_ERROR("[ScriptSystem] hdt_load_assembly_and_get_function_pointer failed: 0x{:x}", rc);
        hfxr_close(m_hostfxrCtx);
        return false;
    }

    // 5. Load ScriptBridge entry points
    if (!LoadBridgeFunctions(reinterpret_cast<void*>(loadAndGet))) {
        hfxr_close(m_hostfxrCtx);
        return false;
    }
    SA_LOG_INFO("[ScriptSystem] ScriptBridge entry points loaded");

    // 6. Pass function pointer table to managed side (avoids SHARED library requirement)
    // m_functionTable must live as long as ScriptSystem — managed NativeApi holds a raw pointer to it.
    m_functionTable = SA_Script_BuildFunctionTable();
    m_fnInit(&m_functionTable);
    SA_LOG_INFO("[ScriptSystem] NativeApi initialized on managed side");

    // 7. Set C API context
    SA_Script_SetContext({ ctx.scene, ctx.renderer, ctx.input, ctx.debug, ctx.physics, 0.f, 0.f });

    m_available = true;
    SA_LOG_INFO("[ScriptSystem] C# scripting ready");
    return true;
}

bool ScriptSystem::LoadBridgeFunctions(void* loadAndGetFn) {
    auto loadAndGet = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(
        reinterpret_cast<uintptr_t>(loadAndGetFn));
    std::string bridgeDll  = (fs::path(m_ctx.managedDir) / "StellarAlia.ScriptBridge.dll").string();
    auto        bridgeDllW = ToPathStr(bridgeDll);
    constexpr const char* kType = "StellarAlia.Bridge.ScriptBridgeEntry, StellarAlia.ScriptBridge";

    auto load = [&](const char* method, void** outFn) -> bool {
        auto typeW   = ToPathStr(std::string(kType));
        auto methodW = ToPathStr(std::string(method));
        int r = loadAndGet(bridgeDllW.c_str(), typeW.c_str(), methodW.c_str(),
                           UNMANAGEDCALLERSONLY_METHOD, nullptr, outFn);
        if (r != 0 || !*outFn) {
            SA_LOG_ERROR("[ScriptSystem] Failed to load Bridge method '{}': 0x{:x}", method, r);
            return false;
        }
        return true;
    };

    return load("Initialize",          reinterpret_cast<void**>(&m_fnInit))
        && load("Compile",             reinterpret_cast<void**>(&m_fnCompile))
        && load("Instantiate",         reinterpret_cast<void**>(&m_fnInstantiate))
        && load("InvokeLifecycle",     reinterpret_cast<void**>(&m_fnInvoke))
        && load("RemoveInstance",      reinterpret_cast<void**>(&m_fnRemove))
        && load("Unload",              reinterpret_cast<void**>(&m_fnUnload))
        && load("GetClassSchemaBlob",   reinterpret_cast<void**>(&m_fnGetSchemaBlob))
        && load("GetClassDefaultsBlob", reinterpret_cast<void**>(&m_fnGetDefaultsBlob))
        && load("ApplyFieldValues",     reinterpret_cast<void**>(&m_fnApplyFields));
}

// ── OnPlayStart ───────────────────────────────────────────────────────────────

void ScriptSystem::OnPlayStart(Scene& gameScene) {
    if (!m_available) return;

    // Redirect all script API calls (SetPosition etc.) to the game copy.
    SA_Script_SetContext({ &gameScene, m_ctx.renderer, m_ctx.input, m_ctx.debug, m_ctx.physics, 0.f, 0.f });

    auto& reg = gameScene.Registry();

    // Collect .cs source paths from all ScriptComponent entities
    auto view = reg.view<ScriptComponent>();
    std::vector<const char*> pathPtrs;
    std::vector<std::string> paths;
    for (auto e : view) {
        const auto& sc = view.get<ScriptComponent>(e);
        fs::path full = ResolveScriptPath(m_ctx.assetRegistry, sc.scriptId);
        if (!full.empty()) paths.push_back(full.string());
    }
    if (paths.empty()) {
        SA_LOG_WARN("[ScriptSystem] No script paths found — nothing to compile");
        m_playing = true;
        return;
    }

    for (auto& p : paths) pathPtrs.push_back(p.c_str());

    SA_LOG_INFO("[ScriptSystem] Compiling {} script(s)...", paths.size());
    m_totalTime = 0.f;
    int ok = m_fnCompile(const_cast<void*>(static_cast<const void*>(pathPtrs.data())),
                         static_cast<int>(pathPtrs.size()));
    SA_LOG_INFO("[ScriptSystem] Compile result: {}", ok);
    if (!ok) {
        SA_LOG_ERROR("[ScriptSystem] Compile failed — check log for Roslyn diagnostics");
        return;
    }

    // Compile succeeded → previous-ALC schemas are stale.
    m_schemaCache.Clear();

    // Instantiate each entity's class, then inject any Inspector-edited field values
    // before lifecycle (OnAttach / OnStart) fires.
    for (auto e : view) {
        const auto& sc = view.get<ScriptComponent>(e);
        const fs::path scriptPath = ResolveScriptPath(m_ctx.assetRegistry, sc.scriptId);
        if (scriptPath.empty()) continue;
        std::string cn = sc.className.empty() ? ClassNameFromPath(scriptPath) : sc.className;
        m_fnInstantiate(static_cast<uint64_t>(e), const_cast<char*>(cn.c_str()));
        if (!sc.fields.empty())
            InjectFieldValues(static_cast<uint64_t>(e), sc);
    }

    // Register on_destroy signal so OnDetach fires on any entity destruction
    auto sink = entt::sink{reg.on_destroy<ScriptComponent>()};
    m_destroyConn = sink.connect<&ScriptSystem::OnScriptDestroyed>(*this);

    InvokeAll(reg, 0 /*OnAttach*/, 0.f);
    InvokeAll(reg, 1 /*OnStart*/,  0.f);

    m_playing = true;
}

// ── RecompileEditing ──────────────────────────────────────────────────────────

bool ScriptSystem::RecompileEditing(entt::registry& reg) {
    if (!m_available || m_playing) return false;

    // Compile every .cs the AssetRegistry knows about — not just the ones already
    // referenced by ScriptComponent. The Inspector needs to display schema for
    // any script the user might drag onto a freshly-added component, so the ALC
    // must hold every user type up-front. Runtime Play still Instantiates only
    // the referenced ones (see OnPlayStart).
    std::vector<std::string> paths;
    std::vector<const char*> pathPtrs;
    if (m_ctx.assetRegistry) {
        for (const auto* entry : m_ctx.assetRegistry->EntriesByType("Script"))
            if (entry) paths.push_back(entry->sourcePath.string());
    }
    if (paths.empty()) return true;
    for (auto& p : paths) pathPtrs.push_back(p.c_str());

    SA_LOG_INFO("[ScriptSystem] RecompileEditing: {} script(s)...", paths.size());
    int ok = m_fnCompile(const_cast<void*>(static_cast<const void*>(pathPtrs.data())),
                         static_cast<int>(pathPtrs.size()));
    // Pre-#74 RecompileEditing immediately called Unload() to release the ALC.
    // That tossed the just-loaded user assemblies, which broke schema reflection
    // (FindUserScriptType saw `_alc == null`). We now keep the ALC alive in Edit
    // mode so the Inspector can pull `ScriptClassSchema` lazily. ScriptLoader.Load
    // already replaces its `_alc` field on the next Compile, so leaking the old
    // CollectibleALC is fine — it is GC-eligible after the swap.
    m_schemaCache.Clear();
    if (!ok) {
        SA_LOG_WARN("[ScriptSystem] RecompileEditing: compile failed — check Diagnostics tab");
        return false;
    }

    // ── #75 Field migration: reconcile sc.fields with each entity's new schema ──
    //   - retained:  field name+kind unchanged → keep old value
    //   - reset:     name unchanged, kind changed → default-construct (warn)
    //   - dropped:   field no longer in schema   → discard (logged in summary)
    //   - defaulted: field added by recompile     → default-construct
    int totalRetained = 0, totalReset = 0, totalDropped = 0, totalDefaulted = 0;
    auto view = reg.view<ScriptComponent>();
    for (auto e : view) {
        auto& sc = view.get<ScriptComponent>(e);
        // className may be empty when user relies on the file-stem fallback.
        std::string className = sc.className;
        if (className.empty() && sc.scriptId.IsValid() && m_ctx.assetRegistry) {
            if (const auto* entry = m_ctx.assetRegistry->FindByID(sc.scriptId))
                className = entry->sourcePath.stem().string();
        }
        if (className.empty()) continue;

        const ScriptClassSchema* schema = GetSchemaFor(className);
        if (!schema) continue;

        // Helper: seed value for an absent or kind-mismatched field. Prefer the
        // C# initializer captured in schema.defaults; fall back to zero-init
        // when no initializer or kind mismatches (e.g. user changed field type).
        auto seedValue = [&schema](const ScriptFieldDescriptor& f) -> ScriptFieldValue {
            auto dit = schema->defaults.find(f.name);
            if (dit != schema->defaults.end() && VariantMatchesKind(dit->second, f.kind))
                return dit->second;
            return MakeDefaultForKind(f.kind);
        };

        std::unordered_map<std::string, ScriptFieldValue> migrated;
        migrated.reserve(schema->fields.size());
        for (const auto& f : schema->fields) {
            auto it = sc.fields.find(f.name);
            if (it != sc.fields.end()) {
                if (VariantMatchesKind(it->second, f.kind)) {
                    migrated.emplace(f.name, std::move(it->second));
                    ++totalRetained;
                } else {
                    SA_LOG_WARN("[ScriptSystem] Field '{}::{}' kind changed — reset to default",
                                className, f.name);
                    migrated.emplace(f.name, seedValue(f));
                    ++totalReset;
                }
                sc.fields.erase(it);
            } else {
                migrated.emplace(f.name, seedValue(f));
                ++totalDefaulted;
            }
        }
        // Anything left in sc.fields was dropped by the recompile.
        for (const auto& [name, _] : sc.fields)
            SA_LOG_INFO("[ScriptSystem] Field '{}::{}' no longer in schema — dropped", className, name);
        totalDropped += static_cast<int>(sc.fields.size());
        sc.fields = std::move(migrated);
    }
    if (totalRetained + totalReset + totalDropped + totalDefaulted > 0) {
        SA_LOG_INFO("[ScriptSystem] Field migration: {} retained, {} reset, {} defaulted, {} dropped",
                    totalRetained, totalReset, totalDefaulted, totalDropped);
    }
    return true;
}

// ── Update loop ───────────────────────────────────────────────────────────────

void ScriptSystem::FixedUpdate(float fixedDt, entt::registry& reg) {
    if (!m_available || !m_playing) return;
    SA_Script_SetTime(fixedDt, m_ctx.scene ? 0.f : 0.f);  // total time updated in Update
    InvokeAll(reg, 2 /*OnFixedUpdate*/, fixedDt);
}

void ScriptSystem::Update(float dt, entt::registry& reg) {
    if (!m_available || !m_playing) return;
    m_totalTime += dt;
    SA_Script_SetTime(dt, m_totalTime);
    InvokeAll(reg, 3 /*OnUpdate*/, dt);
}

void ScriptSystem::LateUpdate(float dt, entt::registry& reg) {
    if (!m_available || !m_playing) return;
    InvokeAll(reg, 4 /*OnLateUpdate*/, dt);
}

// ── OnPlayStop ────────────────────────────────────────────────────────────────

void ScriptSystem::OnPlayStop(entt::registry& reg) {
    if (!m_available || !m_playing) return;
    InvokeAll(reg, 5 /*OnStop*/,   0.f);
    InvokeAll(reg, 6 /*OnDetach*/, 0.f);
    m_destroyConn.release();
    m_fnUnload();
    m_playing = false;
}

// ── OnSceneAboutToChange ──────────────────────────────────────────────────────

void ScriptSystem::OnSceneAboutToChange(entt::registry& reg) {
    if (!m_available || !m_playing) return;
    InvokeAll(reg, 5 /*OnStop*/,   0.f);
    InvokeAll(reg, 6 /*OnDetach*/, 0.f);
    m_destroyConn.release();
    m_fnUnload();
    m_playing = false;
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void ScriptSystem::Shutdown(entt::registry& reg) {
    if (m_playing) OnPlayStop(reg);
    if (m_hostfxrCtx) {
        auto hfxr_close = reinterpret_cast<hostfxr_close_fn>(
            reinterpret_cast<uintptr_t>(SA_GET_PROC(m_hostfxrHandle, "hostfxr_close")));
        if (hfxr_close) hfxr_close(m_hostfxrCtx);
        m_hostfxrCtx = nullptr;
    }
    if (m_hostfxrHandle) {
        SA_FREE_LIB(m_hostfxrHandle);
        m_hostfxrHandle = nullptr;
    }
    m_available = false;
}

// ── Private helpers ───────────────────────────────────────────────────────────

void ScriptSystem::InvokeAll(entt::registry& reg, int method, float arg) {
    auto view = reg.view<ScriptComponent>();
    for (auto e : view)
        m_fnInvoke(static_cast<uint64_t>(e), method, arg);
}

void ScriptSystem::OnScriptDestroyed(entt::registry& /*reg*/, entt::entity e) {
    if (m_fnRemove) m_fnRemove(static_cast<uint64_t>(e));
}

// ── Script field reflection (#74) ────────────────────────────────────────────

bool ScriptSystem::FetchAndCacheSchema(const std::string& className) {
    if (!m_fnGetSchemaBlob || className.empty()) return false;

    // Two-step protocol: probe size, then read into resized buffer.
    void* nameUtf8 = const_cast<char*>(className.c_str());
    int needed = m_fnGetSchemaBlob(nameUtf8, nullptr, 0);
    if (needed >= 0) {
        // 0 = bridge says "no such class / no ALC"; >0 means buffer is filled,
        // which shouldn't happen on a capacity=0 call but handle gracefully.
        if (needed == 0) return false;
    }
    const int blobSize = -needed;
    std::vector<std::byte> blob(static_cast<size_t>(blobSize));
    int written = m_fnGetSchemaBlob(nameUtf8, blob.data(), blobSize);
    if (written != blobSize) {
        SA_LOG_WARN("[ScriptSystem] GetClassSchemaBlob('{}') size mismatch: probed={}, wrote={}",
                    className, blobSize, written);
        return false;
    }

    ScriptClassSchema schema;
    if (!DecodeSchema(blob.data(), blob.size(), schema)) {
        SA_LOG_WARN("[ScriptSystem] DecodeSchema('{}') failed (blob corrupted)", className);
        return false;
    }

    // Pull C# field initializers (e.g. `public int hp = 100`). Best-effort —
    // failure leaves defaults empty so the Inspector falls back to zero-init.
    if (m_fnGetDefaultsBlob) {
        int dn = m_fnGetDefaultsBlob(nameUtf8, nullptr, 0);
        if (dn < 0) {
            const int dBlobSize = -dn;
            std::vector<std::byte> dBlob(static_cast<size_t>(dBlobSize));
            int dw = m_fnGetDefaultsBlob(nameUtf8, dBlob.data(), dBlobSize);
            if (dw == dBlobSize)
                DecodeFieldValues(dBlob.data(), dBlob.size(), schema.defaults);
        }
    }

    m_schemaCache.Insert(std::move(schema));
    return true;
}

const ScriptClassSchema* ScriptSystem::GetSchemaFor(const std::string& className) {
    if (!m_available || className.empty()) return nullptr;
    if (const auto* cached = m_schemaCache.Find(className)) return cached;
    if (!FetchAndCacheSchema(className)) return nullptr;
    return m_schemaCache.Find(className);
}

void ScriptSystem::InjectFieldValues(uint64_t entityId, const ScriptComponent& sc) {
    if (!m_fnApplyFields || sc.fields.empty()) return;

    // EntityRef fields are stored as sceneLocalId (persistent) in sc.fields, but
    // the C# Entity struct expects live entt::entity bits. Translate per-field
    // using the schema (only available when className is known) so the variant
    // alternative uint64_t can be unambiguously routed.
    std::string className = sc.className;
    if (className.empty() && sc.scriptId.IsValid() && m_ctx.assetRegistry) {
        if (const auto* entry = m_ctx.assetRegistry->FindByID(sc.scriptId))
            className = entry->sourcePath.stem().string();
    }
    const ScriptClassSchema* schema = GetSchemaFor(className);

    std::unordered_map<std::string, ScriptFieldValue> translated = sc.fields;
    if (schema && m_ctx.scene) {
        auto& reg = m_ctx.scene->Registry();
        for (const auto& f : schema->fields) {
            if (f.kind != ScriptFieldKind::EntityRef) continue;
            auto it = translated.find(f.name);
            if (it == translated.end()) continue;
            auto* p = std::get_if<uint64_t>(&it->second);
            if (!p) continue;
            entt::entity e = m_ctx.scene->FindBySceneLocalId(*p);
            *p = (e == entt::null) ? 0ull : static_cast<uint64_t>(e);
            (void)reg;
        }
    }

    std::vector<std::byte> blob;
    EncodeFieldValues(translated, blob);
    if (blob.empty()) return;
    int applied = m_fnApplyFields(entityId, blob.data(), static_cast<int>(blob.size()));
    if (applied <= 0) {
        SA_LOG_WARN("[ScriptSystem] ApplyFieldValues entity={} returned {} (no fields applied)",
                    entityId, applied);
    }
}

void ScriptSystem::InjectSingleField(uint64_t entityId, const std::string& name,
                                      const ScriptFieldValue& value,
                                      ScriptFieldKind kind) {
    if (!m_fnApplyFields || name.empty()) return;
    // EntityRef: translate sceneLocalId → entt::entity bits before encoding.
    ScriptFieldValue toEncode = value;
    if (kind == ScriptFieldKind::EntityRef && m_ctx.scene) {
        if (auto* p = std::get_if<uint64_t>(&toEncode)) {
            entt::entity e = m_ctx.scene->FindBySceneLocalId(*p);
            *p = (e == entt::null) ? 0ull : static_cast<uint64_t>(e);
        }
    }
    std::vector<std::byte> blob;
    EncodeSingleField(name, toEncode, blob);
    if (blob.empty()) return;
    m_fnApplyFields(entityId, blob.data(), static_cast<int>(blob.size()));
}

} // namespace StellarAlia
