using System.Runtime.InteropServices;

namespace StellarAlia.Bridge;

/// <summary>
/// Unmanaged entry points called by ScriptSystem.cpp via function pointers from hostfxr.
/// All parameters must be blittable (void*, primitive types, enums).
/// Exceptions must NOT escape [UnmanagedCallersOnly] methods.
/// </summary>
public static unsafe class ScriptBridgeEntry
{
    private static ScriptLoader?   s_loader;
    private static ScriptCompiler? s_compiler;

    // ── Initialize (called once during ScriptSystem::Init) ───────────────────

    [UnmanagedCallersOnly]
    public static void Initialize(void* functionTablePtr) {
        try {
            // Verify struct version before touching any function pointers.
            uint gotVersion = ((ScriptApiFunctionTable*)functionTablePtr)->Version;
            if (gotVersion != NativeApi.ExpectedTableVersion) {
                Console.Error.WriteLine(
                    $"[Bridge] ScriptApiFunctionTable version mismatch: " +
                    $"C++ sent {gotVersion}, managed expects {NativeApi.ExpectedTableVersion}. " +
                    $"Rebuild managed DLLs (dotnet publish managed/StellarAlia.ScriptBridge).");
                return;
            }
            NativeApi.Initialize(functionTablePtr);
            s_compiler = new ScriptCompiler();
            s_loader   = new ScriptLoader();
        } catch (Exception ex) {
            Console.Error.WriteLine($"[Bridge] Initialize failed: {ex}");
        }
    }

    // ── Compile (called by OnPlayStart) ──────────────────────────────────────

    /// sourcePathsPtr: void** — array of ANSI C-string pointers, length=count.
    /// Returns 1 on success, 0 on failure (errors logged via SA_Log_Error).
    [UnmanagedCallersOnly]
    public static int Compile(void* sourcePathsPtr, int count) {
        try {
            string[] paths = MarshalStringArray((IntPtr)sourcePathsPtr, count);
            byte[] bytes = s_compiler!.Compile(paths);
            s_loader!.Load(bytes);
            return 1;
        } catch (ScriptCompileException ex) {
            StellarAlia.Log.Error($"[Script compile]\n{ex.Message}");
            return 0;
        } catch (Exception ex) {
            StellarAlia.Log.Error($"[Bridge] Compile: {ex}");
            return 0;
        }
    }

    /// Instantiate the named class for a specific entity.
    [UnmanagedCallersOnly]
    public static void Instantiate(ulong entityId, void* classNamePtr) {
        try {
            string className = Marshal.PtrToStringAnsi((IntPtr)classNamePtr)!;
            s_loader!.Instantiate(entityId, className);
        } catch (Exception ex) {
            StellarAlia.Log.Error($"[Bridge] Instantiate failed: {ex.Message}");
        }
    }

    // ── Per-frame ─────────────────────────────────────────────────────────────

    [UnmanagedCallersOnly]
    public static void InvokeLifecycle(ulong entityId, int method, float arg) {
        try {
            s_loader!.Invoke(entityId, (LifecycleMethod)method, arg);
        } catch (Exception ex) {
            StellarAlia.Log.Error($"[Bridge] Invoke entity={entityId} method={method}: {ex.Message}");
        }
    }

    // ── on_destroy signal ─────────────────────────────────────────────────────

    [UnmanagedCallersOnly]
    public static void RemoveInstance(ulong entityId) {
        try { s_loader?.RemoveInstance(entityId); }
        catch { /* must not throw */ }
    }

    // ── OnPlayStop ────────────────────────────────────────────────────────────

    [UnmanagedCallersOnly]
    public static void Unload() {
        try { s_loader?.Unload(); }
        catch { /* must not throw */ }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private static string[] MarshalStringArray(IntPtr ptr, int count) {
        var result = new string[count];
        for (int i = 0; i < count; i++) {
            IntPtr strPtr = Marshal.ReadIntPtr(ptr, i * IntPtr.Size);
            result[i] = Marshal.PtrToStringAnsi(strPtr)!;
        }
        return result;
    }
}
