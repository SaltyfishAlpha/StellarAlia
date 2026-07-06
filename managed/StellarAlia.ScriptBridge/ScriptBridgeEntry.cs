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
        try { s_loader?.Unload(); FieldReflector.ClearCache(); }
        catch { /* must not throw */ }
    }

    // ── Script field reflection (#74) ─────────────────────────────────────────
    //
    // Two-step blob protocol: caller first passes capacity=0 to receive the
    // required size as a negative value (-needed), then resizes and calls again
    // with that buffer to receive the actual bytes. Avoids predicting size and
    // double-marshalling.

    /// Returns the schema-blob size for `className`. On the second call writes
    /// the blob into `outBuf` and returns the byte count.
    /// className: null-terminated UTF-8.
    /// outBuf:    caller-owned buffer of `capacity` bytes; ignored when capacity=0.
    /// Returns:
    ///   > 0 — schema blob size written
    ///   < 0 — -size needed (capacity too small)
    ///   = 0 — class not found / no loaded ALC / error (logged)
    [UnmanagedCallersOnly]
    public static int GetClassSchemaBlob(IntPtr classNameUtf8, IntPtr outBuf, int capacity) {
        try {
            if (s_loader is null) return 0;
            string? className = Marshal.PtrToStringUTF8(classNameUtf8);
            if (string.IsNullOrEmpty(className)) return 0;

            Type? type = s_loader.FindUserScriptType(className);
            if (type is null) return 0;

            byte[] blob = FieldReflector.BuildSchemaBlob(type);
            if (blob.Length > capacity) return -blob.Length;
            Marshal.Copy(blob, 0, outBuf, blob.Length);
            return blob.Length;
        } catch (Exception ex) {
            StellarAlia.Log.Error($"[Bridge] GetClassSchemaBlob: {ex.Message}");
            return 0;
        }
    }

    /// Apply a field-value blob to the C# instance bound to `entityId`.
    /// Returns the count of fields successfully written, or 0 on error.
    [UnmanagedCallersOnly]
    public static int ApplyFieldValues(ulong entityId, IntPtr blobPtr, int blobLen) {
        try {
            if (s_loader is null || blobLen <= 0) return 0;
            object? instance = s_loader.GetInstance(entityId);
            if (instance is null) return 0;

            var span = new ReadOnlySpan<byte>((void*)blobPtr, blobLen);
            return FieldReflector.ApplyFieldValues(instance, span);
        } catch (Exception ex) {
            StellarAlia.Log.Error($"[Bridge] ApplyFieldValues entity={entityId}: {ex.Message}");
            return 0;
        }
    }

    /// Capture default field values from a fresh `Activator.CreateInstance(type)`
    /// of `className` — i.e. the C# field initializers. Used at schema-fetch
    /// time so the Inspector can seed sc.fields with meaningful values before
    /// the user edits anything. Same two-step capacity protocol.
    [UnmanagedCallersOnly]
    public static int GetClassDefaultsBlob(IntPtr classNameUtf8, IntPtr outBuf, int capacity) {
        try {
            if (s_loader is null) return 0;
            string? className = Marshal.PtrToStringUTF8(classNameUtf8);
            if (string.IsNullOrEmpty(className)) return 0;
            Type? type = s_loader.FindUserScriptType(className);
            if (type is null) return 0;
            object? probe = Activator.CreateInstance(type);
            if (probe is null) return 0;
            byte[] blob = FieldReflector.CaptureFieldValues(probe);
            if (blob.Length > capacity) return -blob.Length;
            Marshal.Copy(blob, 0, outBuf, blob.Length);
            return blob.Length;
        } catch (Exception ex) {
            StellarAlia.Log.Error($"[Bridge] GetClassDefaultsBlob: {ex.Message}");
            return 0;
        }
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
