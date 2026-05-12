#include "function/script/ScriptSystem.hpp"
#include "function/script/ScriptApiExports.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
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

static std::string ClassNameFromPath(const std::string& scriptPath) {
    return fs::path(scriptPath).stem().string();
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
    SA_Script_SetContext({ ctx.scene, ctx.input, ctx.debug, ctx.physics, 0.f, 0.f });

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

    return load("Initialize",      reinterpret_cast<void**>(&m_fnInit))
        && load("Compile",         reinterpret_cast<void**>(&m_fnCompile))
        && load("Instantiate",     reinterpret_cast<void**>(&m_fnInstantiate))
        && load("InvokeLifecycle", reinterpret_cast<void**>(&m_fnInvoke))
        && load("RemoveInstance",  reinterpret_cast<void**>(&m_fnRemove))
        && load("Unload",          reinterpret_cast<void**>(&m_fnUnload));
}

// ── OnPlayStart ───────────────────────────────────────────────────────────────

void ScriptSystem::OnPlayStart(Scene& gameScene) {
    if (!m_available) return;

    // Redirect all script API calls (SetPosition etc.) to the game copy.
    SA_Script_SetContext({ &gameScene, m_ctx.input, m_ctx.debug, m_ctx.physics, 0.f, 0.f });

    auto& reg = gameScene.Registry();

    // Collect .cs source paths from all ScriptComponent entities
    auto view = reg.view<ScriptComponent>();
    std::vector<const char*> pathPtrs;
    std::vector<std::string> paths;
    for (auto e : view) {
        const auto& sc = view.get<ScriptComponent>(e);
        if (!sc.scriptPath.empty()) {
            fs::path full = fs::path(m_ctx.projectDir) / sc.scriptPath;
            paths.push_back(full.string());
        }
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

    // Instantiate each entity's class
    for (auto e : view) {
        const auto& sc = view.get<ScriptComponent>(e);
        if (sc.scriptPath.empty()) continue;
        std::string cn = sc.className.empty() ? ClassNameFromPath(sc.scriptPath) : sc.className;
        m_fnInstantiate(static_cast<uint64_t>(e), const_cast<char*>(cn.c_str()));
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

    auto view = reg.view<ScriptComponent>();
    std::vector<std::string> paths;
    std::vector<const char*> pathPtrs;
    for (auto e : view) {
        const auto& sc = view.get<ScriptComponent>(e);
        if (!sc.scriptPath.empty()) {
            fs::path full = fs::path(m_ctx.projectDir) / sc.scriptPath;
            paths.push_back(full.string());
        }
    }
    if (paths.empty()) return true;
    for (auto& p : paths) pathPtrs.push_back(p.c_str());

    SA_LOG_INFO("[ScriptSystem] RecompileEditing: {} script(s)...", paths.size());
    int ok = m_fnCompile(const_cast<void*>(static_cast<const void*>(pathPtrs.data())),
                         static_cast<int>(pathPtrs.size()));
    // Unload releases the CollectibleALC but also clears NativeApi.s_table on the managed side.
    // Re-init immediately so subsequent OnPlayStart sees a valid function table.
    m_fnUnload();
    m_fnInit(&m_functionTable);
    if (!ok) SA_LOG_WARN("[ScriptSystem] RecompileEditing: compile failed — check Diagnostics tab");
    return ok != 0;
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

} // namespace StellarAlia
