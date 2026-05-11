## 待办issue

24. **[低优先级] 长耗时操作进度反馈**
    - **启动进度条**（难度高）：`OnAttach` 在渲染循环前同步执行，ImGui 无法渲染。需重构为两阶段延迟初始化或独立 splash screen 渲染通道，暂不做。
    - **Reimport All 进度条**（难度中，最有实际价值）：`ReimportDir` 同步阻塞 UI。改法：将其拆成逐帧 N 个文件的状态机，`OnDraw` 期间推进并用 `ImGui::ProgressBar` + modal 显示；或移入工作线程 + 原子进度计数器。
    - 前置条件：依赖 `AssetsPanel` 暴露异步迭代接口；待项目素材量增大后再做。
27. 美化编辑器：骨骼改用球+锥绘制而不是线
29. 引入挂载脚本 并提炼现有运行时库暴露给脚本调用
31. animator编辑器 imguizmo
32. 材质可视化编程 imguizmo
33. 场景物体：贝塞尔曲线与相机移动 imguizmo 脚本issue完成后
36. **[极低优先级] RHI VkMemory 级别别名（Approach A）**
23. 帧率优化
24. 透明材质面片（植物等）
54. **[低优先级，待复现] Reimport 后内存缓存未失效导致 mesh 显示异常** — Reimport 更新 cook cache 磁盘数据后，`ResourceManager` 按 UUID 缓存的 GPU mesh handle 未刷新，渲染仍用旧数据；重启后缓存清空才恢复正常。根因：`ReimportDir` 完成后未调用 `ClearProjectAssets()`。修复方向：reimport 完成时触发 `ResourceManager::ClearProjectAssets()`（同 Issue #38 逻辑，触发时机改为 reimport 后）。现象：曾在 BoomBox.glb（带动画）上观察到固定面片破碎，无法稳定复现。
23. 程序化天空盒，选择一个物体作为光源方向
22. 调试渲染：面片id着色，lod着色，随机着色，depth着色...

---

## Issue #36 — [极低优先级] RHI VkMemory 级别别名（Approach A）

在 Issue #16 完成后，可进一步将 slot pool 的显存合并：所有 transient slot 共享一块大 `VkDeviceMemory`，每个 slot 的 `VkImage` 通过 offset 绑定到对应区段（`VK_IMAGE_CREATE_ALIAS_BIT`）。

**前置**：引入 VMA `placed allocation` 或手写 linear allocator；`IRHIDevice::CreateTextureAliased(desc, memory, offset)` 接口；需要 VkMemoryRequirements 对齐计算。

暂不规划，等 #16 上线并确认收益后再评估必要性。

---

## Issue #19 — （已拆分）Data Inheritance 并入 #68（ComponentSchema UI 层）和运行时单独实施

> 本 issue 已拆分：UI 层骨骼 picker / AssetRef 字段 → #68（ComponentSchema）；.samesh v6 格式 / AnimationSystem 三级解析 / MeshTool CLI → 待单独运行时 issue 追踪。

---

## Issue #29 — 脚本系统（C# Scripting） ✅ DONE
<!-- hostfxr + Roslyn 编译 + CollectibleALC；ScriptApiFunctionTable 指针表替代 P/Invoke 共享库；SDK ref-pack 解决 CS0433；AppDomain 跨 ALC 搜索解决 StellarAlia.Runtime 找不到；ScriptComponent + 完整 OnAttach→OnStart→FixedUpdate/Update/LateUpdate→OnStop→OnDetach 生命周期已验证 -->

---
## Issue #45 — Depth of Field（景深） ✅ DONE
<!-- CoC 从深度重建（薄透镜公式）→ 分离式近/远高斯模糊（4 pass H+V）→ smoothstep 三层合成；DoFFeature 插入 Bloom 之后 Tonemap 之前，disabled 时零开销；参数序列化到 .sascene；Phase 2 路线：六边形 3-pass / Scatter-as-Gather 修复方形高光 -->

---

## Issue #46 — Motion Blur（运动模糊）

**优先级：中（依赖 #42 velocity buffer）**

### 目标

利用 TAA velocity buffer 实现 per-object tile-based 运动模糊。

### 设计

- 依赖 #42 已有的 velocity buffer（RG16F）
- **TileMax pass**：downscale velocity 到 tile（16×16）取最大速度
- **NeighborMax pass**：3×3 tile 扩散
- **Reconstruct pass**：按速度方向采样 hdrTex，写回（Tonemap 之前）
- 仅相机运动或高速物体触发，静止场景无开销

### 参数

```cpp
bool  motionBlurEnabled  = false;
float motionBlurStrength = 0.5f;
int   motionBlurSamples  = 8;   // 重建采样数
float motionBlurMaxSpeed = 0.1f; // 屏幕空间速度截止（比例）
```

---

## Issue #47 — 屏幕修饰效果（Vignette / Chromatic Aberration / Film Grain）

**优先级：低（依赖 #40，LDR pass，与 Tonemap 合并或独立一次 pass）**

### 目标

Tonemap 之后的低开销 LDR 修饰，全部合并进一个 fullscreen pass（或 LUT bake 中）。

### 设计

- 单个 `PostFX` pass（或合并进 Tonemap pass），读 swapchain，写 swapchain
- Vignette：屏幕边缘距离 → 暗化，椭圆形
- Chromatic Aberration：中心向外 UV 偏移，R/G/B 通道分别采样
- Film Grain：per-frame 随机种子 + 高频噪声叠加（强度随亮度衰减）

### 参数

```cpp
bool  vignetteEnabled    = false;
float vignetteIntensity  = 0.4f;
float vignetteSmoothness = 0.6f;

bool  caEnabled          = false;  // Chromatic Aberration
float caStrength         = 0.5f;

bool  filmGrainEnabled   = false;
float filmGrainIntensity = 0.1f;
float filmGrainSize      = 1.6f;
```

---

## Issue #48 — SSR（屏幕空间反射）

**优先级：低（依赖 #42 TAA 降噪，否则噪点过多）**

### 目标

利用 GBuffer 法线/粗糙度 + depth 做屏幕空间光线步进，替代纯 IBL 高光反射，效果适用于平整表面（地板、金属面板）。

### 设计

- **SSR Trace pass**（compute）：HiZ 加速步进，roughness > 阈值时跳过，未命中回退 IBL
- **SSR Resolve pass**：重投影历史混合（依赖 TAA velocity buffer 降噪）
- 新增瞬态纹理：`ssrRaw`(RGBA16F, viewport)、`ssrResolved`(RGBA16F, viewport)
- DeferredLighting 合并 SSR 结果 + IBL 高光

### 参数

```cpp
bool  ssrEnabled      = false;
float ssrMaxRoughness = 0.4f;  // 超过此粗糙度不计算 SSR
int   ssrMaxSteps     = 64;
float ssrThickness    = 0.1f;  // 深度容差
float ssrStrength     = 1.0f;
```

---

## Issue #49 — Volumetric Fog（体积雾）

**优先级：极低（依赖 #40，实现复杂）**

### 目标

Froxel（视锥体体素）光线积分，支持方向光散射 + 环境 fog。

### 设计

- 3D 纹理（160×90×64，RGBA16F）存储 scattering/extinction
- **Fog Inject compute**：写入每个 froxel 的散射+消光
- **Fog Scatter compute**：沿深度方向积分透射率
- **Fog Apply pass**：读 fog 3D 纹理 + depth，叠加到 HDR buffer（Tonemap 之前）

暂不规划实施步骤，等前序 PP 系统稳定后再展开。

---

## Issue #55 — LOD（Level-of-Detail）系统

**优先级：中（Issue #23 帧率优化 子任务；收益最高的几何体优化）**

### 目标

对场景中的 mesh 按每帧屏幕覆盖率自动选择简化级别（LOD0/1/2/3），使靠近时顶点数与现在持平、远离时大幅降低，解决 BVH per-object 剔除无法覆盖的高面数模型瓶颈。

### 设计

#### Cook Pipeline 扩展（`tools/cook/`）

新增 `meshoptimizer` 依赖（MIT 许可，header-only-ish），Cook 时为每个 submesh 生成 N-1 个简化级别：

```cpp
// tools/cook/MeshImporter.cpp — 伪代码
for each submesh:
    lodLevels[0] = {原始 indices, indexCount}
    for lod in 1..kMaxLODs-1:
        targetCount = indexCount * pow(0.5f, lod)   // 50%, 25%, 12.5%
        float err = 0.f
        count = meshopt_simplify(dst, src, srcCount,
            positions, vertexCount, stride,
            targetCount, 0.02f * lod, 0, &err)
        lodLevels[lod] = {appendedIndexOffset, count}
```

**CMake 变更：** `third_party/CMakeLists.txt` 添加 `meshoptimizer`（submodule 或 FetchContent，MIT 许可）。

#### CookedMesh 格式扩展（`tools/cook/CookedMesh.hpp`）

```cpp
static constexpr int kMaxLODLevels = 4;

struct CookedLODRange {
    uint32_t indexOffset;
    uint32_t indexCount;
};

struct CookedSubMesh {
    // ... 现有字段不变 ...
    uint32_t       lodCount = 1;                  // [1..kMaxLODLevels]
    CookedLODRange lods[kMaxLODLevels] = {};      // lods[0] = 完整精度
};
// 向后兼容：lodCount==1 时行为与旧格式完全一致
```

#### DrawItem 扩展（`SceneRenderer.hpp`）

```cpp
static constexpr int kMaxDrawItemLODs = 4;

struct DrawItemLOD {
    uint32_t firstIndex;
    uint32_t indexCount;
};

struct DrawItem {
    // ... 现有字段 ...
    DrawItemLOD  lods[kMaxDrawItemLODs];
    int          lodCount    = 1;
    glm::vec3    worldAABBMin, worldAABBMax;   // BuildDrawList 时计算（subLocalT × worldT）
    mutable int  activeLOD   = 0;             // per-frame mutable，RenderFrame 写
};
```

#### LOD 选择（`SceneRenderer.cpp`）

BVH cull 之后、GBuffer AddPasses 之前：

```cpp
static float ComputeScreenCoverage(
    glm::vec3 aabbMin, glm::vec3 aabbMax,
    const glm::mat4& viewProj, uint32_t w, uint32_t h)
{
    // 8 角点投影到 NDC [-1,1]²，取 2D bounding rect 面积 / 4.0（归一化）
    // 即 projected_rect_area / (w*h) 的近似
}

static int SelectLOD(float coverage, int lodCount) {
    static const float kThresholds[] = { 0.10f, 0.02f, 0.003f };
    for (int i = lodCount - 1; i > 0; --i)
        if (coverage < kThresholds[i - 1]) return i;
    return 0;
}
```

LOD 阈值说明（屏幕覆盖率）：

| LOD | 条件 |
|-----|------|
| 0   | coverage ≥ 10% |
| 1   | 2% ≤ coverage < 10% |
| 2   | 0.3% ≤ coverage < 2% |
| 3   | coverage < 0.3% |

`GBufferFeature::AddPasses` 和 `ShadowFeature::AddPasses` 改为读取 `item->lods[item->activeLOD]`。

#### PerformancePanel

"LOD Stats" 折叠区：各级别当前帧 DrawItem 数量分布。

### 实施步骤

- [ ] 1. `third_party/CMakeLists.txt` — 添加 `meshoptimizer`（FetchContent 或 submodule，MIT）
- [ ] 2. `tools/cook/CookedMesh.hpp` — `CookedLODRange` + `CookedSubMesh::lodCount/lods[]`（向后兼容 lodCount=1）
- [ ] 3. `tools/cook/MeshImporter.cpp` — `meshopt_simplify` 生成 LOD1/2/3；index 数据追加到末尾；记录各级别 offset+count
- [ ] 4. `src/function/renderer/SceneRenderer.hpp` — `DrawItemLOD` 结构体；`DrawItem` 加 `lods[]/lodCount/worldAABBMin/Max/activeLOD`
- [ ] 5. `src/function/renderer/SceneRenderer.cpp` — `BuildDrawList`：从 `CookedSubMesh.lods[]` 填充 DrawItem；计算 `worldAABBMin/Max`（local AABB × subLocalTransform × worldTransform）
- [ ] 6. `src/function/renderer/SceneRenderer.cpp` — `RenderFrame`：LOD 选择 pass（`ComputeScreenCoverage` + `SelectLOD` → `activeLOD`）
- [ ] 7. `GBufferFeature::AddPasses` + `ShadowFeature::AddPasses` — 用 `item->lods[item->activeLOD]` 替代原 `firstIndex/indexCount`
- [ ] 8. `editor/ui/panels/PerformancePanel.cpp` — LOD 分布统计

### 边界情况与约束

| 场景 | 处理 |
|------|------|
| `meshopt_simplify` 返回 0 或极少三角形 | 跳过该 LOD，`lodCount` 保持较小值 |
| Skinned mesh | `skipCull=true` 的 DrawItem 跳过 LOD 选择（activeLOD = 0），骨骼 AABB 运动中失效 |
| worldAABBMin/Max 静态 | `BuildDrawList` 计算一次；移动物体下一帧 LOD 选择滞后 1 帧（可接受） |
| 旧 cook cache | `.samesh` 格式变化需要 Reimport；旧格式读取时 `lodCount` 缺失视为 1 |
| `ComputeScreenCoverage` | 每可见 DrawItem 8 次 NDC 变换，< 500 item 可接受 |
| LOD popping | 初版不加迟滞（hysteresis），后续可加 ±5% 缓冲带 |

**不做：** 运行时 LOD bias 参数 UI（初版硬编码）；Impostor billboard（LOD3 只用低面数 mesh）；GPU 驱动 LOD（见 #57）。

### 受益 issues

- **#23 帧率优化**：高面数模型远距帧率可提升 40–70%
- **#57 Cluster Culling**：LOD1/2/3 的简化 mesh 可直接作为 meshlet 生成输入，形成完整层级剔除体系

---

## Issue #56 — Stencil Masking for Deferred Lighting

**优先级：低（小优化，工程量小；收益在大背景场景）**

### 目标

GBuffer pass 向深度附件的模板通道写入 1（有几何体的像素），Deferred Lighting pass 开启模板测试仅处理 stencil == 1 的像素，使背景像素在固定功能阶段被拒绝，比当前 shader 内 `if (depth >= 1.0) return` 更彻底（无 fragment invocation）。

副作用：alpha-test 修复后（植物 `discard` 不写深度/模板），植物镂空区域 stencil = 0，lighting 自动正确跳过。

### 设计

#### 改动链

```
深度格式: D32F → D24_S8（stencil 通道已有 RHIFormat 定义，barrier 逻辑已正确）

RHIPipelineDesc 扩展:
  + stencilFormat, stencilTestEnable, stencilWriteEnable
  + RHIStencilOpState front/back（failOp/passOp/compareOp/reference/masks）

VulkanDevice::CreatePipeline:
  + ds.stencilTestEnable = true (GBuffer 写; Lighting 读)
  + renderingCI.stencilAttachmentFormat = ToVkFormat(desc.stencilFormat)

ToVkImageLayout(DepthWrite):
  当前返回 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL（depth-only）
  → 改为 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL（兼容 D32F 和 D24_S8）

VulkanCommandList::BeginRenderPass depthAttachment.imageLayout:
  同上，改为 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
```

#### RHIPipelineDesc / IRHIDevice.hpp 扩展

```cpp
// 新增枚举（IRHIDevice.hpp）
enum class RHIStencilOp  : uint8_t { Keep, Zero, Replace, IncrClamp, DecrClamp,
                                      Invert, IncrWrap, DecrWrap };
enum class RHICompareOp  : uint8_t { Never, Less, Equal, LessOrEqual, Greater,
                                      NotEqual, GreaterOrEqual, Always };

struct RHIStencilOpState {
    RHIStencilOp failOp     = RHIStencilOp::Keep;
    RHIStencilOp passOp     = RHIStencilOp::Keep;
    RHICompareOp compareOp  = RHICompareOp::Always;
    uint8_t      reference    = 0;
    uint8_t      compareMask  = 0xFF;
    uint8_t      writeMask    = 0xFF;
};

struct RHIPipelineDesc {
    // ... 现有字段不变 ...
    RHIFormat         stencilFormat      = RHIFormat::Undefined; // Undefined = 无模板
    bool              stencilTestEnable  = false;
    bool              stencilWriteEnable = false;
    RHIStencilOpState stencilFront;
    RHIStencilOpState stencilBack;
};
```

#### GBuffer pipeline 配置（`SceneRenderer.cpp` GBufferFeature::OnInit）

```cpp
pipelineDesc.stencilFormat      = RHIFormat::D24_S8;
pipelineDesc.stencilWriteEnable = true;
pipelineDesc.stencilFront = {
    .passOp    = RHIStencilOp::Replace,
    .compareOp = RHICompareOp::Always,
    .reference = 1
};
```

#### DeferredLighting pipeline 配置

```cpp
pipelineDesc.depthTest           = false;
pipelineDesc.depthWrite          = false;
pipelineDesc.stencilFormat       = RHIFormat::D24_S8;
pipelineDesc.stencilTestEnable   = true;
pipelineDesc.stencilFront = {
    .compareOp = RHICompareOp::Equal,
    .reference = 1
};
```

### 实施步骤

- [ ] 1. `src/platform/rhi/IRHIDevice.hpp` — 新增 `RHIStencilOp`、`RHICompareOp` enums + `RHIStencilOpState`；`RHIPipelineDesc` 加 stencil 字段
- [ ] 2. `src/platform/rhi/vulkan/VulkanUtils.cpp` — `ToVkImageLayout(DepthWrite)` 改返回 `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`
- [ ] 3. `src/platform/rhi/vulkan/VulkanCommandList.cpp` — `BeginRenderPass` 深度附件 imageLayout 同步改为 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`
- [ ] 4. `src/platform/rhi/vulkan/VulkanDevice.cpp` — `CreatePipeline`：新增 `ToVkStencilOpState` 映射函数；`ds` 填充 stencil 字段；`renderingCI.stencilAttachmentFormat` 设置
- [ ] 5. `src/function/renderer/SceneRenderer.cpp` — `RenderFrame` 深度纹理格式改为 `RHIFormat::D24_S8`
- [ ] 6. `GBufferFeature::OnInit` — 所有 GBuffer MaterialType 的 pipeline desc 加模板写入配置（含自定义 shading model）
- [ ] 7. `DeferredLightingFeature::OnInit` — pipeline 加模板测试配置（`depthTest=false`，`stencilTestEnable=true`）
- [ ] 8. 验证（RenderDoc）：lighting pass 背景区域无 fragment 调用；PerformancePanel lighting 耗时对比

### 边界情况与约束

| 场景 | 处理 |
|------|------|
| `D24_S8` 驱动支持 | RTX 3070 必支持；`VkFormatProperties::optimalTilingFeatures` 含 `DEPTH_STENCIL_ATTACHMENT_BIT` |
| Alpha-test 植物 | `discard` 不写深度/模板 → stencil = 0 → lighting 正确跳过（依赖 alpha-test bug 先修复）|
| `D32F` → `D24_S8` 精度 | D24 深度精度下降（24-bit vs 32-bit）；对 Sponza 规模场景不明显；如有需要可改用 D32F_S8（若驱动支持） |
| GBuffer 多 MaterialType | PBR + 所有自定义 shading model 的 pipeline 均需加 stencil write |
| ShadowFeature | shadow pipeline 无颜色/模板附件，无需修改 |
| `AttachmentKey` pipeline cache | `stencilFormat` 是 pipeline 缓存 key 的一部分（`ShaderProgram::AttachmentKey`）；若 AttachmentKey 不含 stencil 字段，需扩展 |
| 向后兼容 | `RHIPipelineDesc` 默认 `stencilTestEnable=false`，所有现有 pipeline 行为不变 |

**不做：** 多层 stencil 值（只用 0/1）；物体 ID 写 stencil；stencil shadow volumes。

### 受益 issues

- **#23 帧率优化**：大背景场景 Deferred Lighting 节省 15–30%
- **植物 alpha-test 修复**：修复后 stencil 使镂空区域在 lighting 中自动正确跳过

---

## Issue #57 — GPU-Driven Meshlet Cluster Culling

**优先级：低（大工程量；场景 mesh 面数 > 10 万时收益显著，现有 Sponza 场景较小）**

### 目标

将 mesh 切分为约 128 三角形一组的 meshlet，GPU compute shader 在顶点着色器运行之前做 per-cluster frustum + backface cone 剔除，通过 `vkCmdDrawIndexedIndirect` 只绘制存活的 meshlet。将顶点处理粒度从 per-object 降至 per-cluster，实现单 mesh 内部的三角形级剔除。

### 设计

#### 数据流对比

```
现状（per-object draw call）：
  vkCmdDrawIndexed(indexCount=100000) → 顶点着色器 ×100000（全跑）

目标（per-meshlet indirect draw）：
  Compute cull → vkCmdDrawIndexedIndirect(survivingMeshletBuffer)
  → 每个 surviving meshlet → 顶点着色器 × 128 × survivingCount（仅存活）
```

#### Meshlet 数据结构

```cpp
struct Meshlet {
    uint32_t  vertexOffset;    // 该 meshlet 顶点在全局 vertex buffer 的起始
    uint32_t  triangleOffset;  // 三角形索引在全局 triangle buffer 的起始
    uint8_t   vertexCount;     // ≤ 64
    uint8_t   triangleCount;   // ≤ 128
    uint8_t   _pad[2];
    glm::vec3 aabbMin, aabbMax;
    glm::vec3 coneAxis;        // backface cone culling 轴（meshoptimizer 计算）
    float     coneCutoff;      // cos(半角)；dot(coneAxis, toCamera) >= cutoff → 整个 meshlet 背面
};
```

#### Cook Pipeline 扩展（共享 #55 的 meshoptimizer）

```cpp
// tools/cook/MeshImporter.cpp — 伪代码
meshopt_Meshlet raw[max_meshlets];
uint8_t verts[max_meshlets * 64], tris[max_meshlets * 128 * 3];

size_t count = meshopt_buildMeshlets(
    raw, verts, tris, indices, index_count,
    vertex_positions, vertex_count, sizeof(Vertex),
    64, 128, 0.0f);  // max_verts=64, max_tris=128, cone_weight=0

for each meshlet m:
    meshopt_Bounds b = meshopt_computeMeshletBounds(
        &verts[m.vertex_offset], &tris[m.triangle_offset],
        m.triangle_count, positions, vertex_count, stride);
    // b → Meshlet::aabbMin/Max, coneAxis, coneCutoff
```

序列化为新资产格式 `.sameshlet`（含 Meshlet 数组 + 全局 vertex table + 全局 triangle buffer）。

#### 运行时 GPU Buffers（`BuildDrawList` 时上传）

```
Meshlet SSBO        — 所有场景 mesh 的 Meshlet[] 合并
Global Vertex SSBO  — 所有 mesh 顶点合并（Meshlet::vertexOffset 为全局偏移）
Triangle Buffer     — 所有 mesh 三角形索引合并（uint8 packed, ×3 = index）
Instance SSBO       — per-DrawItem：{meshletOffset, meshletCount, modelMatrix}
Indirect Draw Buf   — compute shader 写入 VkDrawIndexedIndirectCommand[]
Visible Counter     — atomic uint，compute 累加
```

#### Culling Compute Shader（`assets/shaders/meshlet_cull.comp`）

```glsl
layout(local_size_x = 64) in;

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_totalMeshlets) return;
    Meshlet m = u_meshlets[id];

    // Frustum cull（p-vertex AABB 测试）
    for (int i = 0; i < 6; i++)
        if (dot(u_frustumPlanes[i].xyz, m.aabbMax) + u_frustumPlanes[i].w < 0.0) return;

    // Backface cone cull
    vec3 center = (m.aabbMin + m.aabbMax) * 0.5;
    if (dot(m.coneAxis, normalize(u_cameraPos - center)) >= m.coneCutoff) return;

    uint slot = atomicAdd(u_visibleCount, 1);
    u_indirectCmds[slot] = VkDrawIndexedIndirectCommand{
        uint(m.triangleCount) * 3, 1, m.triangleOffset * 3, int(m.vertexOffset), slot
    };
}
```

#### Vertex Shader 改造

`deferred_geometry_meshlet.vert`：`gl_InstanceIndex`（= firstInstance = slot）→ 从全局 SSBO 读取顶点（set=2 bindings），与 `#54 GPU skinning` set=2 约定需协调（skinned 路径和 meshlet 路径互斥）。

#### SceneRenderer 架构变化

- `BuildDrawList`：新增 `BuildMeshletBuffers` 路径（存在 `.sameshlet` 时）
- `RenderFrame`：dispatch meshlet cull compute → pipeline barrier → `vkCmdDrawIndexedIndirect`
- 原 `DrawItem` draw call 链路保留为 fallback（无 `.sameshlet` 时继续使用）
- 新增私有 `ComputeProgram m_meshletCullProg`

#### IRHICommandList 扩展

```cpp
virtual void DrawIndexedIndirect(
    RHIBufferHandle drawBuffer, uint64_t offset,
    uint32_t drawCount, uint32_t stride) = 0;
```

### 实施步骤

- [ ] 1. `third_party/CMakeLists.txt` — meshoptimizer（若 #55 已完成则跳过）
- [ ] 2. `tools/cook/CookedMesh.hpp` — 新增 `.sameshlet` 格式（Meshlet[], global vertex table, global triangle table）
- [ ] 3. `tools/cook/MeshImporter.cpp` — meshlet 生成 + bounds 计算 + `.sameshlet` 序列化
- [ ] 4. `src/platform/rhi/IRHIDevice.hpp` / `VulkanDevice` — `DrawIndexedIndirect` 接口 + 实现
- [ ] 5. `assets/shaders/meshlet_cull.comp` — frustum + backface cone compute culling
- [ ] 6. `assets/shaders/deferred_geometry_meshlet.vert` — 从 set=2 全局 SSBO 读顶点的 vertex shader
- [ ] 7. `src/function/renderer/SceneRenderer.hpp/.cpp` — `BuildMeshletBuffers`（上传全局 SSBOs）；`m_meshletCullProg`；`RenderFrame` meshlet 路径；双路径 fallback 判断
- [ ] 8. `editor/ui/panels/PerformancePanel.cpp` — Meshlet stats（total/visible/culled）

### 边界情况与约束

| 场景 | 处理 |
|------|------|
| 无 `.sameshlet` 的资产 | fallback 到原 `DrawItem` 路径，双路径必须稳定共存 |
| Skinned mesh | 不生成 meshlet（骨骼变形后 AABB 失效），恒走原路径 |
| Global buffer 大小 | 初版固定上限（如 4M 三角形），超出时 warn 并 fallback |
| Descriptor pool | SSBO 描述符需求增加，`VulkanDevice` 初始化 pool 时扩容 |
| set=2 与 #54 冲突 | GPU skinning 和 meshlet vertex fetch 都占 set=2；两路径互斥，`isSkinned` 和 `hasMeshlet` 不同时成立 |
| IndirectDraw GPU counter | compute 写 draw count 到 `u_visibleCount`，需额外 buffer readback 或 GPU query 统计 PerformancePanel 数据 |

**不做：** HZB 遮挡剔除（单独 issue）；Mesh Shader（需 `VK_EXT_mesh_shader`，普及度有限）；CPU 端 cluster cull fallback。

### 受益 issues

- **#23 帧率优化**：高面数 mesh 顶点处理开销降 50–80%
- **#55 LOD**：LOD1/2/3 简化 mesh 也可生成 meshlet，形成 LOD × cluster 双层剔除体系

---

## Issue #60 — Mesh Split/Merge Cook 工具

**优先级：低（按需启动；触发条件：需要在场景中独立控制 glTF 模型内部各节点的变换时）**

### 目标

独立 CLI 工具，输入 `.glb` 或 `.samesh`，输出多个 `.samesh`（split）或单个 `.samesh`（merge），以及可选的 GLB 导出（round-trip 编辑用）。工具与场景、ECS、灯光无关。

```
StellarAliaMeshTool split --input car.glb --output cook_cache/ [--by-node|--by-material]
StellarAliaMeshTool merge --inputs wheel_fl.samesh wheel_fr.samesh --output cook_cache/wheels.samesh
StellarAliaMeshTool export --input cook_cache/<uuid>.samesh --output car_cooked.glb
```

### 设计

**Split**：输入 `.glb`（多 mesh node）或 `.samesh`（多 submesh）；每个节点输出独立 `.samesh`，`localTransform = identity`（由场景/caller 负责设置）。UUID 派生规则：`DeriveNodeMeshID(glbUUID, nodeIndex)`，与 CookMesh 保持一致，stable。

**Merge**：输入多个 `.samesh`；输出单个 `.samesh`，各 submesh 保留 `localTransform`，相对位置可还原。合并后 UUID 由调用方指定（手动分配，避免与派生 UUID 冲突）。

**Export to GLB**（可选）：`samesh → GLB → [Blender 编辑] → GLB → cook → samesh` round-trip。

### 实施步骤

- [ ] `tools/mesh_tool/main.cpp` — CLI 入口（subcommand: split / merge / export）
- [ ] `tools/mesh_tool/MeshSplit.hpp/.cpp`
- [ ] `tools/mesh_tool/MeshMerge.hpp/.cpp`
- [ ] `tools/mesh_tool/MeshExportGlb.hpp/.cpp`（依赖 tinygltf write path）
- [ ] `CMakeLists.txt` 添加 `add_subdirectory(tools/mesh_tool)`

---

## Issue #61 — ShaderVariantCache（G-Buffer 宏变体系统）

**优先级：中（GPU Skinning 已通过独立 SkinnedMesh 路径实现；触发条件：alpha cutout 植物材质、profiling 显示 geometry pass 无效分支开销显著时）**

### 目标

为 G-Buffer fill 阶段（`deferred_geometry.vert/.frag`）引入 macro bitmask 变体系统，Cook 时预编译所有合法组合，运行时 `ShaderVariantCache` 按需加载对应 SPIR-V，消除 shader 内的动态分支。

### 变体位定义

| bit | 宏 | 意义 |
|-----|----|------|
| 0 | `HAS_ALBEDO_MAP` | 有 albedo 贴图，否则纯色 |
| 1 | `HAS_NORMAL_MAP` | 有法线贴图，否则插值法线（跳过 TBN 计算）|
| 2 | `HAS_METALLIC_ROUGHNESS_MAP` | 有金属/粗糙贴图 |
| 3 | `HAS_EMISSIVE_MAP` | 有自发光贴图 |
| 4 | `HAS_SKINNING` | 骨骼蒙皮（顶点着色器）|
| 5 | `HAS_ALPHA_CUTOUT` | 镂空透明（discard）|

### 设计（方案 B：Cook 时预编译）

Cook 工具枚举合法 bitmask 组合，编译为 `deferred_geometry.<mask>.vert.spv`（和 `.frag.spv`）。运行时 `ShaderVariantCache` 持有 `map<uint32_t, ShaderProgram>`，`GetOrCreate(mask)` 按需加载。`SceneRenderer::BuildDrawList` 按实体资源状态（是否有各贴图、是否 skinned、是否 alpha cutout）计算 mask 并选择对应变体。

> **与自定义 shading model 的关系**：着色模型派发（`*.lighting.glsl` + `shading_dispatch.glsl`）作用于 deferred lighting 阶段，与本 issue 正交，互不替代。

### 实施步骤

- [ ] 定义 `ShaderFeatureMask` 枚举（`src/function/renderer/ShaderVariantCache.hpp`）
- [ ] Cook shader 工具枚举变体，输出 `deferred_geometry.<mask>.vert.spv` / `.frag.spv`
- [ ] 实现 `ShaderVariantCache`：`map<uint32_t, ShaderProgram>` + `GetOrCreate(mask)`
- [ ] `BuildDrawList` 按实体资源计算 mask，`DrawItem` 携带对应变体引用
- [ ] GBufferFeature::AddPasses 使用 DrawItem 内的变体 ShaderProgram 替代固定 program

---

## Issue #62 — EditorContext：统一依赖注入容器 ✅ DONE
<!-- 新建 editor/EditorContext.hpp，提取 EditorMode::BuildContext()，全部 11 个面板构造函数统一改为 explicit Panel(EditorContext& ctx)，删除所有 SetXxx 初始化 setter；AssetsPanel::SetProjectDir 改名为 UpdateProjectDir -->

---

## Issue #63 — EditorSelection：集中化选择状态 ✅ DONE
<!-- 新建 EditorSelection（entity/asset 统一选区 + 订阅通知），SceneHierarchyPanel/AssetsPanel 双写，InspectorPanel 移除跨面板指针改为读 EditorSelection，EditorMode PIE stop 时调用 Clear() -->

---

## Issue #64 — ComponentDrawers 拆分与注册化 ✅ DONE
<!-- ComponentDrawers.hpp 拆为 editor/ui/drawers/ 下 14 个 drawer 对文件 + ComponentDrawerRegistry；IComponentDrawer::TryDraw 加 EditorContext& 参数；EditorMode::BuildContext 显式注册并设 ctx.drawerRegistry；InspectorPanel 改为委托 DrawAll -->

---

## Issue #65 — Panel MVP 重构：Presenter 层 ✅ DONE
<!-- 为 7 个核心面板（SceneHierarchy/Assets/Playback/WorldSettings/PostProcess/Shortcuts/ProjectBrowser）新增 Presenter 类，所有引擎写操作从 OnDraw 迁移至 Presenter::Update()；EditorMode::BuildContext 统一创建并注册，OnUpdate 逐帧驱动 -->

---

## Issue #66 — EditorAction 系统：统一命令派发 + 快捷键 + Undo/Redo ✅ DONE
<!-- EditorActionRegistry + CommandManager 实现；OnUpdate if-chain 替换为 PollAndDispatch；DeleteEntity/Rename/Reparent/Transform/CreateMesh 五类可撤销命令；Edit 菜单 Undo/Redo；Play 边界哨兵防穿越；SaveScene 延迟 NFD + AssetsPanel MarkFilePaneDirty + 已删除文件检测；AssetsPresenter::RequestNFDImport(destDir) 导入目录提示；AssetsPanel DnD 空白区域分左右窗格目标；右键新建 Scene 从模板拷贝。 -->

---

## Issue #67 — PIE（Play In Editor）：内存快照与双场景隔离 ✅ DONE
<!-- Application 在 Editing→Playing 时用 SceneSerializer::SerializeToJson 快照编辑器场景并创建游戏副本 m_gameScene；GetActiveScene() 路由所有系统（脚本/物理/动画/渲染）到当前活跃场景；ScriptSystem::OnPlayStart(Scene&) 将 g_ctx.scene 重定向到游戏副本；Stop 时销毁副本，编辑器场景始终未被修改，无需还原。 -->

---

## Issue #68 — ComponentSchema：声明式组件字段元数据

**优先级：中**（#64 ComponentDrawerRegistry 的"内容层"；吸收 #19 骨骼 picker 的 Editor 部分）

### 目标

引入声明式 `ComponentSchema`，将"某组件有哪些可编辑字段、字段类型是什么、如何渲染"从 C++ 代码逻辑中分离出来，以元数据驱动 `GenericComponentDrawer` 自动生成 ImGui 控件。目标：

1. 内置组件（`TransformComponent`、`MeshComponent`、`SkinnedMeshComponent` 等）各有一份静态 schema，注册到 `ComponentSchemaRegistry`。
2. `GenericComponentDrawer` 读取 schema，自动为每个字段渲染对应控件（slider、color picker、asset picker 等），无需手写 ImGui 代码。
3. 骨骼 picker（原 #19 Section F）作为 `SkinnedMeshComponent.skeletonAsset` 的 `AssetRef` 字段在 schema 中声明，无需特殊 case。
4. 自定义/游戏组件可在运行时调用 `ComponentSchemaRegistry::Register` 追加 schema，无需修改引擎代码。

### 设计

#### A. FieldType 枚举与 FieldDef

```cpp
// editor/schema/ComponentSchema.hpp

enum class FieldType : uint8_t {
    Bool,
    Int32, SliderInt,          // SliderInt 需要 SliderMeta
    Float, SliderFloat,        // SliderFloat 需要 SliderMeta
    Vec2, Vec3, Vec4,
    Quat,                      // 渲染为 Euler 角（degrees），内部转换
    Color3, Color4,            // ImGui::ColorEdit3/4
    String,
    AssetRef,                  // 需要 AssetRefMeta{extension_filter}
    Enum,                      // 需要 EnumMeta{values[]}
    ReadOnly,                  // 只读文本显示，任意 POD（显示为 hex/decimal）
};

struct AssetRefMeta { std::string ext; };        // e.g. "saskel", "samat"
struct EnumMeta    { std::vector<std::string> values; };
struct SliderMeta  { float min = 0.f, max = 1.f; };
using FieldMeta = std::variant<std::monostate, AssetRefMeta, EnumMeta, SliderMeta>;

struct FieldDef {
    std::string name;          // 序列化键名（snake_case，与 SceneSerializer 一致）
    std::string label;         // ImGui 显示标签
    FieldType   type;
    FieldMeta   meta;          // std::monostate = 无附加元数据
    std::string tooltip;       // 悬停提示，可为空
    size_t      offset;        // offsetof(ComponentType, field)
    size_t      size;          // sizeof(field)，ReadOnly 用
};
```

#### B. ComponentSchema 与 ComponentSchemaRegistry

```cpp
// editor/schema/ComponentSchema.hpp（续）

struct ComponentSchema {
    std::string typeName;      // 与 entt reflect 键名一致，e.g. "TransformComponent"
    std::string label;         // Inspector 分组标签，e.g. "Transform"
    std::string category;      // "Add Component" 菜单分组，e.g. "Core"

    std::vector<FieldDef> fields;

    // 组件存在性 / 增删（编辑器 "Add Component" 按钮驱动）
    std::function<bool(entt::registry&, entt::entity)>            hasComp;
    std::function<void*(entt::registry&, entt::entity)>           getCompPtr;  // 返回组件裸指针，字段读写用 offset
    std::function<void(entt::registry&, entt::entity, Scene&)>    addComp;
    std::function<void(entt::registry&, entt::entity)>            removeComp;
};

// editor/schema/ComponentSchemaRegistry.hpp
class ComponentSchemaRegistry {
public:
    void Register(ComponentSchema schema);
    const ComponentSchema* FindByTypeName(std::string_view typeName) const;
    const std::vector<ComponentSchema>& GetAll() const;

private:
    std::vector<ComponentSchema>                      m_schemas;
    std::unordered_map<std::string, size_t>           m_index;   // typeName → index
};
```

#### C. BuiltinSchemas — 静态注册

```cpp
// editor/schema/schemas/BuiltinSchemas.cpp

void RegisterBuiltinSchemas(ComponentSchemaRegistry& reg) {

    // TransformComponent
    reg.Register({
        .typeName = "TransformComponent",
        .label    = "Transform",
        .category = "Core",
        .fields   = {
            { "position", "Position", FieldType::Vec3,  {}, "", offsetof(TransformComponent, position), sizeof(glm::vec3) },
            { "rotation", "Rotation", FieldType::Quat,  {}, "", offsetof(TransformComponent, rotation), sizeof(glm::quat) },
            { "scale",    "Scale",    FieldType::Vec3,  {}, "", offsetof(TransformComponent, scale),    sizeof(glm::vec3) },
        },
        .hasComp    = [](auto& r, auto e) { return r.template any_of<TransformComponent>(e); },
        .getCompPtr = [](auto& r, auto e) -> void* { return &r.template get<TransformComponent>(e); },
        .addComp    = [](auto& r, auto e, auto&) { r.template emplace_or_replace<TransformComponent>(e); },
        .removeComp = [](auto& r, auto e) { r.template remove<TransformComponent>(e); },
    });

    // SkinnedMeshComponent — 骨骼 picker（原 #19 Section F）
    reg.Register({
        .typeName = "SkinnedMeshComponent",
        .label    = "Skinned Mesh",
        .category = "Rendering",
        .fields   = {
            { "meshAsset",     "Mesh",     FieldType::AssetRef, AssetRefMeta{"samesh"}, "", offsetof(SkinnedMeshComponent, meshAsset),     sizeof(AssetID) },
            { "skeletonAsset", "Skeleton", FieldType::AssetRef, AssetRefMeta{"saskel"}, "显式骨骼覆盖；空 = 从 mesh 文件头或 DeriveSkinID 推断", offsetof(SkinnedMeshComponent, skeletonAsset), sizeof(AssetID) },
        },
        // hasComp / getCompPtr / addComp / removeComp ...
    });

    // ... MeshComponent, LightComponent, CameraComponent 等
}
```

#### D. GenericComponentDrawer

```cpp
// editor/ui/drawers/GenericComponentDrawer.hpp

class GenericComponentDrawer : public IComponentDrawer {
public:
    explicit GenericComponentDrawer(const ComponentSchema& schema);

    bool Accepts(entt::registry& reg, entt::entity e) const override;
    void Draw(entt::registry& reg, entt::entity e,
              Scene& scene, const EditorContext& ctx) const override;

private:
    void DrawField(void* compBase, const FieldDef& field,
                   const EditorContext& ctx) const;

    const ComponentSchema& m_schema;
};
```

`Draw` 遍历 `m_schema.fields`，对每个 `FieldDef` 调用 `DrawField`。`DrawField` 按 `FieldType` switch：
- `Vec3` → `ImGui::DragFloat3`
- `Quat` → 转 Euler，`ImGui::DragFloat3`，写回时转 Quat
- `AssetRef` → 显示 UUID 短显 + "Pick" 按钮，打开 modal 过滤 `AssetRefMeta.ext`
- `SliderFloat` → `ImGui::SliderFloat(min, max)`
- `Enum` → `ImGui::Combo`
- `Color3/4` → `ImGui::ColorEdit3/4`
- `ReadOnly` → `ImGui::Text`（格式化为十进制或 hex）

#### E. EditorContext 扩展

```cpp
// editor/EditorContext.hpp（在 #62 基础上追加一个字段）
struct EditorContext {
    // ... 现有字段 ...
    ComponentSchemaRegistry* schemaReg = nullptr;   // NEW
};
```

#### F. 与 #64 ComponentDrawerRegistry 的关系

```
ComponentSchemaRegistry  ──(提供 schema)──►  ComponentDrawerRegistry
                                              │
                            ┌─────────────────┤
                            │  GenericDrawer   │  手写 IComponentDrawer
                            │  (schema-driven) │  (复杂组件保留手写)
                            └────────────────►│
                                              ▼
                                         Inspector OnDraw
```

注册策略（在 `EditorMode::OnAttach` 或等价入口）：
```cpp
RegisterBuiltinSchemas(*m_ctx.schemaReg);

// 对每个有 schema 的组件注册 GenericDrawer
for (auto& schema : m_ctx.schemaReg->GetAll()) {
    m_ctx.componentReg->Register(
        std::make_unique<GenericComponentDrawer>(schema));
}

// 复杂组件手写覆盖（优先级更高，ComponentDrawerRegistry 先匹配手写）
m_ctx.componentReg->Register(std::make_unique<AnimatorComponentDrawer>());
```

#### G. 文件结构

```
editor/schema/
  ComponentSchema.hpp                    — FieldType, FieldDef, ComponentSchema, ComponentSchemaRegistry
  ComponentSchemaRegistry.cpp            — Register / FindByTypeName / GetAll
  schemas/
    BuiltinSchemas.hpp                   — void RegisterBuiltinSchemas(ComponentSchemaRegistry&)
    BuiltinSchemas.cpp                   — TransformComponent, MeshComponent, SkinnedMeshComponent, ...

editor/ui/drawers/
  GenericComponentDrawer.hpp/.cpp        — schema-driven IComponentDrawer 实现
```

### 实施步骤

**— 阶段 1：数据结构（零 ImGui 依赖，可单元测试）—**

- [ ] **Step 1** — 新建 `editor/schema/ComponentSchema.hpp`：定义 `FieldType`、`FieldDef`、`FieldMeta`、`ComponentSchema`；`ComponentSchemaRegistry` 类声明
  - 验证：`ComponentSchema.hpp` 单独 include 无编译错误；`FieldDef` 字段完整
- [ ] **Step 2** — 新建 `editor/schema/ComponentSchemaRegistry.cpp`：实现 `Register`、`FindByTypeName`、`GetAll`
  - 验证：单测：Register 3 个 schema → GetAll 返回 3 个；FindByTypeName("TransformComponent") 返回非 null

**— 阶段 2：内置 Schema 注册 —**

- [ ] **Step 3** — 新建 `editor/schema/schemas/BuiltinSchemas.hpp/.cpp`：为 `TransformComponent`、`MeshComponent`、`DirectionalLightComponent`、`PointLightComponent`、`CameraComponent` 写 schema
  - 验证：`RegisterBuiltinSchemas` 后 `GetAll().size() == 5`；每个 schema 的 `fields` 非空；`offsetof` 值合理（< sizeof(Component)）
- [ ] **Step 4** — `SkinnedMeshComponent` schema：`meshAsset`（`AssetRef{"samesh"}`）+ `skeletonAsset`（`AssetRef{"saskel"}`）
  - 验证：`FindByTypeName("SkinnedMeshComponent")` 返回 schema；fields[1].name == "skeletonAsset"

**— 阶段 3：GenericComponentDrawer —**

- [ ] **Step 5** — 新建 `editor/ui/drawers/GenericComponentDrawer.hpp/.cpp`：实现 `Accepts` + `Draw` + `DrawField`；支持 Vec3、Quat（Euler 转换）、Float、SliderFloat、Bool、Color3、String
  - 验证：在 Inspector 中用 TransformComponent 的 GenericDrawer 替换手写抽屉，Position/Rotation/Scale 控件正常显示和编辑
- [ ] **Step 6** — `DrawField` 支持 `AssetRef`：显示 UUID 短字符串 + "Pick" 按钮；点击后弹出 modal 列表，过滤 `AssetRefMeta.ext`；选中后写入字段
  - 验证：`SkinnedMeshComponent` Inspector 显示 Skeleton 字段；可通过 Pick 选择 `.saskel` 资产；选中后 `skeletonAsset` UUID 变更

**— 阶段 4：EditorContext 集成 —**

- [ ] **Step 7** — `EditorContext` 追加 `schemaReg` 字段；`EditorMode::OnAttach` 创建 `ComponentSchemaRegistry`，调用 `RegisterBuiltinSchemas`，为每个 schema 注册 `GenericComponentDrawer`
  - 验证：`EditorMode` 启动后 Inspector 中 TransformComponent 由 GenericDrawer 渲染，行为与之前手写抽屉一致
- [ ] **Step 8** — 将 `ComponentDescriptor`（原 `InspectorPanel` 的 "Add Component" 数据）迁移到由 schema 驱动（`ComponentSchema::addComp` 替代 `ComponentDescriptor::addComp`）
  - 验证："Add Component" 弹出菜单列出所有已注册 schema 的组件，点击后组件被正确 emplace

### 边界情况与约束

| 场景 | 处理 |
|------|------|
| 手写 Drawer 优先级 | `ComponentDrawerRegistry` 先查手写注册，再 fallback 到 `GenericDrawer`（按注册顺序）；`AnimatorComponentDrawer` 等复杂组件不受影响 |
| `offsetof` 非 POD | C++17 起 `offsetof` 对 standard-layout struct 合法；`TransformComponent` / `MeshComponent` 均为 standard-layout；非标准布局组件（含虚函数）需手写 Drawer |
| Quat → Euler 精度 | `glm::eulerAngles` 在 gimbal lock 附近不稳定；引入 `m_cachedEuler` per-entity 缓存（仅 Inspector 生命周期内），避免每帧反算 |
| AssetRef modal 性能 | 每次打开 modal 调用 `AssetRegistry::EntriesByType(ext)` — registry 不大时可接受；>1000 资产时加搜索框 |
| schema 序列化 | schema 本身不序列化到文件；`FieldDef.name` 须与 `SceneSerializer` 的 JSON 键名对齐，保证 Inspector 修改可被正确保存/加载 |
| 运行时 schema 注册 | `ComponentSchemaRegistry::Register` 非线程安全；应在 `OnAttach`（主线程单次初始化）时调用 |
| **不做** | 嵌套组件 schema（`FieldType::Struct`）— 第一版只支持 POD/AssetID/枚举字段；属性动画曲线编辑 — 独立 issue；schema 热重载 |

### 受益 Issues

- **#19（运行时部分）**：骨骼 picker 在 schema 中实现后，#19 的 `skeletonAsset` 编辑器支持完全解决，运行时三级解析逻辑可独立实施
- **#64 ComponentDrawerRegistry**：GenericDrawer 作为 schema 驱动的通用 Drawer，与手写 Drawer 并存，减少未来新组件的 Drawer 编写成本
- **#65 Panel MVP**：Inspector Presenter 无需枚举组件类型，只需调用 `schemaReg->GetAll()` 统一驱动，Presenter 代码更薄
- **#29 脚本组件**：游戏脚本可调用 `ComponentSchemaRegistry::Register` 在 Editor 中暴露自定义字段，无需修改引擎 Inspector 代码

---

## Phase 4 roadmap（UI 重构全局）

| Phase | Issues | 核心产出 |
|-------|--------|---------|
| Phase 1 | #62 EditorContext | 依赖注入容器；消除 setter 链 |
| Phase 2 | #63 EditorSelection | 解耦跨面板选择；InspectorPanel 不再依赖 SceneHierarchyPanel |
| Phase 3 | #64 ComponentDrawerRegistry | Drawer 动态注册；InspectorPanel.OnDraw() 瘦身 |
| Phase 4 | #65 Panel MVP | Presenter 分层；OnDraw() 只含 ImGui 调用 |
| Phase 5 | #66 EditorAction + CommandManager | 快捷键执行层；Undo/Redo；play boundary |
| Phase 6 | #67 PIE | 场景内存快照；编辑器/游戏场景隔离 |
| Phase 7 | #68 ComponentSchema | 声明式字段元数据；GenericDrawer；骨骼 picker |

---

## Issue #56 — Script 热编译（Editor 内无需重启即可重新编译脚本）

**优先级：中（依赖 #29 已完成）**

### 目标

Editor 处于非 Playing 状态时，`.cs` 文件保存后自动（或手动触发）重新编译，并将诊断结果显示在 Inspector 中；Playing 状态下支持手动触发热重载（Unload → Compile → Instantiate），无需退出/重进 Play。

### 设计

**非 Playing — 后台编译（仅语法检查/诊断，不 Instantiate）**
- `ScriptSystem` 添加 `RecompileAll(paths[])` 方法，只跑 Roslyn 编译、不执行 Instantiate，将 Diagnostic 存入 `m_compileErrors`
- `AssetsPanel`：`.cs` 文件右键菜单新增 "Recompile Scripts"，调用 `Application::GetScriptSystem().RecompileAll(...)`
- Inspector 的 `ScriptDrawer` 显示最新 `CompileErrors()`

**Playing — ALC 热重载**
- `ScriptSystem::HotReload(reg)` = `InvokeAll(OnStop) → InvokeAll(OnDetach) → m_fnUnload() → Compile → Instantiate → InvokeAll(OnAttach) → InvokeAll(OnStart)`
- `WeakReference<ALC>` 等待旧 ALC GC 回收后再加载新程序集（超时 2s 打警告继续）
- 字段值不保留（全量重建实例）；字段序列化保留属于 Phase 2

**文件监听（可选 Phase 2）**
- 平台 API：Windows `ReadDirectoryChangesW` / Linux `inotify`，轮询间隔 200ms
- 检测到 `.cs` 变化 → 非 Playing 自动触发 `RecompileAll`

### 边界

- 热重载期间不暂停帧循环（最多一帧卡顿）
- 编译失败时保持当前运行状态不变，仅更新诊断
- 不支持添加/删除 ScriptComponent 后的热重载（需重新 OnPlayStart）

---

---

## Issue #58 — 日志分层路由：Script 消息自动出现在 Diagnostics tab ✅ DONE
<!-- "script" 命名 spdlog logger + LogEntry::loggerName + ConsolePanelPresenter（Drain/路由/状态）+ ConsolePanel 纯 View；移除 ScriptSystem::CompileErrors() 死代码 -->

### 边界情况与约束

- `ScriptLogger()` 懒初始化时复制 sinks 快照；EditorLogCapture 必须在首次 `SA_Log_*` 调用前已注入。Play 模式下成立：EditorMode 构造时即加入 sink，Play 开始才触发 `SA_Log_*`。
- 不修改 EditorDiagnostics 用于 ShaderCook/Material/Scene 的路径——该路径提供 `assetPath` 链接，script logger 无法提供此功能。
- `m_scriptEntries` 无持久化（Unload 时可选 `ClearScript()`），与 Engine Logs 行为一致。
- #56 热编译 issue 中规划的 `ScriptDrawer` 显示编译错误依赖 `m_compileErrors`；删除前需确认 #56 是否改为读取 Diagnostics tab 内容。

### 受益 issues

- **#29**（脚本系统）：`Debug.Log` 正式可用，不再埋没于 Engine Logs 噪音中
- **#56**（热编译）：编译错误自动出现在 Diagnostics，Inspector 仍可读 `m_compileErrors`（或改读 script logger）

---

## Issue #69 — Script Runtime Library（脚本运行时库扩展）

**优先级：中（依赖 #29 已完成）**

### 目标

将 `StellarAlia.Runtime` 从薄包装层提升为功能完整的游戏脚本 API 库，覆盖 Mathf 数学工具、输入边沿检测、实体生命周期（Create/Destroy）、物理读写（Velocity/AddForce）、物理射线查询（Raycast）、灯光控制六个维度，使常见游戏逻辑可完全在脚本中实现而无需修改引擎 C++ 代码。

### 架构现状与参照

**现状**：`ScriptApiFunctionTable` 是一个平铺 C 结构体，C++ 与 C# 两侧依赖相同字段顺序。好处是直接函数指针调用、零额外开销、无需共享库；弱点是每增加一个 API 须同步修改五处（C++ 实现、C++ 表字段、C# 表字段、NativeApi 绑定、公开类）。

**Unity**：`MonoBehaviour` API 通过 icall / P/Invoke `__Internal` 绑定，用户看到的是干净的 `UnityEngine.dll` 公开 API，底层 marshalling 完全隐藏。关键 API 层次：Mathf、GameObject.Instantiate/Destroy、GetComponent<T>、Input.GetKeyDown/Up、Physics.Raycast、Rigidbody.AddForce。

**UE**：C++ Actor/Component 树，蓝图通过宏反射访问同一套对象，没有额外 marshalling 层。核心理念：用户操作的是 AActor/UActorComponent 实例，Get/SetActorLocation 直接修改世界状态。

**StellarAlia 结论**：函数指针表机制本身合理，保持。改进方向：① 加 `version` 字段以快速捕捉两侧不同步；② 纯 managed 改动优先（不需 C++ recompile）；③ 字符串编码从 Latin-1 统一改为 UTF-8。分多个 phase 增量扩展，每 phase 独立可验证。

### 设计

#### 文件布局

```
managed/StellarAlia.Runtime/
├─ ScriptBase.cs         ← 生命周期（不变）
├─ Entity.cs             ← 新增 Destroy / static Create / Forward·Right·Up 属性
├─ Input.cs              ← 新增 GetKeyJustPressed / GetKeyJustReleased（managed 帧状态）
├─ Mathf.cs              ← 新增：Lerp·Clamp·Clamp01·PingPong·SmoothStep·Approximately
├─ Physics.cs            ← 新增：Raycast → RaycastHit
├─ RigidBodyProxy.cs     ← 扩展：Velocity·AddForce·AddImpulse
├─ LightProxy.cs         ← 新增：PointLight/Directional intensity·color
├─ Debug.cs / Time.cs    ← 不变
└─ NativeApi.cs          ← 新增函数指针槽（version 字段首位）

src/function/script/
├─ ScriptApiExports.hpp  ← version 字段 + 新槽声明
└─ ScriptApiExports.cpp  ← 新 extern "C" 函数实现
```

#### C++ 函数表扩展（version 字段首位，旧字段位置不动）

```cpp
struct ScriptApiFunctionTable {
    uint32_t version = 2;                       // ← 新增，首位
    // ── 原有字段（Block 1）— 位置不变 ─────────
    void    (*Entity_GetPosition)    (uint64_t, float*, float*, float*);
    // ... (所有现有字段) ...
    float   (*Time_GetTotalTime)     ();
    // ── Block 2 — v2 新增 ──────────────────────
    void     (*Entity_Destroy)        (uint64_t id);
    uint64_t (*Entity_Create)         (const char* name, float x, float y, float z);
    void     (*RigidBody_GetVelocity) (uint64_t id, float*, float*, float*);
    void     (*RigidBody_SetVelocity) (uint64_t id, float, float, float);
    void     (*RigidBody_AddForce)    (uint64_t id, float, float, float);
    void     (*RigidBody_AddImpulse)  (uint64_t id, float, float, float);
    int32_t  (*Physics_Raycast)       (float ox, float oy, float oz,
                                       float dx, float dy, float dz, float maxDist,
                                       float* hitX, float* hitY, float* hitZ,
                                       uint64_t* hitEntity);
    void     (*Light_GetColor)        (uint64_t id, float*, float*, float*);
    void     (*Light_SetColor)        (uint64_t id, float, float, float);
    float    (*Light_GetIntensity)    (uint64_t id);
    void     (*Light_SetIntensity)    (uint64_t id, float);
};
```

#### C# Input 帧状态追踪（纯 managed，无 C++ 改动）

```csharp
// Input.cs — 新增字段（ScriptBridgeEntry.InvokeLifecycle 开头调 Input.BeginFrame()）
internal static void BeginFrame() { /* swap prev/curr, sample all pressed keys */ }
public static bool IsKeyJustPressed (Key k) => _curr.Contains(k) && !_prev.Contains(k);
public static bool IsKeyJustReleased(Key k) => !_curr.Contains(k) && _prev.Contains(k);
```

#### C# RaycastHit

```csharp
public readonly struct RaycastHit {
    public readonly Vector3 Point;
    public readonly Entity  Entity;
    public readonly bool    Hit;
}
// Physics.cs
public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance = 1000f);
```

#### Entity 方向向量（纯 managed，依赖 System.Numerics）

```csharp
// Entity.cs
public Quaternion GetRotation() { /* 通过 GetRotationEuler 反算，或新增 GetRotationQuat slot */ }
public Vector3 Forward => Vector3.Transform(-Vector3.UnitZ, GetRotationQuat());
public Vector3 Right   => Vector3.Transform( Vector3.UnitX, GetRotationQuat());
public Vector3 Up      => Vector3.Transform( Vector3.UnitY, GetRotationQuat());
```
> 需要在 NativeApi 增加一个 `Entity_GetRotationQuat(id, w*, x*, y*, z*)` 槽，避免 Euler 往返引入万向节锁噪声。

### 实施步骤

- [ ] **Step 1 — Mathf 工具类**（纯 managed，`managed/StellarAlia.Runtime/Mathf.cs`）
  - `Lerp(a,b,t)`、`Clamp(v,min,max)`、`Clamp01`、`PingPong(t,len)`、`SmoothStep(a,b,t)`、`Approximately(a,b,eps=1e-5f)`、`MoveTowards(cur,target,maxDelta)`
  - 包装 `MathF.*`，不依赖任何 NativeApi 槽

- [ ] **Step 2 — 字符串编码从 Latin-1 改为 UTF-8**（`NativeApi.cs` 全局替换）
  - 将所有 `Encoding.Latin1.GetBytes(str + '\0')` 替换为 `Encoding.UTF8.GetBytes(str + '\0')`
  - C++ 侧已是 UTF-8 字符串，无需修改
  - 验证：实体名含中文/日文时 FindByName 仍可正确匹配

- [ ] **Step 3 — Input 帧状态（GetKeyJustPressed/Released）**（纯 managed）
  - `Input.cs` 添加 `static HashSet<Key> _prev, _curr`
  - `internal static void BeginFrame()` 中 swap 两集合，重新采样 `IsKeyDown(k)` 填入 `_curr`
  - `ScriptBridgeEntry.InvokeLifecycle` 在 `method == OnUpdate` 之前调用 `Input.BeginFrame()`（或统一在帧开始处）
  - 暴露 `IsKeyJustPressed(Key)` / `IsKeyJustReleased(Key)`

- [ ] **Step 4 — version 字段 + Entity_GetRotationQuat + Entity Forward/Right/Up**
  - `ScriptApiFunctionTable` 首字段加 `uint32_t version = 2`（注意：C# 侧首字段同步加 `uint version`）
  - 新增 `SA_Entity_GetRotationQuat(id, w*, x*, y*, z*)` C++ 实现 + 表槽 + C# 绑定
  - `Entity.cs` 添加 `GetRotationQuat()` 私有方法，以及 `Forward`、`Right`、`Up` 属性

- [ ] **Step 5 — Entity 生命周期：Destroy / Create**
  - C++：`SA_Entity_Destroy(id)` — `g_ctx.scene->Registry().destroy(entity)`；`SA_Entity_Create(name, x, y, z)` — `EntityFactory::SpawnEmpty(scene, name, {x,y,z})`
  - C# 侧：`Entity.Destroy()` 实例方法；`Entity.Create(string name, Vector3 pos)` 静态工厂
  - 边界：非 Play 期间（`g_ctx.scene == nullptr`）返回 invalid entity / no-op
  - 验证：脚本中 `Entity.Create("Bullet", pos)` → Stop 后层级面板无残留（因 Play 在游戏副本）

- [ ] **Step 6 — RigidBodyProxy 扩展：Velocity / AddForce / AddImpulse**
  - C++：通过 Jolt `PhysicsSystem::GetBodyInterface()` 读写速度、施力（需 PhysicsSystem 暴露 `GetBodyInterface()` 或新增 helper）
  - `SA_RigidBody_GetVelocity` / `SetVelocity` / `AddForce` / `AddImpulse`
  - `Entity.cs` 添加 `GetRigidBody()` → `RigidBodyProxy`（现有 GetAnimator 模式）
  - `RigidBodyProxy.cs` 添加 `Velocity`（get/set Vector3）、`AddForce(Vector3, ForceMode)`、`AddImpulse(Vector3)`

- [ ] **Step 7 — LightProxy：PointLight intensity / color**
  - C++：`SA_Light_GetColor` / `SetColor` / `GetIntensity` / `SetIntensity`，通过 `try_get<PointLightComponent>` 操作
  - `Entity.cs` 添加 `GetPointLight()` → `LightProxy`
  - `LightProxy.cs`：`Color`（Vector3 get/set）、`Intensity`（float get/set）

- [ ] **Step 8 — Physics.Raycast**
  - C++：`SA_Physics_Raycast(...)` — 调用 Jolt `NarrowPhaseQuery::CastRay`；需 PhysicsSystem 暴露查询接口（pimpl 内添加 `bool Raycast(Ray, float, RaycastHit&)`）
  - C# 侧：`Physics.Raycast(Vector3 origin, Vector3 dir, out RaycastHit hit, float maxDist)` 静态方法
  - 边界：仅 Play 期间有效（Jolt 在 Editing 时已 Reset）；非 Play 返回 false

### 边界情况与约束

| 约束 | 说明 |
|------|------|
| `version` 字段位置 | 必须是结构体第一个字段（C# StructLayout.Sequential 依赖顺序），旧字段位置不可变 |
| Entity_Create 场景上下文 | 非 Play 期间 `g_ctx.scene == nullptr`，直接返回 `~0ull`（invalid），不崩溃 |
| Physics.Raycast Play-only | Jolt 在 `PhysicsSystem::Reset` 后无 body，Raycast 返回 false；非 Play 不暴露 crash 风险 |
| LightProxy 仅 PointLight | DirectionalLight / SpotLight / AreaLight 暂不支持（类型区分逻辑留后） |
| Input.BeginFrame 调用时机 | 必须在每帧第一个 `InvokeLifecycle(OnUpdate)` 前调用，否则首帧 JustPressed 可能漏帧 |
| AddForce / AddImpulse | Jolt 要求在 Step 外调用（FixedUpdate 之前或之后），不可在 Step 内并发修改 body |
| 不做：GetComponent<T>() 泛型 | 需要 C# 反射 + 引擎侧类型注册，复杂度高，属 #68 ComponentSchema 范围 |
| 不做：Coroutine / Invoke(delay) | 需托管调度器，留 Phase 2 独立 issue |

### 受益 issues

- **#33**（贝塞尔曲线与相机移动）：Entity.Forward / Raycast / Mathf.Lerp 是脚本驱动相机的基础
- **#56**（热编译）：Runtime API 稳定后热重载不会因 API 签名变更失效
- **#29**（脚本系统）：`BouncingRotator.cs` 之类示例可升级为展示 Raycast + Physics 的完整游戏原型

---

### UI Bug Issues
长双击重命名目前无法起效
asset panel重命名缩略图会消失
目前没有在asset panel空白处交互（右键创建文件， 点击选择等）的功能
视角操作会影响ui
~~场景继承树根节点拖拽排序无效~~ ✅ 已修：Scene::m_rootOrder 维护根节点用户定序，SceneHierarchyPresenter DnD 分支调用 MoveRootBefore/After，SceneHierarchyPanel 和 SceneSerializer 改用 GetRootOrder() 迭代