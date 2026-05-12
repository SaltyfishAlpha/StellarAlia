# StellarAlia — Architecture Overview

## Table of Contents
1. [Layer Model](#layer-model)
2. [AppMode Architecture](#appmode-architecture)
3. [Project Structure](#project-structure)
4. [Build-Time Pipeline (Shaders → Assets)](#build-time-pipeline)
5. [Offline Asset Pipeline (Cook)](#offline-asset-pipeline-cook)
6. [ECS — Scene & Components](#ecs--scene--components)
7. [Animation System](#animation-system)
8. [Physics System](#physics-system)
9. [Scripting System](#scripting-system)
10. [Input System](#input-system)
11. [Platform Layer — RHI](#platform-layer--rhi)
12. [Resource Layer — Material System](#resource-layer--material-system)
13. [Function Layer — RenderGraph](#function-layer--rendergraph)
14. [Compute Pipeline & ComputeProgram](#compute-pipeline--computeprogram)
15. [GPU IBL Bake (Runtime)](#gpu-ibl-bake-runtime)
16. [Deferred Rendering Pipeline](#deferred-rendering-pipeline)
17. [CPU Frustum Culling & BVH](#cpu-frustum-culling--bvh)
18. [Custom Shading Models](#custom-shading-models)
19. [Frame Loop](#frame-loop)
20. [Editor Architecture](#editor-architecture)
21. [Key Design Decisions](#key-design-decisions)
22. [Profiler](#profiler)
23. [GPU Performance Notes](#gpu-performance-notes)

---

## Layer Model

```
┌─────────────────────────────────────────────────────────────────┐
│  Editor Layer (editor/)                                          │
│                                                                  │
│  EditorMode : AppMode                                            │
│    EditorCamera, EditorUI (ImGui), EditorOverlaySettings        │
│    Panels: Hierarchy, Inspector, Assets, Console, Playback,     │
│            Settings, Performance, WorldSettings, PostProcess,   │
│            Shortcuts                                             │
│    EntityTemplateRegistry — data-driven spawn templates          │
│    AssetsPanel — native file picker (nfd-extended), import       │
│    ProjectManager — create/open/recent projects                  │
│    ProjectBrowserPanel — startup modal (create/open/recents)     │
│    EditorShortcutConfig — JSON-backed user shortcut overrides    │
│    ImGuizmo — interactive transform gizmo                        │
│    DoubleClickClassifier — header-only short/long hold classifier │
├─────────────────────────────────────────────────────────────────┤
│  Function Layer                                                  │
│                                                                  │
│  Application                                                     │
│    Owns: Scene, SceneRenderer, InputSystem, AnimationSystem,    │
│          PhysicsSystem, DebugDraw, ResourceManager,             │
│          MaterialManager, AssetRegistry, AppMode                │
│                                                                  │
│  SceneRenderer                                                   │
│    Owns: FrameUniformsBuffer, GpuIblBake, depth texture,        │
│          all RenderFeatures, DrawItem list, default material     │
│    Init(Desc{device, matMgr, resMgr, shaderDir, cookCacheDir})  │
│    RenderFrame(scene, w, h): BeginFrame → Upload → AddPasses    │
│                 → RG.Compile/Execute → EndFrame → Present       │
│    AddPass(name, PassFlags, execFn): RG pass primitive           │
│    AddFeature(unique_ptr<RenderFeature>): OnInit called immediately
│    ApplyWorldSettings(ws, updateIBL=true)                       │
│    GetRenderGraph() const → const RenderGraph&                  │
│    BuildDrawList(scene): ECS → DrawItem list                    │
│                                                                  │
│  RenderFeature — extension point for custom shaders             │
│    OnInit(FeatureInitContext&)                                   │
│      ctx.matMgr->RegisterTypeFromShaders(MaterialTypeDesc, ctx) │
│    AddPasses(renderer, reg, w, h) → renderer.AddPass(...)       │
│                                                                  │
│  Scene / SceneSerializer                                         │
│    UpdateTransforms(): BFS topo-sort → world matrices            │
│    SetParent / CreateEntity / DestroyEntity                      │
│    GetRootOrder() → user-ordered root list; MoveRootBefore/After │
│    View<C...>() → EnTT view wrapper                             │
│    SceneSerializer::SpawnFromTemplate() — entity template spawn  │
│                                                                  │
│  RenderGraph                                                     │
│    Reset / CreateTexture / ImportTexture / AddPass              │
│    CreateBuffer(name, RGBufferDesc) / ImportBuffer              │
│    Compile / Execute / GetLastFrameStats() / GetLastMemoryStats()│
│    Topological sort + greedy interval slot aliasing             │
│    RGPhysicalSlot / RGPhysicalBufferSlot — persistent handles   │
│                                                                  │
│  FrameUniformsBuffer (owned by SceneRenderer)                   │
│    set=0: per-frame camera + light + IBL data                   │
│    Manages double-buffered GPU UBOs + descriptor sets           │
├─────────────────────────────────────────────────────────────────┤
│  Resource Layer                                                  │
│                                                                  │
│  MaterialManager → MaterialType → MaterialInstance              │
│    Init(device, ResourceManager*)                               │
│    RegisterTypeFromShaders(MaterialTypeDesc, FeatureInitContext) │
│  ShaderProgram: vert+frag SPIRV + reflection + pipeline cache   │
│  ComputeProgram: compute SPIRV + per-set descriptor layouts     │
│  ResourceManager: LoadMesh / LoadTexture / GetBuiltin()         │
│    Init(engineCookCacheDir, device) → VFS::SetEngineCookCacheDir│
│    SetProjectCookCache(dir)         → VFS::SetCookCacheDir      │
│    GetBuiltin(BuiltinTexture::White1x1) → 1×1 white placeholder │
│  AssetRegistry: scans .sameta → UUID/path/type index            │
│    FindByID / ResolveID / EntriesByType                         │
│  VFS (dual-path):                                                │
│    SetEngineCookCacheDir(dir) — fixed at startup                 │
│    SetCookCacheDir(dir)       — updated on project switch        │
│    ResolveCookedPath(id, ext) — checks project first, then engine│
│    GetCookCacheDir()          — returns project dir or engine dir│
├─────────────────────────────────────────────────────────────────┤
│  Platform Layer — RHI                                            │
│                                                                  │
│  IRHIDevice (interface — no Vk* types exposed)                  │
│    CreateShader / CreateDescriptorSetLayout                      │
│    CreatePipeline / CreateComputePipeline                        │
│    AllocateDescriptorSet / WriteDescriptor*                      │
│    BeginFrame / EndFrame / Present / ImmediateCompute           │
│    UploadBufferData / ReadBufferData — CPU↔GPU buffer I/O       │
│    GetMemoryStats() → RHIMemoryStats                            │
│                                                                  │
│  VulkanDevice  (Vulkan 1.3, dynamic rendering)                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## AppMode Architecture

`AppMode` is an abstract interface that plugs a custom logic layer into `Application`.
The engine ships one implementation (`EditorMode`); game runtimes provide their own.

```cpp
class AppMode {
    virtual void OnAttach(Application& app) = 0;  // after all systems init
    virtual void OnDetach() = 0;                   // before systems destroy
    virtual void OnUpdate(float dt) = 0;           // per-frame logic + input
    virtual CameraData GetCameraData(float aspectRatio) const = 0;
    virtual void OnRenderUI(RHI::IRHICommandList* cmd) {}  // ImGui draw calls
    virtual void OnPlayStateChanged(EnginePlayState newState) {}
};
```

**Lifecycle (single active mode):**
```
Application::Init()     → mode.OnAttach(app)
Application::Run()      → loop: mode.OnUpdate(dt)
                                 renderer.RenderFrame(scene, w, h)
                                 mode.OnRenderUI(cmd)
Application::Shutdown() → mode.OnDetach()
```

**Play state machine** (`EnginePlayState`):
- `Editing` — overlays active, physics frozen, scene freely editable
- `Playing` — physics stepping, animation ticking, scripts running, overlays suppressed
- `Paused`  — physics frozen, animation frozen, scripts paused, scene inspectable

`Application` calls `mode.OnPlayStateChanged(newState)` immediately after the transition
so the mode can swap input maps, reset physics, etc.

**PIE dual-scene isolation** (`Application::SetPlayState`):
- `Editing → Playing`: `m_scene` snapshotted via `SceneSerializer::SerializeToJson` → `m_pieSnapshot` (compact JSON string); `m_gameScene` created and populated via `SceneSerializer::DeserializeFromJson`; `m_animSystem.Shutdown` clears editor-scene GPU entries (entity-ID collision avoidance); `PrepareAnimatedEntities` allocates GPU skinning buffers for the game copy; `ScriptSystem::OnPlayStart(*m_gameScene)` redirects `g_ctx.scene` to the game copy
- `GetActiveScene()` → `*m_gameScene` while Playing/Paused, `*m_scene` while Editing
- `GetEditorScene()` → always `*m_scene` (used by Save, scene load — unaffected by play state)
- `Playing → Editing`: `ScriptSystem::OnPlayStop`, `PhysicsSystem::Reset`, `AnimationSystem::Shutdown` on game copy; `m_gameScene.reset()` destroys the copy; editor scene is untouched — no deserialization restore needed
- `EditorContext.scene*` / `.registry*` are live-patched by `EditorMode::OnPlayStateChanged`; `ScriptSystem`'s `g_ctx.scene` is patched by `OnPlayStart`

---

## Project Structure

A project is a directory rooted at a `.saproject` JSON file:

```
<project-root>/
├── <name>.saproject        ← project entry point (JSON)
├── assets/                 ← user-visible content (Assets panel root)
│   ├── scenes/             .sascene
│   ├── models/             .gltf / .glb   (+ .sameta sidecars)
│   ├── textures/           .png / .hdr    (+ .sameta sidecars)
│   ├── materials/          .samat         (+ .sameta sidecars)
│   └── shaders/            user .saglsl / .vert / .frag
└── cook_cache/             derived artifacts (.samesh, .satex, …); .gitignore

<engine-root>/assets/       ← engine built-in assets (independent search path)
├── shaders/                compiled builtins (.spv + .refl, flat)
├── models/                 cube.gltf, plane.gltf + .sameta
├── textures/               default textures
├── hdri/                   brdf_schilk, grasslands_sunset
├── materials/              default_pbr.mat
└── templates/
    ├── entities/           entity spawn templates (.sascene, 1 sub-dir = category)
    │   ├── 3D Object/
    │   ├── Light/
    │   └── Camera.sascene
    ├── scenes/
    │   └── default.sascene  ← New Scene starting template
    └── materials/           Create Material template list (.samat)
```

**`.saproject` format (JSON, minimal):**
```json
{ "name": "DemoProject", "version": 1, "startupScene": "assets/scenes/foo.sascene" }
```

`EditorMode::OnAttach` scans `projectDir/*.saproject`, reads `startupScene`, and loads it.
If `projectDir` is empty (default startup), `ProjectBrowserPanel` is shown instead.
`AssetsPanel` displays `projectDir/assets/` filtered to hide `.sameta` sidecars.

**CMake option `SA_DEBUG_PROJECT`:** When ON, skips the project browser and loads
`SA_PROJECT_DIR` (defaults to `demo_project/`) directly at startup for fast iteration.

**Cook cache layout (dual-path VFS):**
- `engineCookCacheDir` — fixed at startup (`ApplicationPath::COOK_CACHE_DIR`); contains
  engine built-in cooked assets (shapes, default materials, IBL LUTs, etc.).
- `cookCacheDir` — per-project; populated when user loads a project. Searched before
  the engine cache. Empty until a project is loaded.

`Application::UpdateProjectPaths(projectDir, cookCacheDir)` propagates a project switch
to both VFS and `SceneRenderer` at runtime (called by `EditorMode::LoadProject`).

### EntityTemplateRegistry

`EntityTemplateRegistry::Scan(engineAssetsDir)` walks `templates/entities/`, producing a flat
`vector<TemplateEntry>{category, label, path}`. `SceneHierarchyPanel` reads this list to build
its "Create Entity" menu without any hardcoded enum — adding a new template is a file operation only.

`SceneSerializer::SpawnFromTemplate(scene, path)`:
- Saves then restores current `WorldSettings` and scene name
- Calls `LoadFromFile` to append entities
- Returns the list of newly added root entities

---

## Build-Time Pipeline

```
GLSL shader (.vert / .frag / .comp / .saglsl)
        │
        ▼  glslc   (cmake/CompileShaders.cmake)
SPIR-V bytecode (.spv)
        │
        ▼  ShaderReflectTool   (tools/shader_reflect, via SPIRV-Cross)
Reflection sidecar (.refl) — binary format v6, see ShaderReflectionIO.hpp
    bindings[]: { set, binding, type, stage, name, arraySize }
    uboMembers[]: { name, offset, size, uiType, displayName, … }
    pushConstantSize, pushConstantStages
    vertexInputs[]: { location, format }   ← vertex stage only; drives PSO vertex input
```

Both files are output to `<build>/bin/assets/shaders/builtin/`.
CMake `DEPENDS` tracking ensures a changed source triggers recompilation.

### Shading Dispatch Stub (Build-Time)

`cmake/GenerateShadingDispatch.cmake::generate_shading_dispatch()` produces two GLSL headers
into `generated/shaders/` with an **empty evaluator list**:

```
generated/shaders/shading_model_ids.glsl   — #define SHADING_MODEL_PBR 0u  (only PBR)
generated/shaders/shading_dispatch.glsl    — DispatchShadingModel() stub returning false
```

`deferred_lighting.frag` `#include`s both files at build time so the engine compiles cleanly.
The real dispatch (with project custom models) is generated at editor runtime by `ShaderCookLib`
and written into `cook_cache/generated/shaders/`; `deferred_lighting.frag` is then recompiled
against that output — see [Custom Shading Models](#custom-shading-models).

No `*.lighting.glsl` source files exist in `assets/shaders/`; evaluator files are intermediate
outputs produced by `ShaderCookLib` from `.saglsl` sources and placed in `cook_cache/`.

---

## Offline Asset Pipeline (Cook)

**Location:** `tools/cook/`, `tools/importer/`, `tools/shader_reflect/`

All raw source assets are transformed into engine-native cooked formats before
loading at runtime. The cook step runs at build time (via CMake custom commands)
or as a standalone CLI (`StellarAliaCook`).

### Asset Identity — `.sameta` & `AssetID`

Every source asset has a companion `.sameta` sidecar file that persists its
stable UUID (`AssetID`).

```
BoomBox.glb
BoomBox.glb.sameta  ← {"uuid": "xxxxxxxx-…"}
```

`Cook::MetaFile::MetaPathFor(path)` derives the sidecar path.
`AssetID` is a 128-bit UUID (two `uint64_t hi/lo`).

Child UUIDs (for embedded images, materials, per-mesh nodes) are derived
deterministically from the parent asset UUID using Fibonacci hash salts:

```cpp
// Different salt per derived asset type to prevent UUID collisions:
// DeriveImageID    — embedded texture (salt A)
// DeriveMaterialID — glTF material   (salt B)
// DeriveAnimID     — animation clip  (salt C)
```

### AssetRegistry

`AssetRegistry::Scan(projectAssetsDir, engineAssetsDir)` walks both trees recursively,
reading every `.sameta`, and populates a `vector<AssetEntry>` plus two fast-lookup maps:

```cpp
struct AssetEntry {
    AssetID               id;
    std::string           name;        // source filename, e.g. "BoomBox.glb"
    std::string           type;        // "Mesh", "Texture", "Material", "Animation", …
    std::filesystem::path sourcePath;  // absolute path to the source file
};
```

Used by the editor for the asset-picker popup (`EntriesByType("Mesh")`) and by
`ResourceManager` for path-based asset resolution.

### Cooked Texture — `.satex`

```
CookedTexture {
    AssetID  id
    uint32   width, height, mipLevels
    CookedTextureFormat  format   (RGBA8 | RGBA16F | RGBA32F)
    bool     srgb, isHDR
    vector<MipSlice { offset, size }>
    vector<uint8_t>  data
}
```

### Cooked Mesh — `.samesh`

```
CookedMesh {
    AssetID  id
    uint32   vertexStride (48 bytes), indexStride (4 bytes)
    uint32   vertexCount, indexCount
    vector<CookedSubMesh> {
        vertexOffset, vertexCount
        indexOffset,  indexCount
        materialIndex          // index into original glTF material array
        AssetID defaultMaterialID  // → .samat
        glm::mat4 localTransform   // pre-baked node world transform
    }
    vector<uint8_t> vertexData   // Vertex: pos3 normal3 tangent4 uv2 (48 bytes)
    vector<uint8_t> indexData
    vector<uint8_t> skinData     // SkinVertex[]: uvec4 joints + vec4 weights (32 bytes/vert); empty for static meshes
}
// IsSkinned() → !skinData.empty()
// Format version 5+ includes skinData blob
```

### Cooked Skeleton — `.saskel`

Generated automatically from any `.glb` that contains a skin. Stores the bind-pose
bone hierarchy and inverse-bind matrices for CPU skinning.

```
CookedSkeleton {
    AssetID  id
    vector<BoneInfo> { name, parentIndex, inverseBindMatrix }
}
```

`AnimationSystem::PrepareEntity` looks up the `.saskel` via `DeriveSkinID(meshAssetID)`
for backward-compat with assets imported before explicit skeleton assets existed.

### Cooked Animation — `.saanim`

One `.saanim` file per animation clip (one import run per `.glb` clip). Stores
per-bone keyframe channels (position / rotation / scale) with timestamps.

```
CookedAnim {
    AssetID  id
    float    duration   // seconds
    vector<AnimChannel> {
        string boneName
        vector<{float time, glm::vec3 value}>  positions
        vector<{float time, glm::quat value}>  rotations
        vector<{float time, glm::vec3 value}>  scales
    }
}
```

`AnimatorComponent.clipAsset` stores the UUID of the desired `.saanim`.
The editor asset picker filters by `type == "Animation"`.

### Cooked Material — `.samat`

JSON file. Mirrors PBR metallic-roughness parameters and can reference custom shader types:

```json
{
  "name": "Gold",
  "type": "PBR",
  "baseColorFactor": [1.0, 0.86, 0.57, 1.0],
  "roughnessFactor": 0.2,
  "metallicFactor":  1.0,
  "albedoMap":    "uuid-of-albedo-satex",
  "normalMap":    "uuid-of-normal-satex"
}
```

### Standalone Material — `.mat` source

Hand-authored in `assets/materials/`. Identical format to `.samat`.
`CookStandaloneMaterial` copies it into the cook cache under its UUID filename.

---

## ECS — Scene & Components

**Location:** `src/function/scene/`

The engine uses [EnTT](https://github.com/skypjack/entt) for entity-component
storage. `Scene` wraps an `entt::registry` and exposes `View<C...>()`.

### Component Reference

| Component | Purpose |
|-----------|---------|
| `TagComponent` | Human-readable name string |
| `TransformComponent` | Local position / rotation (quat) / scale |
| `WorldTransformComponent` | Cached world-space `mat4`; `dirty` flag for lazy recompute |
| `HierarchyComponent` | `parent` entity handle + `children` list (only when parented) |
| `AnimatedTransformComponent` | Per-frame animated local pose; overrides `TransformComponent` when present |
| `StaticMeshComponent` | `meshAsset` (→ .samesh) — mesh identity only |
| `MeshRendererComponent` | `materialSlots[]` (→ .samat per sub-mesh) + `castShadow` / `receiveShadow` — shared by static and skinned mesh entities |
| `MaterialOverrideComponent` | Unified material override: optional `materialAsset` + named `scalars` + named `textures` |
| `SkinnedMeshComponent` | Per-entity GPU skinning state: `meshAsset`, `skinMatricesBuffer`, `skinDescSet`, `boneCount`, `ready`; mesh geometry (`vertexBuffer`/`indexBuffer`/`skinDataBuffer`) lives in `GPUMesh` (ResourceManager) |
| `AnimatorComponent` | `clipAsset` (→ .saanim), `time`, `speed`, `looping`, `playing` |
| `CameraComponent` | `fovY`, `nearPlane`, `farPlane`, `priority` (highest wins) |
| `ActiveCameraTag` | _(legacy)_ marks the active camera; superseded by `CameraComponent::priority` |
| `DirectionalLightComponent` | color, intensity, castShadow; direction from entity rotation (−Z) |
| `PointLightComponent` | color, intensity, range; position from entity world transform |
| `SpotLightComponent` | color, intensity, range, innerAngle, outerAngle |
| `AreaLightComponent` | color, intensity, size (W×H), twoSided, emissiveScale; LTC-evaluated PBR |
| `RigidBodyComponent` | Physics body: `Type` (Static/Kinematic/Dynamic), mass, friction, restitution, `bodyId` |
| `ColliderComponent` | Collision shape: `Shape` (Box/Sphere/Capsule), extents, offset, rotation |
| `ScriptComponent` | C# script binding: `scriptPath` (relative to project root), `className` (derived from filename if empty) |
| `StaticGeometryTag` | Hint: entity never moves (future culling / lightmap / BVH) |

IBL and skybox are **not** ECS components — they are global scene settings in `WorldSettings`.

**Mesh entity pattern (static):**
```
Entity
  ├── TagComponent
  ├── TransformComponent
  ├── WorldTransformComponent
  ├── StaticMeshComponent       { meshAsset }
  ├── MeshRendererComponent     { materialSlots[], castShadow, receiveShadow }
  └── MaterialOverrideComponent { … } (optional)
```

**Mesh entity pattern (skinned):**
```
Entity
  ├── TagComponent
  ├── TransformComponent
  ├── WorldTransformComponent
  ├── AnimatorComponent         { clipAsset, time, speed, looping, playing }
  ├── SkinnedMeshComponent      { meshAsset, skinMatricesBuffer, skinDescSet, boneCount, ready }
  ├── MeshRendererComponent     { materialSlots[], castShadow, receiveShadow }
  └── MaterialOverrideComponent { … } (optional)
```

### MaterialOverrideComponent

Replaces the old `PBRSurfaceComponent` + `MaterialParamComponent` pair.
When present, the render system clones the base `MaterialInstance` and applies overrides.
Entities without this component share the cached instance (no clone, no allocation).

```cpp
struct MaterialOverrideComponent {
    AssetID                           materialAsset;  // invalid = use mesh-default or slot
    std::map<std::string, ParamValue> scalars;        // named UBO param overrides
    std::map<std::string, AssetID>    textures;       // named texture slot overrides
};
// ParamValue = variant<float, vec2, vec3, vec4>
```

### ColorGradingSettings

Parametric color grading applied post-ACES. When `enabled`, parameters are baked into a 32³
RGBA16F 3D LUT by a compute shader (`postfx_cg_bake.comp`) on parameter change; the LUT is
then sampled once per pixel in the Tonemap pass via `sampler3D`. Only active when
`PostProcessSettings::tonemapMode == Builtin`.

```cpp
struct ColorGradingSettings {
    bool      enabled    = false;
    glm::vec3 lift       = {0.f, 0.f, 0.f};  // shadow additive offset  [-0.5, 0.5]
    glm::vec3 midtone    = {1.f, 1.f, 1.f};  // midtone power (ASC CDL) [ 0.1, 3.0]
    glm::vec3 gain       = {1.f, 1.f, 1.f};  // highlight multiplier    [ 0.0, 3.0]
    float     saturation = 1.f;               // [0, 3]
    float     contrast   = 1.f;              // [0, 3], pivot at 0.5
};
```

### PostProcessSettings

Nested value-type inside `WorldSettings`; all bloom + tonemap runtime parameters live here.

```cpp
struct PostProcessSettings {
    // Bloom
    bool  bloomEnabled   = true;
    float bloomThreshold = 1.0f;   // knee width = threshold × 0.1
    float bloomStrength  = 0.4f;
    float bloomRadius    = 1.0f;   // widest upsample level; each subsequent level ×0.85
    int   bloomMipLevels = 3;      // pyramid depth [2, 8]; change triggers WaitIdle + desc set rebuild

    // Tonemap
    enum class TonemapMode { Builtin, LUT } tonemapMode = TonemapMode::Builtin;
    AssetID tonemapLut;
    float exposure    = 1.f;
    float lutStrength = 1.f;

    // Color Grading (Builtin mode only)
    ColorGradingSettings colorGrading;

    // SSAO (GTAO)
    bool  ssaoEnabled       = false;
    float ssaoRadius        = 32.f;
    float ssaoStrength      = 1.0f;
    float ssaoBias          = 0.025f;
    int   ssaoDirections    = 4;
    int   ssaoSteps         = 3;
    float ssaoBlurSharpness = 10.f;

    // TAA (Temporal Anti-Aliasing)
    bool  taaEnabled      = false;
    float taaBlendStatic  = 0.1f;   // history lerp toward current in still regions
    float taaBlendMotion  = 0.5f;   // history lerp toward current in motion regions
    bool  taaAntiGhosting = true;   // 3×3 YCoCg neighborhood AABB clamp

    // Auto Exposure (eye adaptation)
    bool  autoExposureEnabled = false;
    float aeEvMin        = -4.0f;
    float aeEvMax        =  4.0f;
    float aeAdaptSpeed   =  2.0f;
    float aeLowPercent   =  0.45f;
    float aeHighPercent  =  0.95f;

    // Depth of Field (DoF)
    bool  dofEnabled    = false;
    float focusDistance = 5.0f;   // view-space focus plane distance (meters)
    float aperture      = 1.4f;   // f-number (lower = shallower DoF)
    float focalLength   = 50.0f;  // mm
    int   dofSamples    = 16;     // blur kernel sample count [4, 32]
    bool  autoFocus     = false;  // Phase 2: GPU depth readback (not yet implemented)
    float maxCocPx      = 20.0f;  // max CoC radius in pixels

    // Future-effect placeholder
    bool motionBlurEnabled = false;
};
```

### WorldSettings

Value-type field on `Scene`, serialised in `.sascene`'s `"world"` block:

```cpp
struct WorldSettings {
    enum class BackgroundMode { SolidColor, Skybox };
    BackgroundMode backgroundMode  = BackgroundMode::SolidColor;
    glm::vec3      backgroundColor = { 0.08f, 0.08f, 0.08f };  // linear

    // HDR source + baked IBL products (Skybox mode only)
    AssetID skyboxHdr;
    AssetID sh9;
    AssetID prefilteredEnv;
    AssetID brdfLut;
    AssetID skyboxCubemap;

    PostProcessSettings pp;  // bloom + tonemap + SSAO + TAA + DoF + future-effect params
};
```

**SolidColor IBL behaviour:** `ApplyWorldSettings` encodes `backgroundColor` as a constant
SH L0 ambient term and writes it to a 1×1 solid-colour cubemap used as `t_PrefilteredEnv`.

**PostProcess hot-swap:** `ApplyWorldSettings(ws, updateIBL=false)` updates `BloomFeature`
runtime fields (`m_enabled/threshold/strength/radius`), SSAO/TAA/AutoExposure/DoF parameters,
and tonemap parameters instantly from `ws.pp` without a device stall. `pp.tonemapMode` switching
retains the WaitIdle feature-slot replacement for `LutTonemapFeature`. `pp.bloomMipLevels`
change is **deferred** — set `m_pendingBloomMipCount`; actual GPU rebuild happens at the
start of the next `RenderFrame` resize block (after `WaitIdle`).

### Transform Hierarchy

`HierarchyComponent` stores `parent` + `children`. Only parented entities carry it.

`Scene::UpdateTransforms()`:
1. If `m_hierarchyDirty`: rebuild `m_sortedEntities` via BFS from root entities
2. Walk sorted list (parents always before children):
   - Prefer `AnimatedTransformComponent` over `TransformComponent` for local matrix
   - `world = (no parent) ? TRS(local) : parentWorld × TRS(local)`

`Scene::MarkDirty(entity)` recursively marks the entity and all descendants dirty.
Called by the editor gizmo after any transform manipulation.

### Scene File — `.sascene`

JSON serialised by `SceneSerializer`. Key component keys:

```json
{
  "version": 1,
  "name": "SceneName",
  "world": { "backgroundMode": "SolidColor", "backgroundColor": [0.08, 0.08, 0.08], … },
  "entities": [
    {
      "id": 0,
      "parent": -1,
      "tag": "Box",
      "transform":   { "position": [0,0,0], "rotation": [1,0,0,0], "scale": [1,1,1] },
      "staticMesh":  { "mesh": "uuid" },
      "meshRenderer": { "materials": ["uuid"], "castShadow": true, "receiveShadow": true },
      "materialOverride": {
        "materialAsset": "uuid",
        "scalars":   { "roughnessFactor": [0.3, 0, 0, 0] },
        "textures":  { "albedoMap": "uuid" }
      },
      "animator":    { "clipAsset": "uuid", "speed": 1.0, "looping": true, "playing": true },
      "camera":      { "fovY": 1.047, "near": 0.1, "far": 1000.0, "priority": 0 },
      "directionalLight": { "color": [1,1,1], "intensity": 1.5, "castShadow": false },
      "pointLight":       { "color": [1,1,1], "intensity": 1.0, "range": 10.0 },
      "spotLight":        { "color": [1,1,1], "intensity": 1.0, "range": 10.0,
                            "innerAngle": 0.26, "outerAngle": 0.52 },
      "rigidBody":   { "type": "Dynamic", "mass": 1.0, "friction": 0.5, "restitution": 0.0 },
      "collider":    { "shape": "Box", "extents": [0.5, 0.5, 0.5] }
    }
  ]
}
```

Rotation quaternions: `[w, x, y, z]`. `"parent": -1` = root. `"parent": N` = index into `entities`.
`AnimatedTransformComponent` and `SkinnedMeshComponent` are never serialised (runtime-only).

---

## Animation System

**Location:** `src/function/animation/AnimationSystem.hpp/.cpp`

GPU skinning skeletal animation. Uses `.saskel` (skeleton) and `.saanim` (animation clip) cooked assets.
Bone deformation runs entirely on the GPU vertex shader; only bone matrices (~3 KB) are uploaded per frame.

### Lifecycle

```cpp
// Initialise once (after SceneRenderer is ready):
animSystem.Init(device, sceneRenderer->GetSkinDescLayout());

// Per scene load (per animated entity):
animSystem.PrepareEntity(entity, registry, resMgr, device);
// → allocates skinMatricesBuffer (boneCount×64 B, CPU-visible)
// → allocates skinDescSet (set=2: binding0=skinMats, binding1=gpuMesh.skinDataBuffer)
// → sets SkinnedMeshComponent::ready = true

// Every frame (Playing state):
animSystem.Update(dt, registry, resMgr, device);
// → advances AnimatorComponent::time, evaluates FK → workGlobalPose/workSkinMats
// → uploads workSkinMats to skinMatricesBuffer (~3 KB); no vertex upload

// When stopping (reset to rest pose):
animSystem.EvaluateAll(0.f, registry, resMgr, device);

// On shutdown:
animSystem.Shutdown(device);
// → frees per-entity skinMatricesBuffer + skinDescSet; mesh buffers owned by ResourceManager
```

### GPU Skinning Data Flow

```
PrepareEntity (once):
  GPUMesh::skinDataBuffer  ← per-asset (joints+weights SSBO, uploaded by ResourceManager)
  skinMatricesBuffer       ← per-entity (mat4[boneCount], CPU-visible)
  skinDescSet (set=2)      ← binding0 = skinMatricesBuffer, binding1 = skinDataBuffer

Update (per frame):
  FK → workSkinMats[]  →  UploadBufferData(skinMatricesBuffer, ~3 KB)
  deferred_geometry_skinned.vert:  gl_VertexIndex → skinDataBuffer → 4-bone blend → clip space
```

### Skeleton Resolution

`PrepareEntity` uses `AnimatorComponent::clipAsset` to find the clip, then resolves
the skeleton via `DeriveSkinID(meshAsset)` — deterministic hash from the mesh UUID.
This is a backward-compat layer; future work adds an explicit `skeletonAsset` picker.

### Editor Overlay

`AnimationSystem::GetBoneGlobalPoses(entity)` and `GetBoneSkeleton(entity)` return
the most-recently computed mesh-local bone transforms and bone hierarchy.
`EditorMode::DrawOverlays` uses these to draw the skeleton gizmo (spheres + lines)
on the selected skinned mesh entity, controlled by `EditorOverlaySettings::drawSkeletonGizmo`.

---

## Physics System

**Location:** `src/function/physics/PhysicsSystem.hpp/.cpp`

Thin ECS wrapper around [Jolt Physics](https://github.com/jrouwe/JoltPhysics).
Jolt types are fully hidden behind Pimpl — callers never include Jolt headers.

### Fixed-Step Loop

```cpp
accumulator += dt;
while (accumulator >= kFixedStep) {
    physics.SyncIn(scene);     // create bodies first time; push Kinematic poses to Jolt
    physics.Step(kFixedStep);  // advance simulation
    physics.SyncOut(scene);    // copy Dynamic body poses → WorldTransformComponent
    accumulator -= kFixedStep;
}
physics.DrawDebug(settings, scene);  // optional editor overlay
```

`RigidBodyComponent::bodyId == ~0u` means the body has not been created yet; `SyncIn`
creates it on first encounter and writes the returned Jolt BodyID into `bodyId`.

### Play-State Integration

`PhysicsSystem::Reset(scene)` removes all Jolt bodies and resets `bodyId` to `~0u`.
`Application` calls `Reset` on transition to `Editing` state so the scene returns
to its authored transform positions.

### Editor Debug Overlay

`DrawDebug(PhysicsDebugSettings, scene)` draws collider wireframes into `DebugDraw`
directly from `ColliderComponent` ECS data (no Jolt debug callback needed). Toggles:
`shapes`, `aabbs`, `velocity`, `contacts`. Exposed via `SettingsPanel` → Physics Debug.

### Script API Surface

`PhysicsSystem` exposes the following methods for use by `ScriptApiExports` without leaking Jolt headers to callers:

| Method | Delegates to |
|--------|-------------|
| `GetLinearVelocity / SetLinearVelocity` | `BodyInterface::GetLinearVelocity / SetLinearVelocity` |
| `GetAngularVelocity / SetAngularVelocity` | `BodyInterface::GetAngularVelocity / SetAngularVelocity` |
| `AddForce / AddImpulse` | `BodyInterface::AddForce / AddImpulse` |
| `Raycast(origin, dir, maxDist, hit&)` | `NarrowPhaseQuery::CastRay` + `BodyLockRead` for surface normal |

---

## Scripting System

**Locations:**
- `src/function/script/ScriptSystem.hpp/.cpp` — C++ driver; owned by `Application`
- `src/function/script/ScriptApiExports.hpp/.cpp` — flat C API + `ScriptApiFunctionTable`
- `managed/StellarAlia.Runtime/` — C# engine API surface (`ScriptBase`, `Entity`, `Debug`, `Time`, `Input`, `Mathf`, `AnimatorProxy`, `RigidBodyProxy`, `PointLightProxy`, `Physics`, `QuaternionExt`, `NativeApi`)
- `managed/StellarAlia.ScriptBridge/` — Roslyn compiler (`ScriptCompiler`), collectible ALC loader (`ScriptLoader`), unmanaged entry points (`ScriptBridgeEntry`)
- `demo_project/assets/scripts/` — user `.cs` scripts

### Hosting Model

`ScriptSystem::Init` loads `hostfxr` at runtime, initialises a .NET host context pointing at `StellarAlia.ScriptBridge.runtimeconfig.json`, and retrieves six function-pointer delegates via `get_function_pointer`:

```
Initialize | Compile | Instantiate | InvokeLifecycle | RemoveInstance | Unload
```

`Initialize` receives a `ScriptApiFunctionTable*` — a plain struct of C function pointers (version 2) covering transforms, entity lifecycle, rigidbody physics, point light control, physics raycast, animator, input, debug draw, logging, and time. The first field is `uint32_t version` so both sides can detect layout mismatches at startup. `ScriptApiContext` carries `PhysicsSystem*` so rigidbody and raycast functions can access Jolt through the physics system's public API. The managed `NativeApi` class stores this table and calls through it; this avoids making `StellarAlia.Runtime` a native shared library.

**Key Runtime API classes (all in `StellarAlia` namespace):**

| Class | Description |
|-------|-------------|
| `Mathf` | Pure managed math utilities: `Lerp`, `Clamp`, `Clamp01`, `PingPong`, `SmoothStep`, `Approximately`, `MoveTowards`, and `MathF` wrappers |
| `Input` | `IsKeyDown`, `IsKeyJustPressed`, `IsKeyJustReleased` (frame-state tracked via `HashSet<Key> _prev/_curr`, updated by `BeginFrame()` before first `OnUpdate` each frame) |
| `Entity` | `GetRotation/SetRotation(Quaternion)`, `Forward/Right/Up` direction vectors, `Destroy()`, static `Create()`, `GetRigidBody()`, `GetPointLight()` |
| `RigidBodyProxy` | `LinearVelocity`/`AngularVelocity` (get/set), `AddForce`, `AddImpulse` |
| `PointLightProxy` | `Color`, `Intensity`, `Range` (get/set) |
| `Physics` | `Raycast(origin, direction, maxDist, out RaycastHit)` — wraps Jolt NarrowPhaseQuery |
| `QuaternionExt` | `FromEulerDegrees`, `FromEulerRadians`, `Slerp`, `RotateTowards`, `AngleDegrees`, `ToEulerDegrees` |

### Play Lifecycle

```
OnPlayStart(Scene& gameScene)
  └─ SA_Script_SetContext({&gameScene, …})  ← redirect g_ctx.scene to game copy
     └─ Compile(sourcePaths[])  ← Roslyn in-memory → byte[] assembly
        └─ Load(bytes)          ← CollectibleALC.LoadFromStream
           └─ for each ScriptComponent entity:
                Instantiate(entityId, className)
                InvokeLifecycle(entityId, OnAttach, 0)
                InvokeLifecycle(entityId, OnStart,  0)

per-frame (Application, Playing only, via GetActiveScene().Registry()):
  ScriptSystem::FixedUpdate  → InvokeLifecycle(…, OnFixedUpdate, fixedDt)
  ScriptSystem::Update       → InvokeLifecycle(…, OnUpdate, dt)
  ScriptSystem::LateUpdate   → InvokeLifecycle(…, OnLateUpdate, dt)

OnPlayStop(reg)
  └─ InvokeLifecycle(…, OnStop, 0) + InvokeLifecycle(…, OnDetach, 0) for all instances
     └─ ScriptLoader::Unload()  ← CollectibleALC.Unload() (GC-collectible)
```

`g_ctx.scene` after `OnPlayStop` points to the now-destroyed `m_gameScene`, but `m_playing = false` guards all `InvokeAll` calls, so the dangling pointer is never dereferenced. The next `OnPlayStart` overwrites it before any managed code runs.

`OnSceneAboutToChange` calls `Unload` before `scene.Clear()` to release managed entity refs.

### Compilation — Roslyn Reference Assembly Strategy

`ScriptCompiler.BuildReferences()` builds the Roslyn `MetadataReference` list:

1. **SDK reference pack** (`packs/Microsoft.NETCore.App.Ref/<ver>/ref/net8.0/`) is located by traversing 3 levels up from `typeof(object).Assembly.Location` to reach the dotnet root. Ref-pack DLLs are added first (pure managed API surface, no native blobs).
2. **AppDomain assemblies** (including `StellarAlia.Runtime`) are added for any name not already covered by the ref pack; assemblies whose path falls inside the runtime dir are excluded to prevent CS0433 duplicate-type conflicts (`Vector3` defined in both `System.Numerics.Vectors` ref and `System.Private.CoreLib` runtime).

### Dependency Resolution — Cross-ALC Search

`CollectibleALC.Load()` resolves dependencies by searching `AppDomain.CurrentDomain.GetAssemblies()` rather than `AssemblyLoadContext.Default.Assemblies`. `hostfxr` loads `StellarAlia.Runtime` into its own isolated ALC (not Default), so using Default would fail to find it.

### IDE Project File Generation

On every project open (`Application::UpdateProjectPaths`), the engine calls `GenerateIdeProjectFiles(projectDir)`:

- **`Directory.Build.props`** — always overwritten; contains the absolute `managedDir` path as `$(StellarAliaManaged)`. Gitignored — machine-specific.
- **`{stem}.csproj`** — written only if absent; references `$(StellarAliaManaged)/StellarAlia.Runtime.dll` via the MSBuild variable. Committed to git — no absolute paths.
- **`{stem}.sln`** — written only if absent. Project GUID is deterministically derived from the stem via FNV-1a → UUID v5. Committed to git.

MSBuild automatically discovers `Directory.Build.props` by searching parent directories, so `.csproj` needs no explicit import. `StellarAlia.Runtime.xml` (generated by the Runtime `.csproj` build) ships alongside `StellarAlia.Runtime.dll`, giving IDE IntelliSense tooltip documentation.

### ECS Integration

`ScriptComponent` carries `scriptPath` (relative to project root) and `className` (defaults to the file name stem). `ScriptSystem` subscribes to `entt::registry::on_destroy<ScriptComponent>` to call `RemoveInstance` when an entity is destroyed during play.

### Script Log Routing

`SA_Log_Info/Warn/Error` (called from C# via the function table) use a lazy-initialized `"script"` named spdlog logger that shares all sinks of the default logger. `EditorLogSink` captures `msg.logger_name` into `LogEntry::loggerName`; `ConsolePanelPresenter` routes entries with `loggerName == "script"` to `m_scriptEntries`, which appear automatically in the Diagnostics tab under a `--- Script ---` separator — no explicit `EditorDiagnostics::Push()` required.

---

## Input System

**Location:** `src/function/input/InputSystem.hpp/.cpp`

Three-layer action-mapped input. Sits parallel to `SceneRenderer` in `Application`.

### Map Stack

```cpp
input.RegisterMaps({ viewportMap, uiMap, gameplayMap });
input.PushMap("Viewport");
input.PushMap("UI");     // blocks Viewport unless passthrough=true
input.PopMap();          // Viewport resumes
```

Maps are defined in `ActionMapDef`. Each map contains `ActionDef` entries (named logical actions)
each holding one or more `BindingDef` entries that translate physical device paths into action values.

### BindingDef Kinds

| Kind | Description | Factory |
|------|-------------|---------|
| `Direct` | Single device path → float/vec2 | `BindingDef::Direct("Keyboard/Space")` |
| `WASD` | Four keys → normalised Axis2D | `BindingDef::WASD()` |
| `TwoButtonAxis` | Two keys → signed float [-1, 1] | `BindingDef::TwoButton("Q", "E")` |
| `Composite` | Exactly the listed modifiers AND key must be held (strict AND gate) | `BindingDef::Composite("Keyboard/LeftControl", "Keyboard/D")` |

`Composite` supports arbitrary modifier counts:
```cpp
// Single modifier (Ctrl+D):
BindingDef::Composite("Keyboard/LeftControl", "Keyboard/D")
// Multi-modifier (Ctrl+Shift+Z):
BindingDef::Composite({"Keyboard/LeftControl", "Keyboard/LeftShift"}, "Keyboard/Z")
// Runtime-built vector (e.g. from ShortcutsPanel key capture):
BindingDef::Composite(std::vector<std::string>{...}, "Keyboard/S")
```

`ActionDef` additionally carries `bool userConfigurable = false`. When true, the action
appears in `ShortcutsPanel` and its `bindings[0]` may be replaced by a user override
loaded from `editor_shortcuts.json` via `EditorShortcutConfig`.

### Composite Key Conflict Prevention

When modifier keys of a `Composite` binding are all held **and no extra canonical modifiers
are also held**, `Poll()` runs a pre-pass (`ComputeBlockedPaths`) that adds the binding's
`keyPath` to `m_blockedPaths`. `ReadBindingFloat` (Direct) and `ReadBindingVec2` (WASD)
then route button reads through `GetButtonFiltered`, which returns `0` for any blocked path.

**Strict (exclusive) modifier matching:** `HasExtraModifiers()` checks the eight canonical
modifier paths (`LeftControl`, `RightControl`, `LeftShift`, `RightShift`, `LeftAlt`,
`RightAlt`, `LeftSuper`, `RightSuper`). If any modifier beyond those listed in the binding
is held, the binding does not fire and does not block. This ensures Ctrl+Shift+S does not
accidentally trigger the Ctrl+S binding.

Axis paths (triggers, scroll) bypass the filter and are never blocked.

### Per-Frame Query

```cpp
window->PollEvents();
input.Poll();                              // snapshot devices + evaluate active map
glm::vec2 move = input.ReadVec2("Move");   // composite axis
if (input.WasActivated("Jump")) { … }      // rising edge
if (input.IsActive("MouseLook")) { … }     // held
float scroll = input.ReadFloat("Scroll");
```

`WasActivated` fires once on the frame the action transitions from inactive → active.
`IsActive` returns true for every frame the action is held.

### Input Provider

`IInputProvider` (platform abstraction) is implemented by `GLFWInputProvider`.
`SetCursorCapture(bool)` hides/shows the cursor and enables/disables raw mouse delta —
used by `EditorMode` to toggle between orbit-camera mouse look and normal UI interaction.

---

## Platform Layer — RHI

**Location:** `src/platform/rhi/` (interface), `src/platform/rhi/vulkan/` (backend)

### Handle Model

All GPU objects use opaque 32-bit index handles into internal pools:
```
RHITextureHandle  → m_textures[]      RHIShaderHandle   → m_shaders[]
RHIBufferHandle   → m_buffers[]       RHIPipelineHandle → m_pipelines[]
RHIDescLayoutHandle → m_descLayouts[] RHIDescSetHandle  → m_descSets[]
```
`handle.IsValid()` = `index != UINT32_MAX`.

### Graphics vs Compute Pipelines

| | `RHIPipelineDesc` (graphics) | `RHIComputePipelineDesc` (compute) |
|---|---|---|
| Shaders | vert + frag | single compute |
| Vertex input | reflection-driven (see below) / `noVertexInput` flag | not present |
| Rasterizer / blend / depth | present | not present |
| Attachment formats | color[] + depth | not present |
| Vulkan call | `vkCreateGraphicsPipelines` | `vkCreateComputePipelines` |

### Reflection-Driven Vertex Input

Vertex input attributes are not hardcoded. `ShaderReflection::vertexInputs` (populated
by `ShaderReflectTool` from SPIR-V `stage_inputs`) lists the locations the vertex
shader actually consumes; the SPIR-V optimizer strips declared-but-unused `in`
variables so the list reflects real consumption. `ShaderProgram` forwards it via
`RHIPipelineDesc::vertexInputs[]`, and `VulkanDevice::CreatePipeline` emits one
`VkVertexInputAttributeDescription` per entry — never more.

The mesh data layout (`CookedMesh::Vertex`, 48-byte interleaved
pos+normal+tangent+uv) is unchanged: binding stride is fixed at 48, and the backend
holds a static `LocationToOffset` table (`0→0, 1→12, 2→24, 3→40`). Locations
outside this table are an error (logged and skipped) — declare a vertex attribute
in a shader without a matching mesh attribute, and the next draw will trigger a
validation error from the missing pipeline input, exposing the mismatch immediately.

When `vertexInputCount == 0` (legacy v3-v5 `.refl` files predating the field),
`CreatePipeline` falls back to declaring all 4 attribs — preserves correctness for
shaders cooked before the field existed, at the cost of the original
"attribute at location N not consumed" warnings on shaders that read fewer locs.

### Image Layout Map

```
RHIResourceState::Undefined        → VK_IMAGE_LAYOUT_UNDEFINED
RHIResourceState::RenderTarget     → VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
RHIResourceState::DepthWrite       → VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
RHIResourceState::ShaderRead       → VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
RHIResourceState::UnorderedAccess  → VK_IMAGE_LAYOUT_GENERAL
RHIResourceState::CopySrc/CopyDst  → VK_IMAGE_LAYOUT_TRANSFER_*_OPTIMAL
RHIResourceState::Present          → VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
```

Barriers use `VK_KHR_synchronization2` with conservative `ALL_COMMANDS /
MEMORY_READ|WRITE` masks for correctness.

### 3D Textures

`RHITextureDesc::depth > 1` (and `cubemap == false`) triggers:
- `imageType = VK_IMAGE_TYPE_3D`; `arrayLayers` forced to 1 (Vulkan requirement)
- Main view: `VK_IMAGE_VIEW_TYPE_3D` (for `sampler3D` and `image3D`)

Current users: `TonemapFeature::m_cgLutTex` (32×32×32 RGBA16F, color grading LUT).

### Cubemap Textures

`RHITextureDesc::cubemap = true` triggers:
- `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`; `arrayLayers` forced to 6
- Main view: `VK_IMAGE_VIEW_TYPE_CUBE` (for `samplerCube`)
- Per-mip UAV views: `VK_IMAGE_VIEW_TYPE_2D_ARRAY, layerCount=6`
  (Vulkan forbids `CUBE` views as storage images)

### `IRHICommandList::GenerateMipmaps`

Blit chain from mip 0→1→…→N-1. Input must be in `ShaderRead`; output all mips
in `ShaderRead`. Texture must have `CopySrc` usage bit.

### `IRHIDevice::GetMemoryStats` → `RHIMemoryStats`

Cross-device memory snapshot, populated by `RenderGraph::Execute()` at the end of each frame.

```cpp
struct RHIMemoryStats {
    uint64_t gpuTextureBytes = 0;  // logical sum of all live (non-swapchain) textures
    uint64_t gpuBufferBytes  = 0;  // logical sum of all live buffers
    uint64_t gpuUsedBytes    = 0;  // VMA device-local heap usage (real VRAM including alignment)
    uint64_t gpuBudgetBytes  = 0;  // VMA device-local heap budget (OS-reported)
};
```

`VulkanDevice` implements it via:
- `m_textures`/`m_buffers` iteration for logical byte sums
- `vmaGetHeapBudgets` filtered to `VK_MEMORY_HEAP_DEVICE_LOCAL_BIT` for real VRAM numbers

### Cross-Platform CPU Memory

`src/platform/PlatformMemory.hpp` provides `Platform::GetProcessMemoryBytes()`:
- Windows: `GetProcessMemoryInfo` → `WorkingSetSize`
- Linux: `/proc/self/status` VmRSS
- macOS: `getrusage RUSAGE_SELF ru_maxrss`

Used by `PerformancePanel` to display process RAM usage.

---

## Resource Layer — Material System

### MaterialInstance Data Flow

```
material->SetFloat("roughnessFactor", 0.5f)
    │
    ▼  lookup MaterialType::params["roughnessFactor"] → { offset, size }
    ▼  memcpy into uboData[]; dirty = true

material->Bind(cmd):
    │
    ├─ if dirty → flush()
    │    upload uboData → GPU UBO buffer
    │    vkUpdateDescriptorSets (texture descriptors)
    │    dirty = false
    │
    └─ vkCmdBindDescriptorSets(set=1, descriptorSet)
```

### ShaderProgram

Compiled VS+FS pair. Manages:
- `RHIShaderHandle` vert + frag
- Merged `ShaderReflection` (stage flags OR-ed)
- `RHIDescLayoutHandle` for set=1 (material-specific, from reflection)
- Pipeline cache: `AttachmentKey → RHIPipelineHandle` (lazy, per RT format combo)

```
Descriptor set convention (graphics):
  set=0  per-frame globals (FrameUniformsBuffer)
  set=1  per-material params (MaterialInstance / ShaderProgram)
```

### ComputeProgram

Single compute shader. No `AttachmentKey` cache.

```cpp
class ComputeProgram {
    bool Load(IRHIDevice*, const Desc&);   // Desc: {spv, refl, optional frameLayout}
    void Unload(IRHIDevice*);
    RHIPipelineHandle   GetPipeline(IRHIDevice*);     // created once, cached
    RHIDescLayoutHandle GetLayout(uint32_t setIndex); // auto from reflection
    const ShaderReflection& GetReflection() const;
};
```

### MaterialManager

```
Init(IRHIDevice*, ResourceManager*)
    // ResourceManager provides BuiltinTexture::White1x1 for unset slots.

RegisterTypeFromShaders(MaterialTypeDesc, FeatureInitContext)
    //   struct MaterialTypeDesc {
    //       string name, vertShader, fragShader;  // shader stem, e.g. "pbr"
    //       RHICullMode cullMode  = Back;
    //       bool depthTest  = true;
    //       bool depthWrite = true;
    //   };

LoadMaterial(AssetID, ResourceManager&) → MaterialInstance*  (cached; VFS paths set centrally)
CloneInstance(MaterialInstance*) → unique_ptr<MaterialInstance>       (non-cached copy)
ClearProjectInstances()  — evict m_cachedInstances on project switch; preserves m_types
Shutdown()
```

`CloneInstance` is used by the render system to produce per-entity material copies
that receive `MaterialOverrideComponent` overrides.

---

## Function Layer — RenderGraph

**Location:** `src/function/render_graph/RenderGraph.hpp/.cpp`

### Resource Access Declarations

| Method | Target State | Vulkan Layout |
|--------|-------------|--------------|
| `b.Write(tex)` | `RenderTarget` | `COLOR_ATTACHMENT_OPTIMAL` |
| `b.WriteDepth(tex)` | `DepthWrite` | `DEPTH_ATTACHMENT_OPTIMAL` |
| `b.WriteUAV(tex)` | `UnorderedAccess` | `GENERAL` |
| `b.Read(tex)` | `ShaderRead` | `SHADER_READ_ONLY_OPTIMAL` |

**Read+Write on the same texture is ordering-only.** The topological sort builds
edges from Read→Write only (pure Write→Write does *not* constrain order). When a
pass needs to composite onto a texture written by an earlier pass (e.g.
`InfiniteGrid` / `DebugOverlay` / `SelectionOutline` drawing on top of Tonemap's
swapchain output), declare both `b.Read(rgSwap)` and `b.Write(rgSwap)`. The
executor recognises this combination and **skips the SHADER_READ transition**
— layout stays in `RenderTarget`, the Write block emits a `RT→RT` memory barrier
for the write-after-write hazard. This is essential for the swapchain, which is
not created with `VK_IMAGE_USAGE_SAMPLED_BIT` and cannot legally transition to
`SHADER_READ_ONLY_OPTIMAL`.

### Compile & Execute

```
Reset()
CreateTexture(name, desc)  → RGTextureHandle   (transient; GPU alloc deferred)
ImportTexture(name, handle, initState, finalState)  → RGTextureHandle
CreateBuffer(name, RGBufferDesc)  → RGBufferHandle  (transient; clearOnCreate=true → FillBuffer(0) before first write)
ImportBuffer(name, handle, initState) → RGBufferHandle  (persistent cross-frame buffers, e.g. exposure SSBO)
AddPass(name, setupFn, executeFn)
  — passes must be declared in forward dependency order (writers before readers)
  — setupFn: b.Read/Write/WriteDepth/WriteUAV(tex), b.ReadBuffer/WriteBuffer(buf)

Compile()
  Phase 0: Kahn's topological sort on read/write dependency edges (textures + buffers)
  Phase A: lifetime analysis — firstWritePass / lastReadPass per transient texture/buffer
  Phase B: greedy interval slot coloring — textures → RGPhysicalSlot; buffers → RGPhysicalBufferSlot
           clearOnCreate buffers get FillBuffer(0) + BufferBarrier(CopyDst→StorageWrite) injected

Execute(device, cmd)
  → AllocateSlots(device) — idempotent; creates slot GPU textures/buffers if not already valid
  → for each sorted pass:
      emit barriers — per-PHYSICAL-SLOT state tracking for transients,
                      per-logical-index tracking for imported textures/buffers
      call executeFn(cmd, resources)
  → final state transitions for imported textures/buffers
  → fill m_lastStats (RGStats) + snapshot device.GetMemoryStats() → m_lastMemStats

GetResolvedHandle(h)  — returns physical RHITextureHandle for any RGTextureHandle
                        after AllocateSlots: imported → t.imported; transient → slot handle
GetResolvedBuffer(h)  — returns physical RHIBufferHandle for any RGBufferHandle
GetLastFrameStats()  const → const RGStats&
GetLastMemoryStats() const → const RHIMemoryStats&
InvalidateSlots()           — call on resize; destroys all slot GPU textures and buffers
```

### FrameContext::BindTexture / BindBuffer — Deferred Descriptor Resolution

`ctx.BindTexture(set, binding, rgHandle)` and `ctx.BindBuffer(set, binding, rgHandle)` called
during `AddPasses` **do not** write descriptors immediately. Each enqueues a
`PendingBinding{set, binding, handle}` (texture or buffer variant).

After `Compile()`, `SceneRenderer` calls:
```
m_rg.AllocateSlots(*m_device)   // create/reuse slot GPU textures and buffers
ctx.FlushBindings()             // GetResolvedHandle/GetResolvedBuffer → WriteDescriptorTexture/Buffer
m_rg.Execute(*m_device, *cmd)   // AllocateSlots is no-op; descriptors already valid
```

This allows **transient** textures and buffers to be bound to descriptor sets during
`AddPasses` — the physical handle is unknown until `AllocateSlots`, but `FlushBindings`
runs immediately after. `HDR_Color` (transient texture) and `AE_Histo` (transient buffer)
are examples bound via this mechanism.

### RGPhysicalSlot

```cpp
struct RGPhysicalSlot {
    RHITextureDesc   desc;
    RHITextureHandle handle;       // persistent across frames; recreated on desc change
    int              freeAfterPass = -1;  // last sorted-pass index that reads this slot
};
```

`m_slots` is a `std::vector<RGPhysicalSlot>` that grows but never shrinks (except on
`InvalidateSlots`). Once the pipeline stabilises, the slot count converges.

### RGStats

Per-frame read-only snapshot written at the end of `Execute()`.

```cpp
struct RGStats {
    uint32_t transientCount;          // CreateTexture() textures
    uint32_t importedCount;           // ImportTexture() textures
    uint32_t physicalSlotCount;       // actual RGPhysicalSlot entries (≤ transientCount)
    uint64_t transientBytesLogical;   // sum of logical transient sizes
    uint64_t transientBytesPhysical;  // sum of physical slot sizes (< logical when aliasing)
    uint64_t importedBytesLogical;    // sum of imported texture sizes
    // Buffer tracking
    uint32_t transientBufferCount;          // CreateBuffer() buffers
    uint32_t importedBufferCount;           // ImportBuffer() buffers
    uint32_t physicalBufferSlotCount;       // RGPhysicalBufferSlot entries (≤ transientBufferCount)
    uint64_t transientBufferBytesLogical;   // sum of logical transient buffer sizes
    uint64_t transientBufferBytesPhysical;  // sum of physical buffer slot sizes
    struct Entry {
        std::string name;
        uint32_t width, height, mipLevels;
        const char* formatStr;
        uint64_t bytes;
        int slotIndex;  // ≥ 0 for transients; -1 for imported
    };
    std::vector<Entry> entries;   // one per transient texture
};
```

Exposed via `SceneRenderer::GetRenderGraph()` → `PerformancePanel` → "Render Stats"
collapsing header (imported + transient counts/MB, physical vs logical savings, per-texture detail table).

---

## Compute Pipeline & ComputeProgram

### Frame Uniforms (set=0) Bindings

```
binding=0  FrameData UBO    — camera matrices, time, resolution, SH9 irradiance, TAA jitter/prevViewProj (640 bytes)
binding=1  LightData UBO    — up to 8 lights (directional / point / spot / area)
binding=2  sampler2D        — BRDF LUT
binding=3  samplerCube      — prefiltered specular env (5 mips)
binding=4  samplerCube      — skybox cubemap
binding=5  sampler2D        — LTC inverse-M matrix LUT (64×64 RGBA32F) — area lights
binding=6  sampler2D        — LTC amplitude/GGX-norm LUT (64×64 RGBA32F) — area lights
```

### LightData Layout

```glsl
// LightEntry (96 bytes, std140):
//   direction + intensity   (vec3 + float)   0..16
//   color + range           (vec3 + float)   16..32
//   position + type         (vec3 + int)     32..48   type: 0=dir 1=point 2=spot 3=area
//   innerAngle + outerAngle (float + float)  48..56   (spot) OR areaSize (area)
//   _align0 + _align1       (float + float)  56..64   std140 padding
//   tangentU + _pad0        (vec3 + float)   64..80   (area: width direction from world matrix col 0)
//   tangentV + _pad1        (vec3 + float)   80..96   (area: height direction from world matrix col 2)
// sizeof(LightEntry) = 96; LightUniforms = 16 + 8×96 = 784 bytes
```

### Storage Image Format Rules

| GLSL Qualifier | RHIFormat | VkFormat |
|---|---|---|
| `rgba16f` | `RGBA16F` | `VK_FORMAT_R16G16B16A16_SFLOAT` |
| `rgba32f` | `RGBA32F` | `VK_FORMAT_R32G32B32A32_SFLOAT` |

`VK_FORMAT_R8G8B8A8_UNORM` is not mandatorily supported as a storage image.

---

## GPU IBL Bake (Runtime)

**Location:** `src/function/ibl/GpuIblBake.hpp/.cpp`, `src/function/ibl/SHProjection.hpp/.cpp`

Called by `SceneRenderer::SetIBL()` when cooked IBL assets are absent from the cook cache.

| Map | Size | Format | Description |
|-----|------|--------|-------------|
| `brdfLut` | 512×512 | RGBA32F, 1 mip | Split-sum scale/bias |
| `prefilteredEnv` | 512×512×6 cubemap | RGBA32F, 5 mips | GGX specular |
| `skyboxCubemap` | 1024×1024×6 cubemap | RGBA32F, 1 mip | Direct HDR sky |
| `shCoeffs[9]` | CPU `glm::vec4[9]` | — | SH9 diffuse |

### Integration with SceneRenderer

```
1. If ws.sh9 + ws.prefilteredEnv + ws.brdfLut + ws.skyboxCubemap all in cook cache
   → LoadSH9Coeffs + LoadTexture × 3, bind to FrameUniformsBuffer.

2. Otherwise:
   a. IBL::ProjectHDRtoSH(hdr) → outSH[9]          (CPU, fast)
   b. GpuIblBake::Bake(device, hdr) → Result        (GPU, ~100 ms first run)
   c. Save results → .satex / .sash9 in cook cache
   d. Bind textures to FrameUniformsBuffer
```

### Bake Sequence (GpuIblBake::Bake)

```
CPU: IBL::ProjectHDRtoSH(equirect) → shCoeffs[9]

GPU (ImmediateCompute):
  Step 1: equirect → intermCube (512), skyboxCubemap (1024)   [equirect_to_cube.comp]
  Step 2: GenerateMipmaps(intermCube) — 10 mips for LOD selection
  Step 3: BRDF LUT integration                                 [ibl_brdf_lut.comp]
  Step 4: per-mip GGX prefilter (mip 0..4, roughness 0→1)    [ibl_prefilter.comp]
  Transition all outputs → ShaderRead
```

---

## Deferred Rendering Pipeline

### G-Buffer Layout

| Attachment | Format | Contents |
|------------|--------|----------|
| RT0 | `RGBA8_UNORM` | albedo.rgb + occlusion.a |
| RT1 | `RGBA16_SFLOAT` | oct-encoded normal (RG) + roughness (B) + metallic (A) |
| RT2 | `RGBA16_SFLOAT` | data.rgb + encoded shading-model ID (A) |
| Depth | `D32_SFLOAT` | View-space depth; Lighting pass reconstructs world position via `invViewProj × NDC` |

**RT2.a** encodes the shading model ID via `EncodeShadingFlags(modelID)` / `DecodeShadingModelID(a)`.
`SHADING_MODEL_PBR = 0`; custom evaluators are assigned IDs ≥ 1 in stable alphabetical order.

Normal encoding uses **Octahedral Normal Encoding** (OctEncode/OctDecode).

### Built-in Render Features (pass order)

| Feature | Condition | Output | Shader |
|---------|-----------|--------|--------|
| `ShadowFeature` | `config.shadowEnabled` | shadow map (D32, 2048²) | `shadow.vert/.frag` (+ `shadow_skinned.vert` for skinned) |
| `SkyboxFeature` | always | HDR buffer (transient) | `skybox.vert/.frag` |
| `GBufferFeature` | always | RT0/RT1/RT2 + depth | `deferred_geometry.vert/.frag` (+ `deferred_geometry_skinned.vert` for skinned) |
| `SSAOFeature` | always registered; disabled → fills ssaoTex with 1.0 | half-res R8 AO → blurred into `ssaoTex` | `ssao.frag` + `ssao_blur.frag` |
| `DeferredLightingFeature` | always | HDR (transient RGBA16F) | `deferred_lighting.frag` |
| `SelectionMaskFeature` | always | R8 silhouette mask | `selection_mask.vert/.frag` (+ `selection_mask_skinned.vert` for skinned) |
| `TAAFeature` | always registered; disabled → passes `handles.hdr` through | TAA-resolved into ping-pong history; `handles.taaResolved` | `taa_resolve.frag` |
| `AutoExposureFeature` | always registered; skips if `pp.autoExposureEnabled==false` | 256-bin log-lum histogram → weighted percentile EV → exponential-smoothing exposure; 1-frame CPU readback via staging; reads `handles.hdr` (pre-TAA content) | `postfx_histogram.comp`, `postfx_exposure_adapt.comp` |
| `BloomFeature` | always registered; skips if `pp.bloomEnabled==false` | threshold reads `taaResolved`; composite writes back to `handles.hdr` | `bloom_*.frag` |
| `DoFFeature` | always registered; skips if `pp.dofEnabled==false` | CoC from depth → separable near/far Gaussian blur (H+V × 2) → smoothstep composite; sets `handles.hdr` to DoF output | `dof_coc.frag`, `dof_blur.frag`, `dof_composite.frag` |
| `TonemapFeature` | always registered; active when `pp.tonemapMode==Builtin` | swapchain LDR | `postfx_tonemap.frag` (ACES + optional parametric CG LUT via `sampler3D`); rebakes 32³ LUT via `ImmediateCompute` when `ColorGradingSettings` changes |
| `LutTonemapFeature` | hot-swapped in when `pp.tonemapMode==LUT` | swapchain LDR | `postfx_lut_tonemap.frag` |
| `SelectionOutlineFeature` | always | outline on swapchain | `selection_outline_dilate.frag` + composite |
| `InfiniteGridFeature` | when enabled | XZ grid on swapchain | `infinite_grid.frag` |
| `DebugOverlayFeature` | always | debug lines on swapchain | `debug_line.vert/.frag` |
| user `RenderFeature`s | `AddFeature(...)` | custom | custom |

`BloomFeature`, `TAAFeature`, and `DoFFeature` are always in the feature list; their `AddPasses` early-returns when disabled.
`TonemapFeature` ↔ `LutTonemapFeature` hot-swapped at runtime by `ApplyWorldSettings` (WaitIdle + slot replace).

```cpp
struct RendererConfig {
    bool     shadowEnabled = true;
    uint32_t shadowMapSize = 2048;
    int      bloomMipCount = 3;   // engine-level startup default; runtime changes via PostProcessSettings::bloomMipLevels
};
```

### Bloom

1. **Threshold pass** — extract pixels where luminance > 1.0
2. **Downsample chain** — `bloomMipLevels` levels (default 3, range 2–8), 13-tap COD downsample
3. **Upsample + accumulate** — bilinear upsample, weighted additive blend
4. **Composite** — additive blend into HDR color buffer

### TAA (Temporal Anti-Aliasing)

**Placement:** TAA runs after `SelectionMask` and before `Bloom`.

**Data flow:**
```
GBuffer(jittered proj) → DeferredLighting → TAAFeature
  TAA_Resolve: Read(handles.hdr, historyRead, depth) → Write(historyWrite)
  handles.taaResolved = rgHistoryWrite
→ BloomThreshold reads handles.taaResolved (anti-aliased pre-bloom)
→ BloomComposite writes handles.hdr
→ DoFFeature reads handles.hdr (bloom-composited) + handles.depth
  DoF_CoC / DoF_NearH / DoF_NearV / DoF_FarH / DoF_FarV / DoF_Composite
  handles.hdr = dofOutput (RGBA16F transient)
→ Tonemap reads handles.hdr
```

**Jitter:** Halton(2,3) sequence, 8-tap, written to `fu.jitter` (pixel space) and added to `proj[2][0/1]` (NDC offset). `fu.prevViewProj` stores last frame's unjittered view-projection for reprojection.

**TAAFeature internals:**
- Ping-pong `m_historyTex[2]` (persistent RGBA16F, full-res) — imported into RG each frame
- TAA_Resolve: depth reprojection → prev UV → sample history → 3×3 YCoCg AABB neighborhood clamp → motion-adaptive blend
- `m_historyValid = false` on first frame or resize → shader uses `historyValid = 0` push constant → outputs current unmodified

**`handles.taaResolved`:** new field on `RendererHandles`; equals `handles.hdr` when TAA is disabled, set to `rgHistoryWrite` after `TAAFeature::AddPasses`. `BloomThreshold` reads this to avoid Bloom accumulating in TAA history (prevents progressive brightness).

### CPU Frustum Culling & BVH

**File:** `src/core/spatial/BVHTree.hpp`

`BVHTree<T>` is a pure-algorithm template (glm only, no ECS dependency) providing:
- **Build:** median-split on longest centroid axis, O(N log N)
- **Query(Frustum):** p-vertex half-space test per node; prunes entire subtrees
- **Raycast(Ray):** slab test, nearest child first for early termination
- **`RayAABB(ray, mn, mx, tMax, tHit)`:** public static slab test — exposed for reuse outside the BVH (Phase B picking)

`Frustum::Extract(viewProj)` uses Gribb-Hartmann for Vulkan NDC [0,1] depth (near = row2, far = row3−row2).

`SceneRenderer` instantiates `BVHTree<entt::entity>` with **one leaf per entity** (union of all submesh world AABBs). This matches Unity/UE granularity: culling is at the component level, not per-submesh.

**Per-frame flow:**
```
BuildDrawList:  GPUSubMesh.boundsMin/Max  →  ArvoAABB(wt*localT)  →  m_bvh.Insert(entity)
                                          →  DrawItem::worldAABBMin/Max (per submesh, for Phase B)
                m_bvh.Build()
RenderFrame:    Frustum::Extract(viewProj)  →  m_bvh.Query  →  m_visibleDrawItems
GBufferFeature: iterates m_visibleDrawItems (pointers into m_drawItems)
```

Skinned meshes set `DrawItem::skipCull = true` and bypass BVH; they are always included in `m_visibleDrawItems`.
Skinned meshes also store a `worldAABBMin/Max` approximated from bind-pose bounds × world transform, for Phase B picking.

**`SceneRenderer::RaycastScene(ray)`** — two-phase AABB picking:
- **Phase A:** `m_bvh.Raycast()` — nearest static mesh entity via BVH slab test
- **Phase B:** brute-force scan of `m_drawItems` where `skipCull==true` using `BVHTree::RayAABB` on `DrawItem::worldAABBMin/Max`; returns closest among both phases

**`GPUSubMesh` bounds** are computed in `ResourceManager::LoadMesh` by iterating raw vertex data (stride=48, first 12 bytes = vec3 position) before GPU upload.

Tracy plot: `SA_PROFILE_PLOT("VisibleDrawItems", ...)` tracks cull ratio per frame.

---

## Custom Shading Models

Custom shading models are defined in `.saglsl` unified shader files placed in the project's `assets/` directory. They are cooked **at runtime** (no engine recompile required).

### `.saglsl` File Format

```glsl
// @ShaderName  "My Shader"
// @ShadingModel MyShader        // CamelCase; also the MaterialType name
// @VertShader   deferred_geometry  // optional, default shown

#pragma sa_section gbuffer
// Full GLSL fragment shader writing the 3-MRT G-Buffer layout
#pragma sa_end_section

#pragma sa_section lighting
vec3 EvaluateShading(GBufferData gbuf) { ... }
#pragma sa_end_section
```

### Runtime Cook Flow

When the editor loads a project with `.saglsl` files, `EditorMode::LoadProject` runs:

```
EditorMode::LoadProject()
  ├─ ShaderCook::HasSaglslFiles()     ← skip entirely if no .saglsl present
  ├─ ShaderCook::CookDirectory()      ← mtime-based incremental cook:
  │     compare each .saglsl mtime vs .shader_manifest.json; skip unchanged files
  │     parse .saglsl, generate dispatch GLSL (shading_model_ids.glsl +
  │     shading_dispatch.glsl), compile *.gbuffer.frag → .spv + .refl,
  │     inject @ShadingModel / @VertShader into .refl (ShaderReflection metadata)
  ├─ ShaderCook::RecompileDeferredLighting()  ← only if dispatch changed;
  │     recompile deferred_lighting.frag with the project dispatch (glslc subprocess)
  │     using -I ENGINE_SHADER_SRC_DIR -I cook_cache/generated/shaders/
  ├─ ClearProjectAssets() / ClearProjectInstances()  ← WaitIdle inside
  └─ ApplyProjectShaderTypes(cookedShaderDir)
       ├─ MaterialManager::ClearProjectTypes()
       ├─ DeferredLightingFeature::ReloadShaders()   ← hot-swap frag SPV
       └─ MaterialManager::RegisterTypesFromShaderDir(isProjectType=true)
             reads ShaderReflection::shadingModel / vertShader to auto-register types
```

Outputs land in `cook_cache/shaders/` (SPV + refl) and `cook_cache/generated/shaders/` (dispatch GLSL).
`cook_cache/.shader_manifest.json` records per-file mtime for incremental cook on subsequent loads.

**Cross-directory vertex shader resolution.** Project material types live in
`cook_cache/shaders/` but typically reference `deferred_geometry.vert` from the
engine builtin dir. `FeatureInitContext` carries an `engineShaderDir` field as a
fallback for `MaterialManager::RegisterTypeFromShaders`: each vert/frag SPV +
.refl path is first looked up under `ctx.shaderDir`, then under
`ctx.engineShaderDir` if missing. At engine init both fields point to the engine
dir; `ApplyProjectShaderTypes` sets `shaderDir = cook_cache/shaders/` while
keeping `engineShaderDir` on the engine dir.

### Key Properties

- `MaterialType::isProjectType = true` marks types that come from `.saglsl` files; `ClearProjectTypes()` removes them on project switch, preserving builtin types (PBR, DeferredLighting, etc.).
- `SHADING_MODEL_PBR = 0` is always reserved; custom models are assigned IDs 1..N in alphabetical order.
- The vertex shader (`deferred_geometry.vert`) is shared — only the fragment shader differs.
- `StellarAliaShaderCookLib` (`tools/shader_cook/`) is a static library linked by the editor, analogous to `StellarAliaImporter`.
- **ShaderReflection metadata:** after compiling a `.saglsl` to `.refl`, `ShaderCookLib` injects `ShaderReflection::shadingModel` (from `@ShadingModel`) and `ShaderReflection::vertShader` (from `@VertShader`) into the sidecar file. `RegisterTypesFromShaderDir` reads these fields to auto-register each compiled shader as a `MaterialType` without any hardcoded list.
- **Cook config constants** (`ApplicationPath.hpp.in`): `GLSLC_PATH`, `BIN_DIR` (location of `ShaderReflectTool`), and `ENGINE_SHADER_SRC_DIR` (`@CMAKE_SOURCE_DIR@/assets/shaders`) are baked in at configure time and used by `EditorMode` to invoke `ShaderCookLib` at runtime.

---

## Frame Loop

```
── App code ──────────────────────────────────────────────────────────────────

window->PollEvents()
input.Poll()                               // snapshot devices + evaluate active map
sync swapchain extent if window->Get{W,H}() != device->GetSwapchain{W,H}() — pre-stages
                                              ResizeSwapchain() so RenderFrame and its
                                              internal RTs see the new size in the same
                                              frame the OS callback fires
mode.OnUpdate(dt)                          // editor camera / gameplay logic
[Playing] physics.SyncIn / Step / SyncOut (fixedDt accumulator, GetActiveScene())
[Playing] scriptSystem.FixedUpdate(fixedDt, GetActiveScene().Registry())
[Playing] animSystem.Update(dt, GetActiveScene().Registry())
[Playing] scriptSystem.Update(dt, GetActiveScene().Registry())
[Playing] scriptSystem.LateUpdate(dt, GetActiveScene().Registry())
GetActiveScene().UpdateTransforms()        // BFS propagate dirty transforms
renderer.RenderFrame(GetActiveScene(), w, h)  // full frame — all phases internal
mode.OnRenderUI(cmd)                       // ImGui draw calls (editor panels, gizmo)

── Inside RenderFrame ────────────────────────────────────────────────────────

Phase 1: Collect
   FillCameraUniforms(scene, w, h)   // WorldTransformComponent → view/proj
   GatherLights(scene)               // all light component types

Phase 2: GPU
   device->BeginFrame()           ← fence wait; AutoExposureFeature::ReadbackExposure() called here
                                    (maps staging buffer → m_currentExposure; updates tonemap exposure)
   if (scene.IsAndClearMaterialDirty()) BuildDrawList(scene)
   FrustumCull: Frustum::Extract(viewProj) → m_bvh.Query → m_visibleDrawItems
   frameUniforms.Upload(fi, fu, lu)
   m_rg.Reset()
   ImportTexture(swapchain, depth, gbuffers, shadowMap, ssaoTex, selectionMask, bloomMips, taaHistory)
   CreateTexture(HDR_Color RGBA16F transient)   ← hdr is now transient

   ShadowFeature::AddPasses()
   SkyboxFeature::AddPasses()
   GBufferFeature::AddPasses()        ← iterates m_visibleDrawItems
   SSAOFeature::AddPasses()           ← GTAO 3-pass; disabled → fill 1.0
   DeferredLightingFeature::AddPasses() ← reads ssaoTex binding=5
   SelectionMaskFeature::AddPasses()
   TAAFeature::AddPasses()            ← jittered resolve; sets handles.taaResolved
   AutoExposureFeature::AddPasses()   ← histogram(hdr) + adapt; 1-frame readback feeds tonemap exposure
   BloomFeature::AddPasses()          ← threshold reads taaResolved; composite writes handles.hdr
   DoFFeature::AddPasses()            ← 6 passes (CoC+4×blur+composite); sets handles.hdr = dofOutput when enabled
   TonemapFeature::AddPasses()
   SelectionOutlineFeature::AddPasses()
   InfiniteGridFeature::AddPasses()
   DebugOverlayFeature::AddPasses()
   for each user RenderFeature: feature.AddPasses(...)

   m_rg.Compile()
   m_rg.AllocateSlots()
   ctx.FlushBindings()   ← WriteDescriptorTexture for all pending bindings
   m_rg.Execute()        ← AllocateSlots no-op; commands recorded with valid descriptors
   device->EndFrame() / Present()
```

---

## Editor Architecture

**Location:** `editor/`

`EditorMode` owns all editor-specific state and drives the editor per-frame logic.

### EditorContext — Dependency Injection Container

`editor/EditorContext.hpp` defines a plain non-owning struct that bundles every
dependency a panel may need:

```cpp
struct EditorContext {
    // Engine systems (owned by Application)
    Application*               app;
    Scene*                     scene;
    entt::registry*            registry;
    Resource::AssetRegistry*   assetReg;
    MaterialManager*           matMgr;
    Resource::ResourceManager* resMgr;
    InputSystem*               input;

    // Editor systems (owned by EditorMode)
    EditorSelection*           selection;        // centralised entity/asset selection
    EditorDiagnostics*         diagnostics;
    EditorLogCapture*          logCapture;
    EditorIconCache*           iconCache;
    ImFont*                    iconFont;
    EditorShortcutConfig*      shortcuts;
    EditorOverlaySettings*     overlaySettings;
    EntityTemplateRegistry*    templateReg;
    ProjectManager*            projectMgr;
    ComponentDrawerRegistry*   drawerRegistry;   // owned by EditorMode::m_drawerRegistry
    EditorActionRegistry*      actionReg;        // unified action dispatch (Issue #66)
    CommandManager*            cmdMgr;           // Undo/Redo stack (Issue #66)

    std::filesystem::path      projectDir;

    // Callbacks set by BuildContext()
    std::function<void(const std::filesystem::path&)> onSceneLoad;
    std::function<void(glm::vec3)>                    onFocusEntity;
    std::function<void()>                             onAssetsImport;
    std::function<void()>                             onCookShaders;
    std::function<void(std::filesystem::path)>        onProjectSelected;
};
```

`EditorMode::BuildContext(app)` fills all fields and is called in `OnAttach` after
`EditorIconCache::Init` (so `iconCache`/`iconFont` are valid at panel construction time)
and before any panel is constructed. It also:
- registers all 14 built-in component drawers into `m_drawerRegistry` → `ctx.drawerRegistry`
- registers all built-in `EditorAction` entries into `m_actionRegistry` → `ctx.actionReg`
- wires `ctx.cmdMgr = &m_commandManager`

Every panel constructor takes `EditorContext&` as its sole parameter — there are no
`SetXxx` initialization setters anywhere in the panel layer.

**Construction order in OnAttach (enforced):**
```
m_ui.Init(...)
m_iconCache->Init(...)      ← must precede BuildContext
BuildContext(app)
SceneHierarchyPanel(ctx)    ← writes ctx.selection on every user interaction
AssetsPanel(ctx)            ← writes ctx.selection on asset click
InspectorPanel(ctx)         ← reads ctx.selection only; no panel cross-refs
```

### EditorSelection — Centralised Selection State

`editor/EditorSelection.hpp` owns the single source of truth for what the user has
selected. All panels that need to know the selection read from it; all panels that
change the selection write to it.

```
EditorSelectionType { None, Entity, Asset }

Write path:
  SceneHierarchyPanel  → SelectEntities() / SelectEntity() / Clear()
  AssetsPanel          → SelectAsset() / Clear()           (via SetSelectedPath helper)
  EditorMode viewport  → SelectEntity() / Clear()          (picking + gizmo)
  EditorMode PIE stop  → Clear()                           (game IDs invalidated)

Read path:
  InspectorPanel       → GetType() / GetPrimaryEntity() / GetSelectedAsset()
  EditorMode overlays  → GetPrimaryEntity()                (gizmo, billboard, outline)
```

`SceneHierarchyPanel` retains its own `m_selection` / `m_primarySelected` for rendering
(highlight, rename, drag-drop) and calls `SyncSelectionToCtx()` after every mutation to
push the canonical state into `EditorSelection`. `AssetsPanel` uses a private
`SetSelectedPath(p)` helper that writes `m_selectedPath` and forwards to
`SelectAsset(p)` in one call. `InspectorPanel` holds only `const EditorSelection*` and
reads `GetType()` each frame to decide whether to show an entity inspector or an asset
inspector — the former `const SceneHierarchyPanel* m_hierarchy` and `const AssetsPanel*
m_assetsPanel` cross-panel pointer fields are removed.

### ComponentDrawerRegistry — Component Drawer Pipeline

`editor/ui/drawers/ComponentDrawerRegistry` owns the ordered list of
`IComponentDrawer` instances and drives the per-entity Inspector render:

```cpp
// IComponentDrawer (editor/ui/IComponentDrawer.hpp)
virtual bool TryDraw(entt::registry&, entt::entity, Scene&, EditorContext&) = 0;
// Returns true if the component was present; drawers access engine resources via ctx.

// ComponentDrawerRegistry (editor/ui/drawers/ComponentDrawerRegistry.hpp)
void Register(unique_ptr<IComponentDrawer>);
void DrawAll(entt::registry&, entt::entity, Scene&, EditorContext&);
```

Each drawer lives in its own `.hpp`/`.cpp` file under `editor/ui/drawers/`:

| Drawer | Component |
|--------|-----------|
| `TagDrawer` | `TagComponent` — entity name field |
| `TransformDrawer` | `TransformComponent` — position/rotation/scale; proportional-scale lock button; calls `MarkDirty` + `MarkMaterialDirty` on any change so BVH stays in sync |
| `CameraDrawer` | `CameraComponent` |
| `DirectionalLightDrawer`, `PointLightDrawer`, `SpotLightDrawer`, `AreaLightDrawer` | all four light types (in `LightDrawers.cpp`) |
| `StaticMeshDrawer` | `StaticMeshComponent` |
| `MeshRendererDrawer` | `MeshRendererComponent` |
| `AnimatorDrawer` | `AnimatorComponent` |
| `SkinnedMeshDrawer` | `SkinnedMeshComponent` |
| `MaterialOverrideDrawer` | `MaterialOverrideComponent` |
| `RigidBodyDrawer` | `RigidBodyComponent` |
| `ColliderDrawer` | `ColliderComponent` |
| `ScriptDrawer` | `ScriptComponent` — script path + class name fields |

Shared inline helpers (`DrawAssetIDField`, `RemoveButton`, `HeaderFlags`) live in
`editor/ui/drawers/DrawerHelpers.hpp`. The monolithic `editor/ui/ComponentDrawers.hpp`
is deleted. Registration order in `BuildContext` equals the display order in the
Inspector.

`InspectorPanel` no longer owns a `vector<IComponentDrawer>` — it delegates
`DrawEntityInspector` to `ctx.drawerRegistry->DrawAll(...)`.

### Presenter Layer (MVP)

Each non-trivial panel has a companion `Presenter` class that owns all write
operations on engine state. The `Panel::OnDraw()` method is a **View**: it may read
engine data directly (Scene, Registry, RenderGraph) but must route every mutation
through a `RequestXxx()` call. The `Presenter::Update(float dt)` is called from
`EditorMode::OnUpdate` and drains the pending-operation queue.

```
IPresenter (virtual Update(float dt) = 0)
  ├── SceneHierarchyPresenter  — CreateEntity/DestroyEntity/SetParent/DuplicateEntity/Reparent/AssetDrop
  ├── AssetsPresenter          — NFD import, ImportFile (fs::copy_file), drop queue
  ├── PlaybackPresenter        — app->SetPlayState()
  ├── WorldSettingsPresenter   — ApplyWorldSettings() / RebakeIBL() (priority: rebake > apply-IBL > apply)
  ├── PostProcessPresenter     — ApplyWorldSettings(ws, false) on live parameter change
  ├── ShortcutsPresenter       — RegisterMaps() + NFD import/export + config file I/O
  └── ProjectBrowserPresenter  — CreateProject() + ConsumeCreateSuccess/Error result channels
```

All presenters are created in `EditorMode::BuildContext(app)` and updated every frame:

```cpp
// EditorMode::OnUpdate
m_hierPresenter->Update(dt);
m_assetsPresenter->Update(dt);
m_playbackPresenter->Update(dt);
m_worldPresenter->Update(dt);
m_ppPresenter->Update(dt);
m_shortcutsPresenter->Update(dt);
m_projectBrowserPresenter->Update(dt);
```

**Invariant:** `OnDraw()` contains no `scene.CreateEntity`, `DestroyEntity`,
`SetParent`, `SetPlayState`, `ApplyWorldSettings`, `RebakeIBL`, `RegisterMaps`,
`filesystem::copy`, or NFD file-picker calls. `PerformancePanel`, `SettingsPanel`,
have no Presenter (they are already pure View or make only negligible toggle writes).

**AssetsPresenter specifics:**
- `RequestNFDImport(const fs::path& destDir = {})` — sets `m_pendingNFDImport = true`
  and stores `destDir` as the NFD default directory hint and import destination.
  If `destDir` is empty, `RunNFDImport()` derives the destination from `EditorSelection`.
- `AssetsPanel::RequestImport()` passes `GetCurrentDestDir()` to `RequestNFDImport()`
  so the dialog opens at the panel's currently focused directory.
- `AssetsPanel::MarkFilePaneDirty()` — public method; called by `EditorMode` after
  `SaveScene` to refresh the file pane listing when a new scene file is written.

### EditorActionRegistry — Unified Action Dispatch

`editor/action/EditorActionRegistry.hpp` declares:

```cpp
struct EditorAction {
    std::string                       id;
    std::function<bool(EditorContext&)> canExecute;  // nullptr = always true
    std::function<void(EditorContext&)> execute;
};

class EditorActionRegistry {
    void Register(EditorAction);
    void Trigger(const std::string& id, EditorContext&);        // UI dispatch
    void PollAndDispatch(InputSystem&, EditorContext&);          // input dispatch
};
```

`BuildContext` registers all built-in actions (NewScene, SaveScene, SaveSceneAs,
Undo, Redo, EntityDelete, EntityDuplicate, EntityRename, EntityReparent, EntityFocus,
TogglePanels, etc.). `EditorMode::OnUpdate` replaces the former 10+ if-chain with a
single call:

```cpp
m_actionRegistry.PollAndDispatch(*m_input, ctx);
```

Menus call `m_actionRegistry.Trigger(id, ctx)` directly (e.g. from the Edit menu for
Undo/Redo). The `canExecute` predicate gates both the menu item (greyed out when false)
and the input dispatch.

**Deferred SaveScene:** `SaveScene()` checks `m_currentScenePath` — if empty *or* the
file no longer exists on disk — it sets `m_pendingSaveAs = true` and returns. `OnUpdate`
runs the NFD Save dialog when `m_pendingSaveAs` is true, writes the file, and calls
`m_assetsPanel->MarkFilePaneDirty()` on success. This keeps NFD out of the render phase.

### CommandManager — Undo/Redo Stack

`editor/command/CommandManager.hpp`:

```cpp
class IEditorCommand {
    virtual void Execute(EditorContext&) = 0;
    virtual void Undo(EditorContext&)    = 0;
    virtual std::string GetDescription() const = 0;
    virtual bool IsBoundary() const { return false; }  // play-boundary marker
};

class CommandManager {
    void Execute(unique_ptr<IEditorCommand>, EditorContext&);
    void Undo(EditorContext&);
    void Redo(EditorContext&);
    void PushPlayBoundary();  // called on PIE start
    void PopPlayBoundary();   // called on PIE stop
};
```

The stack is a `std::deque` capped at 50 entries. Undo stops at a
`PlayBoundaryMarker` sentinel inserted by `PushPlayBoundary()` (prevents undoing past
play/edit transitions). `PopPlayBoundary()` removes the marker on PIE stop.

Built-in commands (`editor/command/commands/`):

| Command | Undo behavior |
|---------|--------------|
| `DeleteEntityCommand` | Serialises entity subtree before delete; re-spawns from JSON on Undo |
| `RenameEntityCommand` | Swaps `TagComponent::name` back |
| `ReparentEntityCommand` | Restores old parent + sibling order |
| `TransformCommand` | Stores pre/post `TransformComponent`; writes back on Undo/Redo |
| `CreateMeshEntityCommand` | Destroys on Undo; re-creates on Redo |

### Systems Owned by EditorMode

| System | Purpose |
|--------|---------|
| `EditorCamera` | Free-flying orbit camera; driven by mouse look + WASD; `FocusOn(target)` repositions along current forward vector (yaw/pitch unchanged) |
| `EditorUI` | ImGui lifecycle (NewFrame / Render / backend); dockspace layout |
| `EditorOverlaySettings` | Visibility toggles for all overlay symbols |
| `EntityTemplateRegistry` | Scans `templates/entities/` for spawn menu; `DefaultScenePath()` returns the new-scene template path; used by `AssetsPanel::CreateNewFile(Scene)` |
| `ProjectManager` | Create/open/recent-projects logic; persists `recent_projects.json` |
| `ProjectBrowserPanel` | Startup modal (shown when no project loaded); not an `IEditorWindow` — driven directly from `OnRenderUI` |
| `EditorShortcutConfig` | JSON-backed user shortcut overrides (`editor_shortcuts.json`); `Load`/`Save`/`Reload`/`ImportFrom`/`ExportTo`; `ApplyTo(defaults)` replaces `bindings[0]` for overridden actions; built-in path is read-only (Save disabled in panel) |
| `EditorDiagnostics` | Collects warnings/errors for ConsolePanel Diagnostics tab (action-required events only) |
| `EditorLogCapture` | RAII spdlog sink; passively mirrors all `SA_LOG_*` calls into a ring buffer (`LogEntry { level, timeStr, message, loggerName }`); `loggerName` carries the spdlog logger name (e.g. `"script"`) for downstream routing |
| `EditorIconCache` | LRU-bounded ImGui texture cache (`kMaxThumbnails=256`): one permanent engine-logo texture + per-path thumbnail entries (lazy GPU upload via `ImageLoader` + `VulkanDevice::CreateTexture`); evict callback frees `VkDescriptorSet` + GPU texture; `IsThumbnailCached()` / `CanLoadThumbnail()` let callers gate loads without touching the cache; `ClearAllThumbnails()` must be called before `ResourceManager::ClearProjectAssets()` on project switch |
| `ComponentDrawerRegistry` | Ordered list of `IComponentDrawer` instances (one per component type); `Register(unique_ptr<IComponentDrawer>)` + `DrawAll(reg, entity, scene, ctx)`; registration order = Inspector display order; owned by `EditorMode::m_drawerRegistry`, exposed via `EditorContext::drawerRegistry` |
| `EditorActionRegistry` | Declarative action registry; `Register(EditorAction)` maps id → `{canExecute, execute}`; `PollAndDispatch(input, ctx)` replaces `OnUpdate` if-chain; `Trigger(id, ctx)` for UI dispatch (menus, buttons) |
| `CommandManager` | 50-entry deque Undo/Redo stack; `Execute(cmd, ctx)` runs + pushes; `Undo/Redo` walk the stack; `PushPlayBoundary/PopPlayBoundary` insert/remove a sentinel that prevents Undo from crossing PIE transitions |
| `ConsolePanelPresenter` | Drains `EditorLogSink` once per second; level-filters; routes entries with `loggerName=="script"` to `m_scriptEntries`; exposes `GetEngineEntries()`, `GetScriptEntries()`, unread count, and level-show toggles to `ConsolePanel` |

### Panels

| Panel | Purpose |
|-------|---------|
| `SceneHierarchyPanel` | Entity tree; multi-select (Ctrl-toggle / Shift-range / Ctrl+A all); data-driven spawn menu from `EntityTemplateRegistry`; Ctrl+D duplicate; drag-reparent; short double-click → `FocusEntityCallback` pans camera; long double-click (hold > 0.20 s) → inline rename |
| `InspectorPanel` | Reads `EditorSelection` to decide entity vs. asset view; delegates entity component rendering to `ctx.drawerRegistry->DrawAll()`; owns `IAssetInspector` map for `.mat`/`.sascene`/model/image asset inspection; drives "Add Component" popup from registered `ComponentDescriptor` list |
| `AssetsPanel` | Two-pane Explorer layout: left dir tree (`DrawDirPane`, 200 px) + right file pane (`DrawFilePane`); list/card view toggle (FA `LIST`/`BORDER_ALL` buttons); icon-size slider (16–96 px); FA6 type glyphs per file extension + lazy-loaded LRU thumbnail preview for image files; multi-select (Ctrl-toggle / Shift-range / Ctrl+A); native file picker import (nfd-extended, multi-select) via `AssetsPresenter::RequestNFDImport(GetCurrentDestDir())`; drag-to-viewport drop; Create Material/Shader/Scene/Folder (right-click context menu); `UpdateProjectDir` for runtime project switch; per-frame thumbnail budget (`kMaxThumbLoadsPerFrame=4`); `CanLoadThumbnail()` guard prevents LRU eviction thrashing; `MarkFilePaneDirty()` for external refresh (called after SaveScene); DnD empty-space targets: left pane → `m_assetsRoot`, right pane → `m_selectedDir` (`BeginDragDropTargetCustom` after each `EndChild`) |
| `ProjectBrowserPanel` | Standalone startup modal (not registered in EditorUI); three sections: Create / Open / Recent; uses NFD for folder+file picking |
| `WorldSettingsPanel` | Scene background (SolidColor/Skybox) and IBL asset pickers only |
| `PostProcessPanel` | Bloom (enabled/mipLevels/threshold/strength/radius) + Tonemap (mode/exposure/LUT picker/lutStrength) + Color Grading (enabled/Lift/Midtone/Gain/Saturation/Contrast; Builtin mode only) + SSAO (GTAO) (enabled/radius/strength/bias/directions/steps/blurSharpness) + Depth of Field (enabled/focusDistance/aperture/focalLength/samples/maxCocPx) + TAA (enabled/blendStatic/blendMotion/antiGhosting); calls `ApplyWorldSettings(ws, false)` on any change; default open |
| `PlaybackPanel` | Play / Pause / Stop buttons; triggers `Application::SetPlayState` via `PlaybackPresenter` |
| `ConsolePanel` | Two-tab panel (pure View, driven by `ConsolePanelPresenter`): **Diagnostics** (action-required events from `EditorDiagnostics` + auto-routed `loggerName=="script"` entries) + **Engine Logs** (full `SA_LOG_*` stream, per-level filter) |
| `SettingsPanel` | UI scale, overlay toggles (grid/axes/gizmo/outline/skeleton), physics debug toggles (shapes/AABBs/velocity/contacts) |
| `PerformancePanel` | Display (viewport size, FPS/frame-time); Memory (GPU VRAM progress bar from `RHIMemoryStats`, CPU process RAM); Render Stats (RGStats: imported + transient counts/MB, physical savings, per-texture detail table). Default closed. |
| `ShortcutsPanel` | Lists all `userConfigurable` Button actions; [Change] enters key-capture mode (next non-modifier key + held Ctrl/Shift/Alt → new binding); [×] clears override; [Default] reloads built-in config; [Reload] discards unsaved changes; [Import...]/[Export...] switch or copy the active config file via NFD; [Apply] rebuilds input maps; [Save] writes to active config file (disabled for built-in path); active config filename shown below toolbar |

### Windows Menu

`EditorUI::DrawPanels()` renders a **Windows** menu that provides:
- **Open All** / **Close All** — bulk toggle all registered `IEditorWindow` panels
- **Toggle Panels [F8]** (checkmark when hidden) — temporarily suppresses rendering of all panels without changing `isOpen`; pressing again restores whichever panels were open. Bound to `"TogglePanels"` action (default `F8`, user-configurable via ShortcutsPanel). Implemented via `EditorUI::m_panelsHidden` / `TogglePanelsHidden()`.
- Per-panel checkmark items — individually toggle visibility, calling `OnOpen`/`OnClose`

`IEditorWindow::isOpen = false` in the constructor gives a panel a default-closed state
(e.g. `PerformancePanel`).

### Project Management

`EditorMode::LoadProject(saprojectPath)` is the single entry point for a project switch at runtime:

```
1. Guard: return if PlayState != Editing
2. scene.Clear(); m_currentScenePath = {}
3. app.UpdateProjectPaths(projectDir, projectDir/"cook_cache")
   → propagates to VFS (SetCookCacheDir) + SceneRenderer (SetCookCacheDir)
3.5. resMgr.ClearProjectAssets()     — WaitIdle + destroy GPU textures/meshes/CPU caches
     matMgr.ClearProjectInstances()  — evict cached MaterialInstances; types survive
     m_diagnostics.ClearSource(Runtime) — clear stale runtime warnings from previous project
4. m_assetRegistry->Scan(projectDir/"assets", engineAssetsDir)
5. m_assetsPanel->UpdateProjectDir(projectDir/"assets")
6. LoadSaProject → load startupScene (warns if missing, continues with empty scene)
7. renderer.ApplyWorldSettings(scene.GetWorldSettings())
8. PrepareAnimatedEntities + RebuildDrawList
9. m_projectManager.AddRecent + SaveRecents
10. Cook-cache check: if cook_cache/ empty (ignoring .gitkeep) AND assets/ has .sameta files →
    m_diagnostics.Push(Warning, Runtime, "…run Reimport All…")
```

`ProjectBrowserPanel` is not an `IEditorWindow` — it is owned by `EditorMode` as a
`unique_ptr<ProjectBrowserPanel>` and driven directly from `OnRenderUI` after `NewFrame()`.
`ImGui::OpenPopup` + `BeginPopupModal` work correctly from this context.

`m_showProjectBrowser` is set to `true` in `OnAttach` when no project is found, and
when the user chooses File > New Project… or File > Open Project…

**TextInput map:** `EditorMode::OnUpdate` pushes the `"TextInput"` action map when
`ImGui::GetIO().WantTextInput` is true (active text entry widget), and pops it otherwise.
`WantTextInput` is narrower than `WantCaptureKeyboard` — it does not fire for panel
keyboard-nav focus, so WASD camera movement still works when panels are focused.

### Interactive Gizmo (ImGuizmo)

`EditorMode::DrawImGuizmo()` is called from `OnRenderUI`. It:
1. Computes view/proj from the editor camera (no Vulkan Y-flip for ImGuizmo)
2. Maps `GizmoMode` → `ImGuizmo::OPERATION`; Scale always uses LOCAL space
3. Draws the manipulator into `ImGui::GetBackgroundDrawList()` (behind panels, above scene)
4. On drag: decomposes the manipulated world matrix back to local space
   (`newLocal = inverse(parentWorld) × newWorld`), writes to `TransformComponent`,
   calls `Scene::MarkDirty(selected)`
5. Caches `m_gizmoIsUsing` to suppress cursor capture while dragging
6. Detects drag-end (`wasUsing && !m_gizmoIsUsing`) → calls `Scene::MarkMaterialDirty()` to
   rebuild the BVH after a gizmo transform, keeping ray-picking AABBs in sync

`TransformDrawer` uses the same pattern: it calls both `MarkDirty(entity)` and
`MarkMaterialDirty()` whenever position/rotation/scale changes via the Inspector, so
clicking on a scaled entity in the viewport always uses the updated AABB.

### Viewport Interaction (HandleViewportInteraction)

`EditorMode::HandleViewportInteraction()` is called from `OnRenderUI` after `DrawImGuizmo`. It:
1. Creates a transparent full-screen `##viewport_interact` ImGui window (`NoBringToFrontOnFocus | NoFocusOnAppearing | NoDocking`) as a drop target and picking receiver
2. Calls `ImGuizmo::SetAlternativeWindow(currentWindow)` so ImGuizmo's `IsHoveringWindow()` check accepts this window as valid — without this, the full-screen overlay makes `g.HoveredWindow` non-null and non-gizmo, causing `mbMouseOver=false` and disabling all gizmo handle hit-tests
3. `BeginDragDropTarget` — accepts `"SAASSET"` `.glb/.gltf` drops; computes world spawn position via `RayHitHorizontalPlane(ray, 0)` (or 10-unit fallback), calls `SceneHierarchyPanel::TriggerAssetDrop(path, spawnPos)`
4. Left-click picking: guarded by `!m_gizmoIsUsing && !ImGuizmo::IsOver() && IsWindowHovered()`; fires `SceneRenderer::RaycastScene(ray)` → `SceneHierarchyPanel::SetSelection(e)` or `ClearSelection()`, which in turn write into `EditorSelection`

`ScreenToWorldRay(sx, sy)` unprojects NDC via `inverse(proj * view)` at depth 0 and 1; `cam.proj` already has the Vulkan Y-flip so `ndcY = (sy/sh)*2−1` is used directly.

`SceneHierarchyPanel` public interface: `SetSelection(entity)`, `ClearSelection()`, `TriggerAssetDrop(assetPath, spawnPos)`. `AssetDropOp` has a `spawnPos` field applied to the spawned entity's `TransformComponent::position`.

### EditorOverlaySettings

Owned by `EditorMode`; `SettingsPanel` holds a raw pointer.

```cpp
struct EditorOverlaySettings {
    bool enabled;              // master switch (false during Playing/Paused)
    bool drawGrid;
    bool drawWorldAxes;
    bool drawEntityAxes;
    bool drawCameraFrustum;
    bool drawSelectionCollider;
    bool drawSkeletonGizmo;
    bool drawSelectionAABB;    // screen-space silhouette outline
    float outlineWidth;        // [1, 8] px
    bool drawGizmo;
    GizmoMode gizmoMode;       // Translate / Rotate / Scale
    bool gizmoWorldSpace;      // World vs Local (Scale always Local)
};
```

`enabled` is set to `false` by `OnPlayStateChanged(Playing/Paused)` so all overlay
draw calls are skipped with zero overhead during gameplay.

---

## Key Design Decisions

### No VkRenderPass Objects (Dynamic Rendering)
`VK_KHR_dynamic_rendering` (Vulkan 1.3 core) eliminates `VkRenderPass` and
`VkFramebuffer`. Attachment formats are embedded in `RHIPipelineDesc` and checked
at `BeginRenderPass` time. Enables lazy pipeline creation in `ShaderProgram`
without a separate render pass object per attachment format combination.

### AttachmentKey Pipeline Cache in ShaderProgram
The same shader can be used in passes with different RT format combinations.
`AttachmentKey` is a compact struct (up to 4 color formats + 1 depth format)
that hashes to a unique `VkPipeline`. Compiled once per combination, cached per `ShaderProgram`.

### VulkanCommandList Per-Frame State Reset
`VulkanCommandList::Bind(VkCommandBuffer, VulkanDevice*)` is called at the start
of every frame and from `ImmediateCompute`/`ImmediateSubmit` to wrap a different
command buffer. It clears `m_boundPipeline` to an invalid handle so that
`SetDescriptorSet`/`SetPushConstants` — both of which derive the active
`VkPipelineLayout` from `m_boundPipeline` — cannot leak the layout of the last
pipeline bound on a previous (now-unrelated) command buffer. Without this reset,
init-time `ImmediateCompute` runs (e.g. `GpuIblBake::BakeBrdfLut`) leak a
1-descriptor compute layout into frame 0, causing descriptor set / push-constant
compatibility errors when callers issue `SetDescriptorSet` before `SetPipeline`.

### ComputeProgram Has No AttachmentKey Cache
Compute pipelines have no render targets. One `VkComputePipeline` per shader,
created on first call to `GetPipeline()`.

### Descriptor Set Convention
```
set=0  per-frame globals     (FrameUniformsBuffer — bound before any pass)
set=1  per-material params   (MaterialInstance::Bind())
set=2  per-entity skinning   (GPU skinning passes only: binding0 = SkinMatrices SSBO, binding1 = SkinData SSBO)
```
ComputeProgram owns all its sets; set=0 may be a caller-supplied frame layout.
Set=2 is only bound for `isSkinned` draw items; static mesh pipelines declare no set=2 layout.

### HierarchyComponent Is Optional
Only parented entities carry `HierarchyComponent`. Child ordering within a parent
is maintained by `HierarchyComponent::children` (a `vector<entt::entity>`).
Root-level ordering is maintained by `Scene::m_rootOrder` (a `vector<entt::entity>`),
which is updated by `CreateEntity`, `DestroyEntity`, `SetParent`, and `Clear`.
`GetRootOrder()` returns this list; `MoveRootBefore/After` reorder entries.
`SceneHierarchyPanel` and `SceneSerializer` iterate roots via `GetRootOrder()` rather
than `reg.view<TagComponent>()` to preserve user-defined display and save order.

### AnimatedTransformComponent Overrides, Never Replaces
`AnimationSystem` writes `AnimatedTransformComponent` each frame;
`TransformComponent` retains the rest pose (serialised to disk).
Entities without animation incur zero overhead.

### StaticMeshComponent + MeshRendererComponent Separation
`StaticMeshComponent` carries only `meshAsset` (identity). `MeshRendererComponent`
carries material slots and shadow flags — shared between static and skinned mesh
entities without duplication. A skinned mesh entity has `SkinnedMeshComponent`
instead of `StaticMeshComponent`, but still uses `MeshRendererComponent`.

### MaterialOverrideComponent Replaces Two-Component Override
The old `PBRSurfaceComponent` (typed PBR fields) + `MaterialParamComponent` (generic map)
pair is replaced by a single `MaterialOverrideComponent` with named `scalars` and
`textures` maps. This works with any material type (PBR, custom shaders) without
baking knowledge of parameter names into the component system.

### Physics Pimpl (Jolt Fully Hidden)
`PhysicsSystem` exposes no Jolt types in its public header. All Jolt state is inside
a `Pimpl` struct. Callers never include Jolt headers, keeping compile times fast and
the physics backend swappable.

### Scripting: Function Table Instead of Shared Library
`StellarAlia.Runtime` is a managed-only assembly. The C++ side exposes engine APIs through `ScriptApiFunctionTable` — a plain struct of function pointers passed to `Initialize`. The first field is `uint32_t version` (currently `2`); if C++ and C# structs are out of sync, the managed side detects the version mismatch immediately. This avoids requiring `StellarAlia.Runtime` to P/Invoke into a named native DLL, keeping the build simple (no `SHARED` target) and the pointer table stable across reloads.

### Scripting: SDK Reference Pack for Roslyn
Roslyn compilation references the .NET SDK reference assembly pack (`Microsoft.NETCore.App.Ref`) rather than the runtime implementation assemblies. This mirrors Unity/Godot's approach: ref-pack DLLs expose only the managed API surface with no native blobs, preventing CS0433 duplicate-type errors that arise when both ref-pack and runtime assemblies define the same types (e.g. `Vector3`).

### Scripting: CollectibleALC for Hot-Reload Isolation
User scripts are loaded into a `CollectibleAssemblyLoadContext` so they can be GC-collected after `Unload()`. Dependency resolution searches `AppDomain.CurrentDomain` (not `AssemblyLoadContext.Default`) because `hostfxr` loads `StellarAlia.Runtime` into its own isolated ALC that Default cannot see.

### AppMode Is the Only Customisation Point
Application systems (Scene, Renderer, Physics, Animation, Input, Script) are fixed.
Per-project logic lives entirely in an `AppMode` subclass. This keeps the engine
core stable and the editor and game runtimes cleanly separated.

### Data-Driven Entity Templates
Spawn templates are `.sascene` files in `templates/entities/`. Adding a new
archetype (e.g. a Sphere mesh) is a file operation only — no C++ `CreateKind` enum,
no rebuild. `EntityTemplateRegistry::Scan` picks it up at editor startup.

### Pre-Allocated Descriptor Pool
A single `VkDescriptorPool` with fixed counts (`512 samplers, 256 UBOs, 256 SSBOs,
128 storage images`) is created at device init. `FREE_DESCRIPTOR_SET_BIT` allows
individual set reclamation.

### Rendering Resource Ownership

| Resource | Owner | Rationale |
|----------|-------|-----------|
| Window + Device | App | Hardware lifetime |
| ResourceManager | App | Shared asset cache |
| MaterialManager | App | Pipeline cache is per-`MaterialType` |
| AssetRegistry | App | Shared UUID index |
| FrameUniformsBuffer | SceneRenderer | Frame data is renderer-specific |
| Depth texture | SceneRenderer | Auto-resized on resolution change |
| HDR_Color | RG slot pool (transient) | Created via `CreateTexture` each frame; aliasable |
| SSAO result texture | SceneRenderer | R8_UNORM, half-res; imported into RG each frame |
| TAA history textures (×2) | `TAAFeature` | Persistent RGBA16F ping-pong; imported into RG each frame |
| AE histogram buffer | RG slot pool (transient) | Cleared each frame via `clearOnCreate=true`; aliasable |
| AE exposure SSBO | `AutoExposureFeature` | Persistent float; imported into RG as StorageWrite each frame |
| AE exposure staging | `AutoExposureFeature` | CPU-visible CopyDst; read via `ReadBufferData` after fence wait (1-frame latency) |
| DoF intermediates (CoC R32F + 5× RGBA16F) | RG slot pool (transient) | Created via `CreateTexture` each frame; 5 of 6 participate in slot aliasing (`dofOutput` lives until Tonemap) |
| CG LUT (32³ RGBA16F) | `TonemapFeature` | Persistent 3D texture; baked via `ImmediateCompute` on param change; destroyed in `OnShutdown` |
| GpuIblBake | SceneRenderer | One-shot renderer operation |
| White 1×1 | ResourceManager | `GetBuiltin(BuiltinTexture::White1x1)` |
| DrawItem list | SceneRenderer | Rebuilt by `BuildDrawList(scene)` |
| RenderFeatures | SceneRenderer | `AddFeature` transfers ownership |
| `GPUMesh::vertexBuffer/indexBuffer/skinDataBuffer` | ResourceManager | Per-asset, shared across all entities referencing same mesh |
| `SkinnedMeshComponent::skinMatricesBuffer/skinDescSet` | AnimationSystem | Per-entity; freed in `AnimationSystem::Shutdown()` |

### Index-Based Handles vs Pointers
- Safe to copy; no dangling pointer risk on pool realloc
- Explicit `IsValid()` checks; no null-pointer UB
- 32-bit footprint vs 64-bit pointer

### Dual-Path VFS (Engine Cache + Project Cache)
`VFS` holds two static paths: `s_engineCookCacheDir` (fixed at process startup from
`Application::Desc::engineCookCacheDir`) and `s_projectCookCacheDir` (updated when the
user loads a project via `Application::UpdateProjectPaths`).
`ResolveCookedPath` checks the project cache first, then falls back to the engine cache.
This ensures engine built-in assets (default meshes, materials, shaders) are always
findable regardless of which project is active, while project-specific assets shadow
engine defaults when UUIDs collide.

`ResourceManager::Init` calls `VFS::SetEngineCookCacheDir` (once, fixed).
`ResourceManager::SetProjectCookCache` calls `VFS::SetCookCacheDir` (per project switch).
`SceneRenderer::SetCookCacheDir` updates the IBL bake write path for the active project.

### GPU Skinning — Shared Mesh Data in GPUMesh
`GPUMesh` (returned by `ResourceManager::LoadMesh`) holds `vertexBuffer`, `indexBuffer`, and
`skinDataBuffer` (joints+weights SSBO). These are per-asset, cached by `AssetID`, and shared
across all entity instances that reference the same mesh. `SkinnedMeshComponent` holds only
per-entity state: `skinMatricesBuffer` (bone transforms, ~3 KB, updated each frame) and
`skinDescSet` (set=2 binding the per-entity matrices and per-asset skin data). This eliminates
N×VRAM waste when multiple entities share the same animated mesh.

### skin_deform.glsl — Shared GPU Skinning Include
`assets/shaders/skin_deform.glsl` declares the `SkinVertex` struct, the set=2 SSBO bindings
(`SkinMatrices` + `SkinData`), and the `SkinMatrix()` helper function. All three skinned vertex
shaders (`deferred_geometry_skinned.vert`, `shadow_skinned.vert`, `selection_mask_skinned.vert`)
include it, avoiding duplicated bone-blend logic across passes.

### ProjectBrowserPanel Is Not an IEditorWindow
`IEditorWindow` panels are registered with `EditorUI` and appear in the Windows menu;
they are wrapped in `ImGui::Begin/End` by the registration infrastructure.
`ProjectBrowserPanel` must be a free-floating modal (`BeginPopupModal`) that blocks all
other interaction until dismissed. Registering it as an `IEditorWindow` would break the
modal semantics. Instead it is owned as `unique_ptr<ProjectBrowserPanel>` by `EditorMode`
and driven directly from `OnRenderUI` after `NewFrame()`.

---

## Profiler

**Location:** `src/core/Profiler.hpp`

Thin wrapper around [Tracy](https://github.com/wolfpld/tracy) (submodule at
`third_party/tracy`, pinned to **v0.13.1**). All macros expand to nothing in Release
builds — zero binary overhead, no atomics, no `ScopedZone` objects.

### Public Macros

| Macro | Purpose |
|-------|---------|
| `SA_PROFILE_SCOPE()` | Zone named by `__FUNCTION__` |
| `SA_PROFILE_SCOPE_N(name)` | Zone with explicit string-literal name |
| `SA_PROFILE_SCOPE_C(name, col)` | Zone with name + `0xRRGGBB` colour |
| `SA_PROFILE_FRAME()` | Frame boundary marker (call once per frame) |
| `SA_PROFILE_PLOT(name, val)` | Numeric time-series plot |
| `SA_PROFILE_MESSAGE(str, len)` | Log message on the timeline |

### Runtime Toggle

```cpp
Profiler::SetEnabled(bool)  // pause / resume zone collection at runtime
Profiler::IsEnabled()       // query current state
```

Backed by `std::atomic<bool>` (relaxed). Each `ScopedZone` receives `IsEnabled()` as its
`active` flag — disabled zones return immediately without touching Tracy internals.

Two-level toggle:
- **Compile-time** (`TRACY_ENABLE`): whether Tracy infrastructure exists at all
- **Viewer connection** (`TRACY_ON_DEMAND`): only records when Tracy Viewer is connected; zero overhead otherwise
- **`Profiler::SetEnabled`**: fine-grained in-engine pause (e.g. editor pause button)

### CMake Integration

`third_party/CMakeLists.txt` builds `TracyClient` as a static library from
`tracy/public/TracyClient.cpp`. Target alias: `Tracy::TracyClient`.

```cmake
# TRACY_ENABLE + TRACY_ON_DEMAND only for Debug / RelWithDebInfo:
target_compile_definitions(TracyClient PUBLIC
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:TRACY_ENABLE;TRACY_ON_DEMAND>)
```

`StellarAliaRuntime` links `Tracy::TracyClient` (PUBLIC).

### Implementation Note — No `TRACY_UNIQUE`

Tracy v0.13 removed `TRACY_UNIQUE` as a public macro. `Profiler.hpp` uses its own
`SA_PP_CAT_` / `SA_PP_CAT2_` token-paste helpers (defined at the top of the header)
to generate unique `___sa_loc_N` / `___sa_zone_N` variable names via `__LINE__`.

### Profiling Zones

**`Application::Run` loop** (`src/engine/Application.cpp`):
```
Frame
  ├─ Input
  ├─ Physics
  ├─ ModeUpdate
  └─ Animation
```

**`SceneRenderer::RenderFrame`** (`src/function/renderer/SceneRenderer.cpp`):
```
RenderFrame
  ├─ Shadow::AddPasses / GBuffer::AddPasses / DeferredLighting::AddPasses / SSAO::AddPasses
  ├─ GPU::BeginFrame   ← fence wait; dominates RenderFrame when GPU-bound
  ├─ RG::Compile
  ├─ RG::Execute
  │     ├─ Shadow::Execute
  │     └─ GBuffer::Execute
  └─ GPU::Present
```

`BuildDrawList` has its own top-level `SA_PROFILE_SCOPE_N("BuildDrawList")` zone.

---

## GPU Performance Notes

Measurements taken with Tracy + RenderDoc on Sponza scene (104 draw calls, RTX 3070).

### CPU vs GPU Bottleneck

Tracy confirmed the engine is **GPU-bound**:

| Zone | Share of `RenderFrame` | Meaning |
|------|------------------------|---------|
| `GPU::BeginFrame` | ~78% | CPU blocked on in-flight fence from previous frame — GPU is not finished |
| `RG::Execute` | ~18% | Actual Vulkan command recording |
| Other sub-phases | ~4% | Uniforms upload, AddPasses, Present |

CPU submits commands in ~0.2% of `RenderFrame` wall time; 78% is pure fence wait.
Optimising CPU-side code (pipeline sorting, etc.) does not improve FPS when GPU-bound.

### Red-Frame Spikes

`RG::Execute` occasionally spikes to ~30% of frame time on the first draw of a new
material. Cause: **PSO (pipeline state object) lazy compilation** on first use per session
in `ShaderProgram::GetOrCreatePipeline`. Mitigation: pipeline pre-warm pass at load time
(not yet implemented).

### Texture Bandwidth Hotspots

RenderDoc GPU timestamps on Sponza (GBuffer pass):

| Mesh | Triangles | GPU time | Root cause |
|------|-----------|----------|------------|
| Wall | 84 | ~1 253 μs | Full-screen coverage → millions of pixels × PBR texture sample |
| Floor | 15 | ~440 μs | Same — large screen area drives texture bandwidth, not geometry count |

Fix: generate mipmaps for cook pipeline output (`.satex`) and compress albedo/normal maps
to **BC7** / **BC5** respectively. Mipmap hardware filtering reduces bandwidth proportionally
to mip level; BC compression reduces VRAM footprint and cache miss rate 4–8×.

### Alpha-Test Performance

`discard` in `deferred_geometry.frag` for plant face-cards breaks **Early-Z**: the GPU
cannot cull fragments before the shader runs. Transparent vegetation should be rendered
last in the GBuffer pass (sorted back-to-front within the alpha-test subset) to minimise
overdraw. Performance impact is secondary to the correctness fix (Issue #52 alpha-test bug).

### Future Optimisations (Tracked in Issues)

- **Mipmap + BC7 texture compression**: cook pipeline change in `tools/cook/`.
- **PSO pre-warm**: iterate all registered `MaterialType`s at load time, compile pipelines eagerly.
