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
│    set=1: per-frame camera + light + IBL data                   │
│    Manages double-buffered GPU UBOs + descriptor sets           │
│  MaterialParamRing (owned by SceneRenderer, Issue #72)          │
│    2 MiB per-frame SSBO ring, bump-allocated, set=2 dynamic     │
├─────────────────────────────────────────────────────────────────┤
│  Resource Layer                                                  │
│                                                                  │
│  MaterialManager → MaterialType → MaterialInstance              │
│    Init(device, ResourceManager*)                               │
│    RegisterTypeFromShaders(MaterialTypeDesc, FeatureInitContext) │
│    BindlessTextureHeap: 4096-slot set=0 sampler array           │
│  ShaderProgram: vert+frag SPIRV + reflection + pipeline cache   │
│  ComputeProgram: compute SPIRV + per-set descriptor layouts     │
│  ProgramCache: central owner of all ShaderProgram/ComputeProgram │
│    (Issue #86) name-keyed, engine/project scoped, hot-reload     │
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

**Asset drag-drop wire format (`AssetDragPayload`, defined in `editor/ui/AssetDragPayload.hpp`):**
the `"SAASSET"` payload sent by `AssetsPanel` is a POD struct `{ char path[260]; char type[32]; AssetID id; }`.
`path` is laid out first so that pre-#73 receivers reading `static_cast<const char*>(payload->Data)`
still see a valid null-terminated absolute path (back-compat); new receivers cast to
`const AssetDragPayload*` and use `id` directly via `DrawerHelpers::AcceptAssetIDDrop`.
Selection-change in the file pane fires on `IsItemHovered() && IsMouseReleased(Left)` (not
`IsItemClicked()`) — so pressing on a `.cs` to start a drag does NOT flip `EditorSelection`
to Asset before the drag lands on a drop target.

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

### File-Stream Flow Overview (Issue #90)

Three layers, one direction of dependency (editor → asset system → I/O foundation):

```
Editor CRUD    AssetsPanel: CreateNewFile / DeletePath / MoveAsset / CommitRename / ReimportFile
   │           EditorMode:  LoadProject / CookProjectShaders / FileWatcher poll
   ▼
Asset system   ImportScanner: ScanAndImport → EnsureMeta → CookAssetEntry
   │           Cookers: CookMesh/Texture/Material/InputMap · ShaderCookLib
   │           AssetRegistry (UUID↔path) · MetaFile (the one .sameta parser, resource/MetaFile)
   ▼
I/O foundation core/io/FileIO — every whole-file read/write + fs mutation
               (see "File I/O layer" under Asset Identity). Exceptions: binary
               Cooked*.cpp keep their own structured read/write (cohesive per format).
```

**Three on-disk forms per asset:** source (`Hero.glb`) · `.sameta` sidecar (stable UUID +
type, written by `EnsureMeta`) · cooked product (`<uuid>.samesh`). References are always by
UUID, never by path — rename/move keep the UUID so references never break.

**Extension → type → cook** (`ImportScanner::AssetTypeFromExtension` + `CookAssetEntry`):

| ext | type | import-time cook | cooked product |
|---|---|---|---|
| `.png/.jpg/.hdr/.tga/.bmp` | Texture | CookTexture (stb decode → RGBA8/32F) | `<uuid>.satex` |
| `.dds` | Texture | CookTexture (BCn pass-through, Issue #108) | `<uuid>.satex` v2 |
| `.gltf/.glb/.vrm` | Mesh | CookMesh (via GltfLoader) | `<uuid>.samesh` (+ derived Skeleton/Anim/Material UUIDs) |
| `.obj` | Mesh | CookMesh (via ObjLoader, Issue #108) | `<uuid>.samesh` |
| `.fbx` | Mesh | CookMesh (via FbxLoader/ufbx, Issue #108) | `<uuid>.samesh` |
| `.samat` | Material | CookStandaloneMaterial | cooked material (by UUID) |
| `.sascene` | Scene | — (native JSON) | — |
| `.cs` | Script | — (Roslyn compiles at runtime) | — |
| `.saglsl` / `.saeffect` | Shader | — (**shader-only cook path**) | `cook_cache/shaders/…` |
| `.sainputmap` | InputMap | CookInputMap | `<uuid>.sainputmap` (validated copy) |

**Four flows:** ① **scan/import** — `RunInitialScan → ScanAndImport`: `EnsureMeta` writes a missing
`.sameta` (new UUID + defaults), then `CookAssetEntry` cooks Mesh/Texture/Material/InputMap
(Script/Scene/Shader produce no import-time cook); `AssetRegistry::Scan` indexes every `.sameta`.
② **shader cook** — `.saglsl`/`.saeffect` go through the unified path to `cook_cache/shaders`
(see "Cook-path unification" under Runtime Cook Flow). ③ **runtime load** — by UUID via
`VFS::ResolveCookedPath` → loader reads the cooked product (binary via `IO::ReadBytes`). ④ **save**
— Scene/Project/shortcuts via `IO::WriteJson`/`WriteText`.

**CRUD response matrix:**

| op | response |
|---|---|
| **Create** | `CreateNewFile`: `IO::CopyTemplateReplacing`/`Copy`/`WriteText` → `EnsureMeta` (auto `.sameta`) → `CookAssetEntry` → registry rescan; Script recompiles, Shader/Effect fire `onCookShaders`; then inline rename |
| **Delete** | `DeletePath`: `IO::Remove(source)` + `IO::Remove(.sameta)`. Shader/Effect also fire `onCookShaders` → `PruneOrphanedEffectCookOutputs` drops the orphaned cooked `.refl`/`.spv` so the type/effect leaves the catalog (no ghost) |
| **Modify — rename** (`CommitRename`) | `IO::Rename` (source + `.sameta`) + sync the internal identity when it still matches the old stem: `.cs` `class`, `.saeffect` `@Effect`, `.sainputmap` `"name"`; Shader/Effect re-cook |
| **Modify — move** (`MoveAsset`) | `IO::Rename` (source + `.sameta`); UUID unchanged so references survive |
| **Modify — external edit** | FileWatcher poll: on window focus + Editing, `.cs` → `RecompileEditing`, `.saglsl`/`.saeffect` → `CookProjectShaders` (auto, no manual reimport). Mesh/Texture still need manual Reimport |
| **Modify — manual Reimport** (`ReimportFile`) | dispatch by `meta.type`: Shader→`onCookShaders`, Mesh→CookMesh, Texture→CookTexture, Material→CookStandaloneMaterial, Skeleton/Animation→sidecar cook, Scene→rescan only |
| **Query / load** | `AssetRegistry::FindByID`/`FindBySourcePath`/`EntriesByType` → `VFS::ResolveCookedPath` → loader |

### Asset Identity — `.sameta` & `AssetID`

Every source asset has a companion `.sameta` sidecar file that persists its
stable UUID (`AssetID`).

```
BoomBox.glb
BoomBox.glb.sameta  ← {"uuid": "xxxxxxxx-…"}
```

`Import::MetaFile::MetaPathFor(path)` derives the sidecar path. `MetaFile` (parse /
serialize `.sameta`) lives in `resource/MetaFile.hpp` (Issue #90 relocated it from
`tools/importer` so runtime `AssetRegistry` / `InputMapLoader` share the single parser
instead of re-implementing it — tools link the runtime, so this is the common home).
`AssetID` is a 128-bit UUID (two `uint64_t hi/lo`).

**File I/O layer (`core/io/FileIO`, Issue #90).** All whole-file reads/writes and common
`std::filesystem` mutations route through `StellarAlia::IO::` — `ReadText`/`WriteText`,
`ReadBytes`/`WriteBytes`, `ReadJson`/`WriteJson` (out-param, json_fwd in the header),
`Copy`/`Rename`/`Remove`/`EnsureDir` (unified `error_code` + `SA_LOG`), and
`CopyTemplateReplacing` (template instantiation for AssetsPanel create/rename). Call sites
(SceneSerializer, SaProject, InputMapLoader, EditorShortcutConfig, InputMapImporter, MetaFile,
AssetsPanel) no longer hand-roll `ifstream`/`ofstream`. `IO::Copy` is named to dodge the Win32
`CopyFile` macro. Binary cooked-asset serializers (`Cooked*.cpp`) keep their own structured
`read`/`write` — cohesive per format, intentionally not folded in.

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

`ImportScanner::AssetTypeFromExtension` recognises `.png/.jpg/.jpeg/.bmp/.tga/.hdr` → `Texture`,
`.gltf/.glb` → `Mesh`, `.samat` → `Material`, `.saglsl` → `Shader`, `.sascene` → `Scene`,
`.cs` → `Script`. `.cs` is a runtime-consumed asset (no cook output); its `.cs.sameta`
default-fills `class_name = stem` for `ScriptComponent.className` fallback.

### Cooked Texture — `.satex`

```
CookedTexture {
    AssetID  id
    uint32   width, height, mipLevels
    CookedTextureFormat  format   (RGBA8 | RGBA16F | RGBA32F | BC1 | BC3 | BC5 | BC7)
    bool     srgb, isHDR
    vector<MipSlice { offset, size }>
    vector<uint8_t>  data
}
```

**Version 2 (Issue #108):** BCn enum values added; layout unchanged (v1 files still read).
`.dds` sources take a pass-through path in `CookTexture` (`DdsLoader` parses the DDS/DX10
header, block data + authored mip chain go into `.satex` uncompressed-unchanged, no
stb decode) — legacy FourCC (DXT1→BC1, DXT5→BC3) and DX10 header both handled. `ResourceManager`
maps BC1/BC3/BC7 to sRGB or UNORM by `cooked.srgb`; BC5 is data-only (always linear).
GPU upload sizes mips by block count (`⌈w/4⌉·⌈h/4⌉·blockBytes`), extent stays in texels.

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
        AssetID defaultMaterialID  // → .samat (or the mat_remap_<idx> target, Issue #101)
        glm::mat4 localTransform   // pre-baked node world transform
    }
    vector<string>  materialNames  // v6: per-submesh glTF material name (editor labels)
    vector<uint8_t> vertexData   // Vertex: pos3 normal3 tangent4 uv2 (48 bytes)
    vector<uint8_t> indexData
    vector<uint8_t> skinData     // SkinVertex[]: uvec4 joints + vec4 weights (32 bytes/vert); empty for static meshes
}
// IsSkinned() → !skinData.empty()
// Format version 5+ includes skinData blob; v6 appends the material name table
// (readers accept v5 with empty names — no forced reimport, and the version
// mismatch makes NeedsRecook upgrade the file on the next scan anyway)
```

### Model Loader Dispatch (Issue #108)

`CookMesh` no longer calls `GltfLoader::Load` directly — it goes through
`ModelLoader::Load(path)` (`src/resource/loaders/`), which dispatches by extension to a
format loader that all produce the same format-agnostic `SceneData` IR
(`MeshData.hpp`: meshes/materials/images/nodes/skins/animations). Because the IR is shared,
material extraction (#101), skeleton/anim sidecars, and slot linkage (#102/#103) work for
every format for free; `.samesh` is unchanged.

| loader | formats | notes |
|---|---|---|
| `GltfLoader` | `.gltf` `.glb` `.vrm` | `.vrm` = glb container → geometry/skin/anim; MToon materials fall back to PBR |
| `ObjLoader` (tinyobjloader) | `.obj` | shapes split by material; MTL → approximate PBR; no skin/anim |
| `FbxLoader` (ufbx) | `.fbx` | axis/unit normalisation + geometry-transform bake in load opts; skin deformers → `SkinVertex`, animation stacks → multi-clip |

`ModelLoader::SupportsExtension` is the single source of truth shared by `ImportScanner` and
the AssetsPanel import dialog. `MeshUtils` fills gaps common to OBJ/FBX: **normals** recomputed
by area-weighted face accumulation (hand-written); **tangents** via vendored **MikkTSpace**
(the industry-standard basis — mesh expanded to unindexed corners → generate → hash-weld back),
only for primitives lacking a tangent stream (glTF's own TANGENT is untouched). Vendored libs
(`third_party/CMakeLists.txt`, submodule-first + build-time warning when absent): tinyobjloader,
ufbx, MikkTSpace. **Material passthrough:** `MaterialData` carries `shadingModel` (default
`"PBR"`) + `extraParams`/`extraTextures` bags; `CookMaterial` writes them as the `.samatc`
`type` + arbitrary key/value JSON, and `MaterialManager` silently ignores unknown keys — so a
loader emitting `type="MToon"` + custom params is consumed with zero cook/runtime changes
(given the model is registered, see #109).

**Not done (Issue #108 scope):** MMD PMX/VMD (Phase 5, deferred); `.blend` direct parsing
(won't-do — no stable spec; workaround = Blender headless CLI → glTF).

### Imported-Material Workflow — Extract & Remap (Issue #101)

glTF materials cook to derived-UUID `.samatc` files (read-only cook products, not in the
AssetRegistry). To edit them, the AssetsPanel context menu on a `.glb/.gltf` offers
**Extract Materials**: each derived `.samatc` is copied to an editable
`assets/materials/<glbStem>_<matName>.samat` source asset (fresh UUID + `.sameta`), the
mapping is recorded in the glb's `.sameta` as `mat_remap_<idx>=<uuid>` (+
`mat_remap_name_<idx>` for DCC re-export shift detection), and the mesh is force-recooked
so every `CookedSubMesh.defaultMaterialID` points at the extracted asset — asset-level, so
all entities using the mesh follow, and the remap survives reimports (`EnsureMeta` never
rewrites an existing `.sameta`). `MaterialAssetInspector` edits `.samat` sources in place
(reflected param widgets + render-state combos; Save → write-back + `CookStandaloneMaterial`
+ `MaterialManager::EvictInstance` + `MarkMaterialDirty`). Derived `.samatc` stay read-only;
extraction is the only editing path (Unity model).

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
| `MaterialOverrideComponent` | Unified material override: optional `materialAsset` + named `scalars` + named `textures` + per-submesh `slotOverrides` (Issue #101) |
| `SkinnedMeshComponent` | Per-entity GPU skinning state: `meshAsset`, `skinMatricesBuffer` (curr pose) + `skinMatricesBufferPrev` (prev pose, Issue #84), `skinDescSet` + `velocityDescSet`, `boneCount`, `ready`, `poseSeeded`, `lastEvalClipId`; mesh geometry (`vertexBuffer`/`indexBuffer`/`skinDataBuffer`) lives in `GPUMesh` (ResourceManager) |
| `PrevTransformComponent` | Issue #84 — captures last-frame `WorldTransformComponent.matrix` for per-object motion vector reconstruction. Auto-emplaced by `Scene::CreateEntity`; `seeded` flag guards first-frame velocity to be zero |
| `AnimatorComponent` | `clipAsset` (→ .saanim), `time`, `speed`, `looping`, `playing` |
| `CameraComponent` | `fovY`, `nearPlane`, `farPlane`, `priority` (highest wins) |
| `ActiveCameraTag` | _(legacy)_ marks the active camera; superseded by `CameraComponent::priority` |
| `DirectionalLightComponent` | color, intensity, castShadow, isSun (Issue #49: first marked light drives shadow map + volumetric fog god rays; none marked → first found); direction from entity rotation (−Z) |
| `PointLightComponent` | color, intensity, range; position from entity world transform |
| `SpotLightComponent` | color, intensity, range, innerAngle, outerAngle |
| `AreaLightComponent` | color, intensity, size (W×H), twoSided, emissiveScale; LTC-evaluated PBR |
| `FogVolumeComponent` | density, albedo, falloff (Issue #110); local volumetric fog OBB — shape = entity transform × unit box, consumed by VolFog_Inject via a per-frame UBO |
| `RigidBodyComponent` | Physics body: `Type` (Static/Kinematic/Dynamic), mass, friction, restitution, `bodyId` |
| `ColliderComponent` | Collision shape: `Shape` (Box/Sphere/Capsule), extents, offset, rotation |
| `ScriptComponent` | C# script binding: `scriptId : AssetID` (resolves to the `.cs` source via `AssetRegistry::FindByID`), `className` (derived from filename stem if empty), `fields : unordered_map<string, ScriptFieldValue>` (per-entity Inspector-edited values) |
| `EntityIdComponent` | Scene-local 64-bit stable ID assigned by `Scene::CreateEntity` (#75). Survives save/load; used by script `EntityRef` fields so cross-entity references persist across sessions. `Scene::FindBySceneLocalId(id)` resolves back to `entt::entity`; `Scene::AssignSceneLocalId(e, id)` restores IDs at scene load. |
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
  ├── PrevTransformComponent    { prevModel, seeded }    // Issue #84
  ├── AnimatorComponent         { clipAsset, time, speed, looping, playing }
  ├── SkinnedMeshComponent      { meshAsset, skinMatricesBuffer + Prev, skinDescSet + velocityDescSet,
  │                                boneCount, ready, poseSeeded, lastEvalClipId }
  ├── MeshRendererComponent     { materialSlots[], castShadow, receiveShadow }
  └── MaterialOverrideComponent { … } (optional)
```

### MaterialOverrideComponent

Replaces the old `PBRSurfaceComponent` + `MaterialParamComponent` pair.
When present, the render system applies overrides on top of the base `MaterialInstance`:
- **SSBO-path materials** (e.g. PBR): BuildDrawList packs the override blob into the
  per-frame `MaterialParamRing` and binds set=2 with a dynamic offset — no descriptor
  set allocation per entity.
- **Legacy UBO-path materials** (post-fx / screen-effect style types; project `.saglsl`
  models moved to the SSBO path in #73-A): falls back to `CloneInstance` which allocates
  a per-entity descriptor set; the desc set goes through the RHI deferred-destroy queue
  on entity removal.

Entities without this component share the cached instance (no clone, no allocation).

```cpp
struct MaterialOverrideComponent {
    AssetID                           materialAsset;  // fills slots WITHOUT an explicit assignment
    std::map<std::string, ParamValue> scalars;        // named UBO param overrides
    std::map<std::string, AssetID>    textures;       // named texture slot overrides
    std::map<int32_t, MaterialSlotOverride> slotOverrides;  // Issue #101: per-submesh layer
};
// ParamValue = variant<float, vec2, vec3, vec4>
// MaterialSlotOverride = {scalars, textures, alphaMode, doubleSided} — applied on
// top of the entity-wide fields for one submesh index (base → entity → slot).
```

**Base-material resolution priority (#106):** explicit `materialSlots[i]` assignment >
entity-wide `materialAsset` > cooked `defaultMaterialID`. (`materialAsset` originally
stomped slots for all sub-meshes; demoted to a fallback so the slot UI stays authoritative.)
When a material ID is **valid but fails to load** (unknown shader type / missing `.samatc` /
JSON error), BuildDrawList substitutes `SceneRenderer::m_errorMaterial` — a magenta
emissive PBR instance — instead of the silent neutral-gray `m_defaultMaterial`, so broken
references are visually distinct from "nothing assigned".

**UI location (Issue #103):** per-slot overrides are edited inside the expanded
Mesh Renderer slot rows (`SlotOverrideEditor.cpp` — the single slot UI, shared
hover/focus viewport linkage). `MaterialOverrideDrawer` shows only the
entity-wide parts (materialAsset / scalars / textures / render state). The first
edit on a slot creates the component and/or the `slotOverrides` entry on demand;
the undo command tears down exactly what it created.

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

    // Motion Blur (Camera Mode, Issue #46 Phase 1)
    bool  motionBlurEnabled  = false;
    float motionBlurStrength = 0.5f;
    int   motionBlurSamples  = 8;
    float motionBlurMaxSpeed = 0.1f;

    // Screen modifications (Issue #47) — applied after Tonemap on LDR buffer.
    // Each layer is independently toggleable; all disabled = single LDR→swapchain copy.
    bool  vignetteEnabled    = false;
    float vignetteIntensity  = 0.4f;   // [0..1] elliptical falloff start
    float vignetteSmoothness = 0.6f;   // [0.01..1] falloff width
    bool  caEnabled          = false;
    float caStrength         = 0.5f;   // [0..5] radial RGB offset multiplier
    bool  filmGrainEnabled   = false;
    float filmGrainIntensity = 0.1f;   // [0..0.3] amplitude
    float filmGrainSize      = 1.6f;   // [0.5..5] noise tile scale

    // Screen Space Reflections (Issue #48) — compute pass after DeferredLighting,
    // before TAA. Replaces the IBL env-probe specular with screen-traced colour.
    bool  ssrEnabled      = false;
    float ssrMaxRoughness = 0.4f;   // fade SSR out above this roughness
    int   ssrMaxSteps     = 64;     // [16..128] linear march sample count
    float ssrThickness    = 0.1f;   // view-space depth tolerance for hit test
    float ssrStrength     = 1.0f;   // [0..1] reflection blend weight

    // Volumetric Fog (Issue #49) — froxel single scattering after SSR / before
    // ForwardTransparent+TAA. See "Volumetric Fog" section.
    bool      volFogEnabled       = false;
    float     volFogDensity       = 0.02f;  // global extinction σt (1/m)
    glm::vec3 volFogAlbedo        = {0.9f, 0.9f, 0.9f};  // scatter albedo σs/σt
    float     volFogAnisotropy    = 0.6f;   // HG phase g [-0.9, 0.9]
    float     volFogDistance      = 64.f;   // froxel volume far end (m)
    float     volFogHeightBase    = 0.f;    // height fog reference y (m)
    float     volFogHeightFalloff = 0.f;    // 0 = uniform; >0 exponential falloff (1/m)
    float     volFogAmbient       = 0.2f;   // SH-L0 ambient scatter factor
    // Issue #110
    bool      volFogTemporal      = true;   // fog history volume (jitter converges w/o TAA)
    float     volFogTemporalBlend = 0.9f;   // history weight [0..0.98]
    float     volFogNoiseScale    = 0.1f;   // density noise frequency (1/m)
    float     volFogNoiseStrength = 0.f;    // 0 = off
    glm::vec3 volFogWind          = {0.5f, 0.f, 0.3f};  // noise advection (m/s)

    // Screen Effects (Issue #88) — ordered per-scene stack of custom .saeffect
    // post-process passes. Only listed instances run (Unity Volume / UE Blendable
    // model). Each: { name (=@Effect), enabled, params: name→ParamValue overrides }.
    std::vector<ScreenEffectInstance> screenEffects;
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
runtime fields (`m_enabled/threshold/strength/radius`), SSAO/TAA/AutoExposure/DoF/MotionBlur/PostFX
parameters, and tonemap parameters instantly from `ws.pp` without a device stall.
`pp.tonemapMode` switching retains the WaitIdle feature-slot replacement for `LutTonemapFeature`.
`pp.bloomMipLevels` change is **deferred** — set `m_pendingBloomMipCount`; actual GPU rebuild
happens at the start of the next `RenderFrame` resize block (after `WaitIdle`).

**Built-in post-process on compute (Issue #92).** Engine post-process features may run on
`ComputeProgram` instead of a fullscreen-fragment `MaterialType` (same pattern as SSR /
AutoExposure) — an internal rewrite, orthogonal to the user-facing ScreenEffect system.
`BloomFeature`'s four passes are all compute: **threshold / downsample** write their mip as a
UAV (`imageStore`); **upsample / composite** read-modify-write the destination (`imageLoad + add
+ imageStore`) to replace hardware Additive blend, which compute lacks. The bloom mip pyramid
(`m_bloomMip[]`) and the `HDR_Color` transient carry `UnorderedAccess` in addition to
`RenderTarget|Sampled`. This changes no texture/buffer allocation (only the usage flag) — the
delta vs the old fragment path is graphics ShaderPrograms/pipelines → ComputePrograms/pipelines.
`PostFXFeature` stays fragment (it writes the swapchain directly, which cannot be a storage
image, and there is no blit-to-swapchain); Tonemap/SSR compute conversion is left to later issues.

### Transform Hierarchy

`HierarchyComponent` stores `parent` + `children`. Only parented entities carry it.

`Scene::UpdateTransforms()`:
1. If `m_hierarchyDirty`: rebuild `m_sortedEntities` via BFS from root entities
2. Walk sorted list (parents always before children):
   - Prefer `AnimatedTransformComponent` over `TransformComponent` for local matrix
   - `world = (no parent) ? TRS(local) : parentWorld × TRS(local)`

`Scene::MarkDirty(entity)` recursively marks the entity and all descendants dirty.
Called by the editor gizmo after any transform manipulation.

`Scene::EnsureWorldUpToDate(entity)` (Issue #81) is the *lazy* counterpart used
by the script API's world-space accessors. Single-frame ordering inside
`Application::Run()` is:

```
Physics.SyncOut    — Dynamic body poses → TransformComponent, MarkDirty
ScriptSystem.FixedUpdate
Mode.OnUpdate
AnimationSystem.Update
ScriptSystem.Update + LateUpdate   ← user scripts run here
Scene::UpdateTransforms()          ← whole-tree refresh
RenderFrame
```

Scripts thus run *before* the per-frame `UpdateTransforms` sweep. If a script
reads `WorldPosition` of an entity whose ancestor was marked dirty earlier in
the frame (or in the prior tick), the cached `WorldTransformComponent.matrix`
is stale. `EnsureWorldUpToDate(entity)` walks the parent chain top-down and
recomputes any dirty link, mirroring the body of `UpdateTransforms` exactly
(`AnimatedTransformComponent` priority preserved). Cost: O(depth) — typically
< 5 levels. Stack-allocated up to depth 32, heap-fallback beyond.

### Script Transform API (Issue #81)

`Entity` (managed) exposes coordinate space explicitly:

| Property                | Backed by                           | Behaviour |
|-------------------------|-------------------------------------|-----------|
| `LocalPosition` (set/get) | `TransformComponent.position`     | parent-relative; MarkDirty on set |
| `LocalRotation*`        | `TransformComponent.rotation`       | quat / Euler variants |
| `LocalScale`            | `TransformComponent.scale`          | parent-relative scale |
| `WorldPosition`         | `WorldTransformComponent.matrix`    | reads call `EnsureWorldUpToDate` first; setters compute `parentInv × world` and write local |
| `WorldRotation*`        | extracted rotation of world matrix  | setters do `inverse(parentWorldRot) * worldQ` |
| `LossyWorldScale`       | basis-vector lengths of world matrix | **read-only** — Unity convention; non-uniform parent scale makes setter ill-defined |
| `WorldMatrix`           | full 4×4 from world transform       | transposed from glm column-major to System.Numerics row-major in the C# wrapper |
| `Forward / Right / Up`  | `WorldRotation` × unit axes         | always world (engine forward = −Z) |
| `Translate(v, Space)`   | pure C# — `WorldPosition +=` …      | `Space.Self` rotates `v` by `WorldRotation` first |
| `Rotate(q, Space)`      | pure C# — left/right multiply       | `Space.Self` = pre-mul on `LocalRotation`; `Space.World` = post-mul on `WorldRotation` |
| `TransformPoint / TransformDirection` and inverses | pure C# — `Vector3.Transform` over `WorldMatrix` / `WorldRotation` | no extra native call beyond the matrix/quat getter |

Native function table version was bumped 5 → 6
(`ScriptApiFunctionTable::version` + `NativeApi.ExpectedTableVersion`); the
old `Entity_GetPosition` slots were *renamed* to `Entity_GetLocalPosition`
(same semantics, clearer name) and six `Entity_*World*` accessors appended.
There is no compatibility shim for unqualified `GetPosition`/etc. — the
old wrappers were removed outright; ports must touch every call site.

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
animSystem.Init(device,
                sceneRenderer->GetSkinDescLayout(),       // set=3 bindings 0/1 (existing)
                sceneRenderer->GetVelocityDescLayout());  // set=3 bindings 0/1/2 (Issue #84)

// Per scene load (per animated entity):
animSystem.PrepareEntity(entity, registry, resMgr, device);
// → allocates skinMatricesBuffer + skinMatricesBufferPrev (each boneCount×64 B, CPU-visible)
// → allocates skinDescSet (set=3: binding0=currMats, binding1=gpuMesh.skinDataBuffer)
// → allocates velocityDescSet (set=3: binding0=currMats, binding1=skinData, binding2=prevMats)  (#84)
// → sets SkinnedMeshComponent::ready = true, poseSeeded = false

// Every frame (Playing state):
animSystem.Update(dt, registry, resMgr, device);
// → detects clip swap via lastEvalClipId; pose-swap (curr ↔ prev) on normal frames,
//   skips swap on first-write / clip-swap → velocity = 0 those frames
// → re-binds skinDescSet binding 0 + velocityDescSet bindings 0/2 (UPDATE_AFTER_BIND-safe)
// → advances AnimatorComponent::time, evaluates FK → workGlobalPose/workSkinMats
// → uploads workSkinMats to skinMatricesBuffer; on first-write/clip-swap also uploads to prev

// When stopping (reset to rest pose):
animSystem.EvaluateAll(0.f, registry, resMgr, device);
// (scrubbing path does NOT touch prev buffer — velocity remains valid for next play)

// On shutdown:
animSystem.Shutdown(device);
// → frees per-entity skinMatricesBuffer + skinMatricesBufferPrev + skinDescSet + velocityDescSet
```

### GPU Skinning Data Flow

```
PrepareEntity (once):
  GPUMesh::skinDataBuffer       ← per-asset (joints+weights SSBO, uploaded by ResourceManager)
  skinMatricesBuffer            ← per-entity (mat4[boneCount], CPU-visible, curr pose)
  skinMatricesBufferPrev        ← per-entity (mat4[boneCount], CPU-visible, prev pose; Issue #84)
  skinDescSet     (set=3)       ← binding0 = currMats     binding1 = skinData
  velocityDescSet (set=3, #84)  ← binding0 = currMats     binding1 = skinData     binding2 = prevMats

Update (per frame, Playing only):
  if (!firstWrite && !clipSwap) {
      swap(curr, prev)                   // pointer swap of RHIBufferHandle members
      re-bind skinDescSet[0]  and  velocityDescSet[0/2]
  }
  FK → workSkinMats[]  →  UploadBufferData(skinMatricesBuffer, ~3 KB per skinned entity)
  if (firstWrite || clipSwap) {
      UploadBufferData(skinMatricesBufferPrev, same data)  // seed prev = curr → velocity = 0
      poseSeeded = true
  }
  deferred_geometry_skinned.vert:  reads only set=3 bindings 0/1 (skin_deform.glsl)
  velocity_prepass_skinned.vert :  reads all three bindings via skin_deform_dual.glsl (SkinMatrix + SkinMatrixPrev)
```

### Pose Double-Buffer (Issue #84)

`Update` keeps a logical "curr" and "prev" pair of bone-matrix SSBOs per entity. Three force-reseed triggers
guard against velocity spikes: (a) first write after `PrepareEntity`, (b) `meshAsset` swap → `PrepareEntity`
re-creates buffers with `poseSeeded = false`, (c) `AnimatorComponent::clipAsset` change → `lastEvalClipId`
mismatch. In all three cases `prev = curr` is uploaded for that frame so per-vertex velocity is zero.

`EvaluateAll` (scrubbing in Editing state) deliberately bypasses the swap — editor scrubbing has no
"previous frame" concept and we don't want it to produce per-vertex motion vectors.

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
- `managed/StellarAlia.Runtime/` — C# engine API surface (`ScriptBase`, `Entity`, `Debug`, `Time`, `Input`, `InputMap`, `Mathf`, `AnimatorProxy`, `RigidBodyProxy`, `PointLightProxy`, `Physics`, `PostProcess`, `QuaternionExt`, `NativeApi`)
- `managed/StellarAlia.ScriptBridge/` — Roslyn compiler (`ScriptCompiler`), collectible ALC loader (`ScriptLoader`), unmanaged entry points (`ScriptBridgeEntry`)
- `demo_project/assets/scripts/` — user `.cs` scripts

### Hosting Model

`ScriptSystem::Init` loads `hostfxr` at runtime, initialises a .NET host context pointing at `StellarAlia.ScriptBridge.runtimeconfig.json`, and retrieves nine function-pointer delegates via `get_function_pointer`:

```
Lifecycle:           Initialize | Compile | Instantiate | InvokeLifecycle | RemoveInstance | Unload
Field reflection:    GetClassSchemaBlob | GetClassDefaultsBlob | ApplyFieldValues
```

The field-reflection group is loaded the same way as the lifecycle entries but lives outside `ScriptApiFunctionTable` — they are managed→native pull endpoints (Inspector → ALC) rather than native→managed lifecycle drivers.

`Initialize` receives a `ScriptApiFunctionTable*` — a plain struct of C function pointers (currently **version 7**, see field table below) covering transforms (local + world), entity lifecycle, rigidbody physics, point light control, physics raycast, animator, input, debug draw, logging, time, InputAction/InputMap, MeshRenderer & MaterialOverride parameter access, and PostProcess screen modifications. The first field is `uint32_t version` so both sides can detect layout mismatches at startup. `ScriptApiContext` carries `Scene*`, `SceneRenderer*`, `InputSystem*`, `DebugDraw*`, and `PhysicsSystem*` so the C-side dispatchers can reach the live engine subsystems. The managed `NativeApi` class stores this table and calls through it; this avoids making `StellarAlia.Runtime` a native shared library.

**Function table versioning rules:**
- New entries are **appended to the end** of `ScriptApiFunctionTable` — never reordered. The managed-side `[StructLayout(LayoutKind.Sequential)]` struct must add fields in the same order.
- Existing entries' positions never shift; this keeps ABI compatibility for in-flight builds during incremental development.
- Bump `version` whenever fields are added or removed. Mismatch is a hard fail at `ScriptBridgeEntry.Initialize` (managed throws; ScriptSystem disables scripting).
- Current versions: v3 added InputAction/Mesh/MaterialOverride; v4 RigidBody diagnostics; v5 InputMap stack control; v6 world-space transform accessors (Issue #81); v7 PostProcess screen modifications (Issue #47, 16 entries).

**Key Runtime API classes (all in `StellarAlia` namespace):**

| Class | Description |
|-------|-------------|
| `Mathf` | Pure managed math utilities: `Lerp`, `Clamp`, `Clamp01`, `PingPong`, `SmoothStep`, `Approximately`, `MoveTowards`, and `MathF` wrappers |
| `Input` | `IsKeyDown`, `IsKeyJustPressed`, `IsKeyJustReleased` (frame-state tracked via `HashSet<Key> _prev/_curr`, updated by `BeginFrame()` before first `OnUpdate` each frame) |
| `Entity` | `GetRotation/SetRotation(Quaternion)`, `Forward/Right/Up` direction vectors, `Destroy()`, static `Create()`, `GetRigidBody()`, `GetPointLight()` |
| `RigidBodyProxy` | `LinearVelocity`/`AngularVelocity` (get/set), `AddForce`, `AddImpulse` |
| `PointLightProxy` | `Color`, `Intensity`, `Range` (get/set) |
| `Physics` | `Raycast(origin, direction, maxDist, out RaycastHit)` — wraps Jolt NarrowPhaseQuery |
| `PostProcess` (Issue #47) | Unity-style static accessors for the active scene's screen modifications: `PostProcess.Vignette.{Enabled,Intensity,Smoothness}`, `PostProcess.ChromaticAberration.{Enabled,Strength}`, `PostProcess.FilmGrain.{Enabled,Intensity,Size}`. Setters mutate `WorldSettings::pp.*` then call `SceneRenderer::ApplyWorldSettings(ws, /*updateIBL=*/false)` for live-apply — same path the editor uses on slider drag. |
| `QuaternionExt` | `FromEulerDegrees`, `FromEulerRadians`, `Slerp`, `RotateTowards`, `AngleDegrees`, `ToEulerDegrees` |

### Play Lifecycle

```
OnPlayStart(Scene& gameScene)
  └─ SA_Script_SetContext({&gameScene, …})  ← redirect g_ctx.scene to game copy
     └─ Compile(sourcePaths[])  ← Roslyn in-memory → byte[] assembly
        └─ Load(bytes)          ← CollectibleALC.LoadFromStream
           └─ m_schemaCache.Clear()  ← previous-ALC schemas now stale
           └─ for each ScriptComponent entity:
                Instantiate(entityId, className)
                InjectFieldValues(entityId, sc)   ← #74: push sc.fields into instance
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

On every project open (`Application::UpdateProjectPaths`), the engine calls `GenerateIdeProjectFiles(projectDir)`. Issue #82 reworked this to be machine-independent:

1. **Library/managed/ refresh** — `CopyManagedLibsToProject` copies `StellarAlia.Runtime.{dll,pdb,xml}` from the engine's `BIN_DIR/managed` into `{projectDir}/Library/managed/`. `ScriptBridge` and `Microsoft.CodeAnalysis.*` are engine-internal and intentionally excluded. Missing source / copy failures log a warning but do not block project load.
2. **`Directory.Build.props`** — always overwritten. Sets `<StellarAliaManaged>$(MSBuildThisFileDirectory)Library\managed</StellarAliaManaged>` — a relative path resolved at MSBuild time. Contains no machine-specific data, so it is safe to commit.
3. **`{stem}.csproj`** — written only if absent. References `$(StellarAliaManaged)/StellarAlia.Runtime.dll`, which now resolves inside the project tree.
4. **`{stem}.sln`** — written only if absent. Project GUID is deterministically derived from the stem via FNV-1a → UUID v5.
5. **`.gitignore`** — written only if absent (preserves user-customised ignores). Content comes from `assets/templates/project/.gitignore.template`, with a hardcoded fallback when engine assets are unreachable. Ignores `Library/`, `bin/`, `obj/`, `.idea/`, `.vs/`, `cook_cache/`.

`ProjectManager::CreateProject` additionally seeds `.gitignore` and the starter `Controls.sainputmap` up front so a freshly created project has a clean working tree before the editor first hands off to `GenerateIdeProjectFiles`.

MSBuild automatically discovers `Directory.Build.props` by searching parent directories, so `.csproj` needs no explicit import. `StellarAlia.Runtime.xml` powers IDE IntelliSense tooltips; `StellarAlia.Runtime.pdb` powers script-side debugging.

Runtime DLL loading is **not** affected by this localisation: `ScriptSystem` still loads `StellarAlia.Runtime` via `hostfxr` from the engine's `BIN_DIR/managed` (`ScriptSystem::Context::managedDir`). The project-local copy under `Library/managed/` serves IDE tooling only. Switching between Debug and Release engine builds therefore changes what gets cached under `Library/managed/`, but the API surface is identical so IDE compilation is unaffected.

Workflow implications:

- **Clone-and-open**: cloning a project on a fresh machine, then opening it in StellarAlia editor once, is sufficient to populate `Library/managed/`. After that, opening the `.sln` standalone in Rider/VS works without further setup.
- **CI without engine**: running `dotnet build` on a script project outside the editor requires `Library/managed/` to already exist — either by opening the project in the editor first, or by some future build-pipeline step that mirrors `CopyManagedLibsToProject`. Out-of-scope for #82.
- **Engine version drift**: a project's `Library/managed/` always reflects the last editor that opened it. Pinning to a specific engine version would require a separate `version.txt` mechanism (deferred).

### ECS Integration

`ScriptComponent` carries `scriptId : AssetID` (the `.cs.sameta` UUID), `className` (defaults to the file name stem), and `fields : unordered_map<string, ScriptFieldValue>` (#74 — Inspector-edited field values per entity, injected into the C# instance at Play start). `ScriptSystem::Context` holds a `Resource::AssetRegistry*`; `OnPlayStart` / `Instantiate` use `AssetRegistry::FindByID(sc.scriptId)->sourcePath` to resolve the absolute `.cs` path before passing it to Roslyn. `ScriptSystem` subscribes to `entt::registry::on_destroy<ScriptComponent>` to call `RemoveInstance` when an entity is destroyed during play.

`SceneSerializer` writes `script: { asset_id, class }`; loaders that encounter pre-#73 `script.path` extract the stem into `className` and leave `scriptId` invalid (user re-drops the `.cs` to repair). `sc.fields` is not yet persisted (deferred to #75).

`RecompileEditing` (Edit-mode compile, triggered by FileWatcher + `EditorMode::OnAttach` / `LoadProject` warm-up) compiles **every `.cs` in `AssetRegistry::EntriesByType("Script")`** — not just the ones referenced by ScriptComponent — so the Inspector can resolve `ScriptClassSchema` for any user script the moment the user drags it onto a freshly-added component. Unlike pre-#74, RecompileEditing does NOT call `Unload()` afterwards; the ALC stays live in Edit mode so reflection queries hit a populated context. `ScriptLoader.Load()` already swaps `_alc` on each Compile, so the previous CollectibleALC is GC-eligible after the next compile — no leak.

### Script Field Reflection (#74 / #75)

The Inspector displays — and edits — the public fields of every user `ScriptBase` subclass via a managed-side `System.Reflection` scan exposed to native through two binary blob protocols (schema blob + field-value blob), both defined in `src/function/script/ScriptFieldBlob.{hpp,cpp}` and mirrored byte-for-byte in `managed/StellarAlia.ScriptBridge/FieldBlobIO.cs`.

**Data model (`src/function/script/ScriptFieldSchema.hpp`)**:

```cpp
enum class ScriptFieldKind : uint8_t {
    Bool=0, Int32=1, Float=2, String=3, Vec2=4, Vec3=5, Vec4=6,
    AssetRef=16, EntityRef=17, Color=18, Enum=19,
    Unsupported=255,
};
struct ScriptFieldDescriptor {
    string name, label, typeHint;
    ScriptFieldKind kind; uint16_t byteSize;
    // #75 attribute trailer (schema v2):
    string tooltip, header;
    bool   hidden;
    bool   hasRange; float rangeMin, rangeMax;
};
struct ScriptClassSchema {
    string className;
    vector<ScriptFieldDescriptor> fields;
    // #75 — C# field initializers (Activator.CreateInstance) used to seed
    // Inspector defaults when a field is first displayed or new-after-recompile.
    unordered_map<string, ScriptFieldValue> defaults;
};
using ScriptFieldValue = variant<bool, int32_t, float, string,
                                  vec2, vec3, vec4, AssetID, uint64_t>;
```

**Wire format** (little-endian, no padding):

```
Schema blob (GetClassSchemaBlob → DecodeSchema):
  u16 schemaVersion; str className; u32 fieldCount;
  field_v1 (sv=1, #74): { str name; u8 kind; str typeHint; u16 byteSize }
  field_v2 (sv=2, #75): field_v1 + { str tooltip; str header;
                                      u8 flags (bit0=hidden, bit1=hasRange);
                                      [f32 rangeMin; f32 rangeMax] if hasRange }
  Forward-compat: reader switches on schemaVersion; older binaries reading v2
  blobs ignore the trailer because all v1 fields are length-prefixed.

Field-value blob (ApplyFieldValues / InjectSingleField /
                  Defaults blob / .sascene `script.fields` mirror):
  u32 recordCount;
  record: { str name; u8 kind; u16 payloadLen; byte payload[payloadLen] }
  payload by kind: Bool=1B, Int32/Float/Enum=4B, Vec2/3/4=8/12/16B,
                   String=u16-prefixed utf8, AssetRef=16B uuid (hi LE+lo LE),
                   EntityRef=8B u64, Color=16B (RGBA float, Vec4-compatible)
```

**Managed exports** (loaded once at `ScriptSystem::Init`):

| Export | Protocol | Purpose |
|---|---|---|
| `GetClassSchemaBlob(classNameUtf8, outBuf, capacity)` | two-step | Build & emit schema blob v2 for a `ScriptBase` type |
| `GetClassDefaultsBlob(classNameUtf8, outBuf, capacity)` | two-step | `Activator.CreateInstance(type)` + `FieldReflector.CaptureFieldValues` — captures the C# `= initializer` values so the Inspector seeds with meaningful defaults |
| `ApplyFieldValues(entityId, blob, blobLen)` | one-shot | Reflection `FieldInfo.SetValue` per record onto the live instance; mismatched kind/payload → record skipped |

**Recognised C# field types** (`FieldReflector.ResolveKind`):

| C# type | Kind | typeHint |
|---|---|---|
| `bool` / `int` / `float` / `string` | Bool / Int32 / Float / String | "" |
| `Vector2` / `Vector3` / `Vector4` (System.Numerics) | Vec2/3/4 | "" |
| `StellarAlia.Color` | Color | "" — wire is 4×f32, Inspector renders `ColorEdit4` |
| `StellarAlia.AssetRef` (16-byte UUID) | AssetRef | from `[AssetType("Mesh")]` attribute, "" if absent |
| `StellarAlia.Entity` | EntityRef | "" |
| `enum E : int` | Enum | enum FQN |

**Inspector attributes** (`managed/StellarAlia.Runtime/ScriptAttributes.cs`):

- `[Range(min, max)]` — `SliderInt`/`SliderFloat` replaces Drag
- `[Tooltip("…")]` — `ImGui::SetTooltip` on hover
- `[Header("…")]` — `ImGui::SeparatorText` emitted before the field
- `[HideInInspector]` — field stays in schema (still serialized + InjectFieldValues) but `ScriptDrawer` skips rendering
- `[SerializeField]` — reserved for future opt-in private-field scanning; currently only `public` instance fields are inspected
- `[AssetType("Mesh")]` — fills `ScriptFieldDescriptor.typeHint` for AssetRef picker filtering

**EntityRef translation** (`ScriptSystem::InjectFieldValues` / `InjectSingleField`):

`sc.fields["target"]` stores the persistent `EntityIdComponent.sceneLocalId`, but the C# `Entity` struct holds the live `entt::entity` bits. Native translates before encoding the value blob: `scene.FindBySceneLocalId(sceneLocalId) → entt bits`. A missing/freed target encodes as 0 → `Entity.IsValid` returns false on the managed side.

**Persistence** (`SceneSerializer`):

`script.fields[]` is a JSON array of `{ name, kind: "<KindName>", value }` records. Color writes as a 4-element JSON array; AssetRef as a UUID string; EntityRef as a u64 (sceneLocalId). Round-trip is lossless for all #75-supported kinds. Pre-#75 scenes with `script` but no `fields` key load with empty `sc.fields` → defaults seed on next Inspector display.

**`scene_local_id` mirror**: each entity JSON gains a `scene_local_id` (u64) field. `Scene::AssignSceneLocalId` restores it on load (and bumps the monotonic counter); pre-#75 scenes auto-assign at load time.

**Recompile migration** (`ScriptSystem::RecompileEditing`):

After `m_fnCompile` + `m_schemaCache.Clear()`, ScriptSystem walks every `ScriptComponent` and reconciles `sc.fields` against the freshly-fetched schema:

- **retained**: name unchanged, variant alternative matches new kind → keep old value
- **reset**: name unchanged but kind changed (user edited `.cs` field type) → reseed from `schema.defaults`, fall back to kind-zero
- **dropped**: name no longer in schema → discard, logged as `INFO`
- **defaulted**: new field in schema → seed from `schema.defaults` (C# initializer) when available, else kind-zero

Counts are logged as a one-line summary per `RecompileEditing` call.

**`ScriptSchemaCache`** is the single owner of decoded schemas. `Clear()` is called on every successful Compile (both `OnPlayStart` and `RecompileEditing`) so stale layouts can't leak across recompiles. Cached entries return raw pointers — stable for the cache's lifetime (`unordered_map` references survive insertion).

**`ScriptApiFunctionTable.version` is NOT bumped by #74/#75** — all reflection exports live outside the table (`LoadAndGet` path), and bumping version would force a managed DLL rebuild whose stale-mismatch failure mode (`AccessViolationException` at runtime) is much harsher than the linkage failure mode of a missing export name. The contract is: lifecycle exports go in the table (versioned), reflection / utility exports are pulled by name (independent).

**Out of #75 scope** (open for follow-up): `List<T>` and nested `[Serializable] struct` field expansion (dot-path schema entries); `Dictionary<K,V>`; UI-extensible custom attributes; `AnimationCurve` editor; MultiObject Edit.

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

Current users: `TonemapFeature::m_cgLutTex` (32×32×32 RGBA16F, color grading LUT);
`VolumetricFogFeature` froxel volumes ((w/8)×(h/8)×64 RGBA16F RG transients, Issue #49)
and `FrameUniformsBuffer::m_fogVolumePlaceholder` (1×1×2 dummy).
`UploadTextureData` copies the full `desc.depth` extent (fixed in Issue #49 — was `{w,h,1}`).

### Cubemap Textures

`RHITextureDesc::cubemap = true` triggers:
- `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`; `arrayLayers` forced to 6
- Main view: `VK_IMAGE_VIEW_TYPE_CUBE` (for `samplerCube`)
- Per-mip UAV views: `VK_IMAGE_VIEW_TYPE_2D_ARRAY, layerCount=6`
  (Vulkan forbids `CUBE` views as storage images)

### `IRHICommandList::GenerateMipmaps`

Blit chain from mip 0→1→…→N-1. Input must be in `ShaderRead`; output all mips
in `ShaderRead`. Texture must have `CopySrc` usage bit. This is the fixed-function
(hardware blit) mip path — distinct from the compute mip generation below.

### Per-Mip Storage-Image Writes & SPD (Issue #94)

Two axes to keep separate: **who generates mips** (hardware blit above, or a compute
shader) and **how a compute shader binds the mips it writes** (the descriptor helpers here).

Compute mip writes bind a texture's mip level as a storage image (UAV):
- `WriteDescriptorStorageImageMip(ds, binding, tex, mip)` — one mip into a **scalar**
  `image2D` binding (used by IBL prefilter, which writes one convolved mip at a time).
- `WriteDescriptorStorageImageArrayMip(ds, binding, arrayElement, tex, mip)` — one mip into
  a specific **array element** of an `image2D u_mips[N]` binding. The only difference from the
  scalar variant is VK `dstArrayElement`; the scalar one delegates here with element 0. Lets a
  single dispatch hold the whole chain (element i → mip i) and write any level. `FrameContext::
  BindStorageImageArrayMip` is the RG-side deferred wrapper. Storage-image arrays reflect their
  size (`arraySize`) so the auto-derived descriptor layout gets `descriptorCount = N`.

**SPD — single-pass downsampler** (`assets/shaders/spd_downsample.comp`). Generic reduction
pyramid (min/max/avg via the `SPD_REDUCE` macro) built in **one dispatch**: each 256-thread
workgroup reduces a 64×64 source tile to mip1..mip6 in registers+LDS; the last workgroup
(elected via a `coherent` atomic-counter SSBO) reduces the mip6 image to mip7..mip12. `coherent
image2D u_mips[12]` makes cross-workgroup mip6 writes visible. LDS-only (no subgroup ops) so it
compiles at the default `vulkan1.0` target; supports up to 4096² (12 mips). Because it is one
dispatch writing the whole chain in one UAV state, it needs **no per-subresource render-graph
tracking**. Applicability = reduction-type mips only: Hi-Z (min, Issue #89), min/max pyramids,
box mipmaps. NOT IBL prefilter (per-mip convolution, not reduction) nor Bloom (13-tap Karis) nor
AutoExposure (histogram). Validated headless in `examples/spd_test` (readback vs CPU box-average).

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

### Deferred Resource Destruction (Issue #72)

`VulkanDevice::FreeDescriptorSet`, `DestroyBuffer`, `DestroyTexture` are non-immediate.
They queue the Vulkan objects into `m_pendingFree[MAX_FRAMES]` (one slot per in-flight
frame); the RHI handle is invalidated immediately so future lookups return null.

```
DestroyXxx(handle):
    handle slot in m_textures / m_buffers / m_descSets → valid = false (immediate)
    VkObject + VmaAllocation → m_pendingFree[m_frameIdx].push(...)

BeginFrame():
    vkWaitForFences(fence[m_frameIdx])      ← GPU done with this slot's previous submit
    FlushPendingFree(m_frameIdx)            ← safe: vkDestroyXxx / vmaDestroy / vkFreeDescriptorSets

WaitIdle():
    vkDeviceWaitIdle                         ← every fence drained
    FlushPendingFree(0..MAX_FRAMES-1)        ← flush every slot

~VulkanDevice():
    WaitIdle()                               ← drain pending before pool/allocator teardown
```

This eliminates `vkFreeDescriptorSets in-use` / `vkDestroyImage in-use` validation
errors when materials, draw items, or skin entries are released during scene transitions
or per-frame state updates. Aligned with UE5's `FRHIResource::DeferredDelete` pattern.

### Additional RHI Surface

| API | Purpose |
|-----|---------|
| `IRHIDevice::CreateBindlessTextureLayout(capacity)` | Builds a fixed-size sampler2D-array layout with `UPDATE_AFTER_BIND` + `PARTIALLY_BOUND` flags. Used once by `BindlessTextureHeap`. |
| `IRHIDevice::WriteDescriptorTextureArray(ds, binding, arrayElement, tex)` | Writes a specific element of a sampler array. Used by `BindlessTextureHeap::Register/Release`. |
| `IRHIDevice::WriteDescriptorBuffer(..., dynamic)` | New `dynamic=true` overload writes `STORAGE_BUFFER_DYNAMIC` (or `UNIFORM_BUFFER_DYNAMIC`) descriptor type to match the bound layout slot. |
| `IRHIDevice::GetMinStorageBufferOffsetAlignment()` | Used by `MaterialParamRing` to align bump-allocator offsets. |
| `IRHICommandList::SetDescriptorSet(set, ds, dynamicOffsets={})` | Optional `std::span<const uint32_t>` parameter forwards to `vkCmdBindDescriptorSets`' dynamic-offset array. |

### Bindless Vulkan Features

`VulkanDevice::InitDevice` enables (Vulkan 1.2 core):
- `runtimeDescriptorArray`
- `descriptorBindingPartiallyBound`
- `shaderSampledImageArrayNonUniformIndexing`
- `descriptorBinding*UpdateAfterBind` (UBO/Sampled/Storage)

Descriptor pool sizing (`InitDescriptorPool`) reserves 4608 combined-image-sampler
descriptors (covers the 4096-slot bindless heap + per-MaterialType legacy bindings) and
64 `STORAGE_BUFFER_DYNAMIC` for the per-frame MaterialParamRing.

---

## Resource Layer — Material System

### Set Layout Convention (Issue #72, aligned with UE5 / Unity HDRP)

```
set=0  BindlessTextureHeap   ← bound 1× per cmd buffer (engine-wide stable)
set=1  FrameUniforms          ← bound 1× per pass (binding=7 appended Issue #56:
                                 t_ShadowMap for forward passes, written once at Init)
set=2  MaterialParams SSBO    ← per-draw via dynamic offset into MaterialParamRing
set=3  Skin / per-object      ← per-skinned-draw (only on skinned pipelines)
```

The PBR `MaterialParams` block declaration is shared via `material_params_pbr.glsl`
(Issue #56) — `deferred_geometry.frag`, `depth_prepass_mask.frag`, and
`forward_transparent.frag` consume the SAME per-draw blob (`DrawItem::
materialUboOffset`), so the include is the single source of truth for the byte
layout (tail member `alphaCutoff`, default 0.5 via annotation). ShaderReflectTool's
annotation parser follows `#include` (SPIR-V carries no comments). `.samatc` gained
top-level `alphaMode`/`doubleSided` pipeline-state fields (missing = opaque legacy);
`MaterialInstance` carries them as `MaterialRenderState` (Mask/Blend are SSBO-path
only — legacy-UBO assets downgrade to Opaque with a warning). Per-entity overrides
live on `MaterialOverrideComponent` (`alphaMode`/`doubleSided` int8, -1 = inherit),
edited via the Material Override drawer (undoable) and serialized with the scene.

Lower set index = more stable. Vulkan layout compatibility cascades from set=0 upward,
so placing the most stable resource (bindless heap) at set=0 means it survives all
pipeline switches without invalidation. Skin at the highest set ensures per-entity bone
changes don't cascade-invalidate frame / material bindings.

### MaterialType paths (SSBO+bindless vs legacy UBO)

The `usesMaterialParamsSSBO` flag on `MaterialType` selects the path, detected from
SPIR-V reflection at registration time:

| Detection | Path | Per-draw cost | Texture override cost |
|-----------|------|---------------|-----------------------|
| set=2 binding=0 is `StorageBuffer` named `MaterialParams` | **SSBO + bindless** | memcpy 80B blob into ring + 1× `vkCmdBindDescriptorSets` with dynamic offset | `heap.Register(tex)` (deduped) writes 1 uint to blob |
| Otherwise | **Legacy UBO** | `Bind()` rebinds the per-instance desc set | `CloneInstance` + per-entity descriptor set |

The MaterialParams SSBO block has a hard convention: members named `t_*_Idx` (uint = 4 B)
are bindless texture indices; other members are scalar params. ShaderCookLib reflection
splits them into `ParamDef[]` and `TextureDef[]` (with `uboBlobOffset`).

### MaterialInstance Data Flow

**SSBO+bindless path** (PBR / built-in mesh materials):

```
asset load → MaterialManager::LoadMaterial:
    type->CreateInstance(device, defaultTex):
        allocate descSet from set=2 layout (binding=0 = STORAGE_BUFFER_DYNAMIC)
        m_paramBlob = default values (params + t_*_Idx defaults = 0 = white)
    WireSSBODescriptor:
        WriteDescriptorBuffer(descSet, 0, MaterialParamRing.buffer, dynamic=true)
    SetTexture(name, texHandle):
        heap.Register(tex) → bindlessIdx (deduped)
        memcpy bindlessIdx → m_paramBlob[TextureDef::uboBlobOffset]

BuildDrawList per entity:
    blob = base->paramBlob              ← copy of asset baseline
    for (name, val) in matOverride.scalars: memcpy val → blob[ParamDef::offset]
    for (name, texID) in matOverride.textures:
        tex = resMgr->LoadTexture(texID); idx = heap.Register(tex)
        memcpy idx → blob[TextureDef::uboBlobOffset]
    item.materialUboOffset = materialRing.Allocate(blob.data(), blob.size())

Render execute:
    SetDescriptorSet(0, bindlessSet)    once per pass
    SetDescriptorSet(1, frameSet)       once per pass
    SetDescriptorSet(2, mat->descSet, {&item.materialUboOffset, 1})   per draw
    SetDescriptorSet(3, skinDescSet)    per skinned draw
    DrawIndexed
```

**Legacy UBO path** (post-fx materials etc. — project `.saglsl` shading models migrated
to the SSBO+bindless path in #73-A; the template and demo shaders declare
`std430 readonly buffer MaterialParams` + `t_*_Idx` and sample via the shared
`bindless_textures.glsl` include):

```
material->SetFloat / SetTexture → memcpy CPU blob, mark dirty
material->Bind(cmd):
    if dirty: upload UBO + write texture descriptors
    SetDescriptorSet(2, m_descSet)
```

### ShaderProgram

Compiled VS+FS pair. Manages:
- `RHIShaderHandle` vert + frag
- Merged `ShaderReflection` (stage flags OR-ed)
- 4 slot layouts:
  - slot 0 = `m_bindlessLayout` — passed in via `Desc::bindlessLayout`, always wired
    (engine-wide convention) even when shader doesn't access set=0; this keeps slot 0
    layout-compatible across all pipelines so set=0 binding survives pipeline switches
  - slot 1 = `m_frameLayout` — passed in via `Desc::frameLayout`
  - slot 2 = `m_materialLayout` — derived from reflection set=2 (or empty if shader has no set=2)
  - slot 3 = `m_set3Layout` — derived from reflection set=3 (skin, only set when shader uses set=3)
- Pipeline cache: `PipelineStateKey → RHIPipelineHandle` (Issue #56 — key = AttachmentKey
  **plus the full fixed-function state** `PipelineRenderState` {cull/blend/topology/
  depthCompareOp/depthTest/depthWrite/stencil}, so one shader serves
  {Opaque/Mask}×{single/double-sided}×{LEQUAL/EQUAL}×{stencil} permutations.
  Legacy loose-flag `GetOrCreatePipeline` overload retained; explicit-state overload
  + `MaterialType::DefaultRenderState()` for variant composition)

### SSBO_DYN promotion

`VulkanDevice::CreateDescriptorSetLayout` promotes a `StorageBuffer` at set=2 binding=0
named `MaterialParams` to `STORAGE_BUFFER_DYNAMIC` (SPIR-V reflection cannot express the
"dynamic" suffix). Dynamic-descriptor layouts cannot coexist with `UPDATE_AFTER_BIND_BIT`
(spec VUID-03001 / VUID-03011) — the layout creator detects any dynamic binding in the
set and drops UAB for that layout only.

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

When supplied, `Desc::frameLayout` occupies **set=1** (matching the engine-wide per-frame
set convention — set=0 bindless, set=1 frame), so a compute shader can `#include
"frame_uniforms.glsl"` and bind the renderer's `frameSet` at set=1 exactly like a graphics
post-FX pass. `CreateComputePipeline` fills the unused set=0 slot with an empty layout.
`SSRFeature` (Issue #48) is the first consumer; the global IBL/sky/LTC samplers in the frame
layout are declared `RHIShaderStage::All` (not Fragment-only) so compute passes can sample them.

### ProgramCache (Issue #86)

Central, Resource-layer owner of **all** GPU programs — graphics `ShaderProgram` (vert+frag)
and `ComputeProgram`. Owned by `SceneRenderer`, injected via `FeatureInitContext::programs`.
Holders (`MaterialType`, `RenderFeature`) keep raw pointers; the cache owns lifetime.

```cpp
class ProgramCache {
    void Init(IRHIDevice*, frameLayout, bindlessLayout, shaderDir);
    ComputeProgram* GetCompute (stem, useFrameLayout=true, projectScope=false);
    ShaderProgram*  GetGraphics(key, vertStem, fragStem, primaryDir, fallbackDir, projectScope);
    bool ReloadGraphicsFrag(key, fragSpv, fragRefl);   // hot-reload (.saglsl dispatch)
    void ClearProjectPrograms();                       // project switch
    void Shutdown();
};
```

- **Not a dedup layer**: keyed per holder (MaterialType name / `"feature:variant"` / compute
  stem) — each holder owns its own program instance. shader/material separation already comes
  from `MaterialInstance → MaterialType → ShaderProgram`; cross-holder dedup is not a goal and
  would collide with `ShaderProgram`'s AttachmentKey-only pipeline cache. Value is central
  ownership + uniform loading + uniform hot-reload + engine/project scoping.
- `MaterialType::shader` is a `ShaderProgram*` into the cache (not by value). `MaterialManager::
  RegisterTypeFromShaders` delegates program acquisition to `GetGraphics` and reads parameter
  layout back from `shader->GetMergedReflection()` (single `.refl` load).
- **Cleanup order** (project switch): instances → `ClearProjectTypes` → `ClearProjectPrograms`
  (types drop their program pointers before the programs are freed → no dangling/double-free).
- Compute features (`AutoExposure`, `Tonemap` CG-bake, `SSR`) and graphics feature variants
  (skinned shadow/gbuffer/velocity/selection) all acquire programs from the cache.
- **Foundation for "above-program" abstractions**: a `ComputeProgram` is the shared base for
  any future user-facing compute layer (screen effects, gameplay compute, particles) — those
  diverge *above* the program (resource model / scheduling / lifetime) and are separate systems.

### ScreenEffect System — `.saeffect` (Issue #88)

Declarative custom post-processing: users author a `.saeffect` (a shader + `@`-annotations),
zero C++, to insert a pass at a frame injection point — the post-processing-side peer of
`.saglsl → MaterialType`. Mirrors the material system layer-for-layer:

| Material layer | ScreenEffect layer | Location |
|---|---|---|
| `.saglsl` (user) | `.saeffect` (user) | project `assets/shaders/` |
| `MaterialType` | `ScreenEffectType` (enum `EffectInject` + `@In/@Out` + `ParamDef[]`) | `function/material/ScreenEffectType.hpp` |
| `MaterialManager` | `ScreenEffectRegistry` (scans `*.saeffect.refl`, owns types, `ClearProjectEffects(device)`) | `function/material/ScreenEffectRegistry.{hpp,cpp}` |
| `RenderFeature` (engine-only) | `ScreenEffectFeature` (nested in `SceneRenderer`, one anchor per injection point) | `SceneRenderer.hpp/.cpp` |

- **Authoring** (`.saeffect`): header `@Effect / @Stage fragment|compute / @Inject / @In / @Out`
  + `#pragma sa_section fragment|compute` body. `@Param` values are inline `set=2 binding=0`
  `EffectParams` UBO member annotations — reflected by ShaderReflectTool exactly as `MaterialParams`.
- **Cook** (`ShaderCookLib::CookEffects`, independent of `.saglsl` dispatch): produces the standard
  `<stem>.saeffect.{frag|comp}.{spv,refl}` with `.refl` metadata keys `effect/stage/inject/in/out`
  (no special `.refl` format — same `SetMeta` mechanism as material `shadingModel`).
- **Injection points** (`EffectInject`): `AfterLighting / AfterTAA / BeforeTonemap / AfterTonemap`.
  `SceneRenderer::Init` pre-places one anchor `ScreenEffectFeature` per point in `m_features`;
  `RenderFrame` redirects `handles.hdr` to an anchor's output when it ran (same pattern as SSR/DoF).
  Validated HDR-space points are `AfterLighting`/`BeforeTonemap`; `@In` vocabulary
  (`ResolveEffectHandle`) covers `hdr/depth/gbufferRT0..2/velocity/ssaoTex/taaResolved`.
- **Compute stage** (Issue #91): `@Stage compute` effects run via the same anchor executor —
  `@Out` is a UAV storage image (`set=2` binding after the `@In` samplers), written with
  `BindStorageImage` + `WriteUAV` + `Dispatch(⌈w/8⌉,⌈h/8⌉)` (mirrors `SSRFeature`); `@Out hdr`
  chains like the fragment path. `ProgramCache::GetCompute` is dir-aware (project `cook_cache/shaders`
  primary, engine builtin fallback), so **project-authored compute `.saeffect` load** (the #88
  gap is closed). Create menu: "Shader ▸ Screen Effect — Compute" (`NewEffectCompute.saeffect`).
  `@Out ldr` cross-buffer + built-in Tonemap→compute are deferred to **#93** (LDR / Tonemap-compute).
- **Activation model** (Unity Volume Override / UE Blendable): the registry is only a *catalog*;
  an effect runs **only if listed** in `PostProcessSettings::screenEffects` (per-scene, serialized).
  `ApplyWorldSettings` resolves each active instance to `m_activeScreenEffects` (type-default param
  blob overlaid with the instance's named `@Param` overrides). `PostProcessPanel`'s "Screen Effects"
  section adds/removes/reorders/toggles entries and draws params via the shared `ParamWidgets` layer.
- **Asset integration**: `.saeffect` → `AssetTypeFromExtension` type `"Shader"` (auto `.sameta`,
  script icon, text inspector, "Shader ▸ Screen Effect" create menu from `NewEffect.saeffect` template).
- **GPU-safe reload**: shader/effect re-registration destroys pipelines, so mid-frame UI triggers
  (create/reimport/delete/rename) call `SceneRenderer::RequestProjectShaderReload`, which defers
  `ApplyProjectShaderTypes` (self-`WaitIdle`) to the next `RenderFrame` top.

### MaterialManager

```
Init(IRHIDevice*, ResourceManager*)
    // ResourceManager provides BuiltinTexture::White1x1 for unset slots.
    // Creates BindlessTextureHeap (4096 slots, set=0); slot 0 = default white.

SetMaterialParamRingBuffer(RHIBufferHandle)
    // Called once by SceneRenderer::Init after MaterialParamRing.Init succeeds.
    // Every SSBO-path MaterialInstance's set=2 binding=0 will be wired to this buffer.

GetTextureHeap() → BindlessTextureHeap&   // set=0 sampler array (4096 slots, deduped)
GetMaterialParamRingBuffer() → RHIBufferHandle

RegisterTypeFromShaders(MaterialTypeDesc, FeatureInitContext)
    //   struct MaterialTypeDesc {
    //       string name, vertShader, fragShader;  // shader stem, e.g. "pbr"
    //       RHICullMode cullMode  = Back;
    //       bool depthTest  = true;
    //       bool depthWrite = true;
    //   };
    // Reflection inspects set=2 binding=0:
    //   - StorageBuffer named "MaterialParams" → SSBO+bindless path (usesMaterialParamsSSBO=true)
    //   - otherwise → legacy UBO path

LoadMaterial(AssetID, ResourceManager&) → MaterialInstance*  (cached; VFS paths set centrally)
CloneInstance(MaterialInstance*) → unique_ptr<MaterialInstance>       (non-cached copy)
ClearProjectInstances()  — evict m_cachedInstances on project switch; preserves m_types
Shutdown()
```

`CloneInstance` is now used only by the legacy UBO path for per-entity overrides on
non-SSBO MaterialTypes. SSBO+bindless materials never clone — overrides are applied
to a per-draw blob in `MaterialParamRing`.

### MaterialParamRing

```cpp
class MaterialParamRing {                          // src/function/material/
    bool Init(IRHIDevice*, uint64_t bytesPerFrame = 2 MiB);
    void Reset();                                   // BeginFrame
    uint32_t Allocate(const void* blob, uint32_t size);  // returns dynamic offset
};
```

- 2 MiB cpu-visible SSBO, bump allocator
- Alignment = `device->GetMinStorageBufferOffsetAlignment()` (typically 16 B on NV/AMD)
- Fail-loud on capacity exhaustion (returns `kInvalidOffset` + `SA_LOG_ERROR`)
- Per-frame `Reset()` called from `SceneRenderer::RenderFrame` entry

### BindlessTextureHeap

```cpp
class BindlessTextureHeap {                        // src/function/material/
    bool Init(IRHIDevice*, RHITextureHandle defaultTex, uint32_t capacity = 4096);
    uint32_t Register(RHITextureHandle tex);        // deduped by tex handle index
    void     Release(uint32_t slot);
};
```

- Single set=0 desc set with 4096-slot sampler2D array (`UPDATE_AFTER_BIND` +
  `PARTIALLY_BOUND` flags via dedicated `CreateBindlessTextureLayout` RHI call)
- Slot 0 reserved for default white texture (`kInvalid` sampling falls back to white)
- Dedup: same `RHITextureHandle.index` returns the previously-assigned slot — no waste
  when many materials share the same texture; safe to call `Register` per BuildDrawList

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
           slot compat = format + width + height + depth (Issue #49: 3D volumes must not alias 2D slots) + mipLevels + usage superset
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

### Frame Uniforms (set=1) Bindings

```
binding=0  FrameData UBO    — camera matrices, time, resolution, SH9 irradiance, TAA jitter / prevViewProj / currUnjitteredViewProj, volFogFar (704 bytes)
binding=1  LightData UBO    — up to 8 lights (directional / point / spot / area)
binding=2  sampler2D        — BRDF LUT
binding=3  samplerCube      — prefiltered specular env (5 mips)
binding=4  samplerCube      — skybox cubemap
binding=5  sampler2D        — LTC inverse-M matrix LUT (64×64 RGBA32F) — area lights
binding=6  sampler2D        — LTC amplitude/GGX-norm LUT (64×64 RGBA32F) — area lights
binding=7  sampler2D        — directional shadow map (Issue #56, forward passes)
binding=8  sampler3D        — volumetric fog integrated volume (Issue #49; 1×1×2 (0,0,0,1) dummy when fog off — transparents sample unconditionally, dummy makes it a no-op)
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
| Depth | `D24_UNORM_S8_UINT` (Issue #56) | Depth + stencil: geometry writes stencil=1, DeferredLighting stencil-tests ==1 (background/cut-outs rejected fixed-function). Lighting reconstructs world position via `invViewProj × NDC`. Stencil formats carry two VkImageViews — DEPTH\|STENCIL for attachments, depth-only for sampling (`TextureEntry::sampledDepthView`). Shadow map stays D32F |

**RT2.a** encodes the shading model ID via `EncodeShadingFlags(modelID)` / `DecodeShadingModelID(a)`.
`SHADING_MODEL_PBR = 0`; custom evaluators are assigned IDs ≥ 1 in stable alphabetical order.

Normal encoding uses **Octahedral Normal Encoding** (OctEncode/OctDecode).

### Built-in Render Features (pass order)

| Feature | Condition | Output | Shader |
|---------|-----------|--------|--------|
| `ShadowFeature` | `config.shadowEnabled` | shadow map (D32, 2048²) | `shadow.vert/.frag` (+ `shadow_skinned.vert` for skinned) |
| `SkyboxFeature` | always | HDR buffer (transient) | `skybox.vert/.frag` |
| `DepthPrepassFeature` (Issue #56) | always | Owns the frame's depth+stencil clear (GBuffer therefore always Loads), then draws MASK (alpha-test) geometry: one albedo.a sample + `discard`, depth + stencil=1 written fixed-function. `RendererConfig::depthPrepassMode` (MaskedOnly default; Full = TODO, falls back with warn) | `deferred_geometry(.vert\|_skinned.vert)` + `depth_prepass_mask.frag` |
| `GBufferFeature` | always | RT0/RT1/RT2 + depth (loadOp=Load). Opaque = LEQUAL/depthWrite; MASK = EQUAL/no-write + `early_fragment_tests` (no discard — prepass already resolved cut-outs); all write stencil=1; BLEND items skipped (forward path). Per-item pipeline variants composed in BuildDrawList via `PipelineRenderState` | `deferred_geometry.vert/.frag` (+ `deferred_geometry_skinned.vert` for skinned) |
| `VelocityPrepassFeature` (Issue #84) | enabled when `MotionBlurFeature::m_enabled` OR `TAAFeature::m_enabled` (Issue #85) | Per-object writes to `handles.velocity` (RG16F): each visible draw rasterises curr & prev clip-space positions. `gl_Position` uses jittered VP (matches GBuffer depth); the velocity output uses unjittered `currUnjitteredViewProj` × `prevViewProj` so TAA can reproject without jitter compensation. Skinned variant samples curr/prev bone matrices via set=3 bindings 0/2 (`velocityDescSet`) | `velocity_prepass.vert/.frag` (+ `velocity_prepass_skinned.vert` for skinned) |
| `SSAOFeature` | always registered; disabled → fills ssaoTex with 1.0 | half-res R8 AO → blurred into `ssaoTex` | `ssao.frag` + `ssao_blur.frag` |
| `DeferredLightingFeature` | always | HDR (transient RGBA16F). Issue #56: depth+stencil bound as a **read-only attachment** (stencil test ==1) while the depth plane is simultaneously sampled — `RGPassBuilder::ReadDepthStencil` + `RHIDepthAttachment::readOnly` + descriptor written with DEPTH_STENCIL_READ_ONLY layout. PBR math lives in shared `pbr_shading.glsl`; local shadow sampler renamed `t_GShadowMap` | `deferred_lighting.frag` |
| `SSRFeature` (Issue #48 / #89) | always registered; skips if `pp.ssrEnabled==false` | 5 compute passes: HiZ_Copy + HiZ_SPD (min pyramid) → SSR_Trace (8-spp GGX, screen-space Hi-Z DDA) → SSR_Resolve (bilateral) → SSR_Temporal (velocity-reprojected adaptive accumulation + variance clip) → composite replaces IBL env-probe specular into `SSR_Composite`; sets `handles.hdr`. See [Screen Space Reflections](#screen-space-reflections-issue-48-phase-1--89-phase-2) | `ssr.comp`, `hiz_copy/hiz_spd.comp`, `ssr_resolve/ssr_temporal.comp` |
| `VolumetricFogFeature` (Issues #49/#110) | always registered; skips if `pp.volFogEnabled==false` (binds the fog-volume dummy to frame set binding 8 and returns) | 4 passes: VolFog_Inject (compute, per-froxel media incl. `FogVolumeComponent` OBBs + wind-advected noise, in-scattered light incl. sun shadow tap + analytic medium self-shadow, IGN depth jitter under temporal∥TAA) → VolFog_Temporal (#110, prevViewProj-reprojected exponential blend on the media volume, persistent 3D ping-pong) → VolFog_Scatter (compute, front-to-back transmittance integration per column) → VolFog_Apply (fullscreen `hdr·T + inscatter` into `VolFog_Output`); sets `handles.hdr`; publishes `m_integratedHandle` + frame set binding 8 for transparents. See [Volumetric Fog](#volumetric-fog-issues-49--110) | `volumetric_inject.comp`, `volumetric_temporal.comp`, `volumetric_scatter.comp`, `fullscreen_tri` + `volumetric_apply.frag`, `volumetric_common.glsl` |
| `ForwardTransparentFeature` (Issue #56 → #105) | runs after SSR+VolumetricFog / before SelectionMask+TAA; skips when no visible BLEND item | ① blends BLEND items back-to-front (per-frame clip.w sort) in place onto `handles.hdr` (a plain transient at that point — the pre-TAA move is what removed #56's copy-then-blend), AlphaBlend, depth read-only/no-write, PBR-only shading (shadow map via set=1 binding=7); fragments fog themselves from the froxel volume at set=1 binding=8 (`color·T + inscatter` at fragment depth, Issue #49 Step 9 — declares `b.Read` on `m_integratedHandle` when valid); ② when TAA is enabled, redraws the same items into an R8 `ReactiveMask` (coverage union `a + dst·(1−a)` via `(1,1,1,a)` output under AlphaBlend) exposed as `m_reactiveMask` for TAA. BLEND casts no shadow, writes no velocity, not in GBuffer | `deferred_geometry(.vert\|_skinned.vert)` + `forward_transparent.frag` / `transparent_reactive.frag` |
| `SelectionMaskFeature` | always | R8 silhouette mask; rasterised with the **unjittered** VP (Issue #107 — the outline composites after TAA) | `selection_mask.vert/.frag` (+ `selection_mask_skinned.vert` for skinned) |
| `TAAFeature` | always registered; disabled → passes `handles.hdr` through and sets `handles.taaResolved = handles.hdr` (Issue #49 Step 7: the redirected hdr, so BloomThreshold sees SSR/fog/transparents — the frame-start default `m_rgHdr` predates those redirects) | TAA_Resolve into ping-pong history (`handles.taaResolved`); reads `ReactiveMask` at binding 4 to raise the blend floor on transparent coverage (`blendReactive` 0.65, 0 when absent). TAA_Copy (Issue #105) then copies history → transient `PostTAA_HDR` and redirects `handles.hdr` — downstream RMW (BloomComposite UAV add) must never touch the history in place | `taa_resolve.frag`; `fullscreen_tri` + `forward_copy.frag` |
| `AutoExposureFeature` | always registered; skips if `pp.autoExposureEnabled==false` | 256-bin log-lum histogram → weighted percentile EV → exponential-smoothing exposure; 1-frame CPU readback via staging; reads `handles.hdr` (post-TAA copy since Issue #105) | `postfx_histogram.comp`, `postfx_exposure_adapt.comp` |
| `BloomFeature` | always registered; skips if `pp.bloomEnabled==false` | threshold reads `taaResolved`; composite writes back to `handles.hdr` | `bloom_*.frag` |
| `DoFFeature` | always registered; skips if `pp.dofEnabled==false` | CoC from depth → separable near/far Gaussian blur (H+V × 2) → smoothstep composite; sets `handles.hdr` to DoF output | `dof_coc.frag`, `dof_blur.frag`, `dof_composite.frag` |
| `MotionBlurFeature` | always registered; skips if `pp.motionBlurEnabled==false` | Per-object velocity (filled by `VelocityPrepassFeature`, Issue #84) → TileMax (16×) → NeighborMax (3×3 dilate) → McGuire 2012 reconstruct; sets `handles.hdr` to motion-blur output | `motion_blur_tile_max.frag`, `motion_blur_neighbor_max.frag`, `motion_blur_reconstruct.frag` |
| `TonemapFeature` | always registered; active when `pp.tonemapMode==Builtin` | LDR (transient `handles.ldr`) | `postfx_tonemap.frag` (ACES + optional parametric CG LUT via `sampler3D`); rebakes 32³ LUT via `ImmediateCompute` when `ColorGradingSettings` changes |
| `LutTonemapFeature` | hot-swapped in when `pp.tonemapMode==LUT` | LDR (transient `handles.ldr`) | `postfx_lut_tonemap.frag` |
| `PostFXFeature` (Issue #47) | always registered; never skipped | reads `handles.ldr` → writes swapchain. Single fullscreen pass applies vignette / chromatic aberration / film grain (uniform-control-flow toggles). All three disabled = single `texture()` copy. | `postfx.frag` |
| `SelectionOutlineFeature` | always | outline on swapchain | `selection_outline_dilate.frag` + composite |
| `InfiniteGridFeature` | when enabled | XZ grid on swapchain; `gl_FragDepth` from the **unjittered** VP (Issue #107; the ray already came from the unjittered `invViewProj`) | `infinite_grid.frag` |
| `DebugOverlayFeature` | always | debug lines on swapchain; push constant = `m_currentUnjitteredViewProj` (Issue #107; `m_currentViewProj` stays jittered for culling + transparent sort) | `debug_line.vert/.frag` |
| user `RenderFeature`s | `AddFeature(...)` | custom | custom |

`BloomFeature`, `TAAFeature`, `DoFFeature`, `MotionBlurFeature`, `SSRFeature`, and `VolumetricFogFeature` are always in the feature list; their `AddPasses` early-returns when disabled.
`TonemapFeature` ↔ `LutTonemapFeature` hot-swapped at runtime by `ApplyWorldSettings` (WaitIdle + slot replace).

```cpp
struct RendererConfig {
    bool     shadowEnabled = true;
    uint32_t shadowMapSize = 2048;
    int      bloomMipCount = 3;   // engine-level startup default; runtime changes via PostProcessSettings::bloomMipLevels
    DepthPrepassMode depthPrepassMode = DepthPrepassMode::MaskedOnly;  // Issue #56; Full = TODO
};
```

### Bloom

1. **Threshold pass** — extract pixels where luminance > 1.0
2. **Downsample chain** — `bloomMipLevels` levels (default 3, range 2–8), 13-tap COD downsample
3. **Upsample + accumulate** — bilinear upsample, weighted additive blend
4. **Composite** — additive blend into HDR color buffer

### TAA (Temporal Anti-Aliasing)

**Placement:** TAA runs after `ForwardTransparent`/`SelectionMask` and before `Bloom`.

**Data flow:**
```
GBuffer(jittered proj) → VelocityPrepass (Issue #84; gated on MotionBlur OR TAA OR SSR enabled)
  Each visible DrawItem: gl_Position = jittered VP * model, varyings = unjittered VPs * model
  Frag: out_velocity = (unjittered currUV − unjittered prevUV) → handles.velocity
→ DeferredLighting → SSR → ForwardTransparent (Issue #105: blends into handles.hdr in place,
  writes R8 ReactiveMask when TAA on) → TAAFeature
  TAA_Resolve: Read(handles.hdr, historyRead, depth, handles.velocity, ReactiveMask?)
               → Write(historyWrite)
  Reprojection uses handles.velocity directly (Issue #85); reactive coverage raises the blend
  floor (max(blend, coverage·0.65)) so velocity-less transparents don't ghost
  handles.taaResolved = rgHistoryWrite
  TAA_Copy (Issue #105): historyWrite → transient PostTAA_HDR; handles.hdr = PostTAA_HDR
  (downstream RMW like BloomComposite must never touch the ping-pong history in place;
  pre-#105 this copy only existed inside ForwardTransparent, so with zero BLEND items the
  anti-aliased image never reached Tonemap at all — latent gap, fixed here)
→ BloomThreshold reads handles.taaResolved (anti-aliased pre-bloom)
→ BloomComposite adds bloom into handles.hdr (PostTAA_HDR)
→ DoFFeature reads handles.hdr + handles.depth
  DoF_CoC / DoF_NearH / DoF_NearV / DoF_FarH / DoF_FarV / DoF_Composite
  handles.hdr = dofOutput (RGBA16F transient)
→ MotionBlurFeature reads handles.hdr + handles.velocity + handles.depth
  MB_TileMax / MB_NeighborMax / MB_Reconstruct (no MB_Velocity any more — Issue #84)
  handles.hdr = mbOutput (RGBA16F transient)
→ Tonemap reads handles.hdr
```

**Jitter:** Halton(2,3) sequence, 8-tap, written to `fu.jitter` (pixel space) and added to `proj[2][0/1]` (NDC offset). `fu.viewProj` is jittered (drives rasterization); `fu.prevViewProj` and `fu.currUnjitteredViewProj` are both unjittered (drive velocity reconstruction).

**Dual viewProj convention (Issues #46 + #85 + #107):** the engine maintains both jittered and unjittered current-frame VPs in `FrameUniforms` — mirroring UE5 `nonJitteredProjMatrix` / HDRP `nonJitteredVP`. **Scene** rasterization (GBuffer, VelocityPrepass `gl_Position`, ForwardTransparent, Shadow) uses `fu.viewProj` (jittered) so TAA's sub-pixel sampling works correctly. Velocity computation (VelocityPrepass `v_CurrClip` output) uses `fu.currUnjitteredViewProj`, producing `handles.velocity` free of per-frame jitter offsets — TAA, MotionBlur and SSR read it directly without per-shader jitter compensation. **Editor overlays** (SelectionMask→outline, InfiniteGrid `gl_FragDepth`, DebugOverlay lines via `m_currentUnjitteredViewProj`) use the **unjittered** VP (Issue #107, UE-style): they draw after TAA with no resolve to average the jitter, and the TAA-converged image sits at the unjittered position anyway. Known residual: overlay-vs-geometry intersection boundaries can flicker sub-pixel (the depth buffer itself is jitter-rasterised) — accepted. With TAA off the two matrices are identical, so behaviour is unchanged.

**First-frame guard for `fu.prevViewProj`** (Issue #46): `m_prevUnjitteredViewProj` initialises to `mat4(1.f)`. TAA absorbs this via history weighting, but Motion Blur produces visible garbage from the resulting velocity. `ApplyCameraToUniforms` therefore seeds `m_prevUnjitteredViewProj = currViewProj` on the first call, so frame 0's velocity is exactly zero.

**TAAFeature internals:**
- Ping-pong `m_historyTex[2]` (persistent RGBA16F, full-res) — imported into RG each frame
- TAA_Resolve (Issue #85): samples `handles.velocity` directly for prev-UV reprojection — replaces the older depth + `WorldPos × prevViewProj` path. As a result TAA now sees per-object motion and no longer ghosts on moving rigid bodies / animated skinned meshes
- 3×3 YCoCg AABB neighborhood clamp on history (anti-ghosting) → motion-adaptive blend (`velLen` → blendStatic ↔ blendMotion)
- `m_historyValid = false` on first frame or resize → shader uses `historyValid = 0` push constant → outputs current unmodified
- Binding layout (set=2): 0 = current HDR, 1 = history, 2 = depth (kept for layout stability), 3 = `handles.velocity` (Issue #85), 4 = transparent `ReactiveMask` (Issue #105; hdr bound as layout-filler + `blendReactive=0` when absent)

**`handles.taaResolved`:** field on `RendererHandles`; equals `handles.hdr` when TAA is disabled, set to `rgHistoryWrite` after `TAAFeature::AddPasses`. `BloomThreshold` reads this to avoid Bloom accumulating in TAA history (prevents progressive brightness). Since Issue #105 the same AddPasses also redirects `handles.hdr` to the `PostTAA_HDR` copy, so the anti-aliased frame reaches DoF/MB/Tonemap unconditionally (previously only via `ForwardTransparentFeature`'s copy, i.e. only when a BLEND item was visible).

**VelocityPrepass gating** (Issue #85): the prepass writes `handles.velocity` whenever **either** `MotionBlurFeature::m_enabled` **or** `TAAFeature::m_enabled` is true. With both features disabled the pass returns immediately and the RG leaves `handles.velocity` unallocated (the greedy slot allocator skips transients with `firstWritePass < 0`).

### Motion Blur (Per-Object, Issues #46 + #84)

**Architecture:** one velocity buffer, written by a dedicated prepass, consumed by a three-pass reconstruction chain (and by TAA since Issue #85). Phase 1 (#46) introduced `handles.velocity` and the McGuire 2012 reconstruct chain with a depth-reprojection fill. Phase 2 (#84) replaced the fill with `VelocityPrepassFeature` — per-object draws that capture camera + rigid-body + skinned pose deformation in a single RG16F target. Issue #85 made the prepass write **unjittered** velocity (rasterization still uses jittered VP) so TAA can reproject without per-shader jitter compensation.

**Placement:**
- `VelocityPrepassFeature` runs immediately after `GBufferFeature` (depth populated, no consumers ahead).
- `MotionBlurFeature` runs after DoF, before Tonemap.

`VelocityPrepassFeature::AddPasses` early-returns unless `MotionBlurFeature` **or** `TAAFeature` is enabled (Issue #85). With both off, `handles.velocity` stays unallocated by the RG (greedy interval coloring skips transients with `firstWritePass < 0`).

**Velocity RT contract** (`RendererHandles::velocity`):
- Format: `RG16F`, viewport resolution, `RenderTarget | Sampled`.
- Semantics: `(currUV - prevUV)` in NDC ratio, **no** artistic scaling. Strength + maxSpeed are applied later, at `MB_Reconstruct`.
- Lifetime: RG transient — created in `SceneRenderer::RenderFrame`, valid for the current frame.
- Writer: `VelocityPrepassFeature` — per-draw rasterization. Skybox / non-rasterized pixels stay at the `clearOnLoad=true` 0 vector.
- Consumers today: `MotionBlurFeature`. Future consumers (TAA per-object velocity upgrade, SSR reprojection) will read the same RT without prepass changes.

**Per-draw push constants** for the prepass: `{ mat4 currModel; mat4 prevModel; }` = 128 B, exactly the guaranteed push-constant limit. `currModel` comes from `WorldTransformComponent.matrix × DrawItem.subLocalTransform`; `prevModel` from `PrevTransformComponent.prevModel × subLocalTransform` (fall back to `currModel` when `PrevTransform` is missing or unseeded → velocity = 0). `viewProj` (jittered, for `gl_Position`), `currUnjitteredViewProj` (Issue #85, for `v_CurrClip`), and `prevViewProj` (unjittered, for `v_PrevClip`) are read from `u_Frame`.

**Skinned variant** uses `velocity_prepass_skinned.vert`, which includes `skin_deform_dual.glsl` — a sibling of `skin_deform.glsl` declaring set=3 bindings 0/1/2 (curr/skinData/prev) and exposing `SkinMatrix()` + `SkinMatrixPrev()`. Per-entity `velocityDescSet` (allocated by `AnimationSystem::PrepareEntity` using `SceneRenderer::GetVelocityDescLayout()`) binds all three SSBOs; the existing `skinDescSet` (set=3 bindings 0/1) remains unchanged and is used by deferred geometry / shadow / selection mask.

**Pipeline (4 passes; MB_Velocity from #46 removed in #84):**
```
0. VelocityPrepass (post-GBuffer)
   For each visible DrawItem:
     static    pipeline = velocity_prepass.vert + velocity_prepass.frag
     skinned   pipeline = velocity_prepass_skinned.vert + velocity_prepass.frag
   Push currModel + prevModel; bind set=3 velocityDescSet (skinned only).
   Depth: LoadOp::Load, depthTest=true, depthWrite=false → only the closest surface writes.
   Vert: gl_Position = u_Frame.viewProj (jittered) * currModel * pos     ← matches GBuffer rasterization
         v_CurrClip  = u_Frame.currUnjitteredViewProj * currModel * pos  ← unjittered for velocity (#85)
         v_PrevClip  = u_Frame.prevViewProj           * prevModel * pos  ← unjittered
   Frag: out_velocity = (v_CurrClip.xy / w − v_PrevClip.xy / w) * 0.5    ← unjittered ΔUV
   Writes handles.velocity (RG16F, viewport).

1. MB_TileMax        handles.velocity → tileMax (RG16F, ⌈w/16⌉ × ⌈h/16⌉)
                     Picks the longest velocity vector in each 16×16 tile.

2. MB_NeighborMax    tileMax → neighborMax (RG16F, same resolution)
                     3×3 dilate — dominant velocity across nine neighbour tiles.

3. MB_Reconstruct    hdr + handles.velocity + neighborMax + depth → mbOutput (RGBA16F, viewport)
                     McGuire 2012 stochastic sampling: jittered samples along neighborMax
                     direction, weighted by soft depth compare + sample-velocity cone.
                     Strength + maxSpeed applied here.
                     SceneRenderer redirects handles.hdr to mbOutput.
```

**Pass-3 self-read trick:** writing to `mbOutput` then redirecting `handles.hdr` mirrors DoF Composite's pattern — avoids the read-and-write-same-RT race. The redirect happens immediately after `MotionBlurFeature::AddPasses` in `RenderFrame`, so Tonemap reads the motion-blurred result without RG aliasing concerns.

**Parameters** (`PostProcessSettings`):
- `motionBlurEnabled` (default false)
- `motionBlurStrength` ∈ [0, 2], default 0.5 — 1.0 is roughly physically correct
- `motionBlurSamples` ∈ [4, 32], default 8 — reconstruct sample count
- `motionBlurMaxSpeed` ∈ [0.01, 0.3] NDC, default 0.1 — clamps catastrophic velocity near silhouettes

**Coverage after #84:**
- Stationary camera + moving rigid body → blurs (prepass picks up `currModel ≠ prevModel`)
- Stationary camera + skinned mesh animation → blurs (skinned vert samples curr/prev bone matrices)
- Camera motion + static geometry → blurs (currViewProj differs from prevViewProj inside the prepass even when prevModel == currModel)
- Skybox / depth==1.0 → no blur (no draw covers those pixels; RT stays at clear value 0)

**Resource cost when enabled** (1080p): velocity RT = 8 MB, MB intermediates (tileMax+neighborMax+mbOutput) ≈ 32 KB + 16 MB. When disabled: 0 — RG skips slot allocation for unused transients.

### Screen Modifications (Issue #47)

**Placement:** `PostFXFeature` runs after Tonemap on a transient LDR buffer (`handles.ldr`) and writes the swapchain. Insertion uses the reverse-insert pattern (PostFX is inserted just before Tonemap, so the final order ends `..., Tonemap, PostFX, SelectionOutline, ...`).

**Why a separate pass instead of folding into Tonemap:** chromatic aberration samples three RGB shifts; folding it into Tonemap would multiply LUT evaluations 3× along both the Builtin and LUT tonemap paths. Splitting also keeps Tonemap unaware of the screen-modification toggles, so future LDR-domain effects (lens flare, dirt, bloom-dirt) can chain onto the same `handles.ldr` surface.

**`RendererHandles::ldr` contract:**
- Format: `IRHIDevice::GetSwapchainFormat()` (avoids pipeline cache churn vs. the previous direct-to-swapchain tonemap)
- Lifetime: RG transient `"LDR_Color"`, created in `SceneRenderer::RenderFrame` next to `HDR_Color` and `Velocity`
- Writer: `TonemapFeature` or `LutTonemapFeature`
- Reader: `PostFXFeature`

**Data flow:**
```
HDR → Tonemap (handles.hdr → handles.ldr)
    → PostFX  (handles.ldr → handles.swapchain)
    → SelectionOutline / InfiniteGrid / DebugOverlay (read+write swapchain)
```

**`postfx.frag` (single fullscreen pass):**
1. Chromatic Aberration — radial RGB offset (`dir = uv - 0.5`), three `texture()` taps when enabled, one tap otherwise.
2. Vignette — elliptical falloff using `aspectRatio` (`w/h`), `smoothstep(intensity, intensity+smoothness, length(d))`.
3. Film Grain — `hash12(uv * grainSize * resolution + u_Frame.time) * 2 - 1`, attenuated in bright regions via `mix(1.0, 0.3, luminance)` to mimic film stock.

All three effects use `if (enable > 0.5)` uniform-control-flow — the shader compiler eliminates branches per draw because the flag is a push constant. All disabled = pure LDR→swapchain copy.

**Push constants** (48 B, `std430`-friendly): three groups of `(enable, params..., padding)` ×3 + `aspectRatio`. `caPxScale = 0.005f` fixed in feature code (NDC units; tunable but kept constant per the design).

**Editor gizmo overlays unaffected:** SelectionOutline / InfiniteGrid / DebugOverlay run *after* PostFX on the swapchain — gizmos and debug lines do not pick up vignette / CA / grain, so editor visuals remain stable while artists tune the effects.

**Script API (`StellarAlia.PostProcess`):** see [Scripting System → Script API Surface](#script-api-surface) for the `PostProcess.Vignette` / `.ChromaticAberration` / `.FilmGrain` Unity-style accessors.

### Screen Space Reflections (Issue #48 Phase 1 → #89 Phase 2)

`SSRFeature` runs a Hi-Z-accelerated, stochastic, temporally-denoised reflection between
`DeferredLightingFeature` and `SelectionMask`/`TAA`. Phase 2 (#89) replaced Phase 1's
view-space linear march (which striped) and single-sample-relying-on-TAA denoise with **five
compute passes** internal to the feature:

```
DeferredLighting (handles.hdr ← lit, incl. iblSpec)
  → HiZ_Copy   : depth(D32F) → Hi-Z mip0 (R32F)
  → HiZ_SPD    : min-reduce mip0 → mip1..N (SPD, #94, reduce = min; imageLoad in-place)
  → SSR_Trace  : 8 GGX-sampled rays/pixel, screen-space Hi-Z march → SSR_Radiance(rgb=radiance,
                 a=pdf<0⇒mirror) + SSR_Hit(r=mean view hit distance #104, b=conf, a=mask)
  → SSR_Resolve: light 3×3 roughness-scaled bilateral pre-blur → SSR_Reflection(rgb=radiance,a=conf)
  → SSR_Temporal: reproject history by virtual image (smooth) / velocity (rough) + adaptive-count
                 accumulate + variance clip, then split-sum composite → SSR_Composite;
                 write new history (ping-pong)
    SceneRenderer redirects handles.hdr = SSR_Composite
  → ForwardTransparent (blends in place, Issue #105) → SelectionMask → TAA → Bloom → …
```

**Hi-Z traversal (`ssr.comp` `HiZTrace`).** Screen-space DDA over the min-depth pyramid: cells
the ray passes entirely in front of are skipped at a coarse mip, and where it reaches the
nearest surface it descends to mip 0 + a view-space thickness test. Depth is compared in
**view-space Z reconstructed perspective-correctly** (1/z_view is what's linear across the
screen) — this, not the pyramid, is what removes Phase 1's banding. Crossing is found by the
**depth-plane intersection** over the whole cell span `[t, tCell]` (testing only cell entry
punches self-similar fractal holes); cell stepping uses a **texel-scaled `crossOffset`** (a
fixed t-epsilon leaves vertical stripe holes); the screen ray is **clipped to the viewport**
(a near-plane-projected endpoint degenerates the depth interpolation → false black hits); and
rays with `viewR.z ≥ 0` (reflecting toward/behind the camera → off-screen) are dropped to IBL.

**Stochastic + denoise (Phase C).** The trace GGX-importance-samples the reflection direction
(roughness < 0.05 ⇒ exact mirror, no jitter); 8 samples/pixel/frame make the per-frame signal
dense enough to denoise. `ssr_resolve.comp` does a light bilateral pre-blur; `ssr_temporal.comp`
is the primary denoiser — it blends the reprojected history with an **adaptive running-average**
(`alpha = count/(count+1)`, count in history alpha: fresh / disoccluded ⇒ shows current
immediately, stable ⇒ strong denoise) and **variance-clips** it to the current 3×3 mean±1.5σ.
History is a persistent ping-pong pair, rebuilt on resize.

**Virtual-image reprojection (#104).** A reflection pixel's content is the mirror virtual image
— it parallaxes like a point at `P_v = C + dir·(t_s + t_h)` behind the mirror (exact for planar
mirrors), NOT like the mirror surface, so reprojecting by surface `handles.velocity` mismatched
the history on every camera move and the variance clip kept resetting the accumulation (moving
camera ⇒ trailing / re-noising reflections). The trace outputs the edge-fade-weighted mean
view-space ray length in `SSR_Hit.r` (`hitDist = (hitZView − rayStart.z)/viewR.z`, viewR is
unit); the temporal pass reprojects `P_v` with `u_Frame.prevViewProj` and blends toward the
surface-velocity path by roughness (`1 − smoothstep(0.05, 0.25, r)`) — glossy lobes have noisy
hit distances. `tH ≤ 0` (miss/sky) falls back to the surface path; object motion inside the
mirror is still covered by the variance clip.

**Split-sum replacement (unchanged principle).** On the resolved radiance the temporal pass
recomputes `iblSpec = prefilteredColor·(F_ibl·brdfSS.x + brdfSS.y)·occlusion` exactly as
`deferred_lighting.frag` does and outputs `hdrIn + conf·(ssrSpec − iblSpec)` (conf = screen-edge
fade × on-screen hit fraction × roughnessFade(ssrMaxRoughness) × ssrStrength). `deferred_lighting.frag`
is untouched. Rays that miss / hit off-screen / hit occluded or bottom faces fall back to the
IBL term — the fundamental SSR limitation, hence SSR + IBL are a fallback hierarchy, not
alternatives.

**Resources.** Hi-Z = single R32F mip-chain (SSRFeature member, resized) fed by the #94 per-mip
UAV binding (`WriteDescriptorStorageImageArrayMip`); an atomic counter for the SPD global step is
a transient `clearOnCreate` SSBO; SSR_Radiance/Hit/Reflection are transient RGBA16F; history is
2× persistent RGBA16F. `VelocityPrepassFeature`'s gate now includes `ssrEnabled` (SSR reprojection
consumes `handles.velocity`). Disabled → `AddPasses` early-returns, zero cost.

### Volumetric Fog (Issues #49 + #110)

Froxel single scattering: the view frustum is voxelised into a 3D texture
(`(w/8)×(h/8)×64` RGBA16F, squared depth distribution `viewZ(k)=fogFar·(k/N)²` — front-loads
slice resolution without referencing the camera near plane). Medium model: global density with
optional exponential height falloff, uniform albedo, Henyey-Greenstein phase. Three passes,
placed **after SSR / before the AfterLighting anchor + ForwardTransparent** (fog is part of
lighting; transparents composite on top and self-fog), pre-TAA (TAA denoises the volume jitter):

1. **VolFog_Inject** (compute 8×8×1, full volume): per-froxel world position (near/far ray
   endpoints lerped — affine in view depth), density → σs/σt, then in-scattered light:
   directional lights × HG phase (only the **sun** takes a single shadow-map tap — see below —
   and every directional light is attenuated by the **analytic medium self-shadow**
   `exp(-σt(y)·min(1/k, fogFar)/ω.y)`, the closed-form light-path transmittance for exponential
   height density; the `min` clamps the uniform limit to the volume scale and keeps it continuous
   in k), point/spot lights with the same falloff as `pbr_shading.glsl`, plus an SH-L0 ambient
   term. Local fog volumes (#110) add on top: `FogVolumeComponent` OBBs are gathered per frame
   into a double-buffered cpuVisible UBO (16 max, `GetCurrentFrameIndex()`-indexed, imported via
   `ImportBuffer` + `BindBuffer` at inject set=2 binding=2); per froxel, inside-unit-box test in
   volume-local space + per-axis edge falloff, σt/σs accumulate linearly so albedo blends by
   density weight. A 2-octave value noise (pure ALU) advected by `volFogWind` optionally
   modulates the summed density (`volFogNoiseStrength=0` = off). Neither local volumes nor
   noise participate in the analytic self-shadow (global medium only — UE-style approximation).
   Sample depth is IGN-jittered (golden-ratio frame advance) when the fog temporal volume
   **or** TAA is enabled (`temporalJitter` push constant); neither → stable banding rather
   than flicker. Output: `rgb = σs·L_scat, a = σt`.
2. **VolFog_Temporal** (#110, compute 8×8×1, full volume): reprojects last frame's blended
   media volume through `prevViewProj` (per-froxel world pos → prev uv/viewZ → inverse slice
   mapping with the PC-passed `prevFogFar`) and exponentially blends
   (`volFogTemporalBlend`=0.9 history weight) — media values are point quantities and
   reproject; path integrals do not, hence pre-Scatter placement. Persistent ping-pong
   `m_histTex[2]` (3D RGBA16F), invalidated on resize / volume-dim change / fogFar change /
   toggle; out-of-frustum or first frame falls back to the current sample. This makes the
   inject jitter converge **without scene TAA**.
3. **VolFog_Scatter** (compute 8×8×1, XY only — Z is a serial prefix product): front-to-back,
   Frostbite energy-conserving per-slice integral `S·(1−e^{−σt·dz})/σt`; reads the temporal
   output (or raw media when temporal is off); each slice stores accumulated
   `rgb = inscatter, a = transmittance` up to the slice END.
4. **VolFog_Apply** (fullscreen fragment): depth → viewZ → inverse slice mapping (half-voxel
   shift compensates the slice-END convention; half-texel clamps guard the repeat sampler),
   hardware trilinear upsamples XY and interpolates Z; `out = hdr·T + inscatter` into
   `VolFog_Output`; `RenderFrame` redirects `handles.hdr` (same pattern as SSR/DoF).

**Sun selection (`DirectionalLightComponent::isSun`).** `GatherLights` returns the sun index
(first `isSun=true` directional, else first directional — legacy behaviour when none marked);
it drives **both** `lightSpaceMatrix` (shadow map rendering) and the fog's shadowed-scattering
light — they must be the same light or the god-ray shadow tap tests against the wrong depth map.
Stored per frame in `m_sunLightIndex`, passed to inject as a push constant.

**Transparent integration (Step 9).** The integrated volume is published on frame set
binding 8 (`t_FogVolume`, appended like #56's `t_ShadowMap`); `forward_transparent.frag`
samples it at the fragment's own depth and applies `color·T + inscatter` before blending.
When fog is off the feature rebinds a persistent 1×1×2 `(0,0,0,1)` dummy
(`FrameUniformsBuffer::m_fogVolumePlaceholder`) — unconditional sampling becomes a no-op, and
the rebind prevents a stale transient volume from lingering in the binding. `volFogFar` rides
in `FrameUniforms` (the former `_fpad` slot).

**Editor integration (#110).** `FogVolumeComponent` (density / albedo / falloff; shape = the
entity's transform × unit box) has a drawer, an Add Component entry, a `fogVolume` serializer
block, an always-on viewport OBB wireframe in `EditorMode::DrawOverlays` (dim; selected =
yellow — no billboard icon exists, so the wireframe is its only viewport presence) and a
spawn template `templates/entities/Effects/Fog Volume.sascene`.

**Known limits (fog-side audit vs modern engines).** Per-light volumetric shadows for
point/spot are blocked on engine point-light shadow maps. Local volumes are box-only and the
noise is a single global (PP-level) setting — per-volume noise / 3D density mask textures
(HDRP parity) are recorded as X-13. An extinction shadow volume (Frostbite) would be needed
for local volumes / noise to self-shadow; the analytic self-shadow covers the global medium
only.

### CPU Frustum Culling & BVH

**File:** `src/core/spatial/BVHTree.hpp`

`BVHTree<T>` is a pure-algorithm template (glm only, no ECS dependency) providing:
- **Build:** median-split on longest centroid axis, O(N log N)
- **Query(Frustum):** p-vertex half-space test per node; prunes entire subtrees

(The former `Raycast`/`RayAABB` slab-test API was removed with `RaycastScene` in Issue #102 — editor picking is now GPU ID based; see *Editor ID Picking* below.)

`Frustum::Extract(viewProj)` uses Gribb-Hartmann for Vulkan NDC [0,1] depth (near = row2, far = row3−row2).

`SceneRenderer` instantiates `BVHTree<entt::entity>` with **one leaf per entity** (union of all submesh world AABBs). This matches Unity/UE granularity: culling is at the component level, not per-submesh.

**Per-frame flow:**
```
BuildDrawList:  GPUSubMesh.boundsMin/Max  →  ArvoAABB(wt*localT)  →  m_bvh.Insert(entity)
                                          →  DrawItem::worldAABBMin/Max (per submesh; transparent
                                             back-to-front centroid sort, Issue #56)
                m_bvh.Build()
RenderFrame:    Frustum::Extract(viewProj)  →  m_bvh.Query  →  m_visibleDrawItems
GBufferFeature: iterates m_visibleDrawItems (pointers into m_drawItems)
```

Skinned meshes set `DrawItem::skipCull = true` and bypass BVH; they are always included in `m_visibleDrawItems`.

**`GPUSubMesh` bounds** are computed in `ResourceManager::LoadMesh` by iterating raw vertex data (stride=48, first 12 bytes = vec3 position) before GPU upload.

Tracy plot: `SA_PROFILE_PLOT("VisibleDrawItems", ...)` tracks cull ratio per frame.

### Editor ID Picking (Issue #102, extended by #111 / X-12)

Viewport mouse selection is GPU based (UE HitProxy style), replacing the former AABB `RaycastScene`:

- `SceneRenderer::RequestIdPick(px, py, purpose = Select)` queues a pick; the next frame's `IdPickFeature` records a one-shot pass drawing **all** `m_drawItems` into a lazily-created persistent `R32_UINT` buffer (`RenderTarget|Sampled|CopySrc`, own persistent `D32F` depth `m_idDepth` (`DepthStencil|Sampled|CopySrc`, Issue #111) → nearest surface wins, transparents included). Push constant = `{mat4 model; uint id}` (packed 68 B); `id` = 1-based index into a snapshot `vector<{entity, submeshIndex}>` captured at pass-build time, so the result survives draw-list rebuilds. Shaders: `id_pass.vert/.frag` + `id_pass_skinned.vert`.
- Both textures are imported with `finalState = ShaderRead` (the layout the readback APIs assume); after `EndFrame/Present`, `ResolveIdPick()` does a blocking readback (queue-ordered after the frame) and maps the picked pixel through the snapshot. X-12: the readback is a **3×3 window** via `IRHIDevice::ReadbackTextureRegion` (mip0/layer0, caller-clamped; Vulkan impl mirrors `ReadbackTextureMips` with `imageOffset/extent`) instead of the former full-image copy — hover picks run every frame during a drag, and the 3×3 neighbourhood is exactly what normal reconstruction needs.
- **Pick exclusion (X-12):** `SetPickExcluded(entity)` (entt::null clears) skips one entity in the ID pass — the drag ghost must not occlude placement picks or the ray would hit the ghost itself and climb toward the camera every frame.
- **Placement picks (Issue #111):** `PickPurpose { Select, Place }` separates selection clicks from asset-drop placement. For `Place`, `ResolveIdPick()` additionally reads back the depth target, unprojects the hit pixel with the inverse of the **jittered** VP snapshotted at pass-build time (`IdPickState::invViewProj` — must match what `id_pass.vert` rasterised with, not the unjittered editor-overlay VP), and reconstructs a geometric world normal via min-delta side differencing of the depth neighbourhood; any unreliable neighbour (off-screen / background / different draw item) leaves the `+Y` default so alignment degrades to upright. `PickResult` gains `purpose / hasSurface / worldPos / worldNormal`. `VulkanDevice::ReadbackTextureMips` derives the barrier aspect from the format via `FormatAspectFlags()` (VulkanUtils) and copies the DEPTH aspect for depth formats, instead of hardcoding COLOR.
- `EditorMode` consumes `TryConsumePickResult` next frame with a **cycling click state machine**: first click selects the entity; a second click on the already-selected entity focuses the clicked material slot (`EditorSelection::FocusSlot`) — the viewport keeps that submesh highlighted and the Inspector slot row scrolls into view with a flash; a third click on the same submesh cycles back to the entity level (selection kept, focus cleared). Any selection change also clears the focus. Billboard icons keep their separate 2D screen-distance test and take priority.
- **Slot highlight (forward direction):** hovering a slot row in `MeshRendererDrawer` (the single slot UI since Issue #103) reports `EditorSelection::SetHoveredSlot` (frame-scoped); `EditorMode` mirrors hover-else-focus into `SceneRenderer::SetHighlightSlot(entity, slot)`, which narrows `SelectionMaskFeature` to that single submesh — the outline pass is untouched.
- `RHIFormat::R32_UINT` was added for the pick buffer (clear only to 0: float/uint bit-identical, so the float clear path needs no uint branch).

---

## Custom Shading Models

Custom shading models are defined in `.saglsl` unified shader files placed in the project's `assets/` directory. They are cooked **at runtime** (no engine recompile required).

### `.saglsl` File Format

```glsl
// @ShaderName  "My Shader"
// @ShadingModel MyShader        // CamelCase GLSL identifier; also the MaterialType name
// @VertShader   deferred_geometry  // optional, default shown

#pragma sa_section gbuffer
#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "bindless_textures.glsl"        // set=0 globalTex[] + SampleBindless(idx, uv)

// #73-A: SSBO + bindless — reflection detects the StorageBuffer named
// MaterialParams and puts the type on the zero-clone SSBO path. Texture slots
// are `uint t_<Name>_Idx` members (0 = default white); MaterialManager strips
// the `_Idx` suffix so .samat texture keys stay "t_<Name>".
layout(std430, set = 2, binding = 0) readonly buffer MaterialParams {
    vec4 baseColorFactor;  // @Color4("Base Color") = 1,1,1,1
    uint t_BaseColor_Idx;  // @Texture("Albedo Map")
} u_Mat;

// ... write the 3-MRT G-Buffer; encode this model's dispatch ID via the
// cook-injected SA_SHADING_MODEL_ID alias (see below), e.g.
//   out_GData = vec4(0, 0, 0, EncodeShadingFlags(SA_SHADING_MODEL_ID));
#pragma sa_end_section

#pragma sa_section lighting
vec3 EvaluateShading(GBufferData gbuf) { ... }
#pragma sa_end_section
```

**`SA_SHADING_MODEL_ID` alias (#73-A).** `CompileEntry` injects
`#define SA_SHADING_MODEL_ID SHADING_MODEL_<UPPER_SNAKE>` right after `#version`
into every generated `<snake>.gbuffer.frag`. Authored code never spells out the
name-derived macro — which is what lets the `NewShader.saglsl` template work with
create-time token replacement (the template can't know the final macro name).

### Runtime Cook Flow

When the editor loads a project with `.saglsl` files, `EditorMode::LoadProject` runs:

```
EditorMode::LoadProject()
  ├─ ShaderCook::HasSaglslFiles()     ← skip entirely if no .saglsl present
  ├─ ShaderCook::CookDirectory()      ← mtime-based incremental cook:
  │     compare each .saglsl mtime vs .shader_manifest.json; skip unchanged files
  │     parse .saglsl, generate dispatch GLSL (shading_model_ids.glsl +
  │     shading_dispatch.glsl), compile *.gbuffer.frag → .spv + .refl,
  │     SetMeta shadingModel / vertShader into .refl (generic metadata map, Phase 0 #88)
  ├─ ShaderCook::HasSaeffectFiles() / CookEffects()   ← Issue #88, independent of dispatch:
  │     parse .saeffect (@Effect/@Stage/@Inject/@In/@Out), compile <stem>.saeffect.{frag,comp}
  │     → .spv + .refl, SetMeta effect/stage/inject/in/out (same standard .refl mechanism)
  ├─ ShaderCook::RecompileDeferredLighting()  ← whenever the project has ≥1 model
  │     (modelCount > 0 — so engine-side shader edits are picked up on every load);
  │     recompile deferred_lighting.frag with the project dispatch (glslc subprocess)
  │     using -I ENGINE_SHADER_SRC_DIR -I cook_cache/generated/shaders/.
  │     Issue #56: the PBR fallback math lives in pbr_shading.glsl, which MUST stay
  │     in ENGINE_SHADER_SRC_DIR so this runtime compile path resolves it
  ├─ ClearProjectAssets() / ClearProjectInstances()  ← WaitIdle inside
  └─ ApplyProjectShaderTypes(cookedShaderDir)   ← self-WaitIdle (safe between frames or at RenderFrame top)
       ├─ MaterialManager::ClearProjectTypes()
       ├─ ScreenEffectRegistry::ClearProjectEffects(device)   ← Issue #88
       ├─ DeferredLightingFeature::ReloadShaders()   ← hot-swap frag SPV
       ├─ MaterialManager::RegisterTypesFromShaderDir(isProjectType=true)
       │     reads ShaderReflection GetMeta("shadingModel") / GetMeta("vertShader")
       └─ ScreenEffectRegistry::Scan(isProjectType=true)   ← reads GetMeta("inject") → ScreenEffectType catalog
```

Mid-frame UI triggers (create/reimport/delete/rename of `.saglsl` / `.saeffect`) go through
`SceneRenderer::RequestProjectShaderReload`, which defers `ApplyProjectShaderTypes` to the next
`RenderFrame` top (destroying pipelines mid-command-buffer is unsafe). The cook itself is
also deferred (#73-A): `EditorContext::onCookShaders` only sets `m_pendingShaderCook`, so an
editor-initiated create/rename/delete coalesces with the FileWatcher event for the same write
into a single `CookProjectShaders` run on the next Update tick (previously two full cooks).

**Cook-path unification (Issue #90).** Both the load-time cook (`LoadProject`) and the
reimport/create/delete/rename cook (`EditorMode::CookProjectShaders`, run via the CLI) now write to
the **same** project `cook_cache/shaders` (reimport previously wrote to `BUILTIN_SHADER_DIR`, which
diverged from the dir scanned on startup and left "ghost" effects). `m_shaderDir` (BUILTIN) stays the
engine fallback only. `PruneOrphanedEffectCookOutputs(cookDir, sourceDir)` runs on **both** cook
paths, dropping `<stem>.saeffect.*` whose source was deleted so the effect leaves the catalog. The
FileWatcher poll auto-cooks on window focus for `.saglsl` / `.saeffect` (previously `.cs` only),
mirroring the script `m_pendingRecompile` path.

Outputs land in `cook_cache/shaders/` (SPV + refl) and `cook_cache/generated/shaders/` (dispatch GLSL).
`cook_cache/.shader_manifest.json` records per-file mtime for incremental cook on subsequent loads.

**Incremental correctness & error surfacing (#73-A).** The editor CLI cook no longer passes
`--force`; `CompileEntry`'s up-to-date check compares SPV/refl mtimes against **both** the
`.saglsl` source and `generated/shading_model_ids.glsl` (IDs shift when models are added or
removed, and the SPV bakes its ID — `writeIfChanged` keeps the ids file's mtime stable when
content is identical, so unchanged model sets skip recompiles). Duplicate `@ShadingModel`
names are detected after parse (same snakeName ⇒ same macro): the first occurrence wins, the
rest fail individually instead of corrupting `shading_model_ids.glsl` for every model. glslc
stderr is captured per entry (`2> <stem>.frag.err`, invisible child console in GUI sessions
otherwise); failures land in `CookResult::failures {model, source, message}` and in
`cook_errors.txt` as tab-separated `source \t model \t message`, which
`EditorMode::CookProjectShaders` (out-of-process) and `LoadProject` (in-process) both turn
into Diagnostics-panel errors with the first real compiler message. `.saeffect` failures
get the same treatment (#73-B) via `CompileEffectEntry`'s stderr capture and a **separate**
manifest `spvOutDir/cook_errors_effects.txt` — `cook_errors.txt` is owned (written and
cleared) by `CookDirectory` in `dispatchOutDir`, which `CookEffects` neither receives nor
always runs alongside; `CookProjectShaders` reads both manifests ("Shader …" / "Effect …"
diagnostics).

**Stale-type pruning & rename migration (#106).** `CookDirectory` ends every cook with two
more outputs: it prunes `<snake>.gbuffer.frag[.spv|.refl]` whose model is absent from the
parsed entry set (source deleted or `@ShadingModel` renamed — otherwise
`RegisterTypesFromShaderDir` would resurrect the stale type from the leftover `.refl` on
every reload, keeping deleted models alive forever), and writes
`generated/shaders/shader_models.txt` (tab-separated `source → model`). EditorMode
snapshots that file before each cook and diffs after: the same source mapping to a
different model means the user renamed `@ShadingModel`, which orphans `.samat` files
referencing the old type name. A modal (`DrawShaderRenameModal`, drawn after `DrawPanels`)
offers **Migrate All** — rewrites each referencing `.samat`'s `"type"`, force-recooks the
`.samatc` (it stores the type too), evicts the cached instance — or **Ignore** (orphans
render magenta until reassigned via the Inspector's shader combo). A rename is skipped
when another source still provides the old model name (migration would steal live
materials). Detection works across both cook paths (CLI + in-process LoadProject) and
across editor restarts, since the mapping is a file.

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
- `SHADING_MODEL_PBR = 0` is always reserved; custom models are assigned IDs 1..N in snake-name alphabetical order.
- **Editor authoring flow (#73-A):** `CreateNewFile(Saglsl)` token-replaces `NewShader` in the template with the file stem **sanitized to a GLSL identifier** (spaces stripped); `CommitRename` syncs `@ShaderName`/`@ShadingModel` to the chosen name **only during the inline rename right after creation** (`m_renameIsCreation` — the header still holds the template placeholder then). Later renames never touch file contents: `.samat` references the model by type name, so renaming the model would orphan them. New materials author no `params` at all: an absent key means *inherit* — the renderer resolves it from the material layer below (see the layered-resolve entry), shader-reflection defaults apply only when no layer authors the key. `ParseSaglslMeta` no longer text-scans the UBO block, it only reads the header annotations.
- **Material Inspector shader combo (#106):** the `.samat` Inspector's type field is a combo over registered types filtered by `usesMaterialParamsSSBO` (the surface-material contract — PBR + project `.saglsl`; Shadow/Skybox/post-fx excluded). Switching merges rather than resets: existing param values are kept (including keys the new type lacks — switching back restores them); missing keys stay absent = inherit. An unregistered type shows a red warning plus the same combo for one-click reassignment; `Save()` recooks the `.samatc` and evicts the cached instance. Explicit non-goal: UUID-based shader references (builtin types have no source asset/meta — name keys stay).
- **Layered material resolve:** a `.samat` key that is present is *authored*; absent = inherit. `MaterialInstance` records the authored param/texture/render-state keys of its source JSON (`IsParamAuthored` etc.), and `BuildDrawList` resolves each submesh bottom-up: cooked submesh default → replacement material asset (mesh-renderer slot, else `MaterialOverrideComponent::materialAsset`) → entity-wide override maps → per-slot override maps. `InheritUnauthoredFromLower` copies the lower layer's authored values (name-matched across types; bindless texture indices are global so they transfer) into the effective param blob before the override maps stack on top — shader-reflection defaults apply only when no layer authors a field. `alphaMode`/`doubleSided` fall through the same way. The material Inspector shows unauthored keys as `(inherit)` with per-key `+`/`-` override buttons.
- **Slot material field (#106):** `DrawMaterialField` (DrawerHelpers) is the shared material picker — grayed `(fallback)` label when unassigned, used by every Mesh Renderer slot row and MaterialOverrideDrawer's `materialAsset`. Slot rows are always assignable; `materialSlots` auto-grows on assignment (gaps stay invalid = cooked default) via an undoable whole-vector `CallbackCommand`, replacing the removed manual "+ Add Slot / − Remove Last" flow.
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
   GatherLights(scene)               // all light component types; outputs sun index (isSun, Issue #49) → lightSpaceMatrix + fog

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
   VelocityPrepassFeature::AddPasses()← per-DrawItem prepass writing handles.velocity; gated on MotionBlur enabled (Issue #84)
   SSAOFeature::AddPasses()           ← GTAO 3-pass; disabled → fill 1.0
   DeferredLightingFeature::AddPasses() ← reads ssaoTex binding=5
   SSRFeature::AddPasses()            ← sets handles.hdr = SSR_Composite when enabled
   VolumetricFogFeature::AddPasses()  ← Inject/Temporal/Scatter/Apply; sets handles.hdr = VolFog_Output; frame set binding 8 = froxel volume (dummy when off) (Issues #49/#110)
   ForwardTransparentFeature::AddPasses() ← blends BLEND items into handles.hdr in place; fragments self-fog via binding 8; R8 ReactiveMask (Issue #105)
   SelectionMaskFeature::AddPasses()
   TAAFeature::AddPasses()            ← jittered resolve; sets handles.taaResolved + handles.hdr = PostTAA_HDR copy (Issue #105)
   AutoExposureFeature::AddPasses()   ← histogram(hdr) + adapt; 1-frame readback feeds tonemap exposure
   BloomFeature::AddPasses()          ← threshold reads taaResolved; composite writes handles.hdr
   DoFFeature::AddPasses()            ← 6 passes (CoC+4×blur+composite); sets handles.hdr = dofOutput when enabled
   MotionBlurFeature::AddPasses()     ← 3 passes (tileMax+neighborMax+reconstruct); reads handles.velocity from prepass
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
| `MeshRendererDrawer` | `MeshRendererComponent` — slot rows expand into the per-slot override editor (`SlotOverrideEditor.cpp`, Issue #103) |
| `AnimatorDrawer` | `AnimatorComponent` |
| `SkinnedMeshDrawer` | `SkinnedMeshComponent` |
| `MaterialOverrideDrawer` | `MaterialOverrideComponent` |
| `RigidBodyDrawer` | `RigidBodyComponent` |
| `ColliderDrawer` | `ColliderComponent` |
| `ScriptDrawer` | `ScriptComponent` — Script asset picker + optional `className` override; **schema-driven** fields via `ScriptSystem::GetSchemaFor` (#74). Editable kinds: Bool / Int32 / Float / Vec2/3/4 / String (#74); Color (ColorEdit3/4), AssetRef (picker + SAASSET drop, type-filtered by `[AssetType]`), EntityRef (picker + SAENTITY drop, fallback "(missing #N)") (#75). Honours `[Range]`/`[Tooltip]`/`[Header]`/`[HideInInspector]`. Edits write `sc.fields`, seeded from C# `= initializer` defaults on first display; in Play mode each change `InjectSingleField`-deltas back to the live instance |

Shared inline helpers (`DrawAssetIDField`, `AcceptAssetIDDrop`, `TrackedFieldEdit<T>`,
`RemoveButton`, `HeaderFlags`) live in `editor/ui/drawers/DrawerHelpers.hpp`. The
monolithic `editor/ui/ComponentDrawers.hpp` is deleted. Registration order in
`BuildContext` equals the display order in the Inspector.

**`TrackedFieldEdit<T, DrawFn>(target, ctx, desc, draw, onApplied={})`** — wraps a single
ImGui control so its edit becomes a single undoable `SetFieldCommand<T>`. The closure
mutates `*target` directly; `TrackedFieldEdit` snapshots the pre-edit value on
`IsItemActivated`, captures the post-edit value on `IsItemDeactivatedAfterEdit`, and
pushes the command (continuous drags collapse into one record). `onApplied` is forwarded
into the command and re-fires on Execute/Undo — used by `TransformDrawer` to keep
`MarkDirty + MarkMaterialDirty` synced when the user undoes a transform edit.

**`AcceptAssetIDDrop(outId, filterType, ctx, desc, onApplied={})`** — drop target for
the `"SAASSET"` payload from `AssetsPanel`. Filters by `AssetEntry::type`, writes
`AssetID` into `outId`, and pushes a `SetFieldCommand<AssetID>` (drag-in becomes
undoable). Used by `ScriptDrawer`; other drawers' `AssetID` fields still go through the
in-popup picker via `DrawAssetIDField`.

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
| `SetFieldCommand<T>` | Generic single-field set (`bool`, `int`, `float`, `glm::vec2/3/4/quat`, `std::string`, `AssetID`); takes `T* target`, `oldValue`, `newValue`, optional `onApplied` callback re-fired on Execute/Undo for downstream dirty propagation; used by `TrackedFieldEdit` / `AcceptAssetIDDrop` to make every Inspector scalar edit and asset drop undoable |

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

`EditorMode::LoadProject(saprojectPath)` handles project **switch** at runtime; `EditorMode::OnAttach` handles the **initial** load. Both delegate the filesystem-level pipeline (scan + script compile + scene load) to `LoadProjectFiles(projectDir)`:

```
LoadProjectFiles(projectDir) → optional<SaProject>:
  scriptWatcher.Watch(projectDir/"assets"); m_pendingRecompile = false
  assetRegistry.Scan(projectDir/"assets", engineAssetsDir)
  scriptSystem.RecompileEditing(reg)  — compile every .cs so Inspector schema is ready
  if (m_assetsPanel) m_assetsPanel->UpdateProjectDir(...)  — null during initial OnAttach
  locate .saproject; LoadSaProject; SceneSerializer::LoadFromFile(startupScene)
  return parsed SaProject (nullopt when no .saproject found)
```

`LoadProject` wraps `LoadProjectFiles` with switch-only steps:

```
1. Guard: return if PlayState != Editing
2. scene.Clear(); m_currentScenePath = {}
3. ShaderCook for project .saglsl (if any) → cook_cache/shaders/
4. app.UpdateProjectPaths(projectDir, projectDir/"cook_cache")
   → propagates to VFS (SetCookCacheDir) + SceneRenderer (SetCookCacheDir)
5. resMgr.ClearProjectAssets()     — WaitIdle + destroy GPU textures/meshes/CPU caches
   matMgr.ClearProjectInstances()  — evict cached MaterialInstances; types survive
   renderer.ResetProjectIBL()
   renderer.ApplyProjectShaderTypes(cookedShaderDir)  — GPU hot-swap
   m_diagnostics.ClearSource(Runtime)
6. LoadProjectFiles(projectDir)  ← shared with OnAttach
7. renderer.ApplyWorldSettings(scene.GetWorldSettings())
8. PrepareAnimatedEntities + RebuildDrawList
9. m_projectManager.AddRecent + SaveRecents
10. Cook-cache check: if cook_cache/ empty (ignoring .gitkeep) AND assets/ has .sameta files →
    m_diagnostics.Push(Warning, Runtime, "…run Reimport All…")
```

`OnAttach` differs: no clear/cook step (nothing to clear) and the panel-registration phase runs **after** `LoadProjectFiles` returns — that's why `UpdateProjectDir` is gated on `m_assetsPanel != nullptr` inside the helper.

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
   rebuild the BVH after a gizmo transform, keeping culling AABBs in sync

`TransformDrawer` uses the same pattern: it calls both `MarkDirty(entity)` and
`MarkMaterialDirty()` whenever position/rotation/scale changes via the Inspector, so
clicking on a scaled entity in the viewport always uses the updated AABB.

### Viewport Interaction (HandleViewportInteraction)

`EditorMode::HandleViewportInteraction()` is called from `OnRenderUI` after `DrawImGuizmo`. It:
1. Creates a transparent full-screen `##viewport_interact` ImGui window (`NoBringToFrontOnFocus | NoFocusOnAppearing | NoDocking`) as a drop target and picking receiver
2. Calls `ImGuizmo::SetAlternativeWindow(currentWindow)` so ImGuizmo's `IsHoveringWindow()` check accepts this window as valid — without this, the full-screen overlay makes `g.HoveredWindow` non-null and non-gizmo, causing `mbMouseOver=false` and disabling all gizmo handle hit-tests
3. **Pick-result consume block** — runs FIRST (X-12 ordering constraint: `RequestIdPick` clears any unconsumed result, so the per-hover-frame request would wipe the result resolved at the end of the previous frame and the ghost would never spawn). `TryConsumePickResult` routes by `purpose`: **Place** with `m_pendingPlacement.active` completes an Issue #111 two-phase drop — surface hit → spawn at `worldPos` (plus `RotationUpTo(worldNormal)` when `dropAlignSurfaceNormal` is on; shortest-arc up→normal with antiparallel guard), miss → fallback `RayHitHorizontalPlane(fallbackRay, 0)` or 10 units in front, plus the stand offset — then `TriggerAssetDrop(path, spawnPos, spawnRot)`. **Place** during a hover (X-12) spawns/moves the drag ghost instead. **Select** drives the two-level state machine: select entity → drill into material slot (see *Editor ID Picking*); `SceneHierarchyPanel::SetSelection(e)` / `ClearSelection()` write into `EditorSelection`
4. `BeginDragDropTargetCustom(win->Rect(), win->ID)` — accepts `"SAASSET"` `.glb/.gltf` drops (vanilla `BeginDragDropTarget` is no-op here since the overlay window submits no item) with `AcceptBeforeDelivery | AcceptNoDrawDefaultRect` (X-12): every hover frame requests a `Place` pick at the cursor; the release frame (`IsDelivery()`) sets `commitRequested` when a ghost exists, else falls back to the Issue #111 `m_pendingPlacement {path, fallbackRay}` two-phase path (instant drop / asset not imported).
5. Left-click picking: guarded by `!m_gizmoIsUsing && !ImGuizmo::IsOver() && IsWindowHovered()`; billboard icons get a 2D screen-distance test first, otherwise `SceneRenderer::RequestIdPick(px, py)` queues a GPU ID pick (Issue #102), consumed by block 3 next frame
6. **Ghost commit/cancel (X-12)** — `commitRequested`: read the ghost's `TransformComponent` (block 3 already applied the last hover pick), `DestroyDropPreview()`, then the undoable `TriggerAssetDrop(path, pos, rot)`; ghost alive but payload gone → cancelled, destroy silently

**Drag ghost preview (X-12):** `DropPreview {ghost, assetPath, standOffset, hoveredThisFrame, commitRequested}`. The ghost is a real scene entity (`EntityFactory::CreateStaticMesh` directly — NOT through the command stack, so undo only ever sees the final spawn), spawned on the first consumed hover pick, moved each frame with `MarkDirty` + `MarkMaterialDirty` (ghost jumps can span the scene — the culling BVH must follow), and excluded from the ID pass via `SetPickExcluded`. `standOffset = -minY` of the model's merged submesh AABB (8 corners × `GPUSubMesh::localTransform`, `ComputeStandOffset`), applied along the placement up axis in all three paths (ghost, commit-via-ghost, pendingPlacement fallback) so the AABB bottom rests on the surface. Cleanup: payload leaves the viewport, `LoadProject` (scene already cleared — the helper skips the stale handle), and `OnPlayStateChanged(≠Editing)`. The ghost intentionally shows up in the hierarchy panel (UE shows transient preview actors too); hiding it isn't worth the plumbing.

`ScreenToWorldRay(sx, sy)` unprojects NDC via `inverse(proj * view)` at depth 0 and 1; `cam.proj` already has the Vulkan Y-flip so `ndcY = (sy/sh)*2−1` is used directly.

`SceneHierarchyPanel` public interface: `SetSelection(entity)`, `ClearSelection()`, `TriggerAssetDrop(assetPath, spawnPos, spawnRot = identity)`. `AssetDropOp` carries `spawnPos` / `spawnRot` (Issue #111), forwarded through `CreateStaticMeshCommand` (undoable, also has a `spawnRot` default-identity parameter) into `EntityFactory::CreateStaticMesh/CreateSkinnedMesh`.

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
    bool dropAlignSurfaceNormal; // Issue #111: rotate dropped asset's up axis to surface normal (default false)
    bool debugIdView;          // X-8: fullscreen submesh-ID coloring (Issue #102)
};
```

`debugIdView` lives in the SettingsPanel's separate **"Debug Views"** header (not
under the Overlay master switch) and is mirrored to `SceneRenderer::SetDebugIdView`
before the `enabled` early-return, so it keeps working during Play. Future X-8
modes (lod / depth / random shading) extend that header.

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

### Descriptor Set Convention (Issue #72)
```
set=0  BindlessTextureHeap   (4096 sampler2D, bound 1× per cmd buffer)
set=1  FrameUniforms          (per-frame camera/light/IBL, bound 1× per pass)
set=2  MaterialParams SSBO    (per-draw via dynamic offset into MaterialParamRing,
                               OR per-instance UBO for legacy MaterialTypes)
set=3  Skin                   (per-skinned-draw: binding0 = SkinMatrices SSBO,
                               binding1 = SkinData SSBO)
```
Aligned with UE5 / Unity HDRP — most stable resource at lowest set index. Set=0 layout
is engine-wide identical (every pipeline carries the bindless heap layout in slot 0
whether or not its shader samples bindless), keeping set=0/1 bindings alive across all
pipeline switches.

ComputeProgram is independent: it owns all its sets directly from reflection and does
not follow the mesh-rendering set convention. Compute shaders typically use set=0 for
their own bindings without conflict.

Set=2 layout differs per shader (SSBO_DYN for PBR vs UBO for legacy SimpleAlbedo); set=3
is only present on skinned pipelines. Layout differences at set=N invalidate bindings
for sets N..max, so non-skinned pipelines never disturb set=3 and the SSBO/UBO material
divergence only affects set 2..3.

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
`skinDescSet` (set=3 binding the per-entity matrices and per-asset skin data). This eliminates
N×VRAM waste when multiple entities share the same animated mesh.

### skin_deform.glsl — Shared GPU Skinning Include
`assets/shaders/skin_deform.glsl` declares the `SkinVertex` struct, the set=3 SSBO bindings
(`SkinMatrices` + `SkinData`), and the `SkinMatrix()` helper function. All three skinned vertex
shaders (`deferred_geometry_skinned.vert`, `shadow_skinned.vert`, `selection_mask_skinned.vert`)
include it, avoiding duplicated bone-blend logic across passes. Skin lives at the highest set
index (Issue #72) so per-entity bone changes don't cascade-invalidate lower-set bindings.

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
