# StellarAlia — Architecture Overview

## Table of Contents
1. [Layer Model](#layer-model)
2. [Build-Time Pipeline (Shaders → Assets)](#build-time-pipeline)
3. [Offline Asset Pipeline (Cook)](#offline-asset-pipeline-cook)
4. [ECS — Scene & Components](#ecs--scene--components)
5. [Platform Layer — RHI](#platform-layer--rhi)
6. [Resource Layer — Material System](#resource-layer--material-system)
7. [Function Layer — RenderGraph](#function-layer--rendergraph)
8. [Compute Pipeline & ComputeProgram](#compute-pipeline--computeprogram)
9. [GPU IBL Bake (Runtime)](#gpu-ibl-bake-runtime)
10. [Deferred Rendering Pipeline](#deferred-rendering-pipeline)
11. [Custom Shading Models](#custom-shading-models)
12. [Frame Loop](#frame-loop)
13. [EntityFactory](#entityfactory)
14. [Key Design Decisions](#key-design-decisions)

---

## Layer Model

```
┌─────────────────────────────────────────────────────────────────┐
│  Application Layer (examples/)                                   │
│                                                                  │
│  // Per frame — only two calls needed                            │
│  scene.UpdateTransforms()                                        │
│  renderer.RenderFrame(scene, w, h)    ← full frame, internal    │
│                                                                  │
│  // Scene setup (once after load)                                │
│  SceneSerializer::LoadFromFile(scene, "scene.sascene")          │
│  renderer.ApplyWorldSettings(scene.GetWorldSettings())          │
│  renderer.BuildDrawList(scene)                                   │
│  renderer.AddFeature(std::make_unique<OutlineFeature>())        │
├─────────────────────────────────────────────────────────────────┤
│  Function Layer                                                  │
│                                                                  │
│  SceneRenderer                                                   │
│    Owns: FrameUniformsBuffer, GpuIblBake, depth texture,        │
│          all RenderFeatures, DrawItem list, default material     │
│    Init(Desc{device, matMgr, resMgr, shaderDir, cookCacheDir})  │
│    RenderFrame(scene, w, h): BeginFrame → Upload → AddPasses    │
│                 → RG.Compile/Execute → EndFrame → Present       │
│    AddPass(name, PassFlags, execFn): RG pass primitive           │
│    AddFeature(unique_ptr<RenderFeature>): OnInit called immediately
│    ApplyWorldSettings(ws, updateIBL=true):                      │
│      background mode/color → SkyboxFeature;                     │
│      IBL clear (SolidColor) or SetIBL (Skybox);                 │
│      tonemap param update or feature hot-swap (Builtin ↔ LUT)  │
│    SetIBL(ws): offline-first; GPU bake + cache on miss          │
│    BuildDrawList(scene): ECS → DrawItem list                    │
│                                                                  │
│  RenderFeature — extension point for custom shaders             │
│    OnInit(FeatureInitContext&)                                   │
│      ctx.matMgr->RegisterTypeFromShaders(MaterialTypeDesc, ctx) │
│    AddPasses(renderer, reg, w, h) → renderer.AddPass(...)       │
│    No RenderGraph / RGTextureHandle types exposed to subclasses │
│                                                                  │
│  FeatureInitContext — bundles all OnInit dependencies            │
│    { device, matMgr, resMgr, frameLayout, shaderDir }           │
│                                                                  │
│  Scene / SceneSerializer                                         │
│    UpdateTransforms(): BFS topo-sort → world matrices            │
│    SetParent / CreateEntity / DestroyEntity                      │
│    View<C...>() → EnTT view wrapper                             │
│                                                                  │
│  RenderGraph                                                     │
│    Reset / ImportTexture / AddPass / Compile / Execute          │
│    Topological pass ordering via read/write dependency edges     │
│                                                                  │
│  FrameUniformsBuffer (owned by SceneRenderer)                   │
│    set=0: per-frame camera + light + IBL data                   │
│    Manages double-buffered GPU UBOs + descriptor sets           │
├─────────────────────────────────────────────────────────────────┤
│  Resource Layer                                                  │
│                                                                  │
│  MaterialManager → MaterialType → MaterialInstance              │
│    Init(device, ResourceManager*)   ← resMgr owns White1x1     │
│    RegisterTypeFromShaders(MaterialTypeDesc, FeatureInitContext) │
│  ShaderProgram: vert+frag SPIRV + reflection + pipeline cache   │
│  ComputeProgram: compute SPIRV + per-set descriptor layouts     │
│  ResourceManager: LoadMesh / LoadTexture / GetBuiltin()         │
│    GetBuiltin(BuiltinTexture::White1x1) → 1×1 white placeholder │
│  VFS: (AssetID, ext) → absolute path in cook cache             │
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

## Build-Time Pipeline

```
GLSL shader (.vert / .frag / .comp)
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

### Shader Variant System (Stage 7+, not yet implemented)

Feature flags will be encoded as a bitmask (`uint32_t`), e.g.:

```
bit 0: HAS_ALBEDO_MAP
bit 1: HAS_NORMAL_MAP
bit 2: HAS_METALLIC_ROUGHNESS_MAP
bit 3: HAS_SKINNING
…
```

Cook step will pre-compile each valid bitmask combination to `<shader>.<mask>.spv`.
At runtime `ShaderVariantCache` maps `mask → ShaderProgram*` (load on first access).
**Not yet implemented** — all shaders currently compile as a single variant.

---

## Offline Asset Pipeline (Cook)

**Location:** `tools/cook/`, `tools/shader_reflect/`

All raw source assets are transformed into engine-native cooked formats before
loading at runtime. The cook step runs at build time (via CMake custom commands)
or as a standalone CLI (`StellarAliaCook`).

### Asset Identity — `.sameta` & `AssetID`

Every source asset has a companion `.sameta` sidecar file that persists its
stable UUID (`AssetID`).

```
grasslands_sunset_4k.hdr
grasslands_sunset_4k.hdr.sameta  ← {"uuid": "xxxxxxxx-…"}
```

`Cook::MetaFile::MetaPathFor(path)` derives the sidecar path.
`AssetID` is a 128-bit UUID (two `uint64_t hi/lo`).

Child UUIDs (for embedded images, materials, per-mesh nodes) are derived
deterministically from the parent asset UUID using Fibonacci hash salts:

```cpp
// Different salt per derived asset type to prevent UUID collisions:
// DeriveImageID    — embedded texture (salt A)
// DeriveMaterialID — glTF material   (salt B)
// DeriveNodeMeshID — mesh node       (salt C, planned Stage 6.3)
```

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

`ResourceManager::LoadTexture(AssetID)` deserialises and uploads via `UploadTextureMips`.

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
        glm::mat4 localTransform   // pre-baked node world transform (merged cook)
                                   // or identity (per-node cook, Stage 6.3)
    }
    vector<uint8_t> vertexData   // Vertex: pos3 normal3 tangent4 uv2 (48 bytes)
    vector<uint8_t> indexData
}
```

`ResourceManager::LoadMesh(AssetID)` deserialises and uploads vertex/index buffers.

**Current cook mode (CookMesh):** all nodes in a GLB are DFS-traversed; their
world transforms are baked into each submesh's `localTransform`. The whole GLB
produces a single `.samesh`.

**Planned Stage 6.3 (MeshSplitTool):** one `.samesh` per mesh node, `localTransform`
= identity. The caller (scene file or application code) owns the transform.

### Cooked Material — `.samat`

JSON file. Mirrors the glTF PBR metallic-roughness parameters:

```json
{
  "name": "Gold",
  "baseColorFactor": [1.0, 0.86, 0.57, 1.0],
  "roughnessFactor": 0.2,
  "metallicFactor":  1.0,
  "albedoMap":    "uuid-of-albedo-satex",
  "normalMap":    "uuid-of-normal-satex"
}
```

`MaterialManager::LoadMaterial(AssetID, cookDir, resMgr)` loads the `.samat`, looks
up textures via `ResourceManager`, and populates a `MaterialInstance`.

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
| `HierarchyComponent` | `parent` entity handle + `children` list (added only when parented) |
| `AnimatedTransformComponent` | Per-frame animated local pose; overrides `TransformComponent` when present |
| `StaticMeshComponent` | `meshAsset` (→ .samesh) + `materialSlots[]` (→ .samat per submesh) |
| `PBRSurfaceComponent` | Typed PBR override: baseColor, roughness, metallic, albedoMap, normalMap |
| `MaterialParamComponent` | Generic shader override: `map<name, variant<float,vec2,vec3,vec4>>` + texture map |
| `CameraComponent` | fovY, nearPlane, farPlane |
| `ActiveCameraTag` | Marks the active camera (one per scene) |
| `DirectionalLightComponent` | color, intensity; direction from entity rotation (−Z) |
| `PointLightComponent` | color, intensity, range; position from entity world transform |
| `SpotLightComponent` | color, intensity, range, innerAngle, outerAngle; position + direction |
| `AreaLightComponent` | color, intensity, size (width × height), twoSided, emissiveScale; position + orientation from transform |
| `StaticGeometryTag` | Hint: entity never moves (for future culling / lightmap / BVH) |

IBL and skybox are **not** ECS components — they are global scene settings in `WorldSettings`.

`AreaLightComponent` axis convention: local +X = tangentU (width direction),
local +Z = tangentV (height direction), local +Y = surface normal. `GatherLights`
extracts tangentU/V from the entity's world matrix columns 0 and 2.

### WorldSettings

Value-type field on `Scene`, serialised in `.sascene`'s `"world"` block:

```cpp
struct WorldSettings {
    // ── Background ──────────────────────────────────────────────
    enum class BackgroundMode { SolidColor, Skybox };
    BackgroundMode backgroundMode  = BackgroundMode::SolidColor;
    glm::vec3      backgroundColor = { 0.08f, 0.08f, 0.08f };  // linear

    // HDR source + baked IBL products (Skybox mode only)
    AssetID skyboxHdr;        // source HDR equirect panorama
    AssetID sh9;              // SH9 coefficient file (.sash9)
    AssetID prefilteredEnv;   // GGX specular cubemap, 5 mips
    AssetID brdfLut;          // split-sum BRDF LUT
    AssetID skyboxCubemap;    // HDR converted to cubemap

    // ── Tonemap ─────────────────────────────────────────────────
    enum class TonemapMode { Builtin, LUT };
    TonemapMode tonemapMode  = TonemapMode::Builtin;
    AssetID     tonemapLut;   // LUT mode only
    float       exposure     = 1.f;
    float       gamma        = 2.2f;
    float       lutStrength  = 1.f;
};
```

**SolidColor IBL behaviour:** `ApplyWorldSettings` encodes `backgroundColor` as a constant
SH L0 ambient term (`irrSH[0] = backgroundColor / 0.282095`) and writes it to a 1×1 RGBA32F
solid-colour cubemap used as `t_PrefilteredEnv`, so metallic surfaces reflect the background
colour instead of sampling a black placeholder. The real BRDF LUT (pre-baked at `Init` via
`GpuIblBake::BakeBrdfLut`) ensures correct specular split-sum.

**Tonemap hot-swap:** `ApplyWorldSettings` can replace `TonemapFeature` (ACES) with
`LutTonemapFeature` at runtime by calling `device->WaitIdle()`, shutting down the old feature,
and initialising the new one in-place in the `m_features` vector slot.

### Two-Tier Material Override

Rendering reads overrides in priority order:

```
Layer 1: StaticMeshComponent::materialSlots[i] → AssetID   ("which template")
Layer 2: PBRSurfaceComponent  — fast path, typed fields, no string lookup
          MaterialParamComponent — generic path, any shader, string → variant
Layer 3: SceneRenderer clones the base MaterialInstance and flushes overrides
```

Entities with no override share the cached `MaterialInstance` (no clone, no allocation).
Both components can coexist: PBR fields apply first, `MaterialParamComponent` after.

### Transform Hierarchy

`HierarchyComponent` stores `parent` (entt::entity) + `children` list. Only entities
that actually participate in a relationship carry this component.

`Scene::UpdateTransforms()`:
1. If `m_hierarchyDirty`: rebuild `m_sortedEntities` via BFS from root entities
2. Walk `m_sortedEntities` (parents always before children):
   - Prefer `AnimatedTransformComponent` over `TransformComponent` for local matrix
   - `world = (no parent) ? TRS(local) : parentWorld * TRS(local)`
   - Clear `dirty` flag on `WorldTransformComponent`

`Scene::SetParent(child, parent)` maintains both sides of the link and marks dirty.
`Scene::MarkDirty(entity)` recursively marks the entity and all descendants dirty.

### Scene File — `.sascene`

JSON serialised by `SceneSerializer`. Full schema:

```json
{
  "version": 1,
  "name": "SceneName",
  "world": {
    "backgroundMode":  "SolidColor",
    "backgroundColor": [0.08, 0.08, 0.08],
    "skyboxHdr":       "uuid",
    "sh9":             "uuid",
    "prefilteredEnv":  "uuid",
    "brdfLut":         "uuid",
    "skyboxCubemap":   "uuid",
    "tonemapMode":     "Builtin",
    "tonemapLut":      "uuid",
    "exposure":        1.0,
    "gamma":           2.2,
    "lutStrength":     1.0
  },
  "entities": [
    {
      "id": 0,
      "parent": -1,
      "tag": "Root",
      "transform":   { "position": [0,0,0], "rotation": [1,0,0,0], "scale": [1,1,1] },
      "staticMesh":  { "mesh": "uuid", "materials": ["uuid"], "castShadow": true, "receiveShadow": true },
      "pbrSurface":  { "baseColor": [1,1,1,1], "roughness": 0.5, "metallic": 0.0 },
      "materialParams": { "scalars": { "param": [r,g,b,a] }, "textures": { "t": "uuid" } },
      "camera":      { "fovY": 1.047, "near": 0.1, "far": 1000.0 },
      "activeCamera": true,
      "directionalLight": { "color": [1,1,1], "intensity": 1.5, "castShadow": false },
      "pointLight":       { "color": [1,1,1], "intensity": 1.0, "range": 10.0 },
      "spotLight":        { "color": [1,1,1], "intensity": 1.0, "range": 10.0,
                            "innerAngle": 0.26, "outerAngle": 0.52 }
    }
  ]
}
```

Rotation quaternions: `[w, x, y, z]`.
`"parent": -1` = root. `"parent": N` = index into the `entities` array.
`AnimatedTransformComponent` is never serialised (recomputed per frame).

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

Single compute shader. No `AttachmentKey` cache (compute pipelines are format-independent).

```cpp
class ComputeProgram {
    bool Load(IRHIDevice*, const Desc&);   // Desc: {spv, refl, optional frameLayout}
    void Unload(IRHIDevice*);
    RHIPipelineHandle   GetPipeline(IRHIDevice*);          // created once, cached
    RHIDescLayoutHandle GetLayout(uint32_t setIndex);      // auto from reflection
    const ShaderReflection& GetReflection() const;
    bool IsLoaded() const;
};
```

All sets owned by `ComputeProgram`; set=0 may be a caller-supplied frame layout.

### MaterialManager

```
Init(IRHIDevice*, ResourceManager*)
    // ResourceManager provides BuiltinTexture::White1x1 for unset slots.

RegisterTypeFromShaders(MaterialTypeDesc, FeatureInitContext)
    // Absorbs all boilerplate: load .spv + .refl, MergeReflections,
    // extract UBO params, sort texture bindings, ShaderProgram::Load,
    // RegisterType. Feature authors specify only what they care about:
    //
    //   struct MaterialTypeDesc {
    //       string name, vertShader, fragShader;  // shader stem, e.g. "pbr"
    //       RHICullMode cullMode  = Back;
    //       bool depthTest  = true;
    //       bool depthWrite = true;
    //   };

RegisterType(unique_ptr<MaterialType>)   // low-level, avoid in feature code
LoadMaterial(AssetID, cookDir, ResourceManager&) → MaterialInstance*  (cached)
CloneInstance(MaterialInstance*) → unique_ptr<MaterialInstance>       (non-cached copy)
Shutdown()
```

`CloneInstance` is used by the render system to produce per-entity material copies
that receive `PBRSurfaceComponent` / `MaterialParamComponent` overrides.

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
ImportTexture(name, handle, initState, finalState)
AddPass(name, setupFn, executeFn)

Compile()  → Kahn's topological sort on read/write edges

Execute(device, cmd)
  → for each sorted pass:
      emit barriers (state transitions)
      call executeFn(cmd, resources)
  → final state transitions for imported textures
```

The `writtenInPreviousPass` guard ensures consecutive passes on the same attachment
always emit a memory barrier, even when the layout is unchanged.

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

Called by `SceneRenderer::SetIBL()` when cooked IBL assets are absent from the cook
cache. Results are saved to disk so subsequent launches load directly (~1 ms vs ~100 ms).

| Map | Size | Format | Description |
|-----|------|--------|-------------|
| `brdfLut` | 512×512 | RGBA32F, 1 mip | Split-sum scale/bias |
| `prefilteredEnv` | 512×512×6 cubemap | RGBA32F, 5 mips | GGX specular |
| `skyboxCubemap` | 1024×1024×6 cubemap | RGBA32F, 1 mip | Direct HDR sky |
| `shCoeffs[9]` | CPU `glm::vec4[9]` | — | SH9 diffuse (Lambertian-convolved) |

### Standalone BRDF LUT

`GpuIblBake::BakeBrdfLut(device)` runs only the BRDF LUT compute pass (no HDR input needed).
Called once in `SceneRenderer::Init` so `m_cachedBrdfLut` is valid from the first frame —
required for correct metallic specular in SolidColor background mode before any Skybox is loaded.

### Integration with SceneRenderer

`SceneRenderer::SetIBL(const WorldSettings&)` implements the offline-first strategy:

```
1. If ws.sh9 + ws.prefilteredEnv + ws.brdfLut + ws.skyboxCubemap all exist in
   the cook cache → LoadSH9Coeffs + LoadTexture × 3, bind to FrameUniformsBuffer.

2. Otherwise:
   a. IBL::ProjectHDRtoSH(hdr) → outSH[9]          (CPU, fast)
   b. GpuIblBake::Bake(device, hdr) → Result        (GPU, ~100 ms first run)
   c. Save brdfLut / prefilteredEnv / skyboxCubemap → .satex in cook cache
   d. Save SH9 coefficients → .sash9 in cook cache
   e. Bind textures to FrameUniformsBuffer
```

After step 2 the next launch always takes path 1.

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

### Firefly Suppression

PDF-based mip LOD selection in `ibl_prefilter.comp`:
```glsl
// roughness=0 → GGX D=0 for all samples → pdf degenerates to 1e-6 → mipLevel=MAX.
// Special-case: copy LOD 0 directly (physically correct perfect mirror).
if (pc.roughness == 0.0) {
    imageStore(u_Output, ivec3(coord, face), vec4(textureLod(u_HdrCube, R, 0.0).rgb, 1.0));
    return;
}
float mipLevel = pc.roughness == 0.0 ? 0.0
               : max(0.0, 0.5 * log2(saSample / saTexel) + kMipBias);
// kMipBias = 1.0: sample one level coarser than "optimal" to suppress fireflies
```

---

## Deferred Rendering Pipeline

The engine uses a **deferred rendering** pipeline. Geometry is rendered to a G-Buffer
in a first pass; a fullscreen lighting pass reads the G-Buffer and evaluates all lights.

### G-Buffer Layout

| Attachment | Format | Contents |
|------------|--------|----------|
| RT0 | `RGBA8_UNORM` | albedo.rgb + occlusion.a |
| RT1 | `RGBA16_SFLOAT` | oct-encoded normal (RG) + roughness (B) + metallic (A) |
| RT2 | `RGBA16_SFLOAT` | data.rgb + encoded shading-model ID (A) |
| Depth | `D32_SFLOAT` | View-space depth; Lighting pass reconstructs world position via `invViewProj × NDC` |

**RT2.rgb meaning is shading-model dependent:** for PBR (model 0) it stores emissive RGB.
For custom models it holds whatever the `.lighting.glsl` evaluator expects.

**RT2.a** encodes the shading model ID via `EncodeShadingFlags(modelID)` / `DecodeShadingModelID(a)`.
`SHADING_MODEL_PBR = 0` is reserved; custom evaluators are assigned IDs ≥ 1 in
stable alphabetical order by `generate_shading_dispatch()`.

Normal encoding uses **Octahedral Normal Encoding** (OctEncode/OctDecode): lossless
unit-vector → RG float mapping with no polar singularities.

### Built-in Render Features (pass order)

All passes are implemented as `RenderFeature` subclasses private to `SceneRenderer`,
registered in order in `SceneRenderer::Init`:

| Feature | Condition | Output | Shader |
|---------|-----------|--------|--------|
| `ShadowFeature` | `config.shadowEnabled` | shadow map (D32, 2048²) | `shadow.vert/.frag` |
| `SkyboxFeature` | always | HDR buffer (draw or clearColor) | `skybox.vert/.frag`; SolidColor: zero-draw clearColor pass |
| `GBufferFeature` | always | RT0/RT1/RT2 + depth | `deferred_geometry.vert/.frag` |
| `DeferredLightingFeature` | always | HDR buffer (RGBA16F) | `fullscreen_tri.vert` / `deferred_lighting.frag` |
| `SelectionMaskFeature` | always | R8 silhouette mask | `selection_mask.vert/.frag`; renders selected entity + all descendants |
| `BloomFeature` | `config.bloomEnabled` | additive bloom into HDR buffer | `bloom_*.frag` |
| `TonemapFeature` | `config.builtinTonemap` && `ws.tonemapMode==Builtin` | swapchain LDR | `tonemap.frag` (ACES + gamma) |
| `LutTonemapFeature` | `config.builtinTonemap` && `ws.tonemapMode==LUT` | swapchain LDR | `postfx_lut_tonemap.frag` (ACES + 2D strip LUT) |
| `SelectionOutlineFeature` | always | outline composited onto swapchain | `selection_outline_dilate.frag` + composite |
| `InfiniteGridFeature` | when enabled | XZ grid onto swapchain | `infinite_grid.frag` |
| `DebugOverlayFeature` | always | debug lines onto swapchain | `debug_line.vert/.frag` |
| user `RenderFeature`s | `AddFeature(...)` | any | custom |

`TonemapFeature` ↔ `LutTonemapFeature` are hot-swapped at runtime by `ApplyWorldSettings`.
The active tonemap feature is tracked via `m_tonemapFeature` (raw ptr into `m_features`).

`RendererConfig` (in `SceneRenderer::Desc`) controls the optional passes:

```cpp
struct RendererConfig {
    bool     shadowEnabled  = true;
    uint32_t shadowMapSize  = 2048;
    bool     bloomEnabled   = true;
    int      bloomMipCount  = 3;
    bool     builtinTonemap = true;
};
```

### GBufferFeature and Material Type Registration

`GBufferFeature::OnInit` calls `ctx.matMgr->RegisterTypeFromShaders({"PBR", "deferred_geometry", "deferred_geometry"}, ctx)`
and creates the renderer's default `MaterialInstance`. This mirrors the pattern used by
all other built-in features and enables third-party `RenderFeature`s to register their
own G-Buffer-compatible material types without touching `SceneRenderer`.

### Bloom

Multi-scale pyramid approach:
1. **Threshold pass** — extract pixels where luminance > 1.0 (HDR threshold)
2. **Downsample chain** — `bloomMipCount` levels using 13-tap COD downsample (anti-flicker)
3. **Upsample + accumulate** — bilinear upsample each level back to full resolution with weighted additive blend
4. **Composite** — final bloom additive-blended into the HDR color buffer

### Tonemap

`TonemapFeature` applies ACES Filmic tonemap (HDR → LDR) and gamma correction
in a single fullscreen pass, writing to the swapchain image.

---

## Custom Shading Models

Custom material types are defined by `*.lighting.glsl` evaluator files.
The build system assembles them into a generated GLSL dispatch included by `deferred_lighting.frag`.

### Evaluator Contract

Each `*.lighting.glsl` file must implement:

```glsl
// Signature (before macro rename by the generator):
vec3 EvaluateShading(GBufferData gbuf);
// gbuf.albedo, gbuf.normal, gbuf.roughness, gbuf.metallic, gbuf.data, gbuf.worldPos, etc.
```

The generator wraps each evaluator with `#define EvaluateShading Evaluate_<Name>` / `#undef`
so multiple evaluators coexist in one translation unit without name conflicts.

### Registration

```cmake
# In any CMakeLists.txt that provides a custom shading model:
register_lighting_evaluator("${CMAKE_CURRENT_SOURCE_DIR}/my_material.lighting.glsl")

# In root CMakeLists.txt, after all register_lighting_evaluator() calls:
generate_shading_dispatch("${CMAKE_BINARY_DIR}/generated/shaders")
```

Built-in evaluators under `assets/shaders/builtin/` are auto-registered by the root
`CMakeLists.txt`. `SHADING_MODEL_PBR = 0` is always reserved; custom models are
assigned IDs starting from 1 in stable alphabetical file order.

### G-Buffer Fill for Custom Models

Custom material types need a `*.gbuffer.frag` that writes the same MRT layout as
`deferred_geometry.frag`. The type is registered via `RenderFeature::OnInit`:

```cpp
ctx.matMgr->RegisterTypeFromShaders(
    {"MyMaterial", "deferred_geometry", "my_material.gbuffer"}, ctx);
```

The vertex shader (`deferred_geometry.vert`) is shared — only the fragment shader differs.

---

## Frame Loop

The application calls exactly two methods per frame. All GPU work is internal to `SceneRenderer`:

```
── App code ─────────────────────────────────────────────────────────────────────

scene.UpdateTransforms()              // BFS propagate dirty transforms
renderer.RenderFrame(scene, w, h)     // full frame — all phases internal

── Inside RenderFrame ───────────────────────────────────────────────────────────

Phase 1: Collect
   FillCameraUniforms(scene, w, h)   // WorldTransformComponent → view/proj
   GatherLights(scene)               // DirectionalLight + PointLight + SpotLight + AreaLight

Phase 2: GPU
   device->BeginFrame()
      wait fence, vkAcquireNextImageKHR
      reset + begin command buffer
      swapchain: UNDEFINED → COLOR_ATTACHMENT

   frameUniforms.Upload(fi, fu, lu)  // upload camera + light data

   m_rg.Reset() + ImportTexture(swapchain, depth, hdrColor, gbuffers, shadowMap)

   ShadowFeature::AddPasses()          ← optional (config.shadowEnabled)
   SkyboxFeature::AddPasses()          ← writes HDR buffer
   GBufferFeature::AddPasses()         ← geometry → RT0/RT1/RT2 + depth
   DeferredLightingFeature::AddPasses()← fullscreen lighting → HDR buffer
   BloomFeature::AddPasses()           ← optional (config.bloomEnabled)
   TonemapFeature::AddPasses()         ← optional (config.builtinTonemap)
   for each user RenderFeature:
     feature.AddPasses(renderer, reg, w, h)
       └── renderer.AddPass(name, PassFlags, execFn)
             └── m_rg.AddPass(...)  ← PassFlags → RG read/write edges

   m_rg.Compile()   ← topological sort on dependency edges
   m_rg.Execute()   ← barriers + execute lambdas

   device->EndFrame()
      swapchain: COLOR_ATTACHMENT → PRESENT_SRC_KHR
      vkEndCommandBuffer

   device->Present()
      vkQueueSubmit  (wait: imgReady; signal: renderDone)
      vkQueuePresentKHR
      advance frame slot (double-buffered)
```

**GBufferFeature is material-type-agnostic.** It iterates all `DrawItem`s and binds
`item.pipeline` per draw. Multiple shader types (PBR, SimpleAlbedo, custom evaluators)
coexist in the same G-Buffer pass because Vulkan allows `vkCmdBindPipeline` between draws.
All G-Buffer pipelines share the same 3-MRT `AttachmentKey` (RT0=RGBA8, RT1=RGBA16F, RT2=RGBA16F,
depth=D32F), ensuring render pass compatibility. `DeferredLightingFeature` reads the G-Buffer
and dispatches to the correct `EvaluateShading` function by reading the shading-model ID
packed in RT2.a.

**RenderFeature** — extension point for custom shaders:
- `OnInit(const FeatureInitContext&)` — register material types, load shaders
- `AddPasses(renderer, reg, w, h)` — inject RenderGraph passes via `renderer.AddPass`
- No `RenderGraph` or `RGTextureHandle` types appear in feature code

---

## EntityFactory

**Location:** `src/function/scene/EntityFactory.hpp/.cpp`

Stateless factory for common entity archetypes. Every method creates a complete entity
(Tag + Transform + WorldTransform + archetype components) and calls `MarkDirty`.

| Method | Components added |
|--------|-----------------|
| `CreateDirectionalLight` | `DirectionalLightComponent` |
| `CreatePointLight` | `PointLightComponent` |
| `CreateSpotLight` | `SpotLightComponent` |
| `CreateAreaLight` | `AreaLightComponent` + optional `StaticMeshComponent` (emissive panel) |
| `CreateStaticMesh` | `StaticMeshComponent` |
| `CreateCamera` | `CameraComponent` + optional `ActiveCameraTag` |

`CreateAreaLight(scene, name, color, intensity, size, withMesh, rotation, position)`:
- `withMesh = true` (default): attaches a thin quad mesh (`StaticMeshComponent`) with
  an emissive material at `color × emissiveScale`. The mesh scale is `{size.x, 0.01, size.y}`.
- `withMesh = false`: pure invisible light source (AreaLightComponent only).

```cpp
auto sun = EntityFactory::CreateDirectionalLight(scene, "Sun",
    {1.f, 0.95f, 0.85f}, 2.f,
    glm::normalize(glm::angleAxis(glm::radians(-45.f), glm::vec3{1,0,0})));

auto panel = EntityFactory::CreateAreaLight(scene, "AreaPanel",
    {1.f, 0.9f, 0.8f}, 10.f, {2.f, 1.f}, /*withMesh*/true);
```

---

## Key Design Decisions

### No VkRenderPass Objects (Dynamic Rendering)
`VK_KHR_dynamic_rendering` (Vulkan 1.3 core) eliminates `VkRenderPass` and
`VkFramebuffer`. Attachment formats are embedded in `RHIPipelineDesc` and checked
at `BeginRenderPass` time. This enables lazy pipeline creation in `ShaderProgram`
without a separate render pass object per attachment format combination.

### AttachmentKey Pipeline Cache in ShaderProgram
The same shader can be used in passes with different RT format combinations
(shadow depth-only, deferred GBuffer, forward color+depth). `AttachmentKey` is a
compact struct (up to 4 color formats + 1 depth format) that hashes to a unique
`VkPipeline`. Compiled once per combination, cached per `ShaderProgram`.

### ComputeProgram Has No AttachmentKey Cache
Compute pipelines have no render targets. One `VkComputePipeline` per shader,
created on first call to `GetPipeline()`, cached as a single handle.

### Descriptor Set Convention
```
set=0  per-frame globals   (FrameUniformsBuffer — bound before any pass)
set=1  per-material params (MaterialInstance::Bind())
```
ComputeProgram owns all its sets; set=0 may be a caller-supplied frame layout.

### HierarchyComponent Is Optional
Only entities that are actually parented carry `HierarchyComponent`. Root-level
entities with no children have no hierarchy overhead. `Scene::UpdateTransforms()`
uses `TransformComponent` presence (not `HierarchyComponent`) to identify
entities that need world matrix computation.

### AnimatedTransformComponent Overrides, Never Replaces
The animation system writes `AnimatedTransformComponent` each frame;
`TransformComponent` retains the "rest pose" (serialised to disk).
`Scene::UpdateTransforms()` prefers `AnimatedTransformComponent` when present.
Entities without animation incur zero overhead (component not attached).

### Pre-Allocated Descriptor Pool
A single `VkDescriptorPool` with fixed counts (`512 samplers, 256 UBOs, 256 SSBOs,
128 storage images`) is created at device init. `FREE_DESCRIPTOR_SET_BIT` allows
individual set reclamation. Will be replaced with a chained pool allocator if counts
need to grow.

### Rendering Resource Ownership

`SceneRenderer` owns all rendering-internal resources. The app only holds the primitives
that predate or outlive the renderer:

| Resource | Owner | Rationale |
|----------|-------|-----------|
| Window + Device | App | Hardware lifetime; multiple renderers can share one device |
| ResourceManager | App | Shared asset cache across renderers / passes |
| MaterialManager | App | Pipeline cache is per-`MaterialType`, not per-renderer |
| FrameUniformsBuffer | SceneRenderer | Frame data is renderer-specific |
| Depth texture | SceneRenderer | Auto-resized on resolution change |
| GpuIblBake | SceneRenderer | IBL bake is a one-shot renderer operation |
| White 1×1 | ResourceManager | `GetBuiltin(BuiltinTexture::White1x1)` — shared placeholder; `MaterialManager::Init` fetches it from `ResourceManager` |
| DrawItem list | SceneRenderer | Rebuilt by `BuildDrawList(scene)` |
| RenderFeatures | SceneRenderer | `AddFeature` transfers ownership; `OnInit` called immediately |

### Index-Based Handles vs Pointers
- Safe to copy; no dangling pointer risk
- Pool growth (realloc) doesn't invalidate handles
- Explicit `IsValid()` checks; no null-pointer UB
- 32-bit footprint vs 64-bit pointer
