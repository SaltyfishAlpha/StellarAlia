using System.Runtime.InteropServices;
using System.Text;

namespace StellarAlia;

// Internal native API — called through function pointers received at Initialize time.
// The C++ side passes a ScriptApiFunctionTable* in ScriptBridgeEntry.Initialize().
internal static unsafe class NativeApi
{
    private static ScriptApiFunctionTable* s_table;

    internal static void Initialize(void* tablePtr) {
        s_table = (ScriptApiFunctionTable*)tablePtr;
    }

    // ── Entity — transform ────────────────────────────────────────────────────

    internal static void SA_Entity_GetPosition(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->Entity_GetPosition(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetPosition(ulong id, float x, float y, float z)
        => s_table->Entity_SetPosition(id, x, y, z);

    internal static void SA_Entity_GetRotationEuler(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->Entity_GetRotationEuler(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetRotationEuler(ulong id, float x, float y, float z)
        => s_table->Entity_SetRotationEuler(id, x, y, z);

    internal static void SA_Entity_GetScale(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->Entity_GetScale(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetScale(ulong id, float x, float y, float z)
        => s_table->Entity_SetScale(id, x, y, z);

    // ── Entity — identity ─────────────────────────────────────────────────────

    internal static void SA_Entity_GetName(ulong id, byte[] buf, int bufLen) {
        fixed (byte* p = buf)
            s_table->Entity_GetName(id, (sbyte*)p, bufLen);
    }
    internal static int SA_Entity_FindByName(string name, out ulong outId) {
        ulong id = 0;
        byte[] bytes = Encoding.Latin1.GetBytes(name + '\0');
        int r;
        fixed (byte* p = bytes)
            r = s_table->Entity_FindByName((sbyte*)p, &id);
        outId = id;
        return r;
    }
    internal static int SA_Entity_FindChild(ulong id, string childName, out ulong outId) {
        ulong cid = 0;
        byte[] bytes = Encoding.Latin1.GetBytes(childName + '\0');
        int r;
        fixed (byte* p = bytes)
            r = s_table->Entity_FindChild(id, (sbyte*)p, &cid);
        outId = cid;
        return r;
    }
    internal static int SA_Entity_IsValid(ulong id) => s_table->Entity_IsValid(id);

    // ── Animator ──────────────────────────────────────────────────────────────

    internal static int   SA_Animator_IsPlaying (ulong id)              => s_table->Animator_IsPlaying(id);
    internal static void  SA_Animator_SetPlaying(ulong id, int playing)  => s_table->Animator_SetPlaying(id, playing);
    internal static float SA_Animator_GetSpeed  (ulong id)              => s_table->Animator_GetSpeed(id);
    internal static void  SA_Animator_SetSpeed  (ulong id, float speed)  => s_table->Animator_SetSpeed(id, speed);

    // ── Input ─────────────────────────────────────────────────────────────────

    internal static float SA_Input_GetKey(string devicePath) {
        byte[] bytes = Encoding.Latin1.GetBytes(devicePath + '\0');
        fixed (byte* p = bytes)
            return s_table->Input_GetKey((sbyte*)p);
    }
    internal static void SA_Input_GetAxis2D(string devicePath, out float x, out float y) {
        float lx = 0f, ly = 0f;
        byte[] bytes = Encoding.Latin1.GetBytes(devicePath + '\0');
        fixed (byte* p = bytes)
            s_table->Input_GetAxis2D((sbyte*)p, &lx, &ly);
        x = lx; y = ly;
    }

    // ── Debug ─────────────────────────────────────────────────────────────────

    internal static void SA_Debug_DrawLine(
        float x0, float y0, float z0,
        float x1, float y1, float z1,
        float r,  float g,  float b,  float a)
        => s_table->Debug_DrawLine(x0,y0,z0, x1,y1,z1, r,g,b,a);

    // ── Logging ───────────────────────────────────────────────────────────────

    internal static void SA_Log_Info(string msg) {
        byte[] bytes = Encoding.Latin1.GetBytes(msg + '\0');
        fixed (byte* p = bytes) s_table->Log_Info((sbyte*)p);
    }
    internal static void SA_Log_Warn(string msg) {
        byte[] bytes = Encoding.Latin1.GetBytes(msg + '\0');
        fixed (byte* p = bytes) s_table->Log_Warn((sbyte*)p);
    }
    internal static void SA_Log_Error(string msg) {
        byte[] bytes = Encoding.Latin1.GetBytes(msg + '\0');
        fixed (byte* p = bytes) s_table->Log_Error((sbyte*)p);
    }

    // ── Time ──────────────────────────────────────────────────────────────────

    internal static float SA_Time_GetDeltaTime() => s_table->Time_GetDeltaTime();
    internal static float SA_Time_GetTotalTime() => s_table->Time_GetTotalTime();
}

// ── ScriptApiFunctionTable — mirrors ScriptApiExports.hpp ────────────────────

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ScriptApiFunctionTable
{
    // Entity — transform
    public delegate*unmanaged<ulong, float*, float*, float*, void> Entity_GetPosition;
    public delegate*unmanaged<ulong, float,  float,  float,  void> Entity_SetPosition;
    public delegate*unmanaged<ulong, float*, float*, float*, void> Entity_GetRotationEuler;
    public delegate*unmanaged<ulong, float,  float,  float,  void> Entity_SetRotationEuler;
    public delegate*unmanaged<ulong, float*, float*, float*, void> Entity_GetScale;
    public delegate*unmanaged<ulong, float,  float,  float,  void> Entity_SetScale;
    // Entity — identity
    public delegate*unmanaged<ulong, sbyte*, int, void>             Entity_GetName;
    public delegate*unmanaged<sbyte*, ulong*, int>                  Entity_FindByName;
    public delegate*unmanaged<ulong, sbyte*, ulong*, int>           Entity_FindChild;
    public delegate*unmanaged<ulong, int>                           Entity_IsValid;
    // Animator
    public delegate*unmanaged<ulong, int>                           Animator_IsPlaying;
    public delegate*unmanaged<ulong, int,   void>                   Animator_SetPlaying;
    public delegate*unmanaged<ulong, float>                         Animator_GetSpeed;
    public delegate*unmanaged<ulong, float, void>                   Animator_SetSpeed;
    // Input
    public delegate*unmanaged<sbyte*, float>                        Input_GetKey;
    public delegate*unmanaged<sbyte*, float*, float*, void>         Input_GetAxis2D;
    // Debug
    public delegate*unmanaged<float,float,float, float,float,float, float,float,float,float, void> Debug_DrawLine;
    // Logging
    public delegate*unmanaged<sbyte*, void>                         Log_Info;
    public delegate*unmanaged<sbyte*, void>                         Log_Warn;
    public delegate*unmanaged<sbyte*, void>                         Log_Error;
    // Time
    public delegate*unmanaged<float>                                Time_GetDeltaTime;
    public delegate*unmanaged<float>                                Time_GetTotalTime;
}
