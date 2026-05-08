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
9. [Input System](#input-system)
10. [Platform Layer — RHI](#platform-layer--rhi)
11. [Resource Layer — Material System](#resource-layer--material-system)
12. [Function Layer — RenderGraph](#function-layer--rendergraph)
13. [Compute Pipeline & ComputeProgram](#compute-pipeline--computeprogram)
14. [GPU IBL Bake (Runtime)](#gpu-ibl-bake-runtime)
15. [Deferred Rendering Pipeline](#deferred-rendering-pipeline)
16. [Custom Shading Models](#custom-shading-models)
17. [Frame Loop](#frame-loop)
18. [Editor Architecture](#editor-architecture)
19. [Key Design Decisions](#key-design-decisions)

---

## Layer Model

```
┌─────────────────────────────────────────────────────────────────┐
│  Editor Layer (editor/)                                          │
│                                                                  │
│  EditorMode : AppMode                                            │
│    EditorCamera, EditorUI (ImGui), EditorOverlaySettings        │
│    Panels: Hierarchy, Inspector, Assets, Console, Playback,     │
│            Settings, WorldSettings, Shortcuts                    │
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
│    View<C...>() → EnTT view wrapper                             │
│    SceneSerializer::SpawnFromTemplate() — entity template spawn  │
│                                                                  │
│  RenderGraph                                                     │
│    Reset / CreateTexture / ImportTexture / AddPass              │
│    Compile / Execute / GetLastFrameStats() → RGStats            │
│    Topological pass ordering via read/write dependency edges     │
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
- `Playing` — physics stepping, animation ticking, overlays suppressed
- `Paused`  — physics frozen, animation frozen, scene inspectable

`Application` calls `mode.OnPlayStateChanged(newState)` immediately after the transition
so the mode can swap input maps, reset physics, etc.

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
Reflection sidecar (.refl)
    bindings[]: { set, binding, type, stage, name, arraySize }
    uboMembers[]: { name, offset, size }  ← per UBO/SSBO binding
    pushConstantSize, pushConstantStages
```

Both files are output to `<build>/bin/assets/shaders/builtin/`.
CMake `DEPENDS` tracking ensures a changed source triggers recompilation.

### Custom Shading Model Dispatch (Build-Time)

Each material type beyond PBR is defined by a `*.lighting.glsl` evaluator file.
`cmake/GenerateShadingDispatch.cmake` assembles them into two generated GLSL headers:

```
assets/shaders/builtin/simple_albedo.lighting.glsl
        │
        ▼  register_lighting_evaluator() + generate_shading_dispatch()
generated/shaders/shading_model_ids.glsl   — #define SHADING_MODEL_SIMPLE_ALBEDO 1u
generated/shaders/shading_dispatch.glsl    — #include per-model + DispatchShadingModel()
generated/shaders/evaluators/simple_albedo.lighting.glsl  (build-time copy)
```

`DispatchShadingModel(modelID, gbuf, out_color)` is called by `deferred_lighting.frag`;
it switches on `modelID` and invokes the appropriate `Evaluate_<Name>()` function.

**Dependency tracking:** evaluator copies are `add_custom_command` build steps (not
configure-time `configure_file`). Editing a `*.lighting.glsl` triggers a file copy and
therefore SPV recompilation without a CMake re-run. Adding or removing evaluator files
still requires a CMake re-run to regenerate the dispatch switch and model-ID defines.

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
}
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
| `SkinnedMeshComponent` | GPU skinned mesh: `meshAsset`, `dynVertexBuffer`, `indexBuffer`, `subMeshes`; written by `AnimationSystem::PrepareEntity` |
| `AnimatorComponent` | `clipAsset` (→ .saanim), `time`, `speed`, `looping`, `playing` |
| `CameraComponent` | `fovY`, `nearPlane`, `farPlane`, `priority` (highest wins) |
| `ActiveCameraTag` | _(legacy)_ marks the active camera; superseded by `CameraComponent::priority` |
| `DirectionalLightComponent` | color, intensity, castShadow; direction from entity rotation (−Z) |
| `PointLightComponent` | color, intensity, range; position from entity world transform |
| `SpotLightComponent` | color, intensity, range, innerAngle, outerAngle |
| `AreaLightComponent` | color, intensity, size (W×H), twoSided, emissiveScale; LTC-evaluated PBR |
| `RigidBodyComponent` | Physics body: `Type` (Static/Kinematic/Dynamic), mass, friction, restitution, `bodyId` |
| `ColliderComponent` | Collision shape: `Shape` (Box/Sphere/Capsule), extents, offset, rotation |
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
  ├── SkinnedMeshComponent      { meshAsset, dynVB, indexBuffer, subMeshes, ready }
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

    // Tonemap
    enum class TonemapMode { Builtin, LUT };
    TonemapMode tonemapMode = TonemapMode::Builtin;
    AssetID     tonemapLut;
    float       exposure    = 1.f;
    float       gamma       = 2.2f;
    float       lutStrength = 1.f;
};
```

**SolidColor IBL behaviour:** `ApplyWorldSettings` encodes `backgroundColor` as a constant
SH L0 ambient term and writes it to a 1×1 solid-colour cubemap used as `t_PrefilteredEnv`.

**Tonemap hot-swap:** `ApplyWorldSettings` can replace `TonemapFeature` with
`LutTonemapFeature` at runtime via `device->WaitIdle()`, in-place in the `m_features` slot.

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

CPU skinning skeletal animation. Uses `.saskel` (skeleton) and `.saanim` (animation clip) cooked assets.

### Usage Pattern

```
// Once after scene load (per animated entity):
animSystem.PrepareEntity(entity, registry, resMgr, device);
// → allocates dynVertexBuffer, sets SkinnedMeshComponent::ready = true

// Every frame (Playing state):
animSystem.Update(dt, registry, resMgr, device);
// → advances AnimatorComponent::time, evaluates FK, CPU-skins verts, uploads dynVB

// When stopping (reset to rest pose):
animSystem.EvaluateAll(0.f, registry, resMgr, device);
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
| Vertex input | defined / `noVertexInput` flag | not present |
| Rasterizer / blend / depth | present | not present |
| Attachment formats | color[] + depth | not present |
| Vulkan call | `vkCreateGraphicsPipelines` | `vkCreateComputePipelines` |

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

### Cubemap Textures

`RHITextureDesc::cubemap = true` triggers:
- `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`; `arrayLayers` forced to 6
- Main view: `VK_IMAGE_VIEW_TYPE_CUBE` (for `samplerCube`)
- Per-mip UAV views: `VK_IMAGE_VIEW_TYPE_2D_ARRAY, layerCount=6`
  (Vulkan forbids `CUBE` views as storage images)

### `IRHICommandList::GenerateMipmaps`

Blit chain from mip 0→1→…→N-1. Input must be in `ShaderRead`; output all mips
in `ShaderRead`. Texture must have `CopySrc` usage bit.

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

### Compile & Execute

```
Reset()
CreateTexture(name, desc)  → RGTextureHandle   (transient; GPU alloc deferred to Stage 3)
ImportTexture(name, handle, initState, finalState)  → RGTextureHandle
AddPass(name, setupFn, executeFn)

Compile()  → Kahn's topological sort on read/write edges

Execute(device, cmd)
  → for each sorted pass:
      emit barriers (state transitions)
      call executeFn(cmd, resources)
  → final state transitions for imported textures
  → fill m_lastStats (RGStats) at end

GetLastFrameStats() const → const RGStats&
```

### RGStats

Per-frame read-only snapshot written at the end of `Execute()`. Physical values equal
logical values until RG-handle aliasing (#16) is implemented.

```cpp
struct RGStats {
    uint32_t transientCount;        // CreateTexture() textures
    uint32_t importedCount;         // ImportTexture() textures
    uint32_t physicalSlotCount;     // == transientCount (pre-aliasing)
    uint64_t transientBytesLogical; // sum of all logical transient sizes
    uint64_t transientBytesPhysical;// == logical (pre-aliasing)
    struct Entry { std::string name; uint32_t width, height, mipLevels;
                   const char* formatStr; uint64_t bytes; };
    std::vector<Entry> entries;     // one per transient texture
};
```

Exposed to the editor via `SceneRenderer::GetRenderGraph()` → `SettingsPanel` → "Render Stats"
collapsing header (transient/imported counts, MB totals, optional per-texture detail table).

---

## Compute Pipeline & ComputeProgram

### Frame Uniforms (set=0) Bindings

```
binding=0  FrameData UBO    — camera matrices, time, resolution, SH9 irradiance
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
| `ShadowFeature` | `config.shadowEnabled` | shadow map (D32, 2048²) | `shadow.vert/.frag` |
| `SkyboxFeature` | always | HDR buffer | `skybox.vert/.frag` |
| `GBufferFeature` | always | RT0/RT1/RT2 + depth | `deferred_geometry.vert/.frag` |
| `DeferredLightingFeature` | always | HDR buffer (RGBA16F) | `deferred_lighting.frag` |
| `SelectionMaskFeature` | always | R8 silhouette mask | `selection_mask.vert/.frag` |
| `BloomFeature` | `config.bloomEnabled` | additive bloom into HDR | `bloom_*.frag` |
| `TonemapFeature` | `ws.tonemapMode==Builtin` | swapchain LDR | `tonemap.frag` (ACES + gamma) |
| `LutTonemapFeature` | `ws.tonemapMode==LUT` | swapchain LDR | `postfx_lut_tonemap.frag` |
| `SelectionOutlineFeature` | always | outline on swapchain | `selection_outline_dilate.frag` + composite |
| `InfiniteGridFeature` | when enabled | XZ grid on swapchain | `infinite_grid.frag` |
| `DebugOverlayFeature` | always | debug lines on swapchain | `debug_line.vert/.frag` |
| user `RenderFeature`s | `AddFeature(...)` | custom | custom |

`TonemapFeature` ↔ `LutTonemapFeature` hot-swapped at runtime by `ApplyWorldSettings`.

```cpp
struct RendererConfig {
    bool     shadowEnabled  = true;
    uint32_t shadowMapSize  = 2048;
    bool     bloomEnabled   = true;
    int      bloomMipCount  = 3;
    bool     builtinTonemap = true;
};
```

### Bloom

1. **Threshold pass** — extract pixels where luminance > 1.0
2. **Downsample chain** — `bloomMipCount` levels, 13-tap COD downsample
3. **Upsample + accumulate** — bilinear upsample, weighted additive blend
4. **Composite** — additive blend into HDR color buffer

---

## Custom Shading Models

Each custom material type needs:

1. A `*.lighting.glsl` evaluator implementing `vec3 EvaluateShading(GBufferData gbuf)`.
2. A `*.gbuffer.frag` writing the same 3-MRT layout as `deferred_geometry.frag`.
3. CMake registration: `register_lighting_evaluator(path)`.
4. C++ registration in a `RenderFeature::OnInit`:
   ```cpp
   ctx.matMgr->RegisterTypeFromShaders(
       {"MyMaterial", "deferred_geometry", "my_material.gbuffer"}, ctx);
   ```

The vertex shader (`deferred_geometry.vert`) is shared — only the fragment shader differs.
`SHADING_MODEL_PBR = 0` is always reserved; custom models start from 1 in alphabetical file order.

---

## Frame Loop

```
── App code ──────────────────────────────────────────────────────────────────

window->PollEvents()
input.Poll()                               // snapshot devices + evaluate active map
mode.OnUpdate(dt)                          // editor camera / gameplay logic
  physics.SyncIn / Step / SyncOut (fixedDt accumulator)
  animSystem.Update(dt)
scene.UpdateTransforms()                   // BFS propagate dirty transforms
renderer.RenderFrame(scene, w, h)          // full frame — all phases internal
mode.OnRenderUI(cmd)                       // ImGui draw calls (editor panels, gizmo)

── Inside RenderFrame ────────────────────────────────────────────────────────

Phase 1: Collect
   FillCameraUniforms(scene, w, h)   // WorldTransformComponent → view/proj
   GatherLights(scene)               // all light component types

Phase 2: GPU
   device->BeginFrame()
   frameUniforms.Upload(fi, fu, lu)
   m_rg.Reset() + ImportTexture(swapchain, depth, hdrColor, gbuffers, shadowMap)

   ShadowFeature::AddPasses()
   SkyboxFeature::AddPasses()
   GBufferFeature::AddPasses()
   DeferredLightingFeature::AddPasses()
   SelectionMaskFeature::AddPasses()
   BloomFeature::AddPasses()
   TonemapFeature::AddPasses()
   SelectionOutlineFeature::AddPasses()
   InfiniteGridFeature::AddPasses()
   DebugOverlayFeature::AddPasses()
   for each user RenderFeature: feature.AddPasses(...)

   m_rg.Compile()
   m_rg.Execute()
   device->EndFrame() / Present()
```

---

## Editor Architecture

**Location:** `editor/`

`EditorMode` owns all editor-specific state and drives the editor per-frame logic.

### Systems Owned by EditorMode

| System | Purpose |
|--------|---------|
| `EditorCamera` | Free-flying orbit camera; driven by mouse look + WASD; `FocusOn(target)` repositions along current forward vector (yaw/pitch unchanged) |
| `EditorUI` | ImGui lifecycle (NewFrame / Render / backend); dockspace layout |
| `EditorOverlaySettings` | Visibility toggles for all overlay symbols |
| `EntityTemplateRegistry` | Scans `templates/entities/` for spawn menu |
| `ProjectManager` | Create/open/recent-projects logic; persists `recent_projects.json` |
| `ProjectBrowserPanel` | Startup modal (shown when no project loaded); not an `IEditorWindow` — driven directly from `OnRenderUI` |
| `EditorShortcutConfig` | JSON-backed user shortcut overrides (`editor_shortcuts.json`); `Load`/`Save`/`Reload`/`ImportFrom`/`ExportTo`; `ApplyTo(defaults)` replaces `bindings[0]` for overridden actions; built-in path is read-only (Save disabled in panel) |
| `EditorDiagnostics` | Collects warnings/errors for ConsolePanel Diagnostics tab (action-required events only) |
| `EditorLogCapture` | RAII spdlog sink; passively mirrors all `SA_LOG_*` calls into a ring buffer for ConsolePanel Engine Logs tab |

### Panels

| Panel | Purpose |
|-------|---------|
| `SceneHierarchyPanel` | Entity tree; multi-select (Ctrl-toggle / Shift-range / Ctrl+A all); data-driven spawn menu from `EntityTemplateRegistry`; Ctrl+D duplicate; drag-reparent; short double-click → `FocusEntityCallback` pans camera; long double-click (hold > 0.20 s) → inline rename |
| `InspectorPanel` | Component editor for selected entity; auto-generated drawers per component type; `IAssetInspector` for `.mat`/`.satex`/`.samesh` selection |
| `AssetsPanel` | File tree for `projectDir/assets/`; multi-select (Ctrl-toggle / Shift-range / Ctrl+A all); native file picker import (nfd-extended, multi-select); drag-to-viewport drop; Create Material; `SetProjectDir` for runtime project switch |
| `ProjectBrowserPanel` | Standalone startup modal (not registered in EditorUI); three sections: Create / Open / Recent; uses NFD for folder+file picking |
| `WorldSettingsPanel` | Scene-level settings: skybox HDR/LUT pickers, tonemap mode, exposure, background color |
| `PlaybackPanel` | Play / Pause / Stop buttons; triggers `Application::SetPlayState` |
| `ConsolePanel` | Two-tab panel: **Diagnostics** (action-required events from `EditorDiagnostics`) + **Engine Logs** (real-time `SA_LOG_*` stream via `EditorLogCapture`, per-level filter) |
| `SettingsPanel` | UI scale, display info, overlay toggles, physics debug toggles, render graph stats (transient/imported counts, MB totals, per-texture detail) |
| `ShortcutsPanel` | Lists all `userConfigurable` Button actions; [Change] enters key-capture mode (next non-modifier key + held Ctrl/Shift/Alt → new binding); [×] clears override; [Default] reloads built-in config; [Reload] discards unsaved changes; [Import...]/[Export...] switch or copy the active config file via NFD; [Apply] rebuilds input maps; [Save] writes to active config file (disabled for built-in path); active config filename shown below toolbar |

### Project Management

`EditorMode::LoadProject(saprojectPath)` is the single entry point for a project switch at runtime:

```
1. Guard: return if PlayState != Editing
2. scene.Clear(); m_currentScenePath = {}
3. app.UpdateProjectPaths(projectDir, projectDir/"cook_cache")
   → propagates to VFS (SetCookCacheDir) + SceneRenderer (SetCookCacheDir)
4. m_assetRegistry->Scan(projectDir/"assets", engineAssetsDir)
5. m_assetsPanel->SetProjectDir(projectDir/"assets")
6. LoadSaProject → load startupScene (warns if missing, continues with empty scene)
7. renderer.ApplyWorldSettings(scene.GetWorldSettings())
8. PrepareAnimatedEntities + RebuildDrawList
9. m_projectManager.AddRecent + SaveRecents
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

### ComputeProgram Has No AttachmentKey Cache
Compute pipelines have no render targets. One `VkComputePipeline` per shader,
created on first call to `GetPipeline()`.

### Descriptor Set Convention
```
set=0  per-frame globals   (FrameUniformsBuffer — bound before any pass)
set=1  per-material params (MaterialInstance::Bind())
```
ComputeProgram owns all its sets; set=0 may be a caller-supplied frame layout.

### HierarchyComponent Is Optional
Only parented entities carry `HierarchyComponent`. `UpdateTransforms()` uses
`TransformComponent` presence (not `HierarchyComponent`) to identify entities
that need world matrix computation.

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

### AppMode Is the Only Customisation Point
Application systems (Scene, Renderer, Physics, Animation, Input) are fixed.
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
| GpuIblBake | SceneRenderer | One-shot renderer operation |
| White 1×1 | ResourceManager | `GetBuiltin(BuiltinTexture::White1x1)` |
| DrawItem list | SceneRenderer | Rebuilt by `BuildDrawList(scene)` |
| RenderFeatures | SceneRenderer | `AddFeature` transfers ownership |

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

### ProjectBrowserPanel Is Not an IEditorWindow
`IEditorWindow` panels are registered with `EditorUI` and appear in the Windows menu;
they are wrapped in `ImGui::Begin/End` by the registration infrastructure.
`ProjectBrowserPanel` must be a free-floating modal (`BeginPopupModal`) that blocks all
other interaction until dismissed. Registering it as an `IEditorWindow` would break the
modal semantics. Instead it is owned as `unique_ptr<ProjectBrowserPanel>` by `EditorMode`
and driven directly from `OnRenderUI` after `NewFrame()`.
