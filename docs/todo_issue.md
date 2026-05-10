## 待办issue

24. **[低优先级] 长耗时操作进度反馈**
    - **启动进度条**（难度高）：`OnAttach` 在渲染循环前同步执行，ImGui 无法渲染。需重构为两阶段延迟初始化或独立 splash screen 渲染通道，暂不做。
    - **Reimport All 进度条**（难度中，最有实际价值）：`ReimportDir` 同步阻塞 UI。改法：将其拆成逐帧 N 个文件的状态机，`OnDraw` 期间推进并用 `ImGui::ProgressBar` + modal 显示；或移入工作线程 + 原子进度计数器。
    - 前置条件：依赖 `AssetsPanel` 暴露异步迭代接口；待项目素材量增大后再做。
27. 美化编辑器：骨骼改用球+锥绘制而不是线
28. 美化编辑器：灯光、相机等不可视物体添加icon贴图作为标识，素材由我提供;添加引擎logo，注册.saproject的图标为引擎logo；为各种引擎素材添加图标；asset panel可以显示文件对应图标并调节显示大小；
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

## Issue #19 — Phase 3 backlog（低优先级）

- `SkinnedMeshComponent.skeletonAsset` picker（独立指定骨骼资产）
- 多 mesh 共用同一骨骼

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
