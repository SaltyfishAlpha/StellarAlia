using System.Runtime.InteropServices;
using System.Text;

namespace StellarAlia;

// Internal native API — called through function pointers received at Initialize time.
// The C++ side passes a ScriptApiFunctionTable* in ScriptBridgeEntry.Initialize().
internal static unsafe class NativeApi
{
    internal const uint ExpectedTableVersion = 2;

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

    internal static void SA_Entity_GetRotationQuat(ulong id, out float w, out float x, out float y, out float z) {
        float lw, lx, ly, lz;
        s_table->Entity_GetRotationQuat(id, &lw, &lx, &ly, &lz);
        w = lw; x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetRotationQuat(ulong id, float w, float x, float y, float z)
        => s_table->Entity_SetRotationQuat(id, w, x, y, z);

    internal static void SA_Entity_GetScale(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->Entity_GetScale(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetScale(ulong id, float x, float y, float z)
        => s_table->Entity_SetScale(id, x, y, z);

    // ── Entity — lifecycle ────────────────────────────────────────────────────

    internal static void SA_Entity_Destroy(ulong id) => s_table->Entity_Destroy(id);
    internal static ulong SA_Entity_Create()          => s_table->Entity_Create();

    // ── Entity — identity ─────────────────────────────────────────────────────

    internal static void SA_Entity_GetName(ulong id, byte[] buf, int bufLen) {
        fixed (byte* p = buf)
            s_table->Entity_GetName(id, (sbyte*)p, bufLen);
    }
    internal static int SA_Entity_FindByName(string name, out ulong outId) {
        ulong id = 0;
        byte[] bytes = Encoding.UTF8.GetBytes(name + '\0');
        int r;
        fixed (byte* p = bytes)
            r = s_table->Entity_FindByName((sbyte*)p, &id);
        outId = id;
        return r;
    }
    internal static int SA_Entity_FindChild(ulong id, string childName, out ulong outId) {
        ulong cid = 0;
        byte[] bytes = Encoding.UTF8.GetBytes(childName + '\0');
        int r;
        fixed (byte* p = bytes)
            r = s_table->Entity_FindChild(id, (sbyte*)p, &cid);
        outId = cid;
        return r;
    }
    internal static int SA_Entity_IsValid(ulong id) => s_table->Entity_IsValid(id);

    // ── RigidBody ─────────────────────────────────────────────────────────────

    internal static void SA_RigidBody_GetLinearVelocity(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->RigidBody_GetLinearVelocity(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_RigidBody_SetLinearVelocity(ulong id, float x, float y, float z)
        => s_table->RigidBody_SetLinearVelocity(id, x, y, z);
    internal static void SA_RigidBody_GetAngularVelocity(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->RigidBody_GetAngularVelocity(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_RigidBody_SetAngularVelocity(ulong id, float x, float y, float z)
        => s_table->RigidBody_SetAngularVelocity(id, x, y, z);
    internal static void SA_RigidBody_AddForce  (ulong id, float x, float y, float z)
        => s_table->RigidBody_AddForce(id, x, y, z);
    internal static void SA_RigidBody_AddImpulse(ulong id, float x, float y, float z)
        => s_table->RigidBody_AddImpulse(id, x, y, z);

    // ── PointLight ────────────────────────────────────────────────────────────

    internal static void SA_PointLight_GetColor(ulong id, out float r, out float g, out float b) {
        float lr, lg, lb;
        s_table->PointLight_GetColor(id, &lr, &lg, &lb);
        r = lr; g = lg; b = lb;
    }
    internal static void  SA_PointLight_SetColor    (ulong id, float r, float g, float b) => s_table->PointLight_SetColor(id, r, g, b);
    internal static float SA_PointLight_GetIntensity(ulong id)                             => s_table->PointLight_GetIntensity(id);
    internal static void  SA_PointLight_SetIntensity(ulong id, float v)                   => s_table->PointLight_SetIntensity(id, v);
    internal static float SA_PointLight_GetRange    (ulong id)                             => s_table->PointLight_GetRange(id);
    internal static void  SA_PointLight_SetRange    (ulong id, float v)                   => s_table->PointLight_SetRange(id, v);

    // ── Physics ───────────────────────────────────────────────────────────────

    internal static int SA_Physics_Raycast(
        float ox, float oy, float oz,
        float dx, float dy, float dz, float maxDist,
        out float hitX, out float hitY, out float hitZ,
        out float nrmX, out float nrmY, out float nrmZ,
        out ulong hitEntity)
    {
        float lhx, lhy, lhz, lnx, lny, lnz;
        ulong eid = ulong.MaxValue;
        int r = s_table->Physics_Raycast(ox, oy, oz, dx, dy, dz, maxDist,
                                         &lhx, &lhy, &lhz, &lnx, &lny, &lnz, &eid);
        hitX = lhx; hitY = lhy; hitZ = lhz;
        nrmX = lnx; nrmY = lny; nrmZ = lnz;
        hitEntity = eid;
        return r;
    }

    // ── Animator ──────────────────────────────────────────────────────────────

    internal static int   SA_Animator_IsPlaying (ulong id)              => s_table->Animator_IsPlaying(id);
    internal static void  SA_Animator_SetPlaying(ulong id, int playing)  => s_table->Animator_SetPlaying(id, playing);
    internal static float SA_Animator_GetSpeed  (ulong id)              => s_table->Animator_GetSpeed(id);
    internal static void  SA_Animator_SetSpeed  (ulong id, float speed)  => s_table->Animator_SetSpeed(id, speed);

    // ── Input ─────────────────────────────────────────────────────────────────

    internal static float SA_Input_GetKey(string devicePath) {
        byte[] bytes = Encoding.UTF8.GetBytes(devicePath + '\0');
        fixed (byte* p = bytes)
            return s_table->Input_GetKey((sbyte*)p);
    }
    internal static void SA_Input_GetAxis2D(string devicePath, out float x, out float y) {
        float lx = 0f, ly = 0f;
        byte[] bytes = Encoding.UTF8.GetBytes(devicePath + '\0');
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
        byte[] bytes = Encoding.UTF8.GetBytes(msg + '\0');
        fixed (byte* p = bytes) s_table->Log_Info((sbyte*)p);
    }
    internal static void SA_Log_Warn(string msg) {
        byte[] bytes = Encoding.UTF8.GetBytes(msg + '\0');
        fixed (byte* p = bytes) s_table->Log_Warn((sbyte*)p);
    }
    internal static void SA_Log_Error(string msg) {
        byte[] bytes = Encoding.UTF8.GetBytes(msg + '\0');
        fixed (byte* p = bytes) s_table->Log_Error((sbyte*)p);
    }

    // ── Time ──────────────────────────────────────────────────────────────────

    internal static float SA_Time_GetDeltaTime() => s_table->Time_GetDeltaTime();
    internal static float SA_Time_GetTotalTime() => s_table->Time_GetTotalTime();
}

// ── ScriptApiFunctionTable — mirrors ScriptApiExports.hpp ────────────────────
// IMPORTANT: field order must exactly match ScriptApiFunctionTable in C++.

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ScriptApiFunctionTable
{
    public uint Version;
    // Entity — transform
    public delegate*unmanaged<ulong, float*, float*, float*, void>            Entity_GetPosition;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            Entity_SetPosition;
    public delegate*unmanaged<ulong, float*, float*, float*, void>            Entity_GetRotationEuler;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            Entity_SetRotationEuler;
    public delegate*unmanaged<ulong, float*, float*, float*, float*, void>    Entity_GetRotationQuat;
    public delegate*unmanaged<ulong, float,  float,  float,  float,  void>    Entity_SetRotationQuat;
    public delegate*unmanaged<ulong, float*, float*, float*, void>            Entity_GetScale;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            Entity_SetScale;
    // Entity — lifecycle
    public delegate*unmanaged<ulong, void>                                    Entity_Destroy;
    public delegate*unmanaged<ulong>                                          Entity_Create;
    // Entity — identity
    public delegate*unmanaged<ulong, sbyte*, int,    void>                    Entity_GetName;
    public delegate*unmanaged<sbyte*, ulong*, int>                            Entity_FindByName;
    public delegate*unmanaged<ulong, sbyte*, ulong*, int>                     Entity_FindChild;
    public delegate*unmanaged<ulong, int>                                     Entity_IsValid;
    // Animator
    public delegate*unmanaged<ulong, int>                                     Animator_IsPlaying;
    public delegate*unmanaged<ulong, int,   void>                             Animator_SetPlaying;
    public delegate*unmanaged<ulong, float>                                   Animator_GetSpeed;
    public delegate*unmanaged<ulong, float, void>                             Animator_SetSpeed;
    // RigidBody
    public delegate*unmanaged<ulong, float*, float*, float*, void>            RigidBody_GetLinearVelocity;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            RigidBody_SetLinearVelocity;
    public delegate*unmanaged<ulong, float*, float*, float*, void>            RigidBody_GetAngularVelocity;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            RigidBody_SetAngularVelocity;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            RigidBody_AddForce;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            RigidBody_AddImpulse;
    // PointLight
    public delegate*unmanaged<ulong, float*, float*, float*, void>            PointLight_GetColor;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            PointLight_SetColor;
    public delegate*unmanaged<ulong, float>                                   PointLight_GetIntensity;
    public delegate*unmanaged<ulong, float,  void>                            PointLight_SetIntensity;
    public delegate*unmanaged<ulong, float>                                   PointLight_GetRange;
    public delegate*unmanaged<ulong, float,  void>                            PointLight_SetRange;
    // Physics
    public delegate*unmanaged<float,float,float, float,float,float, float,
                              float*,float*,float*, float*,float*,float*, ulong*, int> Physics_Raycast;
    // Input
    public delegate*unmanaged<sbyte*, float>                                  Input_GetKey;
    public delegate*unmanaged<sbyte*, float*, float*, void>                   Input_GetAxis2D;
    // Debug
    public delegate*unmanaged<float,float,float, float,float,float, float,float,float,float, void> Debug_DrawLine;
    // Logging
    public delegate*unmanaged<sbyte*, void>                                   Log_Info;
    public delegate*unmanaged<sbyte*, void>                                   Log_Warn;
    public delegate*unmanaged<sbyte*, void>                                   Log_Error;
    // Time
    public delegate*unmanaged<float>                                          Time_GetDeltaTime;
    public delegate*unmanaged<float>                                          Time_GetTotalTime;
}
