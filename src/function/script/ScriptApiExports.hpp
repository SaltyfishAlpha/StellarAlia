#pragma once

#include <cstdint>

namespace StellarAlia {
class Scene;
class InputSystem;
class DebugDraw;
class PhysicsSystem;
}

// ── Internal C++ context ──────────────────────────────────────────────────────

namespace StellarAlia {

struct ScriptApiContext {
    Scene*         scene   = nullptr;
    InputSystem*   input   = nullptr;
    DebugDraw*     debug   = nullptr;
    PhysicsSystem* physics = nullptr;
    float          dt      = 0.f;
    float          totalTime = 0.f;
};

void SA_Script_SetContext(const ScriptApiContext& ctx);
void SA_Script_SetTime(float dt, float totalTime);

// Function pointer table passed to the managed bridge at init time.
// Avoids requiring StellarAliaRuntime to be a SHARED library.
// IMPORTANT: `version` must stay first; increment when adding/removing fields.
struct ScriptApiFunctionTable {
    uint32_t version = 2;
    // Entity — transform
    void    (*Entity_GetPosition)     (uint64_t, float*, float*, float*);
    void    (*Entity_SetPosition)     (uint64_t, float,  float,  float);
    void    (*Entity_GetRotationEuler)(uint64_t, float*, float*, float*);
    void    (*Entity_SetRotationEuler)(uint64_t, float,  float,  float);
    void    (*Entity_GetRotationQuat) (uint64_t, float*, float*, float*, float*);  // w,x,y,z out
    void    (*Entity_SetRotationQuat) (uint64_t, float,  float,  float,  float);   // w,x,y,z in
    void    (*Entity_GetScale)        (uint64_t, float*, float*, float*);
    void    (*Entity_SetScale)        (uint64_t, float,  float,  float);
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
};

ScriptApiFunctionTable SA_Script_BuildFunctionTable();

} // namespace StellarAlia

// ── Flat C API — called through ScriptApiFunctionTable by managed code ────────

extern "C" {

// Entity — transform
void SA_Entity_GetPosition     (uint64_t id, float* x, float* y, float* z);
void SA_Entity_SetPosition     (uint64_t id, float  x, float  y, float  z);
void SA_Entity_GetRotationEuler(uint64_t id, float* x, float* y, float* z);  // degrees
void SA_Entity_SetRotationEuler(uint64_t id, float  x, float  y, float  z);  // degrees
void SA_Entity_GetRotationQuat (uint64_t id, float* w, float* x, float* y, float* z);
void SA_Entity_SetRotationQuat (uint64_t id, float  w, float  x, float  y, float  z);
void SA_Entity_GetScale        (uint64_t id, float* x, float* y, float* z);
void SA_Entity_SetScale        (uint64_t id, float  x, float  y, float  z);

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

} // extern "C"
