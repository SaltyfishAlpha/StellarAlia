using System.Runtime.InteropServices;
using System.Text;

namespace StellarAlia;

// Internal native API — called through function pointers received at Initialize time.
// The C++ side passes a ScriptApiFunctionTable* in ScriptBridgeEntry.Initialize().
internal static unsafe class NativeApi
{
    internal const uint ExpectedTableVersion = 8;

    private static ScriptApiFunctionTable* s_table;

    internal static void Initialize(void* tablePtr) {
        s_table = (ScriptApiFunctionTable*)tablePtr;
    }

    // ── Entity — transform (local, parent-relative) ───────────────────────────

    internal static void SA_Entity_GetLocalPosition(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->Entity_GetLocalPosition(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetLocalPosition(ulong id, float x, float y, float z)
        => s_table->Entity_SetLocalPosition(id, x, y, z);

    internal static void SA_Entity_GetLocalRotationEuler(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->Entity_GetLocalRotationEuler(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetLocalRotationEuler(ulong id, float x, float y, float z)
        => s_table->Entity_SetLocalRotationEuler(id, x, y, z);

    internal static void SA_Entity_GetLocalRotationQuat(ulong id, out float w, out float x, out float y, out float z) {
        float lw, lx, ly, lz;
        s_table->Entity_GetLocalRotationQuat(id, &lw, &lx, &ly, &lz);
        w = lw; x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetLocalRotationQuat(ulong id, float w, float x, float y, float z)
        => s_table->Entity_SetLocalRotationQuat(id, w, x, y, z);

    internal static void SA_Entity_GetLocalScale(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->Entity_GetLocalScale(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetLocalScale(ulong id, float x, float y, float z)
        => s_table->Entity_SetLocalScale(id, x, y, z);

    // ── Entity — transform (world-space; lazy-refreshed in native) ────────────

    internal static void SA_Entity_GetWorldPosition(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->Entity_GetWorldPosition(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetWorldPosition(ulong id, float x, float y, float z)
        => s_table->Entity_SetWorldPosition(id, x, y, z);

    internal static void SA_Entity_GetWorldRotationQuat(ulong id, out float w, out float x, out float y, out float z) {
        float lw, lx, ly, lz;
        s_table->Entity_GetWorldRotationQuat(id, &lw, &lx, &ly, &lz);
        w = lw; x = lx; y = ly; z = lz;
    }
    internal static void SA_Entity_SetWorldRotationQuat(ulong id, float w, float x, float y, float z)
        => s_table->Entity_SetWorldRotationQuat(id, w, x, y, z);

    internal static void SA_Entity_GetLossyWorldScale(ulong id, out float x, out float y, out float z) {
        float lx, ly, lz;
        s_table->Entity_GetLossyWorldScale(id, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }

    // Writes 16 floats (glm column-major) into the caller-provided buffer.
    internal static void SA_Entity_GetWorldMatrix(ulong id, float* out16)
        => s_table->Entity_GetWorldMatrix(id, out16);

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

    internal static void SA_Animator_SetClip(ulong id, string uuid) {
        byte[] bytes = Encoding.UTF8.GetBytes(uuid + '\0');
        fixed (byte* p = bytes)
            s_table->Animator_SetClip(id, (sbyte*)p);
    }
    internal static void SA_Animator_CrossfadeTo(ulong id, string uuid, float fade) {
        byte[] bytes = Encoding.UTF8.GetBytes(uuid + '\0');
        fixed (byte* p = bytes)
            s_table->Animator_CrossfadeTo(id, (sbyte*)p, fade);
    }

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

    // ── InputAction (Block 3, Issue #71) ─────────────────────────────────────

    internal static float SA_InputAction_ReadFloat(string action) {
        byte[] bytes = Encoding.UTF8.GetBytes(action + '\0');
        fixed (byte* p = bytes)
            return s_table->InputAction_ReadFloat((sbyte*)p);
    }
    internal static void SA_InputAction_ReadVec2(string action, out float x, out float y) {
        float lx = 0f, ly = 0f;
        byte[] bytes = Encoding.UTF8.GetBytes(action + '\0');
        fixed (byte* p = bytes)
            s_table->InputAction_ReadVec2((sbyte*)p, &lx, &ly);
        x = lx; y = ly;
    }
    internal static int SA_InputAction_IsActive(string action) {
        byte[] bytes = Encoding.UTF8.GetBytes(action + '\0');
        fixed (byte* p = bytes)
            return s_table->InputAction_IsActive((sbyte*)p);
    }
    internal static int SA_InputAction_WasActivated(string action) {
        byte[] bytes = Encoding.UTF8.GetBytes(action + '\0');
        fixed (byte* p = bytes)
            return s_table->InputAction_WasActivated((sbyte*)p);
    }
    internal static int SA_InputAction_WasDeactivated(string action) {
        byte[] bytes = Encoding.UTF8.GetBytes(action + '\0');
        fixed (byte* p = bytes)
            return s_table->InputAction_WasDeactivated((sbyte*)p);
    }

    // ── StaticMesh / MeshRenderer (Block 3, Issue #71) ───────────────────────

    private static string ReadUuidBuffer(byte[] buf) {
        int len = System.Array.IndexOf(buf, (byte)0);
        return Encoding.UTF8.GetString(buf, 0, len < 0 ? buf.Length : len);
    }

    internal static string SA_StaticMesh_GetAssetUUID(ulong id) {
        byte[] buf = new byte[64];
        fixed (byte* p = buf)
            s_table->StaticMesh_GetAssetUUID(id, (sbyte*)p, buf.Length);
        return ReadUuidBuffer(buf);
    }

    internal static int SA_MeshRenderer_GetSlotCount(ulong id)
        => s_table->MeshRenderer_GetSlotCount(id);

    internal static string SA_MeshRenderer_GetSlotUUID(ulong id, int slot) {
        byte[] buf = new byte[64];
        fixed (byte* p = buf)
            s_table->MeshRenderer_GetSlotUUID(id, slot, (sbyte*)p, buf.Length);
        return ReadUuidBuffer(buf);
    }

    internal static int SA_MeshRenderer_SetSlotUUID(ulong id, int slot, string uuid) {
        byte[] bytes = Encoding.UTF8.GetBytes(uuid + '\0');
        fixed (byte* p = bytes)
            return s_table->MeshRenderer_SetSlotUUID(id, slot, (sbyte*)p);
    }

    internal static int  SA_MeshRenderer_GetCastShadow   (ulong id) => s_table->MeshRenderer_GetCastShadow(id);
    internal static void SA_MeshRenderer_SetCastShadow   (ulong id, int v) => s_table->MeshRenderer_SetCastShadow(id, v);
    internal static int  SA_MeshRenderer_GetReceiveShadow(ulong id) => s_table->MeshRenderer_GetReceiveShadow(id);
    internal static void SA_MeshRenderer_SetReceiveShadow(ulong id, int v) => s_table->MeshRenderer_SetReceiveShadow(id, v);

    // ── MaterialOverride (Block 3, Issue #71) ────────────────────────────────

    internal static float SA_MaterialOverride_GetFloat(ulong id, string param) {
        byte[] bytes = Encoding.UTF8.GetBytes(param + '\0');
        fixed (byte* p = bytes)
            return s_table->MaterialOverride_GetFloat(id, (sbyte*)p);
    }
    internal static void SA_MaterialOverride_SetFloat(ulong id, string param, float value) {
        byte[] bytes = Encoding.UTF8.GetBytes(param + '\0');
        fixed (byte* p = bytes)
            s_table->MaterialOverride_SetFloat(id, (sbyte*)p, value);
    }
    internal static void SA_MaterialOverride_GetVec3(ulong id, string param, out float x, out float y, out float z) {
        float lx = 0f, ly = 0f, lz = 0f;
        byte[] bytes = Encoding.UTF8.GetBytes(param + '\0');
        fixed (byte* p = bytes)
            s_table->MaterialOverride_GetVec3(id, (sbyte*)p, &lx, &ly, &lz);
        x = lx; y = ly; z = lz;
    }
    internal static void SA_MaterialOverride_SetVec3(ulong id, string param, float x, float y, float z) {
        byte[] bytes = Encoding.UTF8.GetBytes(param + '\0');
        fixed (byte* p = bytes)
            s_table->MaterialOverride_SetVec3(id, (sbyte*)p, x, y, z);
    }
    internal static void SA_MaterialOverride_GetVec4(ulong id, string param, out float x, out float y, out float z, out float w) {
        float lx = 0f, ly = 0f, lz = 0f, lw = 0f;
        byte[] bytes = Encoding.UTF8.GetBytes(param + '\0');
        fixed (byte* p = bytes)
            s_table->MaterialOverride_GetVec4(id, (sbyte*)p, &lx, &ly, &lz, &lw);
        x = lx; y = ly; z = lz; w = lw;
    }
    internal static void SA_MaterialOverride_SetVec4(ulong id, string param, float x, float y, float z, float w) {
        byte[] bytes = Encoding.UTF8.GetBytes(param + '\0');
        fixed (byte* p = bytes)
            s_table->MaterialOverride_SetVec4(id, (sbyte*)p, x, y, z, w);
    }

    // ── RigidBody diagnostics (Block 3 v4, Issue #71) ────────────────────────

    internal static int SA_RigidBody_HasComponent(ulong id) => s_table->RigidBody_HasComponent(id);
    internal static int SA_RigidBody_GetType     (ulong id) => s_table->RigidBody_GetType(id);

    // ── InputMap (Block 3 v5, Issue #71 Phase 3a) ────────────────────────────

    internal static int SA_InputMap_Push(string name) {
        byte[] bytes = Encoding.UTF8.GetBytes(name + '\0');
        fixed (byte* p = bytes)
            return s_table->InputMap_Push((sbyte*)p);
    }
    internal static void SA_InputMap_Pop() => s_table->InputMap_Pop();
    internal static int  SA_InputMap_Replace(string name) {
        byte[] bytes = Encoding.UTF8.GetBytes(name + '\0');
        fixed (byte* p = bytes)
            return s_table->InputMap_Replace((sbyte*)p);
    }
    internal static int  SA_InputMap_IsActive(string name) {
        byte[] bytes = Encoding.UTF8.GetBytes(name + '\0');
        fixed (byte* p = bytes)
            return s_table->InputMap_IsActive((sbyte*)p);
    }
    internal static string SA_InputMap_GetActive() {
        byte[] buf = new byte[64];
        fixed (byte* p = buf)
            s_table->InputMap_GetActive((sbyte*)p, buf.Length);
        int len = System.Array.IndexOf(buf, (byte)0);
        return Encoding.UTF8.GetString(buf, 0, len < 0 ? buf.Length : len);
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

    // ── PostProcess (v7, Issue #47) ──────────────────────────────────────────

    internal static int   SA_PostProcess_GetVignetteEnabled()        => s_table->PostProcess_GetVignetteEnabled();
    internal static void  SA_PostProcess_SetVignetteEnabled(int v)   => s_table->PostProcess_SetVignetteEnabled(v);
    internal static float SA_PostProcess_GetVignetteIntensity()      => s_table->PostProcess_GetVignetteIntensity();
    internal static void  SA_PostProcess_SetVignetteIntensity(float v)=> s_table->PostProcess_SetVignetteIntensity(v);
    internal static float SA_PostProcess_GetVignetteSmoothness()     => s_table->PostProcess_GetVignetteSmoothness();
    internal static void  SA_PostProcess_SetVignetteSmoothness(float v)=> s_table->PostProcess_SetVignetteSmoothness(v);
    internal static int   SA_PostProcess_GetCAEnabled()              => s_table->PostProcess_GetCAEnabled();
    internal static void  SA_PostProcess_SetCAEnabled(int v)         => s_table->PostProcess_SetCAEnabled(v);
    internal static float SA_PostProcess_GetCAStrength()             => s_table->PostProcess_GetCAStrength();
    internal static void  SA_PostProcess_SetCAStrength(float v)      => s_table->PostProcess_SetCAStrength(v);
    internal static int   SA_PostProcess_GetFilmGrainEnabled()       => s_table->PostProcess_GetFilmGrainEnabled();
    internal static void  SA_PostProcess_SetFilmGrainEnabled(int v)  => s_table->PostProcess_SetFilmGrainEnabled(v);
    internal static float SA_PostProcess_GetFilmGrainIntensity()     => s_table->PostProcess_GetFilmGrainIntensity();
    internal static void  SA_PostProcess_SetFilmGrainIntensity(float v)=> s_table->PostProcess_SetFilmGrainIntensity(v);
    internal static float SA_PostProcess_GetFilmGrainSize()          => s_table->PostProcess_GetFilmGrainSize();
    internal static void  SA_PostProcess_SetFilmGrainSize(float v)   => s_table->PostProcess_SetFilmGrainSize(v);
}

// ── ScriptApiFunctionTable — mirrors ScriptApiExports.hpp ────────────────────
// IMPORTANT: field order must exactly match ScriptApiFunctionTable in C++.

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ScriptApiFunctionTable
{
    public uint Version;
    // Entity — local transform (parent-relative)
    public delegate*unmanaged<ulong, float*, float*, float*, void>            Entity_GetLocalPosition;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            Entity_SetLocalPosition;
    public delegate*unmanaged<ulong, float*, float*, float*, void>            Entity_GetLocalRotationEuler;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            Entity_SetLocalRotationEuler;
    public delegate*unmanaged<ulong, float*, float*, float*, float*, void>    Entity_GetLocalRotationQuat;
    public delegate*unmanaged<ulong, float,  float,  float,  float,  void>    Entity_SetLocalRotationQuat;
    public delegate*unmanaged<ulong, float*, float*, float*, void>            Entity_GetLocalScale;
    public delegate*unmanaged<ulong, float,  float,  float,  void>            Entity_SetLocalScale;
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

    // ── Block 3 — v3 (Issue #71) ─────────────────────────────────────────────
    // InputAction
    public delegate*unmanaged<sbyte*, float>                                  InputAction_ReadFloat;
    public delegate*unmanaged<sbyte*, float*, float*, void>                   InputAction_ReadVec2;
    public delegate*unmanaged<sbyte*, int>                                    InputAction_IsActive;
    public delegate*unmanaged<sbyte*, int>                                    InputAction_WasActivated;
    public delegate*unmanaged<sbyte*, int>                                    InputAction_WasDeactivated;
    // StaticMesh / MeshRenderer
    public delegate*unmanaged<ulong, sbyte*, int, void>                       StaticMesh_GetAssetUUID;
    public delegate*unmanaged<ulong, int>                                     MeshRenderer_GetSlotCount;
    public delegate*unmanaged<ulong, int, sbyte*, int, void>                  MeshRenderer_GetSlotUUID;
    public delegate*unmanaged<ulong, int, sbyte*, int>                        MeshRenderer_SetSlotUUID;
    public delegate*unmanaged<ulong, int>                                     MeshRenderer_GetCastShadow;
    public delegate*unmanaged<ulong, int, void>                               MeshRenderer_SetCastShadow;
    public delegate*unmanaged<ulong, int>                                     MeshRenderer_GetReceiveShadow;
    public delegate*unmanaged<ulong, int, void>                               MeshRenderer_SetReceiveShadow;
    // MaterialOverride
    public delegate*unmanaged<ulong, sbyte*, float>                           MaterialOverride_GetFloat;
    public delegate*unmanaged<ulong, sbyte*, float, void>                     MaterialOverride_SetFloat;
    public delegate*unmanaged<ulong, sbyte*, float*, float*, float*, void>    MaterialOverride_GetVec3;
    public delegate*unmanaged<ulong, sbyte*, float,  float,  float,  void>    MaterialOverride_SetVec3;
    public delegate*unmanaged<ulong, sbyte*, float*, float*, float*, float*, void> MaterialOverride_GetVec4;
    public delegate*unmanaged<ulong, sbyte*, float,  float,  float,  float,  void> MaterialOverride_SetVec4;
    // RigidBody diagnostics (v4)
    public delegate*unmanaged<ulong, int>                                          RigidBody_HasComponent;
    public delegate*unmanaged<ulong, int>                                          RigidBody_GetType;
    // InputMap stack control (v5)
    public delegate*unmanaged<sbyte*, int>                                         InputMap_Push;
    public delegate*unmanaged<void>                                                InputMap_Pop;
    public delegate*unmanaged<sbyte*, int>                                         InputMap_Replace;
    public delegate*unmanaged<sbyte*, int>                                         InputMap_IsActive;
    public delegate*unmanaged<sbyte*, int, void>                                   InputMap_GetActive;

    // World-space transform accessors (v6, Issue #81)
    public delegate*unmanaged<ulong, float*, float*, float*, void>                 Entity_GetWorldPosition;
    public delegate*unmanaged<ulong, float,  float,  float,  void>                 Entity_SetWorldPosition;
    public delegate*unmanaged<ulong, float*, float*, float*, float*, void>         Entity_GetWorldRotationQuat;
    public delegate*unmanaged<ulong, float,  float,  float,  float,  void>         Entity_SetWorldRotationQuat;
    public delegate*unmanaged<ulong, float*, float*, float*, void>                 Entity_GetLossyWorldScale;
    public delegate*unmanaged<ulong, float*, void>                                 Entity_GetWorldMatrix;

    // ── PostProcess — screen modifications (v7, Issue #47) ───────────────────
    public delegate*unmanaged<int>                                                 PostProcess_GetVignetteEnabled;
    public delegate*unmanaged<int, void>                                           PostProcess_SetVignetteEnabled;
    public delegate*unmanaged<float>                                               PostProcess_GetVignetteIntensity;
    public delegate*unmanaged<float, void>                                         PostProcess_SetVignetteIntensity;
    public delegate*unmanaged<float>                                               PostProcess_GetVignetteSmoothness;
    public delegate*unmanaged<float, void>                                         PostProcess_SetVignetteSmoothness;
    public delegate*unmanaged<int>                                                 PostProcess_GetCAEnabled;
    public delegate*unmanaged<int, void>                                           PostProcess_SetCAEnabled;
    public delegate*unmanaged<float>                                               PostProcess_GetCAStrength;
    public delegate*unmanaged<float, void>                                         PostProcess_SetCAStrength;
    public delegate*unmanaged<int>                                                 PostProcess_GetFilmGrainEnabled;
    public delegate*unmanaged<int, void>                                           PostProcess_SetFilmGrainEnabled;
    public delegate*unmanaged<float>                                               PostProcess_GetFilmGrainIntensity;
    public delegate*unmanaged<float, void>                                         PostProcess_SetFilmGrainIntensity;
    public delegate*unmanaged<float>                                               PostProcess_GetFilmGrainSize;
    public delegate*unmanaged<float, void>                                         PostProcess_SetFilmGrainSize;

    // ── Animator clip control (v8, Issue #83 P2-5) ───────────────────────────
    public delegate*unmanaged<ulong, sbyte*, void>                                 Animator_SetClip;
    public delegate*unmanaged<ulong, sbyte*, float, void>                          Animator_CrossfadeTo;
}
