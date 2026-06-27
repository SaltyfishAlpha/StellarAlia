#pragma once

#include <cstdint>

namespace StellarAlia {
class Scene;
class SceneRenderer;
class InputSystem;
class DebugDraw;
class PhysicsSystem;
}

// ── Internal C++ context ──────────────────────────────────────────────────────

namespace StellarAlia {

struct ScriptApiContext {
    Scene*         scene    = nullptr;
    SceneRenderer* renderer = nullptr;  // used by PostProcess.* setters to live-apply ws.pp changes
    InputSystem*   input    = nullptr;
    DebugDraw*     debug    = nullptr;
    PhysicsSystem* physics  = nullptr;
    float          dt        = 0.f;
    float          totalTime = 0.f;
};

void SA_Script_SetContext(const ScriptApiContext& ctx);
void SA_Script_SetTime(float dt, float totalTime);

// Function pointer table passed to the managed bridge at init time.
// Avoids requiring StellarAliaRuntime to be a SHARED library.
// IMPORTANT: `version` must stay first; increment when adding/removing fields.
struct ScriptApiFunctionTable {
    uint32_t version = 7;
    // Entity — transform (local, parent-relative)
    void    (*Entity_GetLocalPosition)     (uint64_t, float*, float*, float*);
    void    (*Entity_SetLocalPosition)     (uint64_t, float,  float,  float);
    void    (*Entity_GetLocalRotationEuler)(uint64_t, float*, float*, float*);
    void    (*Entity_SetLocalRotationEuler)(uint64_t, float,  float,  float);
    void    (*Entity_GetLocalRotationQuat) (uint64_t, float*, float*, float*, float*);  // w,x,y,z out
    void    (*Entity_SetLocalRotationQuat) (uint64_t, float,  float,  float,  float);   // w,x,y,z in
    void    (*Entity_GetLocalScale)        (uint64_t, float*, float*, float*);
    void    (*Entity_SetLocalScale)        (uint64_t, float,  float,  float);
    // Entity — lifecycle
    void    (*Entity_Destroy)(uint64_t);
    uint64_t(*Entity_Create) ();
    // Entity — identity
    void    (*Entity_GetName)   (uint64_t, char*, int32_t);
    int32_t (*Entity_FindByName)(const char*, uint64_t*);
    int32_t (*Entity_FindChild) (uint64_t, const char*, uint64_t*);
    int32_t (*Entity_IsValid)   (uint64_t);
    // Animator
    int32_t (*Animator_IsPlaying) (uint64_t);
    void    (*Animator_SetPlaying)(uint64_t, int32_t);
    float   (*Animator_GetSpeed)  (uint64_t);
    void    (*Animator_SetSpeed)  (uint64_t, float);
    // RigidBody
    void  (*RigidBody_GetLinearVelocity) (uint64_t, float*, float*, float*);
    void  (*RigidBody_SetLinearVelocity) (uint64_t, float,  float,  float);
    void  (*RigidBody_GetAngularVelocity)(uint64_t, float*, float*, float*);
    void  (*RigidBody_SetAngularVelocity)(uint64_t, float,  float,  float);
    void  (*RigidBody_AddForce)          (uint64_t, float,  float,  float);
    void  (*RigidBody_AddImpulse)        (uint64_t, float,  float,  float);
    // PointLight
    void  (*PointLight_GetColor)    (uint64_t, float*, float*, float*);
    void  (*PointLight_SetColor)    (uint64_t, float,  float,  float);
    float (*PointLight_GetIntensity)(uint64_t);
    void  (*PointLight_SetIntensity)(uint64_t, float);
    float (*PointLight_GetRange)    (uint64_t);
    void  (*PointLight_SetRange)    (uint64_t, float);
    // Physics
    int32_t (*Physics_Raycast)(float, float, float, float, float, float, float,
                               float*, float*, float*, float*, float*, float*, uint64_t*);
    // Input
    float (*Input_GetKey)   (const char*);
    void  (*Input_GetAxis2D)(const char*, float*, float*);
    // Debug
    void  (*Debug_DrawLine)(float,float,float, float,float,float, float,float,float,float);
    // Logging
    void  (*Log_Info) (const char*);
    void  (*Log_Warn) (const char*);
    void  (*Log_Error)(const char*);
    // Time
    float (*Time_GetDeltaTime)();
    float (*Time_GetTotalTime)();

    // ── Block 3 — v3 (Issue #71) ──────────────────────────────────────────────
    // InputAction — named-action query (replaces raw-device Input for game scripts)
    float   (*InputAction_ReadFloat)      (const char* action);
    void    (*InputAction_ReadVec2)       (const char* action, float*, float*);
    int32_t (*InputAction_IsActive)       (const char* action);
    int32_t (*InputAction_WasActivated)   (const char* action);
    int32_t (*InputAction_WasDeactivated) (const char* action);
    // StaticMesh — read-only
    void    (*StaticMesh_GetAssetUUID)    (uint64_t entity, char* buf, int32_t bufLen);
    // MeshRenderer
    int32_t (*MeshRenderer_GetSlotCount)    (uint64_t entity);
    void    (*MeshRenderer_GetSlotUUID)     (uint64_t entity, int32_t slot, char* buf, int32_t bufLen);
    int32_t (*MeshRenderer_SetSlotUUID)     (uint64_t entity, int32_t slot, const char* uuid);
    int32_t (*MeshRenderer_GetCastShadow)   (uint64_t entity);
    void    (*MeshRenderer_SetCastShadow)   (uint64_t entity, int32_t value);
    int32_t (*MeshRenderer_GetReceiveShadow)(uint64_t entity);
    void    (*MeshRenderer_SetReceiveShadow)(uint64_t entity, int32_t value);
    // MaterialOverride — named-parameter scalar/vec3/vec4 read/write
    float (*MaterialOverride_GetFloat)(uint64_t entity, const char* param);
    void  (*MaterialOverride_SetFloat)(uint64_t entity, const char* param, float value);
    void  (*MaterialOverride_GetVec3) (uint64_t entity, const char* param, float*, float*, float*);
    void  (*MaterialOverride_SetVec3) (uint64_t entity, const char* param, float, float, float);
    void  (*MaterialOverride_GetVec4) (uint64_t entity, const char* param, float*, float*, float*, float*);
    void  (*MaterialOverride_SetVec4) (uint64_t entity, const char* param, float, float, float, float);
    // ── v4 — RigidBody diagnostics (Issue #71 demo support) ───────────────────
    int32_t (*RigidBody_HasComponent)(uint64_t entity);
    int32_t (*RigidBody_GetType)     (uint64_t entity);  // 0=Static, 1=Kinematic, 2=Dynamic, -1=missing

    // ── v5 — InputMap stack control (Issue #71 Phase 3a) ──────────────────────
    int32_t (*InputMap_Push)     (const char* name);     // 1=ok, 0=unknown
    void    (*InputMap_Pop)      ();
    int32_t (*InputMap_Replace)  (const char* name);     // 1=ok, 0=unknown
    int32_t (*InputMap_IsActive) (const char* name);     // 1=on stack, 0=not
    void    (*InputMap_GetActive)(char* buf, int32_t bufLen);  // empty buf when stack empty

    // ── v6 — World-space transform accessors (Issue #81) ──────────────────────
    // Readers call Scene::EnsureWorldUpToDate(entity) before reading; setters
    // refresh the parent chain, convert world→local via parent inverse, and
    // MarkDirty self. LossyScale is read-only (Unity convention).
    void (*Entity_GetWorldPosition)    (uint64_t, float*, float*, float*);
    void (*Entity_SetWorldPosition)    (uint64_t, float,  float,  float);
    void (*Entity_GetWorldRotationQuat)(uint64_t, float*, float*, float*, float*); // w,x,y,z out
    void (*Entity_SetWorldRotationQuat)(uint64_t, float,  float,  float,  float);  // w,x,y,z in
    void (*Entity_GetLossyWorldScale)  (uint64_t, float*, float*, float*);
    void (*Entity_GetWorldMatrix)      (uint64_t, float* /*out16, glm column-major*/);

    // ── v7 — PostProcess screen modifications (Issue #47) ─────────────────────
    // Mutates the current scene's WorldSettings::pp and live-applies via the
    // bound SceneRenderer. Bool flags use int32_t (0/1) for ABI clarity.
    int32_t (*PostProcess_GetVignetteEnabled)    ();
    void    (*PostProcess_SetVignetteEnabled)    (int32_t v);
    float   (*PostProcess_GetVignetteIntensity)  ();
    void    (*PostProcess_SetVignetteIntensity)  (float v);
    float   (*PostProcess_GetVignetteSmoothness) ();
    void    (*PostProcess_SetVignetteSmoothness) (float v);
    int32_t (*PostProcess_GetCAEnabled)          ();
    void    (*PostProcess_SetCAEnabled)          (int32_t v);
    float   (*PostProcess_GetCAStrength)         ();
    void    (*PostProcess_SetCAStrength)         (float v);
    int32_t (*PostProcess_GetFilmGrainEnabled)   ();
    void    (*PostProcess_SetFilmGrainEnabled)   (int32_t v);
    float   (*PostProcess_GetFilmGrainIntensity) ();
    void    (*PostProcess_SetFilmGrainIntensity) (float v);
    float   (*PostProcess_GetFilmGrainSize)      ();
    void    (*PostProcess_SetFilmGrainSize)      (float v);
};

ScriptApiFunctionTable SA_Script_BuildFunctionTable();

} // namespace StellarAlia

// ── Flat C API — called through ScriptApiFunctionTable by managed code ────────

extern "C" {

// Entity — transform (local, parent-relative)
void SA_Entity_GetLocalPosition     (uint64_t id, float* x, float* y, float* z);
void SA_Entity_SetLocalPosition     (uint64_t id, float  x, float  y, float  z);
void SA_Entity_GetLocalRotationEuler(uint64_t id, float* x, float* y, float* z);  // degrees
void SA_Entity_SetLocalRotationEuler(uint64_t id, float  x, float  y, float  z);  // degrees
void SA_Entity_GetLocalRotationQuat (uint64_t id, float* w, float* x, float* y, float* z);
void SA_Entity_SetLocalRotationQuat (uint64_t id, float  w, float  x, float  y, float  z);
void SA_Entity_GetLocalScale        (uint64_t id, float* x, float* y, float* z);
void SA_Entity_SetLocalScale        (uint64_t id, float  x, float  y, float  z);

// Entity — transform (world-space; lazy-refreshed via Scene::EnsureWorldUpToDate)
void SA_Entity_GetWorldPosition     (uint64_t id, float* x, float* y, float* z);
void SA_Entity_SetWorldPosition     (uint64_t id, float  x, float  y, float  z);
void SA_Entity_GetWorldRotationQuat (uint64_t id, float* w, float* x, float* y, float* z);
void SA_Entity_SetWorldRotationQuat (uint64_t id, float  w, float  x, float  y, float  z);
void SA_Entity_GetLossyWorldScale   (uint64_t id, float* x, float* y, float* z);
void SA_Entity_GetWorldMatrix       (uint64_t id, float* out16);  // glm column-major

// Entity — lifecycle
void     SA_Entity_Destroy(uint64_t id);
uint64_t SA_Entity_Create ();

// Entity — identity
void    SA_Entity_GetName    (uint64_t id, char* buf, int32_t bufLen);
int32_t SA_Entity_FindByName (const char* name, uint64_t* outId);
int32_t SA_Entity_FindChild  (uint64_t id, const char* childName, uint64_t* outId);
int32_t SA_Entity_IsValid    (uint64_t id);

// RigidBody
void SA_RigidBody_GetLinearVelocity (uint64_t id, float* x, float* y, float* z);
void SA_RigidBody_SetLinearVelocity (uint64_t id, float  x, float  y, float  z);
void SA_RigidBody_GetAngularVelocity(uint64_t id, float* x, float* y, float* z);
void SA_RigidBody_SetAngularVelocity(uint64_t id, float  x, float  y, float  z);
void SA_RigidBody_AddForce          (uint64_t id, float  x, float  y, float  z);
void SA_RigidBody_AddImpulse        (uint64_t id, float  x, float  y, float  z);

// PointLight
void  SA_PointLight_GetColor    (uint64_t id, float* r, float* g, float* b);
void  SA_PointLight_SetColor    (uint64_t id, float  r, float  g, float  b);
float SA_PointLight_GetIntensity(uint64_t id);
void  SA_PointLight_SetIntensity(uint64_t id, float intensity);
float SA_PointLight_GetRange    (uint64_t id);
void  SA_PointLight_SetRange    (uint64_t id, float range);

// Physics
// Returns 1 if hit, 0 otherwise. Outputs hit point, normal, and entity id.
int32_t SA_Physics_Raycast(float ox, float oy, float oz,
                           float dx, float dy, float dz, float maxDist,
                           float* hitX, float* hitY, float* hitZ,
                           float* nrmX, float* nrmY, float* nrmZ,
                           uint64_t* hitEntity);

// Animator
int32_t SA_Animator_IsPlaying (uint64_t id);
void    SA_Animator_SetPlaying(uint64_t id, int32_t playing);
float   SA_Animator_GetSpeed  (uint64_t id);
void    SA_Animator_SetSpeed  (uint64_t id, float speed);

// Input
float SA_Input_GetKey   (const char* devicePath);
void  SA_Input_GetAxis2D(const char* devicePath, float* x, float* y);

// InputAction — named-action query (Block 3, Issue #71)
float   SA_InputAction_ReadFloat      (const char* action);
void    SA_InputAction_ReadVec2       (const char* action, float* x, float* y);
int32_t SA_InputAction_IsActive       (const char* action);
int32_t SA_InputAction_WasActivated   (const char* action);
int32_t SA_InputAction_WasDeactivated (const char* action);

// StaticMesh / MeshRenderer (Block 3, Issue #71)
void    SA_StaticMesh_GetAssetUUID     (uint64_t id, char* buf, int32_t bufLen);
int32_t SA_MeshRenderer_GetSlotCount   (uint64_t id);
void    SA_MeshRenderer_GetSlotUUID    (uint64_t id, int32_t slot, char* buf, int32_t bufLen);
int32_t SA_MeshRenderer_SetSlotUUID    (uint64_t id, int32_t slot, const char* uuid);
int32_t SA_MeshRenderer_GetCastShadow  (uint64_t id);
void    SA_MeshRenderer_SetCastShadow  (uint64_t id, int32_t value);
int32_t SA_MeshRenderer_GetReceiveShadow(uint64_t id);
void    SA_MeshRenderer_SetReceiveShadow(uint64_t id, int32_t value);

// MaterialOverride (Block 3, Issue #71)
float SA_MaterialOverride_GetFloat(uint64_t id, const char* param);
void  SA_MaterialOverride_SetFloat(uint64_t id, const char* param, float value);
void  SA_MaterialOverride_GetVec3 (uint64_t id, const char* param, float* x, float* y, float* z);
void  SA_MaterialOverride_SetVec3 (uint64_t id, const char* param, float x, float y, float z);
void  SA_MaterialOverride_GetVec4 (uint64_t id, const char* param, float* x, float* y, float* z, float* w);
void  SA_MaterialOverride_SetVec4 (uint64_t id, const char* param, float x, float y, float z, float w);

// RigidBody diagnostics (Block 3 v4, Issue #71)
int32_t SA_RigidBody_HasComponent(uint64_t id);
int32_t SA_RigidBody_GetType     (uint64_t id);  // 0=Static, 1=Kinematic, 2=Dynamic, -1=missing

// InputMap stack control (Block 3 v5, Issue #71 Phase 3a)
int32_t SA_InputMap_Push     (const char* name);
void    SA_InputMap_Pop      ();
int32_t SA_InputMap_Replace  (const char* name);
int32_t SA_InputMap_IsActive (const char* name);
void    SA_InputMap_GetActive(char* buf, int32_t bufLen);

// Debug draw
void SA_Debug_DrawLine(float x0, float y0, float z0,
                       float x1, float y1, float z1,
                       float r,  float g,  float b,  float a);

// Logging
void SA_Log_Info (const char* msg);
void SA_Log_Warn (const char* msg);
void SA_Log_Error(const char* msg);

// Time
float SA_Time_GetDeltaTime();
float SA_Time_GetTotalTime();

// PostProcess — screen modifications (Issue #47)
int32_t SA_PostProcess_GetVignetteEnabled    ();
void    SA_PostProcess_SetVignetteEnabled    (int32_t v);
float   SA_PostProcess_GetVignetteIntensity  ();
void    SA_PostProcess_SetVignetteIntensity  (float v);
float   SA_PostProcess_GetVignetteSmoothness ();
void    SA_PostProcess_SetVignetteSmoothness (float v);
int32_t SA_PostProcess_GetCAEnabled          ();
void    SA_PostProcess_SetCAEnabled          (int32_t v);
float   SA_PostProcess_GetCAStrength         ();
void    SA_PostProcess_SetCAStrength         (float v);
int32_t SA_PostProcess_GetFilmGrainEnabled   ();
void    SA_PostProcess_SetFilmGrainEnabled   (int32_t v);
float   SA_PostProcess_GetFilmGrainIntensity ();
void    SA_PostProcess_SetFilmGrainIntensity (float v);
float   SA_PostProcess_GetFilmGrainSize      ();
void    SA_PostProcess_SetFilmGrainSize      (float v);

} // extern "C"
