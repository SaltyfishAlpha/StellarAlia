## 待办issue

24. **[低优先级] 长耗时操作进度反馈**
    - **启动进度条**（难度高）：`OnAttach` 在渲染循环前同步执行，ImGui 无法渲染。需重构为两阶段延迟初始化或独立 splash screen 渲染通道，暂不做。
    - **Reimport All 进度条**（难度中，最有实际价值）：`ReimportDir` 同步阻塞 UI。改法：将其拆成逐帧 N 个文件的状态机，`OnDraw` 期间推进并用 `ImGui::ProgressBar` + modal 显示；或移入工作线程 + 原子进度计数器。
    - 前置条件：依赖 `AssetsPanel` 暴露异步迭代接口；待项目素材量增大后再做。
27. 美化编辑器：骨骼改用球+锥绘制而不是线
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

## Issue #56 — Script 热编译（Editor 内无需重启即可重新编译脚本）✅

**状态：已完成**  
**优先级：中（依赖 #29 已完成）**

### 目标

Editing 状态下，主窗口**聚焦**时若检测到 `.cs` 文件变化则立即后台编译（Roslyn only，不 Instantiate），诊断路由至 Diagnostics tab；主窗口**失焦**期间积压变更标记，待窗口重获焦点时立即触发；Playing / Paused 状态下不自动重编。

### 设计

#### 1. `FileWatcher`（新建）

```
src/platform/io/FileWatcher.hpp
src/platform/io/FileWatcher.cpp
```

```cpp
class FileWatcher {
public:
    void Watch(const std::filesystem::path& dir); // 启动后台监听线程
    void Stop();                                   // 析构时自动调用
    void PollChanges(std::vector<std::filesystem::path>& out); // 主线程每帧轮询
private:
    std::thread              m_thread;
    std::mutex               m_mutex;
    std::vector<std::filesystem::path> m_pending;
    std::atomic<bool>        m_running{false};
    // Windows: ReadDirectoryChangesW 循环写入 m_pending
};
```

- Windows 实现：`ReadDirectoryChangesW`（`FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME`），监听目录递归；后台线程持续 append 到 `m_pending`，`PollChanges` 加锁 swap 到 `out`
- Linux 留接口存根，等移植期加 `inotify`

#### 2. `IWindow::IsFocused()` + GLFWWindow focus callback

```cpp
// IWindow.hpp
virtual bool IsFocused() const = 0;

// GLFWWindow.hpp
bool m_focused = true;   // 初始视为已聚焦，避免首帧漏过

// GLFWWindow.cpp — Create() 中追加：
glfwSetWindowFocusCallback(handle, OnWindowFocus);

// 静态回调：
static void OnWindowFocus(GLFWwindow* w, int focused) {
    static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w))->m_focused = (focused != 0);
}
bool GLFWWindow::IsFocused() const { return m_focused; }
```

#### 3. `ScriptSystem::RecompileEditing(entt::registry& reg)`

- 复用 `OnPlayStart` 中的路径收集 + `m_fnCompile` 调用
- 不调用 `m_fnInstantiate`（此时 ALC 未加载，直接编译出诊断即可）
- 编译 Diagnostic 通过 `ScriptLogger()` spdlog 通道输出（#58 已完成，自动路由 Diagnostics tab）
- 返回 `bool`（成功/失败）；`m_playing` 必须为 `false`，否则 early-return

#### 4. EditorMode 集成

新增成员：
```cpp
Platform::FileWatcher m_scriptWatcher;
bool                  m_pendingRecompile = false;
```

流程：
```
LoadProject() 时:
    m_scriptWatcher.Stop();
    m_scriptWatcher.Watch(projectDir / "assets");   // 递归

OnUpdate() 帧末:
    vector<path> changed;
    m_scriptWatcher.PollChanges(changed);
    for each path: if extension == ".cs" → m_pendingRecompile = true

    if m_pendingRecompile
       && m_app->GetWindow()->IsFocused()
       && m_app->GetPlayState() == EnginePlayState::Editing:
        m_app->GetScriptSystem().RecompileEditing(scene.Registry())
        m_pendingRecompile = false

OnPlayStateChanged(Playing/Paused):
    m_pendingRecompile = false   // 进入 Play 时清空，不干扰运行时

OnPlayStateChanged(Editing):     // Stop Play → 回 Edit
    if m_pendingRecompile && m_app->GetWindow()->IsFocused():
        立即重编（可直接在此调用 RecompileEditing）
```

#### 关系图

```
FileWatcher bg-thread
    ReadDirectoryChangesW → m_pending (mutex)
                                ↓ PollChanges() each frame
                         EditorMode::OnUpdate
                                ↓ .cs 变化?
                         m_pendingRecompile = true
                                ↓ IsFocused() + Editing?
                         ScriptSystem::RecompileEditing()
                                ↓
                         spdlog "script" logger
                                ↓
                         Diagnostics tab (#58 路由已完成)
```

### 实施步骤

1. **`IWindow` / `GLFWWindow`**：添加 `IsFocused()` 纯虚函数 + `m_focused` 字段 + `glfwSetWindowFocusCallback`（~10 行）
2. **新建 `FileWatcher`**：`src/platform/io/FileWatcher.hpp` + `.cpp`，Windows `ReadDirectoryChangesW` 后台线程 + 线程安全轮询
3. **CMakeLists**：将 `FileWatcher.cpp` 加入 engine/platform target
4. **`ScriptSystem::RecompileEditing(reg)`**：收集路径 → `m_fnCompile` → log diagnostics，无 Instantiate；`ScriptSystem.hpp` 声明为 `public`
5. **`EditorMode`**：添加 `m_scriptWatcher` + `m_pendingRecompile`；`LoadProject()` 中启动监听；`OnUpdate()` 末尾轮询 + 焦点判断触发重编；`OnPlayStateChanged` 中清空 / 触发 pending
6. **验证**：① 聚焦时改 `.cs` → 自动编译，Diagnostics 出现结果；② 失焦时改 `.cs` → 重新聚焦才编译；③ Playing 时改 → 不触发

### 边界情况与约束

| 约束 | 说明 |
|------|------|
| 监听范围 | `projectDir / "assets"` 递归，仅过滤 `.cs`；非 `.cs` 变化直接丢弃 |
| 项目未加载 | Project Browser 选择前不启动 FileWatcher；`LoadProject()` 是唯一启动点 |
| 编译同步阻塞 | Roslyn 在主线程同步执行，单次编译约 100–500ms；当前帧轻微卡顿可接受，异步化属 Phase 2 |
| Play 时变更 | `m_pendingRecompile` 继续积累，Stop Play 回 Editing 后若 pending 且聚焦则立即重编 |
| 不做 Linux | FileWatcher Windows 实现；Linux 接口存根，等移植期填充 `inotify` |
| 不做 Playing 热重载 | ALC Unload → Reload 序列属原规划 Phase 2，本 issue 不涉及 |
| 不做字段保留 | 重编不保留脚本字段值，Phase 2 做序列化 |
| `.cs` 删除/重命名 | 视作变更触发重编（编译会因文件消失失败，Diagnostic 正常输出错误）|

### 受益 issues

- **#58**（Diagnostics 路由）：编译错误自动出现在 Diagnostics tab，无需额外 UI
- **#69**（Runtime API 扩展）：API 稳定后自动重编不因签名变更静默失效

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
    int32_t  (*Entity_GetRotationQuat) (uint64_t id, float* w, float* x, float* y, float* z);
    void     (*Entity_SetRotationQuat) (uint64_t id, float w, float x, float y, float z);
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
public Quaternion GetRotation() { /* Entity_GetRotationQuat slot — 直读 glm::quat，无精度损耗 */ }
public void       SetRotation(Quaternion q) { /* Entity_SetRotationQuat slot */ }
public Vector3 Forward => Vector3.Transform(-Vector3.UnitZ, GetRotation());
public Vector3 Right   => Vector3.Transform( Vector3.UnitX, GetRotation());
public Vector3 Up      => Vector3.Transform( Vector3.UnitY, GetRotation());
```
> `GetRotationQuat` / `SetRotationQuat` 直接读写 `glm::quat`（WXYZ），避免 Euler 往返引入万向节锁噪声；Euler 接口保留作便捷重载。

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

- [ ] **Step 4 — version 字段 + 四元数旋转 API + Entity 方向向量**
  - `ScriptApiFunctionTable` 首字段加 `uint32_t version = 2`（C# 侧同步加 `uint version`）
  - C++：新增 `SA_Entity_GetRotationQuat(id, w*, x*, y*, z*)` 和 `SA_Entity_SetRotationQuat(id, w, x, y, z)`，直接读写 `TransformComponent::rotation`（`glm::quat`），无 Euler 往返损耗
  - C# `NativeApi.cs`：新增对应两个函数指针槽绑定
  - `Entity.cs` 公开 `Quaternion GetRotation()` / `void SetRotation(Quaternion)`；`Forward`、`Right`、`Up` 只读属性（通过 `GetRotation()` 推导，不再依赖 Euler 反算）；保留 `GetRotationEuler`/`SetRotationEuler` 用于 Inspector 兼容
  - 新建 `managed/StellarAlia.Runtime/QuaternionExt.cs`（纯 managed）：
    - `QuaternionExt.LookRotation(Vector3 forward, Vector3 up)` — 从前向量构造朝向
    - `QuaternionExt.Slerp(Quaternion a, Quaternion b, float t)` — 包装 `Quaternion.Slerp`
    - `QuaternionExt.FromEuler(float x, float y, float z)` — 角度制 Euler → Quat（消除用户手写 `MathF.PI/180` 的需要）
    - `QuaternionExt.ToEuler(Quaternion q)` — Quat → 角度制 Vector3，供调试用

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

- [ ] **Step 9 — 外部 IDE 支持（XML 文档 + 项目模板 + .csproj / .sln / .gitignore 生成）**

  **文件职责一览**（三类文件共存，不互相替代）：

  | 文件 | 职责 | 纳入版本控制？ |
  |------|------|--------------|
  | `{Name}.saproject` | 引擎项目描述符（startupScene、name、version），由编辑器读取 | ✓ 是 |
  | `{Name}.csproj` | IDE C# 补全，含机器绝对路径 HintPath，由编辑器生成 | ✗ gitignore |
  | `{Name}.sln` | Visual Studio Solution，引用 `.csproj`，供 VS/Rider 双击打开 | ✗ gitignore |
  | `.gitignore` | 排除 IDE 生成文件和 cook cache | ✓ 是 |

  **关于扩展名**：`.csproj` 和 `.sln` 是 IDE 识别 C# 项目的标准格式，不可替换为自定义扩展名（如 `.scproj`），否则 Rider / VS Code / Visual Studio 无法提供 IntelliSense。

  **关于版本控制**：Unity 的 `.csproj`/`.sln` 本身**提交到 git**，因为它们用 MSBuild 变量引用 UnityEngine.dll 而非硬编码绝对路径，内容可跨机器复现。StellarAlia 采用同样策略：`.csproj`/`.sln` 提交，把机器相关的绝对路径单独放进 `Directory.Build.props`，只有这一个文件被 gitignore。

  ---

  **子任务 9a — Runtime XML 文档**
  - `managed/StellarAlia.Runtime/StellarAlia.Runtime.csproj` 添加 `<GenerateDocumentationFile>true</GenerateDocumentationFile>`
  - 对所有公开 API 补充 `/// <summary>` 注释（`ScriptBase`、`Entity`、`Input`、`Mathf`、`Debug`、`Time`、`AnimatorProxy`、`RigidBodyProxy`、`Physics`、`QuaternionExt`）
  - 构建产物 `StellarAlia.Runtime.xml` 随 `StellarAlia.Runtime.dll` 输出到 `bin/managed/`；IDE 自动读取同目录 XML 显示 tooltip

  ---

  **子任务 9b — 项目目录模板（新项目创建时）**

  `assets/templates/project/` 存放静态模板（随引擎分发，无机器相关路径）：

  ```
  assets/templates/project/
  └─ .gitignore.template
  ```

  新建项目时（`ProjectBrowserPanel` "New Project" 流程）生成的完整结构：

  ```
  {ProjectName}/
  ├─ assets/
  │   ├─ scenes/
  │   │   └─ default.sascene        (从 templates/scenes/ 复制)
  │   ├─ scripts/                   (空目录)
  │   └─ materials/                 (空目录)
  ├─ {ProjectName}.saproject        ✓ 版本控制 — 引擎描述符
  ├─ {ProjectName}.csproj           ✓ 版本控制 — 用变量引用 Runtime.dll
  ├─ {ProjectName}.sln              ✓ 版本控制 — VS Solution
  ├─ Directory.Build.props          ✗ gitignore — 仅含机器绝对路径
  └─ .gitignore                     ✓ 版本控制 — 从模板生成
  ```

  `.gitignore.template` 内容（只排除机器相关文件，不排除 `.csproj`/`.sln`）：

  ```gitignore
  # Machine-specific: auto-regenerated by the editor on project open
  Directory.Build.props

  # IDE intermediate files
  .vs/
  .idea/
  obj/

  # Cook cache
  cook_cache/

  # OS artifacts
  .DS_Store
  Thumbs.db
  ```

  实现：`.gitignore` 只在不存在时写入（不覆盖用户修改）；`.saproject` 写入 `{"name":"{stem}","version":1,"startupScene":"assets/scenes/default.sascene"}`。

  ---

  **子任务 9c — `{ProjectName}.csproj`、`{ProjectName}.sln`、`Directory.Build.props` 生成**

  **`Directory.Build.props`**（gitignore，机器相关，每次打开项目重写）：

  ```xml
  <!-- Auto-generated by StellarAlia Editor — gitignored, do not commit -->
  <Project>
    <PropertyGroup>
      <StellarAliaManaged>{managedDir_绝对路径}</StellarAliaManaged>
    </PropertyGroup>
  </Project>
  ```

  **`{ProjectName}.csproj`**（提交到 git，使用变量，跨机器内容一致）：

  ```xml
  <!-- IDE project file — commit to version control -->
  <Project Sdk="Microsoft.NET.Sdk">
    <PropertyGroup>
      <TargetFramework>net8.0</TargetFramework>
      <Nullable>enable</Nullable>
      <ImplicitUsings>disable</ImplicitUsings>
      <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
      <IsPackable>false</IsPackable>
    </PropertyGroup>
    <ItemGroup>
      <Reference Include="StellarAlia.Runtime">
        <HintPath>$(StellarAliaManaged)/StellarAlia.Runtime.dll</HintPath>
      </Reference>
    </ItemGroup>
    <ItemGroup>
      <Compile Include="assets/scripts/**/*.cs" />
    </ItemGroup>
  </Project>
  ```

  > MSBuild 会自动在当前目录及所有父目录搜索 `Directory.Build.props`，无需在 `.csproj` 中显式引用。

  **`{ProjectName}.sln`**（提交到 git，无路径，跨机器内容一致）：

  ```
  Microsoft Visual Studio Solution File, Format Version 12.00
  # Visual Studio Version 17
  Project("{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}") = "{stem}", "{stem}.csproj", "{GUID}"
  EndProject
  Global
    GlobalSection(SolutionConfigurationPlatforms) = preSolution
      Debug|Any CPU = Debug|Any CPU
    EndGlobalSection
    GlobalSection(ProjectConfigurationPlatforms) = postSolution
      {GUID}.Debug|Any CPU.ActiveCfg = Debug|Any CPU
    EndGlobalSection
  EndGlobal
  ```

  > 类型 GUID `{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}` 是 C# MSBuild 项目固定值；实例 GUID 由 `{stem}` SHA-1 UUID v5 确定性生成（同名项目内容完全相同）。

  **C++ 实现**（`Application::UpdateProjectPaths` 末尾）：
  - 私有方法 `GenerateIdeProjectFiles(projectDir, managedDir)`
  - `Directory.Build.props`：始终重写（内含绝对路径，每次打开须刷新）
  - `{stem}.csproj`：仅在不存在时写入（内容与路径无关，用户可安全修改）
  - `{stem}.sln`：仅在不存在时写入（同上）
  - 输出 UTF-8 无 BOM，换行 `\n`

  **首次打开克隆仓库的完整序列**（git 过滤后缺失 `Directory.Build.props` 和 `cook_cache/`）：

  ```
  1. 编辑器启动（projectDir 为空，无项目加载）
  2. ProjectBrowserPanel — 用户选择 {Name}.saproject
  3. EditorMode 读取 .saproject → 取 name、startupScene
  4. Application::UpdateProjectPaths(projectDir, cookCacheDir)
       ├─ a. m_scriptSystem.SetProjectDir(projectDir)
       ├─ b. fs::create_directories(cookCacheDir)          ← cook_cache/ 补建
       ├─ c. m_resMgr / m_renderer 更新 cook cache 路径
       └─ d. GenerateIdeProjectFiles(projectDir, managedDir)
                ├─ 写 Directory.Build.props（始终）        ← gitignore 的文件在此补建
                ├─ 跳过 {stem}.csproj（git 中已有）
                └─ 跳过 {stem}.sln（git 中已有）
  5. EditorMode 加载 startupScene
  6. IDE（Rider/VS）此时刷新项目 → 读到 Directory.Build.props → IntelliSense 可用
  ```

  步骤 4d 是本 step 新增的唯一内容；步骤 4a-c 是现有代码，无需修改。

  ---

  **子任务 9d — 发布场景（各阶段文件清单）**

  | 文件 | 版本控制 | 引擎分发包 | 游戏导出包 |
  |------|---------|-----------|-----------|
  | `{Name}.saproject` | ✓ | 不含（项目方拥有）| 不含 |
  | `{Name}.csproj` / `.sln` | ✓ | 不含 | 不含 |
  | `Directory.Build.props` | ✗ gitignore | 不含，打开时重生成 | 不含 |
  | `StellarAlia.Runtime.dll` + `.xml` | — | ✓ | ✓ |
  | `StellarAlia.ScriptBridge.dll` | — | ✓ | ✓ |
  | `.cs` 源码 | ✓ | 不含 | 不含（见 #70）|
  | `GameScripts.dll` | 不含 | 不含 | ✓（见 #70）|

  **引擎分发**：`bin/managed/` 随引擎可执行打包；`.gitignore.template` 随 `assets/templates/project/` 打包。

  **游戏导出**：AOT 预编译脚本 → `GameScripts.dll`，详见 **Issue #70**。

  ---

  **参照对比**：

  | | Unity | UE | StellarAlia |
  |---|---|---|---|
  | 生成时机 | Asset 变化 / 手动 Regenerate | 手动 "Generate project files" | 项目加载时自动 |
  | 文件格式 | `Assembly-CSharp.csproj` + `.sln` | `.vcxproj` + `.sln` | `{Name}.csproj` + `{Name}.sln` |
  | 路径隔离方案 | Unity 内部变量（不暴露给用户）| `$(UE_ROOT)` 环境变量 | `Directory.Build.props`（gitignore）|
  | `.csproj`/`.sln` 提交？ | ✓（无绝对路径）| ✓（无绝对路径）| ✓（无绝对路径）|
  | 机器特定文件 | 无 | `.vs/` 等 IDE 文件 | `Directory.Build.props` |
  | 项目描述符 | `ProjectSettings/*.asset` | `{Name}.uproject` | `{Name}.saproject` |
  | 文档来源 | `UnityEngine.xml` 随 SDK 分发 | 头文件注释 | `StellarAlia.Runtime.xml` 随引擎 |

  验证：克隆仓库到新机器，编辑器打开项目后 `Directory.Build.props` 自动生成；Rider / Visual Studio 打开 `{ProjectName}.sln`，输入 `Entity.` 弹出补全，Hover 显示 `<summary>`；`git status` 不显示 `Directory.Build.props`（已 gitignore），`.csproj`/`.sln` 已提交。

### 边界情况与约束

| 约束 | 说明 |
|------|------|
| `version` 字段位置 | 必须是结构体第一个字段（C# StructLayout.Sequential 依赖顺序），旧字段位置不可变 |
| `SetRotationQuat` 与 `SetRotationEuler` 并存 | 两者都写 `TransformComponent::rotation`，用户选其一即可；`GetRotationEuler` 内部调 `glm::eulerAngles`，仍有万向节问题，文档注明不应在帧循环内往返转换 |
| `.csproj`/`.sln` 仅供 IDE，不参与引擎编译 | Roslyn 编译路径不读这两个文件；`<Compile>` 与 Roslyn 的源文件列表独立维护 |
| `.csproj` 扩展名不可更改 | Rider / VS Code / Visual Studio 均需标准 `.csproj`；自定义扩展名需专用插件，超出范围 |
| `Directory.Build.props` 每次打开项目重写 | 含绝对路径，必须随 `managedDir` 刷新；`.csproj`/`.sln` 本身无绝对路径，提交后跨机器内容一致 |
| `.gitignore` 只在文件不存在时生成 | 不覆盖用户已有的 `.gitignore`，避免丢失自定义规则 |
| `EnableDefaultCompileItems=false` | 防止 SDK-style csproj 把引擎源目录的 `.cs` 纳入；IDE Build 可能报错但 IntelliSense 不受影响 |
| 游戏导出 AOT 编译 | 超出 #69 范围，见 Issue #70 |
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

## Issue #70 — 游戏发布（GameMode + 脚本 AOT 预编译 + 打包导出）

**优先级：低（依赖 #29、#69 完成；AppMode 架构已就绪）**

### 目标

支持将 StellarAlia 项目导出为独立可运行的游戏包：引擎以 `GameMode`（`AppMode` 子类）启动，无 Editor UI，自动从 `.saproject` 加载启动场景并立即进入 Playing 状态；脚本预编译为 `GameScripts.dll`（不携带 Roslyn 和源码）；打包产物可单独分发给终端玩家。

### 现状

`AppMode` 接口已设计为可插拔（`OnAttach / OnDetach / OnUpdate / GetCameraData / OnRenderUI / OnPlayStateChanged`），`Application` 通过 `std::unique_ptr<AppMode>` 持有当前 mode。目前唯一实现是 `EditorMode`。`GameMode` 的实现代价低——核心逻辑只需去掉编辑器 UI，直接 PlayStart。

### 设计

#### 文件布局

```
src/game/
├─ GameMode.hpp
└─ GameMode.cpp

tools/export/
└─ ProjectExporter.hpp / .cpp   ← Editor 调用，产生游戏包目录
```

#### GameMode

```cpp
class GameMode : public AppMode {
public:
    void OnAttach(Application& app) override;   // 读 .saproject → LoadScene → SetPlayState(Playing)
    void OnDetach()                 override;
    void OnUpdate(float dt)         override;   // 仅转发给 Application 帧循环，无 ImGui
    CameraData GetCameraData(float aspect) const override;  // 取最高优先级 CameraComponent
    // OnRenderUI 不 override — 默认空实现，无 Editor 面板
};
```

启动入口：

```cpp
// main_game.cpp（与 main_editor.cpp 并列）
int main() {
    Application app(std::make_unique<GameMode>());
    Application::Desc desc;
    desc.projectDir = /* 从命令行 / 打包目录读取 */;
    // 不含 Editor 相关路径
    if (!app.Init(desc)) return 1;
    app.Run();
    app.Shutdown();
}
```

#### 脚本 AOT 预编译

当前问题：`ScriptSystem::OnPlayStart` 在运行时调用 Roslyn 编译 `.cs`，游戏包若带 Roslyn DLL + 源码则体积大、启动慢、源码外露。

解决方案：导出时预编译 → 运行时直接加载 `.dll`。

```
导出流程：
  Editor "Export Game" →
    ProjectExporter::Export(projectDir, outputDir)
      ├─ 1. Roslyn 编译所有 .cs → GameScripts.dll（复用 ScriptCompiler）
      ├─ 2. 复制 assets/ → output/assets/
      ├─ 3. 复制 Runtime.dll + ScriptBridge.dll → output/bin/managed/
      ├─ 4. 复制 GameScripts.dll → output/bin/managed/
      ├─ 5. 复制游戏可执行文件（StellarAliaGame.exe）→ output/
      ├─ 6. 写入 output/{Name}.saproject（包含预编译标志）
      └─ 7. 不复制 .cs 源码、Roslyn DLL、Editor 资源

运行时流程（GameMode + ScriptSystem）：
  OnPlayStart → 检测到 GameScripts.dll 存在 → 跳过 Compile，直接 ScriptLoader::Load(dll路径)
```

`ScriptSystem` 改动：

```cpp
// ScriptSystem.hpp 新增
void SetPrecompiledAssembly(const std::string& dllPath);  // 导出包模式

// OnPlayStart 内
if (!m_precompiledDll.empty()) {
    // 直接加载 DLL，跳过 Roslyn 编译
    m_fnLoadPrecompiled(m_precompiledDll.c_str());
} else {
    // 原有 Roslyn 编译路径
}
```

`ScriptBridgeEntry` 新增入口点 `LoadPrecompiled(void* pathPtr)` — 从路径直接 `LoadFromAssemblyPath`，不走 Roslyn。

#### 游戏包目录结构

```
output/{ProjectName}/
├─ StellarAliaGame.exe         ← 游戏可执行（GameMode）
├─ {ProjectName}.saproject     ← 项目描述符
├─ assets/                     ← 游戏资源
│   ├─ scenes/
│   └─ ...
├─ bin/
│   ├─ managed/
│   │   ├─ StellarAlia.Runtime.dll
│   │   ├─ StellarAlia.ScriptBridge.dll
│   │   └─ GameScripts.dll        ← 预编译脚本
│   ├─ vulkan/                    ← Vulkan 运行时 DLL（Windows）
│   └─ dotnet/                    ← .NET 8 Runtime（可选：依赖外部安装）
└─ shaders/                    ← 编译好的 .spv
```

### 实施步骤

- [ ] **Step 1 — GameMode 骨架**
  - 新建 `src/game/GameMode.hpp/.cpp`，实现 `AppMode` 接口
  - `OnAttach`：读 `{projectDir}/{name}.saproject` → 取 `startupScene` → `Application::LoadScene` → `SetPlayState(Playing)`
  - `GetCameraData`：遍历 `CameraComponent` 取最高 `priority`（与 Editor 行为一致）
  - `OnRenderUI`：空（不 override），无 ImGui

- [ ] **Step 2 — 独立游戏入口**
  - `main_game.cpp`（CMake 新增 `StellarAliaGame` 可执行目标）
  - `Application::Desc` 从命令行参数或打包目录内固定路径读取 `projectDir`
  - `CMakeLists.txt`：`StellarAliaGame` 链接 `StellarAliaRuntime`，不链接 Editor 相关代码

- [ ] **Step 3 — ScriptSystem 预编译模式**
  - `ScriptSystem` 新增 `SetPrecompiledAssembly(path)`
  - `OnPlayStart` 分支：有预编译路径时调 `m_fnLoadPrecompiled`，否则走原 Roslyn 路径
  - `ScriptBridgeEntry.LoadPrecompiled`：从文件路径 `LoadFromAssemblyPath` 并执行 `Instantiate` 流程

- [ ] **Step 4 — ProjectExporter**
  - `tools/export/ProjectExporter.hpp/.cpp`
  - `Export(projectDir, outputDir, managedDir)`：编译脚本 → 复制资源 → 复制 DLL → 写 saproject
  - Editor UI：`AssetsPanel` 或菜单栏 "File → Export Game"

- [ ] **Step 5 — Editor 导出 UI**
  - 导出对话框：选择输出目录 → 触发 `ProjectExporter::Export` → 完成提示
  - 错误处理：脚本编译失败时显示 Diagnostics 并中止导出

### 边界情况与约束

| 约束 | 说明 |
|------|------|
| GameMode 无 PIE 隔离 | GameMode 不存在编辑器场景副本，直接在主场景上 PlayStart；Stop 概念不存在（窗口关闭即结束）|
| .NET Runtime 依赖 | 游戏包可选择"依赖外部 .NET 8 安装"或"自包含"（`dotnet publish --self-contained`）；自包含包体约 +60MB |
| Vulkan 运行时 | Windows 下 `vulkan-1.dll` 通常已由驱动安装；Linux 需 `libvulkan.so.1`；导出时可选择打包 |
| 导出不支持热重载 | 预编译 DLL 不走 CollectibleALC 卸载流程（无需重载），`#56 热编译` 功能在 GameMode 下禁用 |
| saproject startupScene | `GameMode::OnAttach` 若找不到该场景文件则报错退出，不降级为空场景 |
| 不做：IL2CPP / AOT native | .NET 8 NativeAOT 可行但复杂度高，留更远期 issue |

### 受益 issues

- **#29**（脚本系统）：预编译模式复用 `ScriptCompiler`，代码路径共享
- **#69**（脚本库）：`GameMode` 是验证 Runtime API 在无 Editor 环境下工作正常的最终测试

---

### UI Bug Issues
~~长双击重命名目前无法起效~~ ✅ 已修：AssetsPanel 引入 DoubleClickClassifier，长按触发重命名，短按保留原导航/打开行为
~~asset panel重命名缩略图会消失~~ ✅ 已修
~~目前没有在asset panel空白处交互（右键创建文件， 点击选择等）的功能~~ ✅ 已修：右键空白弹出 Create 菜单，左键空白清除选中
~~视角操作会影响ui~~ ✅ 已修：RMB mouselook 激活时将 ImGui::GetIO().MousePos 置为 (-FLT_MAX,-FLT_MAX)，消除面板悬停残留
~~场景继承树根节点拖拽排序无效~~ ✅ 已修：Scene::m_rootOrder 维护根节点用户定序，SceneHierarchyPresenter DnD 分支调用 MoveRootBefore/After，SceneHierarchyPanel 和 SceneSerializer 改用 GetRootOrder() 迭代