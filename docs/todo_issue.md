# StellarAlia Issue 文档

> **最近一次整理**：2026-06-25
> **状态来源**：源码核验（详见底部"已完成 issue 索引"段）

---

## 完成情况总览

| 状态 | 数量 | 列表 |
|---|---|---|
| ✅ 已完成 | 38 | #29 #45 #46 #47 **#48** #58 #62-#67 #69 #72 #73 #74 #75 **#77** #78 #81 #82 #84 #85 **#86** **#88** **#89** **#90** **#91** **#92** **#94** #56b（旧#71热编译）#71 Phase 2 #71 Phase 3a/3b + Vulkan D/E/F/G/H + Cross-dir Vtx Shader + UI Bug 5 项 |
| ❌ 未完成（带设计） | 15 | #36 #49 #55 #56 #57 #60 #61 #68 #70 #79 #80 #83 **#87** **#93** **#95** |
| ❌ 未完成（短条目） | 11 | X-1 ~ X-8 + #54 + #73-A + #23 帧率优化伞 |

**关键指标**：`ScriptApiFunctionTable::version = 7` — 表明脚本 API 已经过 v2→v3（Phase 2）→v4→v5（Phase 3a InputMap）→v6（#81 Transform 重命名）→v7（#47 PostProcess）共五轮扩展。

---

## 待办issue（按优先级分组）

> **图例**：本节为 issue 概览索引；带详细设计的 issue 在下方独立小节展开（搜索 `## Issue #N`）。
> 顶部"待办issue"段曾有 #23 / #24 编号重复，已重排为唯一编号；原序号在末尾括注，便于追溯。

### 🔴 中优先级 — 渲染与帧率
- **#23 帧率优化（伞 issue）** — 子任务覆盖 #55 LOD / #56 Stencil / #57 Meshlet / #61 ShaderVariant
- **#55 LOD 系统**（详细设计见下）
- **#61 ShaderVariantCache**（详细设计见下）
- **#68 ComponentSchema**（详细设计见下）

### 🔴 高优先级 — 动画系统（启动时机：#46-#49 后）
- **#83 Skinning 工业级补全（伞 issue / 7 phase roadmap）**（详细设计见下）— 总工程量 ~9.5 周；启动前置：#46 Phase 1 done + 后处理系列稳定

### 🟡 低优先级 — 渲染/工具
- **#36 RHI VkMemory 级别别名**（极低，依赖 #16）
- **#48 SSR**（依赖 #42 TAA）✅ DONE — Phase 1（朴素线性步进）
- **#89 SSR Phase 2：Hi-Z 步进 + 空间去噪** ✅ DONE
- **#95 SSR Phase D：resolve 参数 UI + profiling**（#89 收尾拆出）
- **#93 ScreenEffect LDR / Tonemap-compute**（#91 分出；@Out ldr 跨缓冲 + Tonemap→compute C1/C2/C3 待定）
- **#49 Volumetric Fog**（极低）
- **#56 Stencil Masking for Deferred Lighting**（详细设计见下）
- **#57 GPU-Driven Meshlet Cluster Culling**（详细设计见下）
- **#60 Mesh Split/Merge Cook 工具**（详细设计见下）
- **#70 GameMode + 项目导出**（详细设计见下）

### 🟡 中优先级 — 脚本系统延伸
- **#80 Script Field 复合类型（List<T> + 嵌套 struct）**（依赖 #75，详细设计见下）

### 🟢 极低优先级 / cleanup
- **#79 `ScriptSystem::CaptureFieldValues` 接口空挂**

### 📋 杂项短条目（暂未独立 issue 编号；标 X-* 临时编号去重）

- **X-1 长耗时操作进度反馈**（低优先级，原序号 #24-a）
  - 启动进度条（高难度）：`OnAttach` 在渲染循环前同步执行，ImGui 无法渲染；需重构为两阶段延迟初始化或独立 splash screen 通道
  - Reimport All 进度条（中难度，最有实际价值）：`ReimportDir` 同步阻塞 UI；改法：拆成逐帧 N 个文件的状态机 + ProgressBar modal，或工作线程 + 原子进度计数器
  - 前置：`AssetsPanel` 暴露异步迭代接口
- **X-2 骨骼可视化美化**（原序号 #27）：骨骼改用球+锥绘制取代线
- **X-3 Animator 编辑器 imguizmo**（原序号 #31）
- **X-4 材质可视化编程 imguizmo**（原序号 #32）
- **X-5 贝塞尔曲线相机移动**（原序号 #33；依赖 #71 Phase 2 脚本侧 InputAction）
- **X-6 透明材质面片（植物等）**（原序号 #24-b）
- **X-7 程序化天空盒 + 物体作为光源方向**（原序号 #23-b）
- **X-8 调试渲染**（原序号 #22）：面片 id / lod / 随机 / depth 着色
- **#54 Reimport 后内存缓存未失效**（低优先级，待复现）— `ReimportDir` 完成后未调 `ClearProjectAssets()` 导致 GPU mesh handle 残留旧数据；曾在 BoomBox.glb 观察到面片破碎，无法稳定复现。修法：reimport 完成触发 `ResourceManager::ClearProjectAssets()`
- **#73-A `.saglsl` shading model 迁移到 SSBO+bindless 路径**（低优先级）— #72 后内置 PBR 走 SSBO+bindless 零 per-entity desc set 分配，但 `.saglsl` shading models cook 出 `*.gbuffer.frag` 仍声明 `set=2 uniform MaterialParams`（UBO 路径），`MaterialOverrideComponent` 走 `CloneInstance` legacy 路径。改法：更新 `ShaderCookLib` / `NewShader.saglsl` 模板把声明改成 `std430 readonly buffer MaterialParams` + `t_*_Idx` uint 索引，reflection 触发 `usesMaterialParamsSSBO=true`
- **#74-A 编辑器全局 NOMINMAX** ✅ DONE — `editor/CMakeLists.txt` 对 `StellarAliaEditor` 加 `if(WIN32) target_compile_definitions(... PRIVATE NOMINMAX)`，TU 不再需逐文件 `#define NOMINMAX` 防 Windows.h 宏污染 `numeric_limits::max()`

> 注：原序号 #73 / #74 与下方独立 issue 编号 #73 / #74（Script Inspector 三连）冲突；为避免歧义此处加 -A 后缀。下方"Issue #73 Script Inspector 前置"是已完成的另一 issue。

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

## Issue #46 — Motion Blur（Camera Mode, Phase 1）✅ DONE
<!-- handles.velocity (RG16F viewport) 升格为公开 RG 接口 — Phase 2 接管点；MotionBlurFeature 插在 DoF 之后/Tonemap 之前，4 fullscreen pass：velocity (depth + prevViewProj 推导) → TileMax 16× → NeighborMax 3×3 dilate → McGuire 2012 reconstruct (jittered samples + soft depth + cone weight)，原地 redirect handles.hdr 仿 DoF Composite；strength/maxSpeed 仅在 reconstruct 应用，velocity buffer 保持物理 semantics 兼容 Phase 2 per-object writes；PostProcessSettings 4 字段 + SceneSerializer + PostProcessPanel UI；first-frame guard：ApplyCameraToUniforms 检测 m_prevUnjitteredViewProj == identity 时 seed prev=curr 避免 garbage velocity -->

---

## Issue #47 — 屏幕修饰效果（Vignette / Chromatic Aberration / Film Grain）✅ DONE
<!-- 新增 RGTextureHandle ldr 公开接口 + LDR_Color 瞬态（swapchain format）；Tonemap/LutTonemap 重定向到 handles.ldr；PostFXFeature 单 fullscreen pass 跑在 Tonemap 之后/SelectionOutline 之前（reverse-insert 模式），uniform-control-flow 切换 vignette/CA/film grain，全 disable 时退化为 LDR→swapchain 拷贝；PostProcessSettings +8 字段 + SceneSerializer 双向；PostProcessPanel 三个 CollapsingHeader；Script API：ScriptApiFunctionTable v6→v7，新增 16 entries + SceneRenderer* 接入 ScriptApiContext，managed PostProcess.Vignette/.ChromaticAberration/.FilmGrain Unity 风格嵌套静态类，setter 调用 ApplyWorldSettings(ws,false) 实时生效；demo_project 加 PostProcessTest.cs 验证 -->

---

## Issue #48 — SSR（屏幕空间反射）✅ DONE
<!-- 单 compute pass (ssr.comp) 跑在 DeferredLighting 后 / TAA 前：视空间线性步进 + 二分细化，命中采样 lit HDR，按与 deferred_lighting.frag 相同的 split-sum 权重替换 IBL env-probe 高光（out = hdrIn + conf*(ssrSpec − iblSpec)，occlusion 含 AO 故精确抵消、无重复计费），不改 deferred_lighting.frag；步进抖动靠 TAA 降噪。新增 FrameContext::BindStorageImage（瞬态 UAV 延迟绑定）+ 首次启用 RGPassBuilder::WriteUAV(texture)；ComputeProgram 外部 frameLayout 改占 set=1，frame 全局 sampler stage Fragment→All 供 compute 采样；PostProcessSettings +5 个 ssr* 字段 + 序列化 + PostProcessPanel UI。Phase 2 留 HiZ / glossy 反射模糊 / stochastic+spatial resolve / SSR 专属时序累积。 -->

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

## Issue #56 — 透明面片支持（Alpha-Test + Forward Blend）+ Deferred Stencil Masking

**优先级：中（原为纯 stencil 小优化；现扩展为透明面片总 issue —— 植物 alpha-test + 玻璃/水 forward blend，并整合 stencil masking）**

> **2026-07-01 扩展**：本 issue 从"Stencil Masking"升级为**透明面片支持伞 issue**（吸收短条目 X-6「透明材质面片（植物等）」）。原 stencil 设计保留为 **Part B-stencil**；新增 alpha-test（Part C）与 forward 半透明（Part D）。三者共享同一套 RHI/深度/stencil 基建，故合并规划。

### 背景与现状（源码核验，2026-07-01）

| 事实 | 位置 |
|------|------|
| `RHIBlendMode{Opaque,AlphaBlend,Additive}` 已存在，pipeline blend 已实现 | `IRHIDevice.hpp:21`、`VulkanDevice.cpp:1768` |
| glTF `alphaMode`/`alphaCutoff`/`doubleSided` 载入 `MaterialData` 后**在 cook 丢弃** | `GltfLoader.cpp:229`→`MeshData.hpp:54-56`；`tools/importer/MaterialImporter.cpp:22-65 CookMaterial` 未写出 |
| `.samatc`(JSON) 仅含 params+textures | `MaterialManager.cpp:261-349 LoadMaterial` 无 alpha 反序列化 |
| `MaterialType`/`MaterialTypeDesc` 的 `blendMode`/`cullMode` 硬编码 Opaque/Back | `MaterialType.hpp:77-82`、`MaterialManager.hpp:27-37` |
| `DrawItem` 无 alpha/blend/doubleSided 字段 | `SceneRenderer.hpp:220-245` |
| `deferred_geometry.frag` 无 discard；MaterialParams SSBO=set2、bindless globalTex=set0 | `assets/shaders/deferred_geometry.frag` |
| 纯延迟管线，**无 forward/透明 pass**；DrawItem 排序仅按 pipeline+VB（无 blend 分组） | `SceneRenderer.cpp:989` |
| `RHIPipelineDesc` 深度比较**硬编码 `LEQUAL`**，无 `depthCompareOp` 字段 | `VulkanDevice.cpp:1766` |
| 深度格式 `D32F` | `SceneRenderer.cpp:758 gbKey.depthFormat` |
| Descriptor set 约定（#72 后）：set0=bindless heap，set1=FrameUniforms+lights+IBL+LTC+shadowMatrix，set2=MaterialParams SSBO / GBuffer inputs，set3=skin | agent 核验 |
| pass 顺序：Shadow→Skybox→GBuffer→VelocityPrepass→SSAO→DeferredLighting→SSR→SelectionMask→TAA→AutoExposure→Bloom→DoF→MotionBlur→Tonemap→PostFX→… | `architecture.md §Built-in Render Features` |
| forward 透明可 **100% 复用 set0/set1**（相机/光源/阴影矩阵/IBL/LTC 全在 FrameUniforms），无需改 `FrameUniforms` | `FrameUniforms.hpp:11-36`、agent 核验 |

### 总目标

1. **Alpha-test（MASK，植物/树叶/铁丝网）**：镂空正确显示；用 **depth-prepass + GBuffer EQUAL 主通道**彻底解决 `discard` 导致的 early-Z 失效（现代引擎标准解），而非在 GBuffer frag 里裸 `discard`。
2. **Forward 半透明（BLEND，玻璃/水/粒子面片）**：新增 forward 透明 pass，per-object back-to-front 排序，复用延迟光照着色数学，alpha-blend 叠加进已点亮 HDR。
3. **Stencil masking（原 #56）**：depth-prepass / GBuffer 向 stencil 写 1，DeferredLighting 仅处理 stencil==1，背景与镂空区在固定功能阶段被拒绝。三者天然协同——prepass 写 stencil 即为 masking 数据源。

### 总体架构（新 pass 顺序）

```
Shadow → Skybox
  → [新] DepthPrepass        // opaque+mask 深度专用；写 depth + stencil=1；mask 采 albedo.a 做 discard
  → GBuffer                  // depthCompareOp=EQUAL, depthWrite=OFF；零 overdraw，无 discard
  → VelocityPrepass → SSAO
  → DeferredLighting         // stencilTest==1；背景/镂空固定功能拒绝
  → SSR → SelectionMask → TAA
  → [新] ForwardTransparent  // BLEND 物体，back-to-front，depthTest LEQUAL / depthWrite OFF，alpha blend
  → Bloom → DoF → MotionBlur → Tonemap → PostFX → …
```

关键点：`discard` 只出现在**廉价的 DepthPrepass mask frag**（一次 albedo.a 取样），昂贵的 GBuffer PBR 打包 frag 走 `depthCompareOp=EQUAL` + `early_fragment_tests`，被遮挡/被裁片元在着色前即被 early-Z 拒绝 → 从根上消除 discard 的 early-Z 失效与植被 overdraw；GBuffer frag 因此**无需 discard 变体**。

### 设计

#### Part A — 数据管线打通（alphaMode / alphaCutoff / doubleSided）

链路：`MaterialData`(已有) → `CookMaterial` 写出 → `.samatc` → `LoadMaterial` 反序列化 → `MaterialInstance` 携带 → `BuildDrawList` 分类填 DrawItem。

```cpp
// 分类枚举（新，放 src/function/material/ 或 RHITypes 附近）
enum class AlphaMode : uint8_t { Opaque = 0, Mask = 1, Blend = 2 };

// MaterialImporter.cpp CookMaterial — 追加写出
root["alphaMode"]   = mat.alphaMode;    // "OPAQUE"|"MASK"|"BLEND"
root["alphaCutoff"] = mat.alphaCutoff;
root["doubleSided"] = mat.doubleSided;

// MaterialInstance 新增运行时渲染状态（per-instance，覆盖 type 默认）
struct MaterialRenderState {
    AlphaMode alphaMode   = AlphaMode::Opaque;
    float     alphaCutoff = 0.5f;
    bool      doubleSided = false;
};
// LoadMaterial 读 JSON 填 MaterialInstance::m_renderState；
// alphaCutoff 同时进 MaterialParams SSBO（DepthPrepass mask frag discard 用）

// DrawItem 扩展（SceneRenderer.hpp:220）
AlphaMode alphaMode   = AlphaMode::Opaque;   // BuildDrawList 从 material 读
bool      doubleSided = false;
float     cameraDist  = 0.f;                 // ForwardTransparent 排序键（BLEND）
```

> `alphaCutoff` 需进 MaterialParams SSBO（尾部追加 float，保持 std430 对齐），`deferred_geometry.frag` 与新 prepass/forward frag 的 SSBO 布局同步。向后兼容：旧 `.samatc` 无 `alphaMode` 字段时默认 `"OPAQUE"`；旧 cook 需 Reimport 才带新字段（无需强制，缺失即 Opaque）。

#### Part B-rhi — RHI 扩展（depthCompareOp + 深度格式 + pipeline state key）

在原 stencil 枚举基础上，`RHIPipelineDesc` 追加深度比较函数字段：

```cpp
// IRHIDevice.hpp
RHICompareOp depthCompareOp = RHICompareOp::LessOrEqual;  // 现硬编码 LEQUAL；GBuffer EQUAL 主通道需要
```

`VulkanDevice::CreatePipeline` 把 `ds.depthCompareOp = ToVkCompareOp(desc.depthCompareOp)`（替换 `VulkanDevice.cpp:1766` 写死的 `VK_COMPARE_OP_LESS_OR_EQUAL`）。`RHICompareOp` 与下方 stencil 设计共用同一枚举。

**Pipeline state cache key 扩展**（关键）：当前 `ShaderProgram` 仅以 `AttachmentKey`(color/depth 格式) 缓存 pipeline。为支持同一 shader 的 {Opaque/Mask/Blend}×{单面/双面}×{EQUAL/LEQUAL}×{depthWrite on/off}×{stencil 配置} 多态 pipeline，需把渲染状态并入 key：

```cpp
struct PipelineStateKey {                 // 取代/扩展 AttachmentKey 作为 pipeline map key
    AttachmentKey  attachments;
    RHICullMode    cullMode;
    RHIBlendMode   blendMode;
    RHICompareOp   depthCompareOp;
    bool           depthWrite;
    uint32_t       stencilBits;           // 打包 stencilTest/Write/ref/op（见 Part B-stencil）
};
// MaterialType::GetOrCreatePipeline(device, PipelineStateKey)
// BuildDrawList 按 DrawItem.MaterialRenderState 组装 key 取对应 pipeline
```

以下为原 **stencil** 具体改动（Part B-stencil，depth 格式 `D32F → D24_S8`）：

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

#### Part C — Alpha-Test（MASK）：两档 DepthPrepassMode（对齐 UE `r.EarlyZPass`）

**不做裸 discard**：在延迟 GBuffer 里直接 `discard` 会使 early-Z 退化到 late-Z、植被 overdraw 全额付费——没有现代引擎把它作为 masked 材质的实际路径，故不设该档。现实最低档就是 **MaskedOnly prepass**（UE `r.EarlyZPass` 亦以 masked prepass 为常态）。做成 `RendererConfig` 的运行时模式枚举：

```cpp
// RendererConfig 新增
enum class DepthPrepassMode : uint8_t {
    MaskedOnly,  // 默认：仅 MASK 走预通道（根治 discard 的 early-Z 失效）；opaque 仍直进 GBuffer
    Full,        // 优化档：opaque+mask 全走预通道；GBuffer 全 EQUAL/depthWrite off，零 overdraw + stencil 统一
};
DepthPrepassMode depthPrepassMode = DepthPrepassMode::MaskedOnly;  // 默认；profiling 后可升 Full
```

两档行为对照：

| 模式 | 预通道内容 | GBuffer 深度状态 | MASK 处理 | stencil 来源 | 顶点成本 |
|------|-----------|-----------------|-----------|-------------|---------|
| **MaskedOnly**（默认，先实现） | 仅 MASK 写 depth+stencil | opaque=LEQUAL/write；MASK=EQUAL/no-write | prepass discard；GBuffer 无 discard | opaque=GBuffer 写，MASK=prepass 写 | opaque ×1，MASK ×2 |
| **Full**（后续优化） | opaque+mask 写 depth+stencil | 全 EQUAL, depthWrite off | prepass discard；GBuffer 无 discard | 全 prepass 写 | ×2 |

`DepthPrepassFeature`（仿 `ShadowFeature` depth-only + `VelocityPrepassFeature` test-only）按模式决定塞哪些 DrawItem 进预通道 draw 列表；`GBufferFeature` 按模式对 opaque/mask 分别选 `depthCompareOp`/`depthWrite`。

```
DepthPrepass（depth+stencil 附件，MaskedOnly 只画 MASK / Full 画 opaque+mask）
  frag=空(opaque, 仅 Full 档) / 采 albedo.a discard(mask)；stencil Replace=1；双面→cullMode None
→ GBuffer（同一 depth+stencil 附件；已被 prepass 写过的几何走 EQUAL/depthWrite off；否则 LEQUAL/write）
  MASK 几何 EQUAL + early_fragment_tests → 昂贵 PBR 打包 frag 只在最终可见像素运行，无 discard
```

**为何根治 early-Z**：`discard` 只在 prepass 的**一次 albedo.a 取样** frag 发生（late-Z 代价压到最低）；GBuffer 里 MASK 几何无 discard、不写深度 → early-Z EQUAL 在着色前拒绝被遮挡/被裁片元。这是 UE5 / Doom-2016 植被/masked 材质标准路径。

**着色器**：
- `depth_prepass.vert`（= `deferred_geometry.vert` 复用，输出 clip pos + uv）；skinned = `depth_prepass_skinned.vert`（复用骨骼路径 set3）
- `depth_prepass_opaque.frag`（空 `void main(){}`，仅 Full 档用）
- `depth_prepass_mask.frag`（读 set2 `t_BaseColor_Idx`+`alphaCutoff`，`if (a < cutoff) discard;`）
- `deferred_geometry.frag` **不变**（两档均无需在 GBuffer 里 discard/加 cutout 变体）

**VelocityPrepass 兼容**：其 `depthTest=true/depthWrite=false`(LEQUAL) 对已填充深度仍成立（可见片元深度==存储值），两档均无需改动。

#### Part D — Forward 半透明（BLEND）

新增 `ForwardTransparentFeature`，插在 `TAAFeature` 之后、`BloomFeature` 之前：

```cpp
class ForwardTransparentFeature final : public RenderFeature {
    // OnInit: 注册 forward_transparent 程序（ProgramCache），set0/1 复用，set2=MaterialParams SSBO，set3=skin
    // AddPasses:
    //   1. 收集 alphaMode==Blend 的 DrawItem（BuildDrawList 已分好类）
    //   2. 按 cameraDist 降序排序（back-to-front）
    //   3. 单 pass：color=当前已解析 HDR(loadOp Load), depth=opaque 深度(只读测试)
    //      pipeline: blendMode=AlphaBlend, depthCompareOp=LessOrEqual, depthWrite=OFF,
    //                cullMode= doubleSided?None:Back, stencilTest=off
    //   4. 逐 draw 绑 set0(bindless)/set1(frame)/set2(material)/[set3 skin]，DrawIndexed
    ShaderProgram* m_program = nullptr;
    ShaderProgram* m_skinnedProgram = nullptr;
};
```

**着色器 `forward_transparent.frag`**：把 `deferred_geometry.frag` 的材质取样 + `deferred_lighting.frag` 的着色数学（direct 光照循环 + ShadowFactor PCF + IBL SH 漫反射 + split-sum 镜面）合并成单一前向 frag，输出 `vec4(color, albedo.a)`。着色代码可 `#include` 抽出的公共 `.glsl`（建议把 lighting 数学从 `deferred_lighting.frag` 提取到 `pbr_shading.glsl` 供两处共享，避免复制）。顶点 = `deferred_geometry.vert` 复用。

**HDR handle 衔接**（关键集成点，仿 SSR/DoF 的 `m_outputHandle` 重定向）：透明 pass 需把半透明合成到 **TAA 已解析的不透明色**上，并让 Bloom 及后续都看到结果。实现：在 `handles.taaResolved`（=TAA 输出）上原地 `Load` 混合，然后 `handles.hdr = handles.taaResolved`；因 Bloom threshold 读 `taaResolved`、composite 读写 `hdr`，二者需指向同一合成结果——实现时按现有 feature 循环的 handle 重定向逐一核验（`SceneRenderer.cpp:1486-1500`）。

**放 TAA 之后而非之前**：透明物无速度写入（不在 VelocityPrepass），置于 TAA 前会被历史重投影拖影；置于 TAA 后符合 UE translucency 惯例。代价：透明物不参与 TAA 抗锯齿、且不写深度 → DoF/MotionBlur 对其不精确（主流引擎同样取舍，可接受）。

#### Part E — 编辑器 / 序列化

- `MaterialData`→`.samatc` 已在 Part A；`SceneSerializer` 无需改（alpha 属于材质资产非场景）。
- 编辑器材质 Inspector（若有材质编辑面板）暴露 alphaMode 下拉 + alphaCutoff slider + doubleSided 勾选；接 #68 ComponentSchema 的 Enum/Slider/Bool 字段类型。
- `PerformancePanel`：DepthPrepass / ForwardTransparent 耗时；透明 draw 数量。

### 实施步骤

**Part A — 数据管线（可独立验证：Reimport 后 .samatc 含新字段）**
- [ ] A1. `tools/importer/MaterialImporter.cpp CookMaterial` — 写出 `alphaMode`/`alphaCutoff`/`doubleSided`
- [ ] A2. `MaterialInstance`/`MaterialType` — 新增 `MaterialRenderState`（alphaMode/cutoff/doubleSided）；`AlphaMode` 枚举
- [ ] A3. `MaterialManager::LoadMaterial` — 反序列化上述字段填 `m_renderState`；`alphaCutoff` 进 MaterialParams SSBO
- [ ] A4. `MeshData.hpp`/`deferred_geometry.frag` — MaterialParams SSBO 尾部加 `float alphaCutoff`（std430 对齐）
- [ ] A5. `SceneRenderer.hpp DrawItem` — 加 `alphaMode`/`doubleSided`/`cameraDist`；`BuildDrawList` 从 material 填充

**Part B — RHI（可独立验证：现有场景在 D24_S8 + 新 pipeline key 下渲染无回归）**
- [ ] B1. `IRHIDevice.hpp` — `RHIStencilOp`/`RHICompareOp` 枚举 + `RHIStencilOpState`；`RHIPipelineDesc` 加 stencil 字段 + `depthCompareOp`
- [ ] B2. `VulkanUtils.cpp` — `ToVkImageLayout(DepthWrite)` → `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`；新增 `ToVkCompareOp`/`ToVkStencilOp`
- [ ] B3. `VulkanCommandList.cpp BeginRenderPass` — 深度附件 imageLayout 同步 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`；支持 stencil clear/load
- [ ] B4. `VulkanDevice.cpp CreatePipeline` — `ds.depthCompareOp=ToVkCompareOp(...)`（替换写死 LEQUAL）；填 stencil；`renderingCI.stencilAttachmentFormat`
- [ ] B5. `ShaderProgram` pipeline cache — `AttachmentKey` → `PipelineStateKey`（含 cull/blend/depthCompareOp/depthWrite/stencil）；`GetOrCreatePipeline` 签名更新
- [ ] B6. `SceneRenderer.cpp` — 深度纹理格式 `D32F → D24_S8`（gbKey + m_rgDepth）

**Part C — Alpha-Test / MaskedOnly 档（先实现；可独立验证：植物镂空正确 + RenderDoc 确认 MASK 几何 GBuffer 无 discard/early-Z EQUAL 生效）**
- [ ] C1. shaders — `depth_prepass.vert`(+skinned)、`depth_prepass_mask.frag`（`depth_prepass_opaque.frag` 留待 Full 档）
- [ ] C2. `DepthPrepassFeature`（OnInit 注册程序 + skinDescLayout；AddPasses 仅画 MASK，写 depth+stencil=1，双面 cullMode None）
- [ ] C3. `SceneRenderer::Init` — 在 GBuffer 之前插入 DepthPrepassFeature；`RendererConfig::depthPrepassMode` 默认 MaskedOnly
- [ ] C4. `GBufferFeature` — MASK 几何 pipeline 用 `depthCompareOp=EQUAL, depthWrite=OFF` + `early_fragment_tests`；opaque 几何仍 `LEQUAL, depthWrite on`；depth/stencil loadOp=Load（保留 prepass 写入）
- [ ] C5. `DeferredLightingFeature` — pipeline `stencilTestEnable=true, compareOp=Equal, reference=1`（原 stencil 设计）；opaque GBuffer 亦写 stencil=1
- [ ] C6. BuildDrawList — 按 alphaMode/doubleSided 选 prepass 与 GBuffer 的 pipeline 变体

**Part C-Full — Full 档（后续优化，可选；可独立验证：RenderDoc 确认 GBuffer 全场零 overdraw）**
- [ ] CF1. `depth_prepass_opaque.frag`（空 frag）；DepthPrepassFeature 在 Full 档追加 opaque draw
- [ ] CF2. `GBufferFeature` — Full 档全几何 `EQUAL/depthWrite off`，depth loadOp=Load；MaskedOnly/Full 分支切换

**Part D — Forward 半透明（可独立验证：玻璃/水面片正确混合、被不透明遮挡）**
- [ ] D1. 抽 `deferred_lighting.frag` 光照数学 → `assets/shaders/pbr_shading.glsl`（供延迟与前向共享）
- [ ] D2. shaders — `forward_transparent.vert`(复用 deferred_geometry.vert)(+skinned)、`forward_transparent.frag`
- [ ] D3. `ForwardTransparentFeature`（收集 BLEND + back-to-front 排序 + 单 pass forward 着色）
- [ ] D4. `SceneRenderer::Init` — 在 TAAFeature 之后、BloomFeature 之前插入
- [ ] D5. HDR handle 衔接 — 透明合成结果重定向 `handles.hdr`/`taaResolved`；核验 `SceneRenderer.cpp:1486-1500` 循环
- [ ] D6. BuildDrawList — BLEND 物体不进 GBuffer/prepass draw 列表，单列进透明列表

**Part E — 编辑器 / 收尾**
- [ ] E1. 材质 Inspector — alphaMode 下拉 / alphaCutoff slider / doubleSided 勾选
- [ ] E2. `PerformancePanel` — DepthPrepass / ForwardTransparent 耗时 + 透明 draw 计数
- [ ] E3. demo_project — 加植物(MASK) + 玻璃(BLEND) 验证资产
- [ ] E4. 验证（RenderDoc）：GBuffer 阶段无 discard、early-Z EQUAL 生效；lighting 背景/镂空无 fragment 调用；透明排序正确

### 边界情况与约束

| 场景 | 处理 |
|------|------|
| `D24_S8` 驱动支持 | RTX 3070 必支持；`VkFormatProperties::optimalTilingFeatures` 含 `DEPTH_STENCIL_ATTACHMENT_BIT`；否则回退 `D32F_S8` |
| `D32F → D24_S8` 精度 | D24 精度略降；Sponza 规模不明显；如需保精度用 `D32F_S8`（多占 stencil 平面显存） |
| DepthPrepass 顶点成本 | 默认 MaskedOnly：仅 MASK 几何 ×2 顶点（植被通常占比小），opaque 单遍。Full 档 opaque 亦 ×2，换 GBuffer 全场零 overdraw + 统一 stencil，profiling 后再升档 |
| GBuffer EQUAL 深度必须逐位相等 | prepass 与 GBuffer 用**同一 vert shader + 同一 jitter VP**，保证深度 bit-exact；skinned 用同一骨骼矩阵 |
| 透明物排序 | per-object 质心距离排序，穿插/自重叠仍有错误（延后 #OIT/WBOIT，见不做） |
| 透明物无深度/速度 | 不写深度 → DoF/MotionBlur 不精确；不在 VelocityPrepass → 置 TAA 后避免拖影 |
| 双面材质 | doubleSided → cullMode=None（prepass + GBuffer + forward 三处一致） |
| 自定义 shading model | 所有 GBuffer MaterialType（PBR + 自定义）pipeline 均需 EQUAL/depthWrite off + stencil；prepass 用统一 depth-only 程序 |
| ShadowFeature | shadow pipeline 无颜色/stencil 附件，无需改 |
| 向后兼容 | `RHIPipelineDesc` 默认 `stencilTestEnable=false`/`depthCompareOp=LEQUAL`/`depthWrite=true`，现有 pipeline 行为不变；旧 `.samatc` 缺 alphaMode 视为 Opaque |
| Reimport | `.samatc` 格式加字段，需 Reimport 才带 alpha（缺失即 Opaque，非破坏性） |

**不做：** OIT / WBOIT / per-pixel linked-list（重叠透明穿插留后续独立 issue）；多层 stencil 值（只 0/1）；物体 ID 写 stencil；stencil shadow volumes；alpha-to-coverage（延迟单采样不适用）；透明物投射/接收精确阴影（初版透明不写 shadow map）。

### 受益 issues

- **#23 帧率优化**：大背景场景 Deferred Lighting 省 15–30%（stencil）；植被 GBuffer overdraw 归零（EQUAL 主通道）
- **X-6 透明材质面片（植物等）** — 本 issue 直接吸收并实现
- **#61 ShaderVariantCache**：alpha-cutout 从 GBuffer 变体位移除（改由 prepass 处理），变体系统更简洁
- **#57 Meshlet**：depth-prepass 亦可作为 Hi-Z 遮挡剔除的深度来源

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

## Issue #56b — Script 热编译（Editor 内无需重启即可重新编译脚本）✅ DONE
<!-- 注：原文档误标 #71，与下方 "Issue #71 脚本库扩展 Phase 2" 编号冲突；按 project memory 记录此 issue 编号为 #56 hot-recompile。
FileWatcher(ReadDirectoryChangesW后台线程) + IWindow::IsFocused() + ScriptSystem::RecompileEditing + EditorMode 轮询；失焦积压 m_pendingRecompile，重获焦点触发；Playing 状态不重编；诊断自动路由 Diagnostics tab -->

---

## Issue #58 — 日志分层路由：Script 消息自动出现在 Diagnostics tab ✅ DONE
<!-- "script" 命名 spdlog logger + LogEntry::loggerName + ConsolePanelPresenter（Drain/路由/状态）+ ConsolePanel 纯 View；移除 ScriptSystem::CompileErrors() 死代码 -->

---

## Issue #69 — Script Runtime Library（脚本运行时库扩展）✅ DONE
<!-- Mathf.cs纯managed数学工具、UTF-8字符串编码修正、Input帧状态(IsKeyJustPressed/Released)、四元数API+Entity方向向量(Forward/Right/Up)、Entity.Destroy/static Create、RigidBodyProxy(Velocity/AddForce/AddImpulse)、PointLightProxy(Color/Intensity/Range)、Physics.Raycast(Jolt NarrowPhaseQuery)、ScriptApiFunctionTable version=2、IDE项目文件生成(Directory.Build.props+.csproj+.sln) -->

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

## Issue #71 Phase 2 — 脚本库扩展：InputMap 资产 + Mesh / Material 组件代理 ✅ DONE
<!-- .sainputmap 资产 + .sameta + ImportScanner/InputMapImporter cook 流程；ActionMapJsonParser Parse+Serialize 双向（src/function/input/）；InputMapLoader::LoadAll Application::UpdateProjectPaths() 调用；ScriptApiFunctionTable version 2→3 加 Block 3（InputAction 5槽 + StaticMesh 1槽 + MeshRenderer 6槽 + MaterialOverride 6槽）；managed AssetRef.cs / MeshProxy.cs / MaterialOverrideProxy.cs；Input.cs 追加 InputAction 静态类；Entity.cs 加 GetMesh()/GetMaterialOverride()；Step 9 builtin editor map 资产化（Viewport/EditorGlobal.sainputmap）+ EditorShortcutConfig 改用 .sainputmap 存储（~/.stellar_alia/editor_shortcuts.sainputmap），MakeViewportMaps 标 fallback-only -->

<details>
<summary>原设计（保留）</summary>

**优先级：中（依赖 #69 已完成；#29 脚本系统基础稳定）**

### 目标

在现有脚本 API（version 2）基础上增加三类能力：
① 将 InputSystem 的 ActionMapDef 定义外化为可编辑的 `.sainputmap` 资产（含 `.sameta` + cook 流程），脚本通过具名 action 查询而非裸设备路径；
② 暴露 `StaticMeshComponent` / `MeshRendererComponent` 读写接口（`MeshProxy`），支持运行时换材质槽；
③ 暴露 `MaterialOverrideComponent` 标量参数读写接口（`MaterialOverrideProxy`），支持运行时改 shader 参数（颜色、强度等）。
完成后 ScriptApiFunctionTable 升至 version 3。

### 设计

#### 文件布局概览

```
tools/importer/
├─ InputMapImporter.hpp / .cpp      ← NEW: cook .sainputmap → cook cache

managed/StellarAlia.Runtime/
├─ AssetRef.cs                      ← NEW: 轻量 UUID 句柄（纯 managed）
├─ MeshProxy.cs                     ← NEW: StaticMesh + MeshRenderer 代理
├─ MaterialOverrideProxy.cs         ← NEW: MaterialOverride 参数代理
└─ Input.cs                         ← 追加 InputAction 静态类（具名 action 查询）

src/function/input/
└─ ActionMapJsonParser.hpp / .cpp   ← NEW: JSON → ActionMapDef 解析器
   InputMapLoader.hpp / .cpp        ← NEW: 项目加载时批量注册所有 InputMap 资产

src/function/script/
├─ ScriptApiExports.hpp             ← 追加 Block 3 槽位，version 2→3
└─ ScriptApiExports.cpp             ← 追加对应 extern "C" 实现

src/engine/
└─ Application.cpp                  ← UpdateProjectPaths() 末尾调 InputMapLoader::LoadAll()
```

---

#### A — `.sainputmap` 资产与 cook 流程

**源文件格式**（JSON，扩展名 `.sainputmap`）：

```json
{
  "name": "Gameplay",
  "passthrough": false,
  "actions": [
    {
      "name": "Move",
      "type": "Axis2D",
      "bindings": [
        { "kind": "WASD" },
        { "kind": "Direct", "path": "Gamepad/LeftStick", "deadZone": 0.12 }
      ]
    },
    {
      "name": "Jump",
      "type": "Button",
      "activationThreshold": 0.5,
      "bindings": [
        { "kind": "Direct", "path": "Keyboard/Space" },
        { "kind": "Direct", "path": "Gamepad/ButtonSouth" }
      ]
    },
    {
      "name": "Look",
      "type": "Axis2D",
      "bindings": [
        { "kind": "Direct", "path": "Mouse/Delta", "scale": 0.08 },
        { "kind": "Direct", "path": "Gamepad/RightStick", "deadZone": 0.12 }
      ]
    }
  ]
}
```

Binding 支持的 `kind`：

| kind | 额外字段 | 对应 `BindingDef` |
|------|---------|-----------------|
| `"Direct"` | `path`, `scale`(float 或 [x,y]), `deadZone`, `invert`(bool), `normalize`(bool) | `BindingDef::Direct(path).WithXxx()` |
| `"WASD"` | `up/down/left/right`（可选覆盖）, `normalize`(bool) | `BindingDef::WASD(...)` |
| `"TwoButton"` | `negative`, `positive` | `BindingDef::TwoButton(...)` |
| `"Composite"` | `modifiers`(string[]), `key` | `BindingDef::Composite(...)` |

**`.sameta` 格式**（`controls.sainputmap.sameta` 紧邻源文件）：
```
# StellarAlia Asset Meta v1
uuid=<UUID>
type=InputMap
```

**ImportScanner**（`ImportScanner.cpp::AssetTypeFromExtension`）：
```cpp
if (e == ".sainputmap")  return "InputMap";
```

**Cook tool dispatch**（`tools/cook/main.cpp`）：
```cpp
} else if (entry.meta.type == "InputMap") {
    ok = CookInputMap(entry, opts.outputDir, opts.force);
}
```

**CookInputMap**（`tools/importer/InputMapImporter.cpp`）：
- 用 `nlohmann::json` 解析源文件做结构验证（必须含 `name` 和 `actions` 数组）
- 输出路径：`<cookCacheDir>/<uuid>.sainputmap`（写入验证后的 JSON bytes）
- 增量检测：比较 src/out mtime，`force` 时强制重新生成

**`ActionMapJsonParser`**（`src/function/input/ActionMapJsonParser.hpp/.cpp`）：
```cpp
namespace StellarAlia {
// Parse one .sainputmap JSON string → one ActionMapDef.
bool ActionMapJsonParser::Parse(std::string_view json, ActionMapDef& out);
}
```
支持所有 `BindingDef::Kind` 及 processor 字段（scale / deadZone / invert / normalize）。

**`InputMapLoader`**（`src/function/input/InputMapLoader.hpp/.cpp`）：
```cpp
namespace StellarAlia {
struct InputMapLoader {
    // 扫描 projectDir 下所有 .sainputmap.sameta → 解析 → RegisterMaps
    // 若 stack 为空则 PushMap(defs[0].name) 建立默认上下文
    static void LoadAll(const fs::path& projectDir,
                        const fs::path& cookCacheDir,
                        InputSystem&    inputSystem);
};
}
```
在 `Application::UpdateProjectPaths()` 末尾调用。

---

#### B — 函数表 Block 3（version 2 → 3）

新增槽位全部追加末尾（Block 1 & 2 位置不变）：

```cpp
struct ScriptApiFunctionTable {
    uint32_t version = 3;   // 2 → 3
    // … Block 1 & 2 不变 …

    // ── Block 3 — v3 新增 ─────────────────────────────────────────────
    // InputAction — 具名 action 查询
    float   (*InputAction_ReadFloat)       (const char* action);
    void    (*InputAction_ReadVec2)        (const char* action, float*, float*);
    int32_t (*InputAction_IsActive)        (const char* action);
    int32_t (*InputAction_WasActivated)    (const char* action);
    int32_t (*InputAction_WasDeactivated)  (const char* action);
    // StaticMesh
    void    (*StaticMesh_GetAssetUUID)     (uint64_t entity, char* buf, int32_t bufLen);
    // MeshRenderer
    int32_t (*MeshRenderer_GetSlotCount)   (uint64_t entity);
    void    (*MeshRenderer_GetSlotUUID)    (uint64_t entity, int32_t slot, char* buf, int32_t bufLen);
    int32_t (*MeshRenderer_SetSlotUUID)    (uint64_t entity, int32_t slot, const char* uuid);
    int32_t (*MeshRenderer_GetCastShadow)  (uint64_t entity);
    void    (*MeshRenderer_SetCastShadow)  (uint64_t entity, int32_t value);
    int32_t (*MeshRenderer_GetReceiveShadow)(uint64_t entity);
    void    (*MeshRenderer_SetReceiveShadow)(uint64_t entity, int32_t value);
    // MaterialOverride
    float   (*MaterialOverride_GetFloat)   (uint64_t entity, const char* param);
    void    (*MaterialOverride_SetFloat)   (uint64_t entity, const char* param, float value);
    void    (*MaterialOverride_GetVec3)    (uint64_t entity, const char* param, float*, float*, float*);
    void    (*MaterialOverride_SetVec3)    (uint64_t entity, const char* param, float, float, float);
    void    (*MaterialOverride_GetVec4)    (uint64_t entity, const char* param, float*, float*, float*, float*);
    void    (*MaterialOverride_SetVec4)    (uint64_t entity, const char* param, float, float, float, float);
};
```

---

#### C — C++ 实现要点

**InputAction**：委托 `g_ctx.input->ReadFloat/WasActivated` 等；`g_ctx.input == nullptr` 时返回 0。

**StaticMesh_GetAssetUUID**：`try_get<StaticMeshComponent>` → `uuid.ToString()` → `strncpy`；无组件时 `buf[0]='\0'`。

**MeshRenderer_SetSlotUUID**：`slot >= mr->materialSlots.size()` 时返回 0（不扩充 vector）；否则 `mr->materialSlots[slot] = AssetID::FromString(uuid)`，渲染器下帧自动绑定。

**MaterialOverride_SetFloat**：`try_get<MaterialOverrideComponent>`；不存在则 no-op；存在则 `mo->scalars[param] = ParamValue{value}(float variant)`。

---

#### D — C# 托管层

**`AssetRef.cs`**（新文件，纯 managed）：
```csharp
public readonly struct AssetRef {
    public readonly string Uuid;
    public bool IsValid => !string.IsNullOrEmpty(Uuid);
    public static readonly AssetRef Invalid = new(string.Empty);
    public AssetRef(string uuid) { Uuid = uuid; }
}
```

**`Input.cs` 追加 `InputAction` 静态类**：
```csharp
public static class InputAction {
    public static float   ReadFloat    (string action) => NativeApi.SA_InputAction_ReadFloat(action);
    public static Vector2 ReadVec2     (string action) { NativeApi.SA_InputAction_ReadVec2(action, out float x, out float y); return new(x, y); }
    public static bool    IsActive     (string action) => NativeApi.SA_InputAction_IsActive(action) != 0;
    public static bool    WasActivated (string action) => NativeApi.SA_InputAction_WasActivated(action) != 0;
    public static bool    WasDeactivated(string action)=> NativeApi.SA_InputAction_WasDeactivated(action) != 0;
}
```

**`MeshProxy.cs`** 关键接口：`MeshAsset`（只读 AssetRef）、`MaterialCount`、`GetMaterial(slot)`、`SetMaterial(slot, AssetRef)`、`CastShadow`/`ReceiveShadow`（get/set bool）。

**`MaterialOverrideProxy.cs`** 关键接口：`GetFloat/SetFloat(param)`、`GetVec3/SetVec3(param)`、`GetVec4/SetVec4(param)`，`GetColor/SetColor` 作为 Vec4 别名。

**`Entity.cs` 追加**：`public MeshProxy GetMesh() => new(this);` 和 `public MaterialOverrideProxy GetMaterialOverride() => new(this);`

---

### 实施步骤

- [ ] **Step 1 — ImportScanner + Cook 分派 + CookInputMap**
  - `ImportScanner.cpp`：`AssetTypeFromExtension` 加 `.sainputmap` → `"InputMap"`
  - `tools/cook/main.cpp`：dispatch 加 `"InputMap"` 分支
  - 新建 `tools/importer/InputMapImporter.hpp / .cpp`：nlohmann/json 验证 + 复制 JSON 到 `<uuid>.sainputmap`
  - 验证：`StellarAliaCook --input assets/ --output cook_cache/` 后 cook cache 中出现 `<uuid>.sainputmap`

- [ ] **Step 2 — ActionMapJsonParser**
  - 新建 `src/function/input/ActionMapJsonParser.hpp / .cpp`
  - 支持所有 BindingDef::Kind 及 processor 字段
  - 验证：手动测试解析示例 JSON → ActionMapDef 字段正确

- [ ] **Step 3 — InputMapLoader + Application 集成**
  - 新建 `src/function/input/InputMapLoader.hpp / .cpp`：`LoadAll()` 扫描 + 解析 + `RegisterMaps` + 条件 `PushMap`
  - `Application::UpdateProjectPaths()` 末尾调用
  - 验证：打开含 `.sainputmap` 项目后 `InputSystem::ReadFloat("Move")` 在 PIE 中返回非零

- [ ] **Step 4 — InputAction 脚本 API（Block 3 InputAction 五槽）**
  - `ScriptApiExports.hpp`：version 2→3，添加 InputAction 五槽
  - `ScriptApiExports.cpp`：实现五个 `SA_InputAction_*` 函数
  - `NativeApi.cs`：同步五个函数指针槽，`ExpectedTableVersion = 3`
  - `Input.cs`：追加 `InputAction` 静态类
  - 验证：`if (InputAction.WasActivated("Jump")) Debug.Log("jumped!")` — PIE 按 Space 触发一次

- [ ] **Step 5 — AssetRef C# 类型**
  - 新建 `managed/StellarAlia.Runtime/AssetRef.cs`（纯 managed）
  - 验证：构造、比较、IsValid 正常

- [ ] **Step 6 — MeshProxy 脚本 API（Block 3 StaticMesh + MeshRenderer 槽）**
  - `ScriptApiExports.hpp / .cpp`：追加 StaticMesh 1 槽 + MeshRenderer 6 槽（slot 越界返回 0）
  - `NativeApi.cs`：同步 7 个函数指针槽
  - 新建 `managed/StellarAlia.Runtime/MeshProxy.cs`
  - `Entity.cs`：追加 `GetMesh()`
  - 验证：`Self.GetMesh().SetMaterial(0, new AssetRef("<uuid>"))` 运行时换材质下帧生效

- [ ] **Step 7 — MaterialOverrideProxy 脚本 API（Block 3 MaterialOverride 槽）**
  - `ScriptApiExports.hpp / .cpp`：追加 MaterialOverride 6 槽（无组件时 GetFloat→0，SetFloat→no-op）
  - `NativeApi.cs`：同步 6 个函数指针槽
  - 新建 `managed/StellarAlia.Runtime/MaterialOverrideProxy.cs`
  - `Entity.cs`：追加 `GetMaterialOverride()`
  - 验证：`Self.GetMaterialOverride().SetColor("albedo", new Vector4(1,0,0,1))` 运行时变色

- [ ] **Step 8 — AssetsPanel / Inspector 对 .sainputmap 资产的最小支持**
  - `editor/ui/panels/AssetsPanel.cpp::IsTextAsset()`：扩展名表加 `.sainputmap`（双击走外部文本编辑器打开）
  - `editor/ui/panels/InspectorPanel.cpp::RegisterAssetDrawers()`：不新增专用 drawer，让 `.sainputmap` 落到 `m_defaultAssetDrawer`（DefaultAssetInspector 已支持显示文本/JSON 内容 + sameta 元信息）
  - 验证：项目里放一份 `controls.sainputmap` + `.sameta`，AssetsPanel 双击能在外部编辑器打开；选中后 Inspector 显示 JSON 内容
  - 范围控制：可视化 InputMap 编辑器（节点式 binding UI、绑定捕获）不在本 issue，留 #79+ 独立 issue

- [ ] **Step 9 — Editor builtin + user-override inputmap 全面迁移到 .sainputmap（统一存储格式）**
  - **9a — Builtin defaults 资产化**
    - 新建 `engine/assets/editor/Viewport.sainputmap` + `EditorGlobal.sainputmap`：把 `editor/input/EditorInputMaps.hpp::MakeViewportMaps()` 的两个 `ActionMapDef` 序列化为 JSON
      - `Viewport.sainputmap`：Look / Move / Sprint（PIE Pop 时配套 pop）
      - `EditorGlobal.sainputmap`：SaveScene / NewScene / Undo / Redo / EntityDelete / EntityDuplicate / SelectAll / GizmoTranslate/Rotate/Scale / TogglePanels（始终保留）
    - `EditorMode::OnAttach`：改成调 `ActionMapJsonParser::Parse` 读这两份 builtin 资产 → `RegisterMaps`；不再直接调 `MakeViewportMaps()`
    - Fallback：builtin asset 加载失败时仍回退 hardcoded `MakeViewportMaps()`（保证 editor 可启动）
  - **9b — ActionMapJsonParser 补齐反向 Serialize（round-trip codec）**
    - `ActionMapJsonParser::Serialize(const ActionMapDef& def, std::string& outJson)`：与 `Parse` 对称的反序列化，所有 `BindingDef::Kind` 及 processor 字段（scale / deadZone / invert / normalize）必须 round-trip 等价
    - 配套单元/手测：Parse → Serialize → Parse 二次解析得到结构等价
  - **9c — EditorShortcutConfig 存储格式由自定义 JSON 切换到 `.sainputmap`**
    - **保留** override-layer 语义（in-memory `m_overrides: unordered_map<string, BindingDef>` 不变，`ApplyTo(defaults)` 不变）
    - **改写**`Load/Save/ExportTo/ImportFrom`：从 `{version, overrides:{name:{modifiers,key}}}` 自定义 schema 切换到 `.sainputmap`（即 ActionMapDef JSON，只列出被覆盖的 action）
      - 在内存中以一个 `ActionMapDef{name="EditorOverrides", actions=[…]}` 形式持久化；保存时调 `ActionMapJsonParser::Serialize`，加载时调 `Parse` 再把 `actions[].bindings[0]` 抽进 `m_overrides`
      - 配置文件落地路径同步：`~/.stellar_alia/editor_shortcuts.sainputmap`（旧 `~/.stellar_alia/shortcuts.json` 不做迁移，#71 之前用户极少改 — 加载失败时静默 fall back 到默认）
    - 旧 `nlohmann::json` 直读 / SerializeOverrides 代码删除；EditorShortcutConfig.cpp 只依赖 `ActionMapJsonParser`
  - **9d — 调用点收尾**
    - `EditorInputMaps.hpp`：`MakeViewportMaps()` 标注「fallback-only；正式路径走 builtin `.sainputmap`」
    - `EditorMode::OnAttach` 顺序：load builtin `.sainputmap` → `EditorShortcutConfig::Load(~/.stellar_alia/editor_shortcuts.sainputmap)` → `ApplyTo(builtin)` → `RegisterMaps`（顺序保持现状）
  - 验证：
    - 删掉 builtin `.sainputmap` 时 editor 仍可启动（fallback 生效）
    - 修改 `Viewport.sainputmap` 里的 Move 绑定（如 WASD → 方向键），重启 editor 后 viewport 相机响应改变
    - 调用 `EditorShortcutConfig::SetOverride("SaveScene", Composite({"Ctrl","Shift"},"S"))` 后 Save → 文件内容是合法的 `.sainputmap`（用 `ActionMapJsonParser::Parse` 能解析回 `ActionMapDef`）
    - Reload 后 override 仍生效；Ctrl+S 不再触发 SaveScene，Ctrl+Shift+S 触发
    - `AssetsPanel` 中（项目内放置）和 user dir 中的 `.sainputmap` 走同一 parser（统一可视性）
  - 范围控制：file watcher 触发 InputSystem re-register（hot-reload）不在本 issue，editor 自身 inputmap 修改需重启生效

### 边界情况与约束

| 约束 | 说明 |
|------|------|
| version 字段 | Block 3 全部追加末尾；version 2→3 让运行时快速发现两侧不同步 |
| InputMapLoader 调用时机 | `UpdateProjectPaths()` 内；Play 时不重复（map 已注册）；切换项目时调 `RegisterMaps` 替换同名 def |
| MapStack 非空判断 | `LoadAll` 仅在 stack 为空时 `PushMap`，避免覆盖编辑器侧已 push 的 map |
| MeshRenderer_SetSlotUUID 越界 | `slot >= materialSlots.size()` 返回 0，不扩充 vector |
| MaterialOverrideComponent 不自动添加 | 无此组件时 Set 为 no-op，Get 返回 0；需编辑器手动添加组件 |
| .sainputmap cook 输出格式 | 保持 JSON（不二进制化）；体积小，便于调试；未来需要压缩时切换无需改接口 |
| 多 .sainputmap 自动 push 策略 | 只 push 扫描到的第一个 map；后续 issue 可暴露 `InputAction.Push/Pop(name)` 给脚本 |
| 不做：AssetRef::FromPath() | 需 AssetRegistry 查询，留后续 issue |
| 不做：StaticMesh_SetAssetUUID | 运行时换 mesh 需重新上传 GPU buffer，复杂度高 |
| ~~不做：MaterialOverride 纹理槽~~ → **已通过 #72 解锁** | 渲染侧：bindless heap 索引在 ring blob 里（PBR SSBO 路径）或 `clone->SetTexture` 写 desc set（legacy 路径）。脚本 API `MaterialOverrideProxy.SetTexture(slot, AssetRef)` 待 #71 实现层补完 |
| Editor builtin inputmap 路径 | Step 9 把 `MakeViewportMaps()` 拆 builtin `.sainputmap`，与 game 项目级 `.sainputmap`（Step 3 LoadAll 扫描）走相同 `ActionMapJsonParser`；builtin 路径直接 `Parse + RegisterMaps`，不进 cook cache（engine 内置，无 .sameta 流程） |
| EditorShortcutConfig 持久化格式统一 | 现有 `EditorShortcutConfig` 的自定义 JSON（`{overrides:{actionName:{modifiers,key}}}`）正是 `.sainputmap` ActionMapDef 的镜像 — Step 9c 把存储层切到 `.sainputmap`（落地为 `~/.stellar_alia/editor_shortcuts.sainputmap`，只列被覆盖的 action），in-memory override-layer 语义与 `ApplyTo()` 不变；旧 JSON 文件不做迁移，加载失败 fall back 到默认 |
| ActionMapJsonParser 必须双向 | 原设计仅 `Parse`；Step 9b 补 `Serialize` 完成 round-trip，理由是 EditorShortcutConfig.Save 需要写 `.sainputmap` 格式；BindingDef 所有 Kind + processor 字段必须等价 round-trip |
| InputMap hot-reload | 不做：editor builtin / game `.sainputmap` 修改后需重启 editor 或重新进入 PIE；FileWatcher → InputSystem re-register 留独立 issue（与 #29 脚本热编译类似但解耦） |
| InputMap 可视化编辑器 | 不做：本 issue 仅让 `.sainputmap` 在 AssetsPanel/Inspector 可见可外部编辑；节点式 binding UI / 绑定捕获 / processor 编辑 留 #79+ |

### 受益 issues

- **#33**（贝塞尔曲线相机）：`InputAction.ReadVec2("Look")` + MeshProxy 是脚本驱动相机和换材质的基础工具
- **#69**（脚本库）：AssetRef 类型可回头补充 AnimatorProxy 的 clip 换用功能
- **#70**（游戏发布）：`.sainputmap` 随项目资产打包，GameMode 无需内置硬编码的 input map 定义
- **Editor 自身**（Step 8/9）：viewport 相机与全部编辑器热键改由 builtin `.sainputmap` 驱动，user override 也落地为 `.sainputmap`（EditorShortcutConfig 自定义 JSON 退役），整个 editor 输入栈统一收敛到 `ActionMapJsonParser` 一套 codec

</details>

---

## Issue #71 Phase 3 — 多 InputMap 切换与 Editor 全局热键持久化 ✅ DONE
<!-- Phase 3a: InputSystem::TryPushMap/TryReplaceMap/GetTopMapName/IsMapInStack fail-soft API；ScriptApiFunctionTable version 4→5 加 InputMap_Push/Pop/Replace/IsActive/GetActive 5 槽；managed InputMap.cs 静态类；Phase 3b: EditorInputMaps.hpp MakeViewportMaps 重命名为 MakeBuiltinEditorMaps，拆出 EditorGlobal/Viewport/TextInput/UI 四份 def；Viewport.passthrough=true 让 EditorGlobal 栈底持续可见；EditorMode OnAttach 改 Push EditorGlobal 然后 Push Viewport；OnPlayStateChanged 仅 Pop Viewport 保留 EditorGlobal；ShortcutsPanel/Presenter 同步切换调用点 -->

<details>
<summary>原设计（保留）</summary>

#### 追加规划

Phase 1 (#69) 和 Phase 2 (#71 Step 1–9) 完成后还剩两块未做的工作。本节按依赖关系把它们规划为两个子阶段：

- **Phase 3a — 脚本侧 InputMap 切换 API**（先做，独立可落地）
- **Phase 3b — Editor builtin map 拆分 Viewport / EditorGlobal**（后做，依赖 3a 暴露的 `GetActive()` 便于调试）

两者**不合并**：3a 是脚本运行时能力扩展（managed + 函数表），3b 是编辑器 PIE 输入栈语义改造（C++ + builtin 资产），范围、风险、受影响代码路径完全不同。强行合并会让单次实现的 diff 过大，难以独立验证。

---

### Phase 3a — 脚本侧 InputMap 切换 API

**优先级：高（解锁暂停菜单 / 载具切换 / 关卡专属输入集等所有 PIE 多 map 用例）**

#### 目标

让 C# 脚本通过 `InputMap.Push("PauseMenu") / Pop() / Replace(…) / IsActive(…) / GetActive()` 在运行时切换 InputSystem 的 map 栈；命名不存在时 fail-soft（Log warn + 返回 false / 空串），不再 assert 崩溃。

#### 设计

##### A — InputSystem 公共 API 扩展

`src/function/input/InputSystem.hpp` 现有 `PushMap / PopMap / ReplaceMap` 行为：
- `PushMap(unknown)` → `assert + return` —— **必须改为 return-false fail-soft 路径**
- 缺 `GetTopMapName()` —— 新增
- 缺 `IsMapInStack(name)` —— 新增（用于脚本 `IsActive()` 语义；只判栈顶还是判整栈见下文边界讨论）

签名修订：
```cpp
class InputSystem {
public:
    // 返回 true 表示成功 push（map 名已注册）；false 仅记录 warn 不 push。
    [[nodiscard]] bool TryPushMap   (std::string_view name);
    [[nodiscard]] bool TryReplaceMap(std::string_view name);
    // 兼容旧调用点（editor 内部已知 map 名一定存在），保留 void 版本但内部走 TryPushMap，
    // 失败时 SA_LOG_ERROR 而非 assert（脱掉 assert 让 fuzz / 失败状态下不崩 editor）。
    void PushMap   (std::string_view name);
    void ReplaceMap(std::string_view name);

    // ── 新增查询 ──
    std::string_view GetTopMapName() const;          // 栈空时返回 ""
    bool             IsMapInStack(std::string_view name) const;
};
```

##### B — 函数表 Block 3 v4 → v5（新增 5 槽）

```cpp
// ScriptApiExports.hpp
struct ScriptApiFunctionTable {
    uint32_t version = 5;        // 4 → 5
    // ... 现有 60 槽 ...

    // ── v5 — InputMap stack control (Phase 3a) ────────────────────────────
    int32_t (*InputMap_Push)     (const char* name);     // 1=ok, 0=unknown
    void    (*InputMap_Pop)      ();
    int32_t (*InputMap_Replace)  (const char* name);     // 1=ok, 0=unknown
    int32_t (*InputMap_IsActive) (const char* name);     // 1=在栈中, 0=不在
    void    (*InputMap_GetActive)(char* buf, int32_t bufLen);  // 栈空时 buf[0]='\0'
};
```

`SA_InputMap_*` 实现遵循 `ScriptApiExports.cpp` 现有风格：`g_ctx.input == nullptr` 时返回 0/空串。

##### C — C# 托管层

新建 `managed/StellarAlia.Runtime/InputMap.cs`：

```csharp
namespace StellarAlia;

public static class InputMap {
    /// Push named map on top of the InputSystem stack. Returns false (with a
    /// warning log on the engine side) when the name is not registered.
    public static bool Push(string name)    => NativeApi.SA_InputMap_Push(name) != 0;
    public static void Pop()                 => NativeApi.SA_InputMap_Pop();
    public static bool Replace(string name) => NativeApi.SA_InputMap_Replace(name) != 0;
    public static bool IsActive(string name) => NativeApi.SA_InputMap_IsActive(name) != 0;
    public static string GetActive() { /* fixed buf 64 → string */ }
}
```

`NativeApi.cs` 同步 5 个 wrapper + 5 个函数指针字段，`ExpectedTableVersion = 5`。

#### 实施步骤

- [ ] **Step 1 — InputSystem 公共 API 扩展**
  - `InputSystem.hpp`：声明 `TryPushMap / TryReplaceMap / GetTopMapName / IsMapInStack`
  - `InputSystem.cpp`：`TryPushMap` 走 `FindMap` 未命中 → `SA_LOG_WARN` + 返回 false；命中走原逻辑 + 返回 true
  - 旧 `PushMap/ReplaceMap` 重写为转发到 Try 版本，断言改为 `SA_LOG_ERROR`（不崩 editor）
  - 验证：手动 `input.TryPushMap("NoSuchMap")` 返回 false、editor 不崩、log 有 warn

- [ ] **Step 2 — Block 3 v5 函数表（C++ 端）**
  - `ScriptApiExports.hpp`：version 4→5，追加 5 个槽位 + extern "C" 声明
  - `ScriptApiExports.cpp`：5 个 `SA_InputMap_*` 实现 + 函数表绑定
  - `SA_InputMap_GetActive` 用 `strncpy` 写入 buf，截断到 `bufLen-1`、保证 null 结尾
  - 验证：61 槽两端一致

- [ ] **Step 3 — C# wrapper + InputMap 静态类**
  - `NativeApi.cs`：`ExpectedTableVersion = 5`，5 个 wrapper、5 个 `delegate*unmanaged` 字段
  - 新建 `managed/StellarAlia.Runtime/InputMap.cs`：5 个静态方法
  - 验证：Hello-world 脚本 `InputMap.Push("Gameplay"); Debug.Log(InputMap.GetActive())` 打印 "Gameplay"

- [ ] **Step 4 — Demo 脚本扩展**
  - `demo_project/assets/scripts/PauseToggle.cs`（新建）：按 Escape 在 "Gameplay" 与项目里新增的 "Menu" 之间 toggle，用 `InputMap.Push/Pop` 演示
  - `demo_project/assets/inputmaps/Menu.sainputmap`（新建，简单 Cancel 一条 action）
  - 验证：PIE 中 Esc 来回切，`GetActive()` 反映栈顶变化

#### 边界情况与约束

| 约束 | 说明 |
|------|------|
| `IsActive(name)` 语义 | 当前实现选"是否在栈上任意位置"，匹配 InputSystem::Poll 的 passthrough 链下传语义。脚本想知道"是否栈顶"应改用 `GetActive() == name` |
| `Push` 重复同名 | 不去重，允许 push 同一 map 多次（Pop 也对称地一次只弹一个）。脚本若想防重复，自己用 `IsActive` 检查 |
| 栈空时 Pop | 现有 InputSystem::PopMap 已是 no-op，不变 |
| Editor 侧调用点不改 | EditorMode 内部仍调 `PushMap`（旧 void 版本）。改成 `SA_LOG_ERROR`-on-miss 不影响正常路径 |
| 不做：map 优先级 / 排他锁 | 脚本想做"暂停菜单期间禁用游戏输入" → 让 Menu.sainputmap 的 passthrough=false 即可，无需引入新概念 |
| 不做：脚本 RegisterMap | 运行时动态注册新 map（不从 .sainputmap）超出本 phase；要做该走 #79+（InputMap 编辑器）|
| ScriptApiFunctionTable 版本号 | v4 → v5；managed dll 必须重新 publish，否则 `ExpectedTableVersion` 检查会拦截 |

#### 受益 issues

- **#33**（贝塞尔曲线相机）：相机进入手动飞行模式时 `InputMap.Replace("CinematicCamera")`，退出 `Replace("Gameplay")`
- **#70**（游戏发布）：GameMode 启动时只需保证项目里有一个默认 map，脚本可自行切换；不再硬编码 startup map name
- **Phase 3b** 调试：拆分 Viewport/EditorGlobal 后用 `InputMap.GetActive()` + Debug Console 看 PIE 期间栈顶是哪个 map，省下加 C++ log 的来回

---

### Phase 3b — Editor builtin map 拆分 Viewport / EditorGlobal

**优先级：中（编辑器体验改善；用户期望 PIE 期间 Ctrl+S 仍能保存场景）**

#### 目标

把当前塞在单一 "Viewport" map 里的 17 个 action 拆成两份 builtin `.sainputmap`：

- **Viewport.sainputmap** — 视口操作（Move / Look / Sprint / MouseLook / ToggleUI）：PIE 入口 Pop，PIE 出口 Push
- **EditorGlobal.sainputmap** — 编辑器全局热键（SaveScene / NewScene / Undo / Redo / EntityDelete / EntityDuplicate / EntityRename / SelectAll / GizmoTranslate/Rotate/Scale / TogglePanels）：**始终保留**在栈底，PIE 期间仍可触发

要求：行为变化对项目脚本透明（不破坏现有 Gameplay map）；ImGui 文本输入时 TextInput 仍硬阻断；现有 `EditorShortcutConfig` user override 继续工作。

#### 现状（已验证的好消息 — 改动比预想小）

读过 `InputSystem::Poll / ComputeBlockedPaths / DetectActiveFamily` 后确认：**这三个函数都已是 passthrough-aware 跨 map 实现**（`InputSystem.cpp:94-108, 173-201, 232-249`）：
- Poll 从栈顶向下遍历，遇到 `!passthrough` 停止；同名 action 高层胜（line 98）
- ComputeBlockedPaths 跨 map 收集所有 Composite 的 keyPath
- DetectActiveFamily 跨 map 检查 binding 活跃度

⇒ **不需要重写这三个函数**，也**不需要给 ActionDef 加 priority 字段**。直接给 Viewport 设 `passthrough=true`，让 EditorGlobal 在栈底持续可见即可。

#### 设计

##### A — builtin 资产拆分

| 文件 | 包含 actions | passthrough |
|------|------------|------|
| `assets/editor/EditorGlobal.sainputmap` | SaveScene, NewScene, Undo, Redo, EntityDelete, EntityDuplicate, EntityRename, SelectAll, GizmoTranslate, GizmoRotate, GizmoScale, TogglePanels | `false`（栈底无需穿透） |
| `assets/editor/Viewport.sainputmap` | Move, Look, Sprint, MouseLook, ToggleUI | `true`（PIE 前 viewport 同时还要让 EditorGlobal 可达）|
| `assets/editor/TextInput.sainputmap` | 空 | `false`（不变）|
| `assets/editor/UI.sainputmap` | Navigate, Submit, Cancel | `false`（不变；当前未实际 push）|

##### B — EditorMode 栈管理改造

`editor/EditorMode.cpp::OnAttach`（Step 9d 已经做的位置）：
```cpp
// 旧：input.RegisterMaps(... 三份 ...) ; input.PushMap("Viewport");
// 新：四份 + 两 push
input.RegisterMaps(...);  // 读 EditorGlobal/Viewport/TextInput/UI
input.PushMap("EditorGlobal");   // 栈底
input.PushMap("Viewport");        // 栈顶（passthrough=true）
m_editorGlobalPushed = m_viewportActive = true;
```

`OnPlayStateChanged`：
- **进入 PIE**：`input.PopMap()`（Viewport），保留 EditorGlobal 在栈底；按现有 #71 逻辑 push 项目 Gameplay 在 EditorGlobal 之上
  - 若 Gameplay.passthrough=false（默认）→ EditorGlobal 被遮蔽，PIE 期间 Ctrl+S **不**触发 SaveScene
  - 若 Gameplay.passthrough=true → PIE 期间 Ctrl+S **会**触发 SaveScene
  - **决策**：默认让项目方决定，文档化 trade-off；本 phase 不强制
- **退出 PIE**：Pop Gameplay → Push Viewport（不动 EditorGlobal）

`OnDetach`：Pop Viewport + Pop EditorGlobal（按现有清理风格）。

##### C — `MakeViewportMaps()` fallback 同步拆分

`editor/input/EditorInputMaps.hpp` 重命名为 `MakeBuiltinEditorMaps()` 并返回 4 份 def（EditorGlobal + Viewport + TextInput + UI），保持与 builtin `.sainputmap` 同构（顶部注释已标注 fallback-only 现状）。

##### D — `ShortcutsPanel::BuildEntries` 适配

当前实现遍历 `MakeViewportMaps()` 收集 `userConfigurable && Button` action。新版同样遍历 `MakeBuiltinEditorMaps()` —— Viewport 拆出去的 5 个 action 都不是 `userConfigurable`，所以面板内容自动等价（验证：SaveScene、Undo 等保持可重绑）。

##### E — `EditorShortcutConfig::ApplyTo` 不变

ApplyTo 已是"对 defaults 列表内每个 ActionMapDef 的 actions 逐个 patch bindings[0]"，4 个 def 进来还是同样的逻辑。

#### 实施步骤

- [ ] **Step 1 — 拆分 builtin `.sainputmap` 资产**
  - 编辑 `assets/editor/Viewport.sainputmap`：删除 12 个非 viewport 的 action，仅保留 Move/Look/Sprint/MouseLook/ToggleUI；**顶层加 `"passthrough": true`**
  - 新建 `assets/editor/EditorGlobal.sainputmap`：含 12 个 user-configurable 全局热键，passthrough=false
  - 验证：JSON 通过 `ActionMapJsonParser::Parse` 解析无错

- [ ] **Step 2 — `MakeBuiltinEditorMaps()` 同步**
  - `editor/input/EditorInputMaps.hpp` 重命名 `MakeViewportMaps` → `MakeBuiltinEditorMaps`，按上述 4 份 def 重组
  - Viewport def 的 `passthrough = true`
  - 顶部注释更新

- [ ] **Step 3 — EditorMode `OnAttach` push 顺序**
  - 在加载循环里加 EditorGlobal 文件
  - OnAttach 末尾 `PushMap("EditorGlobal")` then `PushMap("Viewport")`
  - 引入 `bool m_editorGlobalPushed`（仿 `m_viewportActive`）
  - 验证：editor 启动后 Ctrl+S 正常保存，Gizmo T/R/S 正常切换，相机 WASD 正常

- [ ] **Step 4 — `OnPlayStateChanged` 改造**
  - 进 PIE：仅 Pop Viewport（保留 EditorGlobal）
  - 出 PIE：Push Viewport（保留 EditorGlobal）
  - OnDetach：补 Pop EditorGlobal
  - 验证：PIE 期间 Gameplay map 的 Move 仍生效；Gameplay.passthrough=false 时 Ctrl+S 不触发（符合预期）；切回编辑器后 Ctrl+S 恢复

- [ ] **Step 5 — `ShortcutsPanel` / `ShortcutsPresenter` 调用点同步**
  - `ShortcutsPanel.cpp::BuildEntries` 引用 `MakeBuiltinEditorMaps()`
  - `ShortcutsPresenter.cpp::RunRegisterMaps` 同步
  - 验证：Shortcuts 面板正常列出 12 个全局热键 + 1 个 ToggleUI

- [ ] **Step 6 — Phase 3a 调试钩子**
  - 用 `InputMap.GetActive()`（Phase 3a 已落地）在 demo 脚本里打印 PIE 各阶段栈顶名，验证 stack 顺序符合预期
  - 不写专用诊断代码；仅靠 demo 脚本日志确认

- [ ] **Step 7 — 跨 map Composite 抢占回归测试**
  - 手测：编辑场景中按 `S`（GizmoScale，在 EditorGlobal）和 `Ctrl+S`（SaveScene，在 EditorGlobal）—— 前者仅触发 GizmoScale，后者仅触发 SaveScene
  - 手测：PIE 期间按 WASD —— Move（Viewport 路径替换为 Gameplay）触发；EditorGlobal 的 Composite Ctrl+S 是否能抢占 Gameplay 的 Direct S 取决于 Gameplay 配置（passthrough 决策点）

#### 边界情况与约束

| 约束 | 说明 |
|------|------|
| 同名 action 跨 map | InputSystem::Poll line 98 是"高层胜"。拆分后 Viewport 与 EditorGlobal 不应有同名 action；CI / 启动时可加 assertion 验证 4 份 def 的 action name 集合不相交 |
| Viewport `passthrough=true` | 让 EditorGlobal 在栈底持续评估的必要条件；否则 PIE 之前 EditorGlobal 完全被遮蔽，Ctrl+S 都用不了 |
| Gameplay 的 passthrough 决策 | 项目方在 `.sainputmap` 里自行决定。**默认 false**（PIE 期间游戏占用全部输入，Ctrl+S 不触发 SaveScene）— 这是更符合"游戏模式"的语义；用户想要"PIE 中仍可保存"就把 Gameplay.passthrough 改 true |
| TextInput 不变 | 仍 passthrough=false 硬阻断；push 在栈最顶时连 Viewport+EditorGlobal 一起阻断，符合"文本输入不应触发热键"语义 |
| 不做：ActionDef priority | UE5 风格的 per-action priority/consume 更细粒度但改造范围大；当前 passthrough+top-wins 足够覆盖 PIE 用例。留 #79+ 真正需要时再做 |
| 不做：跨 map 单元测试套件 | 当前 InputSystem 无独立测试基础设施；建立测试 framework 是独立 issue。本 phase 靠手测 + Phase 3a 的 `GetActive()` 验证 |
| EditorShortcutConfig override 兼容 | `m_overrides` 是 `unordered_map<actionName, BindingDef>`，跨 map 自动匹配；ApplyTo 遍历 4 份 def 各自 patch，无需改 EditorShortcutConfig.cpp |
| 旧用户配置文件迁移 | `editor_shortcuts.sainputmap` 现存 actions 一定属于 Viewport 或 EditorGlobal 之一；拆分后 ApplyTo 找到对应 ActionDef 仍 patch 成功，无需迁移 |

#### 受益 issues

- **用户体验**：PIE 期间 Ctrl+Z / Ctrl+Shift+Z 撤销编辑器场景修改（如果切回编辑模式做错操作）— Gameplay.passthrough=true 时直接可用
- **#79+ InputMap 可视化编辑器**：拆分后 EditorGlobal 与 Viewport 概念清晰，未来可视化编辑器分页展示更自然
- **#70 游戏发布**：GameMode 不需要关心 EditorGlobal —— 它就是 editor-only builtin，不参与项目打包

</details>

---

## Issue #72 — Material Override 重构：MPB 风格 + SSBO 动态偏移 + Bindless 贴图堆 ✅ DONE
<!-- set 编排对齐 UE5/Unity HDRP：set=0 BindlessTextureHeap (4096 槽 sampler array)，set=1 FrameUniforms，set=2 MaterialParams SSBO_DYN + per-frame MaterialParamRing (2 MiB)，set=3 Skin。PBR 走 SSBO+bindless 零 per-entity desc set 分配；legacy UBO 路径保留供 .saglsl shading model 沿用。SPIR-V reflection 在 set=2 binding=0 检测 "MaterialParams" 自动启用 SSBO 路径，`t_*_Idx` 字段作 bindless 索引。VulkanDevice 加 deferred-destroy 队列（per-frame slot trash bin）替换即时 vkFree/vmaDestroy，根除 in-flight 资源回收 validation 错误。 -->

---

### UI Bug Issues
~~长双击重命名目前无法起效~~ ✅ 已修：AssetsPanel 引入 DoubleClickClassifier，长按触发重命名，短按保留原导航/打开行为
~~asset panel重命名缩略图会消失~~ ✅ 已修
~~目前没有在asset panel空白处交互（右键创建文件， 点击选择等）的功能~~ ✅ 已修：右键空白弹出 Create 菜单，左键空白清除选中
~~视角操作会影响ui~~ ✅ 已修：RMB mouselook 激活时将 ImGui::GetIO().MousePos 置为 (-FLT_MAX,-FLT_MAX)，消除面板悬停残留
~~场景继承树根节点拖拽排序无效~~ ✅ 已修：Scene::m_rootOrder 维护根节点用户定序，SceneHierarchyPresenter DnD 分支调用 MoveRootBefore/After，SceneHierarchyPanel 和 SceneSerializer 改用 GetRootOrder() 迭代

---

## Vulkan Error D — vkUpdateDescriptorSets on in-flight descriptor set ✅ DONE
<!-- VulkanDevice descriptor layout/pool 加 VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT + VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT，让 RG greedy interval slot coloring 复用同一 VkImageView 时仍 spec 合规；无需双缓冲 descSet -->

---

## Vulkan Error E — 未使用顶点属性（Unused Vertex Attributes）✅ DONE
<!-- ShaderReflection.vertexInputs (.refl v6, populated from SPIR-V stage_inputs); ShaderProgram 透传到 RHIPipelineDesc.vertexInputs[]; VulkanDevice::CreatePipeline 用反射数据生成 attribs + location→offset 静态表，v3-v5 旧文件 fallback 到 4-attrib 硬编码。 -->

## Vulkan Error F — 描述符集 / 推送常量布局错配（Stale m_boundPipeline）✅ DONE
<!-- VulkanCommandList::Bind() 重置 m_boundPipeline 防止 ImmediateCompute 的 compute pipeline 跨命令缓冲泄漏；Shadow/GBuffer/SelectionMask 将 frameSet 绑定移到首个 SetPipeline 之后。 -->

## Vulkan Error G — Resize 同步错乱（imageView < renderArea + fence/semaphore in use）✅ DONE
<!-- Application::Run 在读取 GetSwapchainWidth/Height 前调用 ResizeSwapchain(window_w, h)，让内部 RT 与 swapchain 同尺寸创建；BeginFrame 的 m_needResize 路径保留。 -->

## Vulkan Error H — RenderGraph Read+Write 在 swapchain 触发 SHADER_READ 转换 ✅ DONE
<!-- RenderGraph::Execute 检测 pass 同时 Read+Write 同一纹理时跳过 SHADER_READ 转换（仅排序依赖，layout 留在 RenderTarget；Write 块的 wasTexWritten 仍发 RT→RT 内存屏障）。修复 DebugOverlay / InfiniteGrid / SelectionOutline 在 swapchain（无 SAMPLED_BIT）上的非法布局转换。 -->

## Cross-dir Vertex Shader Resolution for Project Material Types ✅ DONE
<!-- FeatureInitContext 新增 engineShaderDir；MaterialManager::RegisterTypeFromShaders 用 resolve() lambda 先 ctx.shaderDir 后 engineShaderDir 查找 SPV/.refl，修正项目 .saglsl 引用 deferred_geometry.vert 时跨目录的路径解析。 -->

---

## Script Inspector 三连规划（#73 / #74 / #75）— 概览

目标：让 C# 脚本的字段像 Unity 那样出现在 Inspector，并支持拖拽资源/实体引用、Undo、热重载字段保活。

按 **P4（C# 字段反射）** 为界切割成三个 issue：
- **#73 — 前置基础设施**（O1 + P1 + P2）：字段级 Undo 框架、`SAASSET` 拖拽 payload 改用 AssetID、`.cs` 资源化（`.cs.sameta` + `ScriptComponent.scriptId`）。
- **#74 — 字段反射核心**（P4 完整）：C# 反射扫描脚本字段、生成跨界 schema、`ScriptComponent` 携带字段值容器、Inspector 用 schema 渲染并能编辑标量字段（来回 IPC 通）。
- **#75 — 字段类型与序列化扩展**（P5 + P6 + O2 + P3 + O3）：.sascene 序列化字段值、热重载按名 patch、AssetRef/EntityRef/Color/Enum 字段类型、`[Range]/[Tooltip]/[Header]/[HideInInspector]`、`List<T>` 与嵌套 struct。

依赖：#74 依赖 #73；#75 依赖 #74。#68 ComponentSchema 与本规划独立但 FieldType / FieldDef 设计可对齐，#75 可考虑统一 schema 表达。

---

## Issue #73 — Script Inspector 前置：字段 Undo + AssetID Payload + Script 资源化 ✅ DONE
<!-- SetFieldCommand<T> + TrackedFieldEdit (IsItemDeactivatedAfterEdit collapses drag to one undo record) + AcceptAssetIDDrop wired into all scalar fields of Tag/Transform/Camera/Light/Animator/MeshRenderer/RigidBody/Collider/MaterialOverride drawers; AssetDragPayload {path, type, id} sent by AssetsPanel (path-first for back-compat); ScriptComponent.scriptPath → scriptId : AssetID, ScriptSystem::Context.assetRegistry resolves to .cs path via FindByID; ImportScanner generates .cs.sameta with class_name=stem; SceneSerializer writes script.{asset_id,class}; AssetsPanel selection moved from IsItemClicked (mouse-down) to IsItemHovered+IsMouseReleased (mouse-up) so click-to-drag doesn't flip Inspector to Asset view; viewport drop fixed to use BeginDragDropTargetCustom (pre-existing bug). -->
---

## Issue #74 — Script Field Reflection（脚本字段反射核心）✅ DONE
<!-- ScriptFieldSchema (Kind/Descriptor/ClassSchema/Value variant) + ScriptFieldBlob (BlobWriter/BlobReader, schema blob v1, field-value blob; LE no-padding, wire-compat with managed FieldBlobIO.cs); managed FieldReflector scans public fields → BuildSchemaBlob/Apply/Capture via Activator+FieldInfo; ScriptBridgeEntry exports GetClassSchemaBlob/ApplyFieldValues/CaptureFieldValues (two-step capacity protocol); ScriptLoader.FindUserScriptType + GetInstance helpers; ScriptSchemaCache (className→schema, Cleared on every Compile); ScriptSystem adds GetSchemaFor/InjectFieldValues/InjectSingleField; OnPlayStart Instantiate→Inject before OnAttach; ScriptComponent.fields (unordered_map<string,ScriptFieldValue>); ScriptDrawer schema-driven render (Bool/Int32/Float/Vec2-4/String editable; AssetRef/EntityRef/Color/Enum/Unsupported as #75 placeholders); Play-mode single-field delta inject on ImGui change. UX fix: RecompileEditing no longer Unloads ALC (schema lookup needs live ALC) and now compiles every .cs in AssetRegistry::EntriesByType("Script") not just referenced ones; OnAttach also warm-up calls RecompileEditing (initial-load path did not previously go through LoadProject). Refactor: shared LoadProjectFiles(projectDir) helper covers watch+scan+recompile+UpdateProjectDir+startup-scene for both OnAttach and LoadProject. Skipped: ScriptApiFunctionTable.version bump (struct unchanged, bump would force dotnet-publish AVE risk; new exports go through LoadAndGet by name). -->

## Issue #75 — Script Field Inspector：序列化、类型扩展与属性 ✅ DONE (partial — List/nested deferred to #80)
<!-- P5 serialization: SceneSerializer reads/writes script.fields[{name,kind,value}] for Bool/Int32/Float/String/Vec2-4/Color/AssetRef/EntityRef + scene_local_id mirror; ScriptFieldKindToString/FromString in schema header; RecompileEditing migration walks all ScriptComponent entities and reconciles vs new schema (retained/reset/defaulted/dropped one-line summary). P6 types: managed StellarAlia.Color + AssetRef (16B UUID hi+lo) + Entity (sceneLocalId↔entt bits translation in ScriptSystem::InjectFieldValues / InjectSingleField with schema-routed EntityRef); ScriptDrawer Color (ColorEdit3/4), AssetRef (DrawAssetIDField + SetFieldCommand undo, [AssetType("Mesh")] typeHint filter), EntityRef (picker + SAENTITY drop + "(missing #N)" fallback). P3: EntityIdComponent {uint64 sceneLocalId} auto-emplaced by Scene::CreateEntity from monotonic m_nextLocalId (reset on Clear); Scene::FindBySceneLocalId / AssignSceneLocalId public API; SceneSerializer round-trips scene_local_id, pre-#75 scenes auto-assign on load. O2 attributes (schema wire v2): [Range]/[Tooltip]/[Header]/[HideInInspector]/[SerializeField] + [AssetType] in StellarAlia.Runtime; FieldReflector BuildSchemaBlob writes tooltip/header/flags/range tail; DecodeSchema branches on schemaVersion≥2; ScriptDrawer applies SliderInt/SliderFloat for Range, SetTooltip on hover, SeparatorText before field, skips render for hidden. Defaults: new GetClassDefaultsBlob export uses Activator.CreateInstance to capture C# initializers into ScriptClassSchema.defaults; FetchAndCacheSchema pulls them alongside schema; EnsureValue<T> + RecompileEditing migration both seed from defaults before falling back to kind-zero. SetFieldCommand<uint64_t> added for EntityRef undo. Deferred to #80: List<T> field kind set + variant alts + ScriptDrawer TreeNode UI; nested [Serializable] struct dot-path expansion. -->

## Issue #77 — AssetsPanel `m_pendingDeselectOtherPath` 死代码清理 ✅ DONE
<!-- #73 把 selection 改为 release-over-item 触发后，到达 selection body 即已判定为 click，deferred flush 机制（字段 + OnDraw flush 块 + BeginDragDropSource/背景点击 clear）全成死代码；删字段，多选 single-click 折叠逻辑直接 inline 进 else 分支。 -->

---

## Issue #78 — EditorMode `OnAttach` 缺 shader cook 步骤 ✅ DONE
<!-- LoadProjectFiles helper 现已包含 ShaderCook::CookDirectory + ApplyProjectShaderTypes 调用，OnAttach / LoadProject 路径等价；启动时 .saglsl shading model 能正确进入 cook cache 并在 startup scene 生效 -->

---

## Issue #79 — `ScriptSystem::CaptureFieldValues` 接口空挂

**优先级：极低**（API 已就位，等 PIE 反向同步 feature 接入）

`ScriptSystem::CaptureFieldValues(uint64_t entityId, ScriptComponent& sc)` 在 #74 实现并暴露，对应 managed 端 `ScriptBridgeEntry.CaptureFieldValues`（两步式 blob 协议）。它把 C# 实例的当前字段值抓回 `sc.fields`，反向于 `InjectFieldValues`。

但 #74 内部**无 caller**。当初保留是为未来"Play→Edit 切换时把游戏运行期修改的值带回编辑器"的 PIE 反向同步功能；目前 PIE 退出时直接销毁 game scene，editor scene 的 ScriptComponent.fields 保留 Inspector 设的值。

**何时启用**：用户希望在 Play 中拖滑块/写脚本改变字段值，回 Edit 时这些变化被持久化（Unity-style "during play, changes are reverted on stop" 的反面行为）—— 这是个 UX 选项，需要明确的产品决定。如果决定支持，挂到 `OnPlayStop` 之前的一次 `CaptureFieldValues` 全实体 sweep 即可。


---

## Issue #80 — Script Field 复合类型：List<T> + 嵌套 struct

**优先级：中**（脚本作者常用 list 表达 waypoints / inventory / config arrays）  **依赖**：#75

#75 实施时拆出来的延期范围：`ScriptFieldValue` 加入 `std::vector<T>` 变体 + 嵌套 `[Serializable] struct` 字段的 dot-path schema 展开。范围与 #75 plan F 节一致，但因为扩散面广（每个 kind switch 都要补分支）单独成 issue。

### 范围

1. **List<T> — 标量列表**：
   - `ScriptFieldKind` 加 `ListBool=32 / ListInt32 / ListFloat / ListString / ListVec3 / ListAssetRef / ListEntityRef`
   - `ScriptFieldValue` 加 `std::vector<bool>` 等变体（注意 `std::vector<bool>` 的位优化需 `std::vector<uint8_t>` 替代）
   - blob 编解码：payload = `u32 count + items[]`
   - `FieldReflector.ResolveKind` 识别 `List<int>` / `List<float>` 等具体泛型
   - `ScriptDrawer`：TreeNode 展开 + 单元素 DragXxx + `+ Add` / `- Remove last` 按钮 + 单元素 Undo
   - 软上限 4096 项（plan 边界条件已定）

2. **嵌套 [Serializable] struct — 一层深**：
   - `FieldReflector` 检测 `[System.Serializable]` 标记的 struct 字段
   - schema 展开为 dot-path 子条目（`boss.hp` / `boss.spawn`）
   - C# blob 编解码识别 dot-path → walk 进 struct 字段
   - ScriptDrawer 自动按 dot-path 前缀分组渲染（TreeNode "boss" 包住所有 `boss.*`）
   - 二层嵌套 → 第二层字段显示为 `unsupported` 灰字

### 边界（plan #75 已定）

| 场景 | 处理 |
|---|---|
| `List<List<T>>` | Unsupported（仅一层）|
| `Dictionary<K,V>` | 不做 |
| List<AssetRef> 元素拖拽 | 每 item 单独 `AcceptAssetIDDrop` + Undo |
| List 超过 4096 项 | 截断 + warn |
| 嵌套 struct 整体默认值 | 反序列化时 default(T) 构造 |

### 改动量估计

约 250 行 native + 150 行 managed + 100 行 ScriptDrawer UI。

---

## Issue #81 — Unity 风格 Local/World Transform 脚本 API ✅ DONE
<!-- Scene::EnsureWorldUpToDate 沿父链懒刷新 dirty 节点；ScriptApiExports rename Position→LocalPosition 八件套 + 新增 6 个 World 导出（GetWorldPosition/Set/GetWorldRotationQuat/Set/GetLossyWorldScale/GetWorldMatrix）；ScriptApiFunctionTable version=6（4→5→6）；managed Entity.cs 完全重写 transform 段（LocalPosition/WorldPosition/LocalRotation/WorldRotation/WorldRotationEuler/LossyWorldScale/WorldMatrix 属性 + Translate/Rotate/TransformPoint/InverseTransformPoint/TransformDirection 家族纯 C# 实现）；Forward/Right/Up 修为从 WorldRotation 推；新增 Space.cs (enum Self/World)；demo 三脚本迁移完成 -->

<details>
<summary>原设计（保留）</summary>

原优先级：高（解锁 Issue #71 Phase 4 相机跟随 / 角色朝向脚本，并为 #70 AOT 导出冻结公开 API）

当前 `Entity.GetPosition` / `SetPosition` / `GetRotationEuler` / `GetRotationQuat` / `GetScale` 全部读写 `TransformComponent`，**含义是 local（父级相对）但命名无前缀**。`Forward / Right / Up` 也用 local rotation 推导。`PlayerController.cs` demo 只能正常运行是因为 cube 没有父级（local == world）。一旦挂到旋转过的父节点下，方向向量与坐标都会错。Unity 的解决方式是显式 `localPosition` vs `position`、`transform.forward` 永远是 world、`TransformPoint` / `Translate(Space.Self|World)` 等。本 issue 全量对齐这套语义，**不保留无前缀的旧 alias**——硬切换，三个 demo 脚本一起改。

### 关键问题：脚本与 UpdateTransforms 的时序

[Application::Run](src/engine/Application.cpp#L138) 单帧顺序：
1. `Physics.SyncOut` → 把 Dynamic body 写入 TransformComponent + `MarkDirty`
2. `ScriptSystem.FixedUpdate`（物理子步内）
3. `Mode.OnUpdate`（编辑器逻辑）
4. `AnimationSystem.Update` → 写 `AnimatedTransformComponent`
5. **`ScriptSystem.Update + LateUpdate`** ← 用户脚本在这里跑
6. `active.UpdateTransforms()` ← 整棵树重算 WorldTransformComponent
7. Render

所以脚本读 `WorldTransformComponent.matrix` 时，矩阵反映的是**上一帧末尾**的状态加本帧物理 SyncOut 标脏的位置；动画或先前脚本刚改过的 entity 的世界矩阵尚未刷新。需要**懒刷新**：脚本读 world 时，沿父链向下重算所有 dirty 节点。

### 目标

1. C# `Entity` 公开 API 全部带 `Local*` / `World*` 前缀，**消除歧义**
2. 新增 `WorldPosition` / `WorldRotation` 读写、`LossyWorldScale` 只读（Unity lossyScale 模式，set world scale 在父级非均匀缩放下无解，不暴露）
3. `Forward / Right / Up` 改用 **world** rotation
4. 新增空间转换：`TransformPoint` / `InverseTransformPoint` / `TransformDirection` / `InverseTransformDirection`
5. 新增 `Translate(Vector3, Space)` / `Rotate(Quaternion, Space)` 便利方法
6. 引入 `enum Space { Self, World }`
7. **TransformDirection 家族、Translate/Rotate 是纯 C# 算术**，构建在 `WorldMatrix` getter（这一项需要 native）+ `System.Numerics` 之上，**不要新增 native 导出**

### 设计

#### 命名约定（Unity 风格但带 Local 前缀强制显式）

| C# 旧 | C# 新 | 行为 |
|---|---|---|
| `GetPosition()` | `LocalPosition` 属性 | parent-relative，TransformComponent 直读直写（同旧）|
| `SetPosition(v)` | `LocalPosition = v` | 同上 |
| `GetRotationEuler()` | `LocalRotationEuler` | local 欧拉（度）|
| `GetRotation()` | `LocalRotation` | local 四元数 |
| `GetScale()` | `LocalScale` | local 缩放 |
| —（新增） | `WorldPosition` | 走 WorldTransform，懒刷新 |
| —（新增） | `WorldRotation` | 同上，四元数从 WorldMatrix 提取 |
| —（新增） | `WorldRotationEuler` | 同上，转欧拉 |
| —（新增） | `LossyWorldScale` | 只读，从 WorldMatrix 分解 |
| —（新增） | `WorldMatrix` | `Matrix4x4`，为纯 C# 转换函数提供原料 |
| `Forward / Right / Up` | （保留名字）| **从 `WorldRotation` 算**，修 bug |
| —（新增） | `Translate(v, Space)` | Self = local 增量；World = 世界增量 |
| —（新增） | `Rotate(q, Space)` | 同上语义，左乘 |
| —（新增） | `TransformPoint(p)` | local → world，纯 C# = `Vector3.Transform(p, WorldMatrix)` |
| —（新增） | `InverseTransformPoint(p)` | world → local，纯 C# = `Vector3.Transform(p, inv(WorldMatrix))` |
| —（新增） | `TransformDirection(v)` | local → world（只旋转），纯 C# = `Vector3.TransformNormal` 后再忽略缩放 |
| —（新增） | `InverseTransformDirection(v)` | 同上反向 |

不保留无前缀 `Position` / `Rotation` 别名 —— 一律编译期报错，迫使迁移。Demo 三脚本一并修。

#### 新 native 导出（最小集）

只暴露 **C# 拿不到** 的东西，其余在 managed 侧算。

```cpp
// src/function/script/ScriptApiExports.hpp/.cpp
// All operate via Scene::EnsureWorldUpToDate(entity) before read.

// Local — 旧 API 重命名（行为不变，名字加 Local 前缀）
void SA_Entity_GetLocalPosition     (uint64_t, float*, float*, float*);
void SA_Entity_SetLocalPosition     (uint64_t, float,  float,  float);
void SA_Entity_GetLocalRotationEuler(uint64_t, float*, float*, float*);
void SA_Entity_SetLocalRotationEuler(uint64_t, float,  float,  float);
void SA_Entity_GetLocalRotationQuat (uint64_t, float*, float*, float*, float*);
void SA_Entity_SetLocalRotationQuat (uint64_t, float,  float,  float,  float);
void SA_Entity_GetLocalScale        (uint64_t, float*, float*, float*);
void SA_Entity_SetLocalScale        (uint64_t, float,  float,  float);

// World — 新增
void SA_Entity_GetWorldPosition     (uint64_t, float*, float*, float*);
void SA_Entity_SetWorldPosition     (uint64_t, float,  float,  float);
void SA_Entity_GetWorldRotationQuat (uint64_t, float*, float*, float*, float*);
void SA_Entity_SetWorldRotationQuat (uint64_t, float,  float,  float,  float);
void SA_Entity_GetLossyWorldScale   (uint64_t, float*, float*, float*);
void SA_Entity_GetWorldMatrix       (uint64_t, float[16] out);  // glm column-major
```

WorldRotationEuler 在 C# 侧用 `QuaternionExt.ToEulerDegrees(WorldRotation)` 推；不为它单独建一个 native。

#### 函数表版本：4 → 5

`ScriptApiFunctionTable::version` bump，`NativeApi.ExpectedTableVersion` 同步。新字段**追加到末尾**，保持序列化稳定，旧字段位置不动（其实是 rename Position → LocalPosition，C++ 函数实体也 rename，但表中槽位 0..7 仍是 transform 八件套，只是名字改了）。

> 关键决策：让旧 `Entity_GetPosition` 槽位重命名为 `Entity_GetLocalPosition`，不留废弃槽位。Managed DLL 必须 rebuild（per project memory note "ScriptApiFunctionTable versioning"）。

#### Scene::EnsureWorldUpToDate（核心）

```cpp
// src/function/scene/Scene.hpp/.cpp
class Scene {
public:
    // Walks parent chain from `entity` up to root; recomputes WorldTransform
    // for any node whose dirty flag is true, top-down. After return, the entity
    // and all its ancestors have fresh WorldTransformComponent.matrix.
    // Cost: O(depth) — typically <5 for game scenes.
    void EnsureWorldUpToDate(entt::entity entity);
};
```

实现：
1. 沿 `HierarchyComponent::parent` 收集祖先链到 root（`std::array<entt::entity, 32>` stack-alloc，超出 fallback heap）
2. 反向（root→leaf）扫描，遇到 dirty 节点用 `UpdateTransforms` 同一段逻辑重算（含 AnimatedTransformComponent 优先级）
3. 不更新 entity 的子树 —— 只走"读自己 world"路径

调用点：所有 `SA_Entity_GetWorld*` 进入时先调一次。

#### SetWorldPosition 实现要点

```cpp
void SA_Entity_SetWorldPosition(uint64_t id, float wx, float wy, float wz) {
    auto e = static_cast<entt::entity>(id);
    auto& reg = g_ctx.scene->Registry();
    if (!reg.valid(e)) return;
    auto* t = reg.try_get<TransformComponent>(e);
    if (!t) return;

    glm::mat4 parentInv(1.f);
    if (auto* h = reg.try_get<HierarchyComponent>(e); h && h->parent != entt::null) {
        g_ctx.scene->EnsureWorldUpToDate(h->parent);
        const auto& pw = reg.get<WorldTransformComponent>(h->parent).matrix;
        parentInv = glm::inverse(pw);
    }
    const glm::vec4 localPos = parentInv * glm::vec4(wx, wy, wz, 1.f);
    t->position = glm::vec3(localPos);
    g_ctx.scene->MarkDirty(e);
}
```

SetWorldRotation 类似：`newLocalQuat = inverse(parentWorldQuat) * worldQuat`，其中 `parentWorldQuat = glm::quat_cast(extractRotation(parentWorld))`，extractRotation 用 `glm::decompose` 或自定义除以 scale。

#### Translate / Rotate 在 C# 侧

```csharp
public void Translate(Vector3 delta, Space space = Space.Self) {
    if (space == Space.World) {
        WorldPosition += delta;
    } else {
        // Self-space: rotate delta by current local rotation, add to LocalPosition
        var rotated = Vector3.Transform(delta, LocalRotation);
        LocalPosition += rotated;
    }
}

public void Rotate(Quaternion delta, Space space = Space.Self) {
    if (space == Space.World) {
        WorldRotation = delta * WorldRotation;
    } else {
        LocalRotation = LocalRotation * delta;
    }
}
```

#### TransformPoint 家族（纯 C#）

```csharp
public Vector3 TransformPoint(Vector3 localPoint) {
    Matrix4x4 m = WorldMatrix;
    return Vector3.Transform(localPoint, m);
}
public Vector3 InverseTransformPoint(Vector3 worldPoint) {
    if (!Matrix4x4.Invert(WorldMatrix, out var inv)) return worldPoint;
    return Vector3.Transform(worldPoint, inv);
}
public Vector3 TransformDirection(Vector3 localDir) {
    // 用 WorldRotation 而不是 WorldMatrix 来避免父级缩放污染方向向量
    return Vector3.Transform(localDir, WorldRotation);
}
public Vector3 InverseTransformDirection(Vector3 worldDir) {
    return Vector3.Transform(worldDir, Quaternion.Conjugate(WorldRotation));
}
```

回答用户问题：**对，TransformPoint / TransformDirection / InverseTransform* / Translate / Rotate 全部纯 C# 实现**，只靠 `WorldMatrix` getter + `LocalPosition`/`LocalRotation`/`WorldRotation` 这几个原子 native 调用。每次只一次跨界 + System.Numerics 矩阵代数。

### 文件清单

```
src/function/scene/Scene.hpp                — +EnsureWorldUpToDate decl
src/function/scene/Scene.cpp                — +EnsureWorldUpToDate impl
src/function/script/ScriptApiExports.hpp    — rename Entity_Get/SetPosition→Local; +6 World 导出; version 4→5
src/function/script/ScriptApiExports.cpp    — rename impl; +6 World impl; update SA_Script_BuildFunctionTable
managed/StellarAlia.Runtime/NativeApi.cs    — bump ExpectedTableVersion; rename + 新增 SA_* wrappers; 更新 ScriptApiFunctionTable 字段顺序
managed/StellarAlia.Runtime/Entity.cs       — 完全重写 transform 段；删旧 GetPosition 等；新增 Local*/World* properties + Translate/Rotate/TransformPoint 家族
managed/StellarAlia.Runtime/Space.cs        — 新文件，enum Space { Self, World }
demo_project/assets/scripts/BouncingRotator.cs   — GetPosition()→LocalPosition; GetRotationEuler()→LocalRotationEuler 等
demo_project/assets/scripts/RotatingObstacle.cs  — 同上
demo_project/assets/scripts/PlayerController.cs  — 无需改动（只用 RigidBody，不碰 Transform）
docs/architecture.md                        — 在 "Transform Hierarchy" 段后追加 "Script Transform API" 小节
```

### 实施步骤

- [ ] **Step 1** — `Scene::EnsureWorldUpToDate` 实现 + 单测（建议在 `tests/` 加一个 fixture：3 层父子链，root dirty → 调 EnsureWorldUpToDate(grandchild) → 三个 world 都新鲜）
  - 验证：编辑器现有功能（gizmo、TransformDrawer）行为不变
- [ ] **Step 2** — `ScriptApiExports` rename + 6 个 World 导出 + bump version + 更新 build table
  - 验证：C++ 编译通过；旧 `SA_Entity_GetPosition` 完全不存在（grep 0 命中）
- [ ] **Step 3** — `NativeApi.cs` 镜像调整：`ExpectedTableVersion = 5`；`ScriptApiFunctionTable` 结构字段对齐；wrapper 方法 rename + 新增
  - 验证：managed DLL 编译通过；ScriptBridgeEntry 启动时版本校验不报警
- [ ] **Step 4** — `Space.cs` 新文件 + `Entity.cs` transform 段完全重写
  - 验证：managed DLL 编译通过；XML doc 注释齐全（避免 CS1591）
- [ ] **Step 5** — 迁移三个 demo 脚本到新 API
  - 验证：进 Play mode，BouncingRotator 仍然上下浮动 + 自转；RotatingObstacle 仍然旋转；PlayerController WASD 移动正常
- [ ] **Step 6** — 父子链回归测试：把一个 demo entity 挂到一个旋转 60° 的父节点下，验证：
  - `LocalPosition` 不变
  - `WorldPosition` 反映父级旋转
  - `Forward` 指向世界 −Z 经过父级旋转后的方向
  - `TransformPoint((0,0,0))` 返回父级原点
- [ ] **Step 7** — `architecture.md` 补充 "Script Transform API" 小节，记录 Local/World 语义 + 帧序时序 + EnsureWorldUpToDate 懒刷新策略

### 边界与约束

| 场景 | 处理 |
|---|---|
| Entity 无 parent | `parentWorld = identity`，World ≡ Local，set 直接写 LocalPosition |
| 父级非均匀缩放下设 WorldRotation | 只提取父级 rotation 分量做 inverse；结果矩阵可能视觉上歪斜，是 Unity 同款限制，文档说明 |
| World scale setter | **不暴露**。用户需手动调整 LocalScale 和父级 scale 链 |
| FixedUpdate 中读 world | 同样调 EnsureWorldUpToDate；物理 SyncOut 已 MarkDirty，刷新逻辑一致 |
| AnimatedTransformComponent 存在 | EnsureWorldUpToDate 走和 `Scene::UpdateTransforms` 同一段逻辑（Animated 优先）|
| Entity 无 TransformComponent | World 读返回 (0,0,0)/identity；set 静默 no-op（保留现有错误兜底约定）|
| 旧脚本编译 | 硬切，编译错误。迁移路径明确：`GetPosition()` → `LocalPosition`；只动 3 个 demo 文件 |
| `Forward / Right / Up` 行为变化 | **如果用户已依赖错误的 local 推导**，会破坏旧行为。Demo 里没人用这三个，社区脚本（如果有）需迁移到 `LocalRotation` 自己算 |
| ScriptApiFunctionTable 字段顺序 | 严格 append-only，C# 镜像跟着改；版本号 bump 强制双端同步 |
| 中间运行的 native exe | 函数表 rename + 新增字段，bin/StellarAlia.exe 必须重链；managed DLL 必须 rebuild（per memory note）|

### 受益 issues

- **Issue #71 Phase 4 / 后续脚本 demo**：相机跟随 (`camera.WorldPosition = target.WorldPosition + offset`)、character look-at (`Self.WorldRotation = LookRotation(target - Self.WorldPosition)`) 现在可写
- **Issue #70 游戏发布 + AOT**：公开 script API 命名冻结点。AOT 编译前定下 Local/World 语义，避免发布后破坏性改名
- **Issue #80 嵌套字段**：序列化层无关，但 transform 字段属性的可见性约定（Inspector 显示 local）由本 issue 文档化后清晰

### 不做

- `transform.parent` 读写（hierarchy mutation 由 SceneHierarchy 命令系统负责，脚本侧暂不开）
- `LossyWorldScale` 可写（Unity 也不行）
- `Quaternion.LookRotation` / `Quaternion.FromToRotation` —— 纯 C# 扩展，可作为 `QuaternionExt` 后续追加，不阻塞本 issue
- 性能优化（如把 EnsureWorldUpToDate 改成基于 dirty-bit 跳过未脏链）—— 等真有 profile 数据再做

### 改动量估计

约 130 行 native（Scene::EnsureWorldUpToDate ~30 + 6 个 World 导出 ~80 + 函数表 ~20）+ 180 行 managed（NativeApi 同步 + Entity.cs 重写 ~120 + Space.cs ~10 + Translate/Rotate/TransformPoint 家族 ~50）+ 30 行 demo 脚本迁移 + 50 行 architecture.md 文档。

</details>

---

## Issue #82 — 脚本项目 managed 链接库本地化 ✅ DONE
<!-- Application::GenerateIdeProjectFiles 新增 CopyManagedLibsToProject helper（复制 StellarAlia.Runtime.{dll,pdb,xml} 到 {projectDir}/Library/managed/）+ LoadGitignoreTemplate helper；Directory.Build.props 改用 $(MSBuildThisFileDirectory)Library\managed 相对路径，可提交；新增末尾 WriteFileIfMissing(.gitignore) 写入；ProjectManager::CreateProject 同步复制 .gitignore.template；assets/templates/project/.gitignore.template 内容更新（删 Directory.Build.props，加 Library/ + bin/）；demo_project 修复 .gitignore + Directory.Build.props。运行时仍走 BIN_DIR/managed 不变 -->

---

## Issue #83 — Skinning 工业级补全（伞 issue / Roadmap）

**优先级：高（启动时机：#46-#49 后处理系列全部完成后；预计总工程量 9-11 周）**

### 背景

调研发现 StellarAlia 当前 skinning 仅覆盖"vertex shader LBS + 单 clip 播放"基础路径，距离 UE5 / Unity / Frostbite 现代角色动画系统差距较大。本 issue 把工业级补全拆成 7 个 phase，每个 phase 单独成一个落地 issue（编号待分配，姑且记为 #83.P1–#83.P7）。**本 #83 是路线图与依赖图，不包含实施细节**；每个 phase 真正动手时再单独 `/plan`。

### 启动前置

- **必须**：#46 Phase 1（Motion Blur Camera Mode）落地 — 给后面 Phase 1 一个 motion blur Phase 2 的明确消费方
- **必须**：#47-#49 计划状态明确（不一定全做完，但不能与 skinning 工程同时抢渲染器改动窗口）
- **建议**：#19 拆出的 ".saskel / .sanim source asset + 三级解析" 跑通到能 cook（如果 #19 一直没动，归并到本路线 Phase 1）

### 关键架构原则

整个路线遵循三条不可妥协的设计约束：

1. **AnimationSystem CPU evaluate + GPU LBS 仍是主路径**——不重写到 compute skinning，除非 Phase 7 性能压力出现
2. **每一个 phase 都要在结束时跑通 demo_project 现有角色动画**（不引入回归）
3. **PrevBonePose double-buffer（Phase 1 落地）是后续所有 phase 的公共基础设施**——一旦定下 layout，所有后续 phase 共用

### 路线

#### Phase 1 — 基础设施（残余 ~3-4 天）

**目标**：搭好后续 6 个 phase 的公共基础设施。本 phase 不产生用户可见效果（除了渲染稳定性提升），但所有后续 phase 都依赖它。

- ~~**PrevBonePose double-buffer**~~ — ✅ **已在 #84 完成**（吸收路径）。`SkinnedMeshComponent` 已含 `skinMatricesBufferPrev` + `velocityDescSet`；`AnimationSystem::Update` swap + force-reseed 已落地
- **mat4 → mat3x4 压缩**：bone matrix 在 SSBO 里改成 3 行 4 列存储，省 25% 带宽（double-buffer 后实际省 50% 因为乘 2）；skin_deform.glsl + skin_deform_dual.glsl 同步加 helper
- **Static Pose Skip**：AnimationSystem 检测 clip 已 paused 或 evaluate 后 pose 与上帧字节相同 → 不重新上传 SSBO（标记 `lastUploadedHash`）。Idle 角色 SSBO 上传降为 0
- **SkeletonAsset 一级化**：`.saskel` 资产格式定稿（脱离 DeriveSkinID 推导），cook tool 输出独立 `<uuid>.saskel`，SkinnedMeshComponent 可显式覆盖 skeleton（吸收 #19 Section F）

**关键交付物（残余）**：skin_deform.glsl 用 mat3x4 + .saskel 资产格式 + AnimationSystem upload 静态 pose 短路

**受益**：~~#46 Phase 2~~（已 unlock）、TAA 升级、cloth/hair 未来集成；其他 phase 的隐式依赖底座

#### Phase 2 — 动画运行时基础（~1.5 周）

**目标**：单 clip 播放 → "可以做完整动作游戏前 70% 需求"的混合播放层。

- **Clip Crossfade**：`AnimatorComponent` 加 `fromClip / toClip / blendTime / blendProgress`；EvaluateAll 在 blend 期间对两 clip 各 evaluate 一次然后 Lerp pose（position lerp / rotation slerp / scale lerp）。**0.15s 默认 crossfade 是工业基线**
- **Animation Events / Notify**：`.sanim` 加 events 数组（time, name, payload string）；AnimationSystem 跨帧扫描 [lastTime, currTime] 内 events，推到一个 `g_ctx.scene.PendingAnimEvents` 队列；ScriptSystem 拉取并按 entity 派发 `OnAnimEvent(name, payload)` 给 C# 脚本
- **Root Motion**：`.sanim` 可标记 root bone（root motion extraction target）；evaluate 时把 root bone 的 translation/rotation **从 pose 拿出来**写到 entity Transform（而不是写入 bone matrix），bone 自身保留为相对静止。Editor 控件选 "Apply Root Motion / Keep In Place"
- **ACL 集成**：第三方库 [Animation Compression Library](https://github.com/nfrechette/acl)（MIT）；MeshImporter 把 raw keyframe 通过 acl_compressor 编码成 ACL stream；运行时 `acl_decompress` 在 EvaluateAll 内替换原 SampleKeyframes。资源体积 ~95% 缩减

**关键交付物**：crossfade 体验立竿见影；ACL 让 demo_project 资源 -90%；root motion 解锁攻击位移

**受益**：脚本侧立即可以做"按 W 跑步动画 + 释放 W 渐回 idle"；攻击动作位移和音效同步打通

#### Phase 3 — Blend Tree 与 Layered Animation（~1 周）

**目标**：让动画"参数驱动"——locomotion blend、上下半身分离。

- **1D Blend Tree**：`Blend1DNode { float param; clip[] children; threshold[] }`；运行时按 param 在 threshold 段间 lerp。典型用例：walk_speed 0→3 m/s 在 idle/walk/run 三 clip 间插值
- **2D Blend Tree**（Cartesian / FreeformDirectional 两种）：8 方向 strafe locomotion 经典用例
- **Animation Mask + Layered Playback**：`AnimatorComponent.layers[]`，每层有 weight + bone mask（bitset 标记哪些 bone 此层覆盖）；EvaluateAll 按 layer 顺序 over-write 或 additive 混合。Upper body Layer 用 spine_01 之上 bone mask 可以独立播射击动画

**关键交付物**：Locomotion 流畅过渡；射击/挥剑 upper body 与跑动 lower body 解耦

**前置**：Phase 1 PrevBonePose 已就绪；Phase 2 Crossfade 已就绪（layer 内部仍用 crossfade）

#### Phase 4 — State Machine + Animator Editor（~2 周）

**目标**：从"代码驱动 PlayClip"过渡到"数据驱动状态切换"，对齐 UE AnimGraph / Unity Animator Controller。

- **AnimGraph 运行时**：状态图序列化到 `.saanimator` 资产（节点 = State，边 = Transition with condition）；条件支持 `param > value` / `bool param` / `trigger`；运行时按事件驱动状态切换
- **Animator Editor**（吸收 X-3 / 旧 #31）：ImNodes 节点图 + ImGuizmo 状态盒摆放 + transition 连线 + parameters panel
- **集成 Phase 2 / Phase 3**：State 内部可以是单 clip / blend tree / 子 state machine；transition 期间走 Crossfade

**关键交付物**：完整 AnimGraph 数据流；编辑器可视化设计角色行为

**前置**：Phase 2 + Phase 3 全部就绪；ImNodes 库引入

#### Phase 5 — IK Solvers（~1 周）

**目标**：让角色"对环境响应"——脚踩地面、眼睛追物体、武器握把对齐。

- **Two-Bone IK**：解 elbow / knee 一对骨头朝 target；闭式解，~30 行代码
- **Look-at IK**：单 bone 朝 target 的 rotation 修正，with constraints (cone limit)
- **Foot IK Pipeline**：raycast 检测脚下地面 → 调整 IK target 抬升脚 → two-bone IK solve 膝盖 → root rotation 修正
- **CCD / FABRIK**：不做 v1（应用面窄）；如果未来需要"长链 IK（绳/触手）"再补

**关键交付物**：`IKConstraintComponent`（target bone + chain length + weight）；AnimationSystem 在 EvaluateAll 末尾、上传前跑 IK pass

**前置**：Phase 1 PrevBonePose 已就绪（IK 修改 currPose，prevPose 反映上帧的 IK 结果，motion blur 才连贯）

#### Phase 6 — Morph Target + Retargeting（~2 周）

**目标**：脸部动画 + 跨骨架动画复用。

- **Morph Target / Blend Shape**：
  - `.samesh` 格式扩展：附加 N 个 morph delta（每个 ~vertex delta + normal delta）
  - SkinnedMeshComponent 加 `morphWeights[N]`
  - vertex shader 在 skin 之前先按 weights 应用 deltas（额外 SSBO + 顶点端循环）
  - Editor 拖 weight slider 实时变形 + 序列化到 .sascene
- **Retargeting**：UE5 IK Rig 思路简化版
  - 定义 "BoneAlias"（Head / Spine / LeftUpperArm / ...）映射两套 skeleton 的对应 bone
  - 运行时把 source clip 的 bone-by-name 改成按 alias 找 target skeleton 的对应 bone，复用 pose
  - 编辑器配置 `.saretarget` 资产（source skeleton + target skeleton + alias map）

**关键交付物**：脸部表情动画跑通；UE 角色资源可在 StellarAlia 自定义骨骼角色上复用

#### Phase 7 — 性能优化 + 渲染集成（~1 周）

**目标**：解决"100 个 NPC 时的 CPU/GPU 瓶颈"+ "其他 feature 想读 skinned 顶点位置"。

- **Skinned Mesh LOD**：远处 NPC 跳到简化骨架（mesh LOD 切换时同步骨架级别）+ 动画更新率降频（远处每 2-4 帧 evaluate 一次，pose 插值过渡）
- **GPU Compute Skinning Prepass**（可选）：把 vertex shader 内联 LBS 移到 compute pass，输出 skinned vertex buffer 到 RG handle `handles.skinnedVertices`；下游 cloth / hair / accurate shadow caster / future ray-traced features 都可读
- **Bone Gizmo 可视化**（吸收 X-2）：球 + 锥绘制骨架；EditorMode debug overlay 可开
- **Profile + 优化 hot path**：1000 bone 角色 evaluate 时间目标 < 0.5ms/frame

**关键交付物**：百级角色场景性能稳定；skinned vertex 数据可被其他 feature 消费

### 依赖图

```
Phase 1 (基础设施) ───┬──→ Phase 2 (Crossfade/Events/Root/ACL)
                     │         │
                     │         ├──→ Phase 3 (Blend Tree/Layers)
                     │         │         │
                     │         │         └──→ Phase 4 (State Machine + Editor)
                     │         │
                     │         └──→ Phase 5 (IK Solvers)
                     │
                     ├──→ Phase 6 (Morph + Retargeting)  ← 独立分支
                     │
                     └──→ Phase 7 (Perf + Compute Skinning)  ← 独立分支
                                  │
                                  └──→ unlock: cloth / hair / ray-traced shadow
```

Phase 1 必须先做。Phase 2-7 中 P6 / P7 可与 P3-P5 并行（只要 Phase 1 done）。

### 总工程量估算

| Phase | 估时 | C++ 代码量 | GLSL/Shader | 资产格式改动 | 第三方依赖 |
|---|---|---|---|---|---|
| P1 基础设施 | ~1 周 | ~400 行 | ~30 行 | .saskel 资产格式 | — |
| P2 运行时基础 | ~1.5 周 | ~700 行 | — | .sanim events + ACL stream | **ACL** (header-only) |
| P3 Blend Tree + Layers | ~1 周 | ~500 行 | — | AnimatorComponent 扩展 | — |
| P4 State Machine + Editor | ~2 周 | ~1200 行 | — | .saanimator 资产 | **ImNodes** |
| P5 IK Solvers | ~1 周 | ~400 行 | — | IKConstraintComponent | — |
| P6 Morph + Retargeting | ~2 周 | ~800 行 | ~40 行 | .samesh v6 morph data + .saretarget | — |
| P7 Perf + Compute Skin | ~1 周 | ~600 行 | ~80 行 | mesh LOD bone map | — |
| **总计** | **~9.5 周** | **~4600 行** | **~150 行** | 4 个新格式 / 1 个升级 | ACL + ImNodes |

### 与 todo 现有条目的吸收

启动本路线时一并 close 以下既有短条目：

- **X-2**（原 #27 骨骼球+锥绘制）→ 并入 P7 Bone Gizmo
- **X-3**（原 #31 animator 编辑器 imguizmo）→ 并入 P4 Animator Editor
- **#19 拆出的"Animation 运行时三级解析"**：如未独立落地，并入 P1 SkeletonAsset 一级化

### 不做（明确 out-of-scope，留更远 issue 或不做）

- **Animation Streaming**（动画从磁盘按需 stream，不全部驻留内存）— 项目规模未达
- **Motion Matching**（数据驱动找 best-fit 动画帧）— 工程量极大，需要专用动画师 + 算法支持，留作 v3.0 远期
- **Procedural Animation / Inverse Dynamics**（rag-doll 模拟）— 不属于动画系统，归 Jolt 物理范畴
- **Animation Authoring Tools**（在 editor 里编辑 .sanim）— StellarAlia 定位是 runtime + 美术管线消费 DCC（Blender/Maya）产出，不重做 DCC

### 受益（整个路线全部完成后）

- **#46 Phase 2 Motion Blur per-object velocity**：P1 PrevBonePose 直接 unlock
- **#42 TAA per-object velocity 修复**：同上
- **#48 SSR per-pixel velocity 不脱影**：P1 unlock
- **#33 贝塞尔曲线相机**：P4 State Machine 思路可移植到 camera spline animator
- **#23 帧率优化伞**：P7 Skinned LOD + 静态 pose skip + mat3x4 综合提速
- **未来 cloth / hair simulation**：P7 GPU Compute Skinning Prepass 是输入源
- **未来动作游戏 demo**：P2-P5 完整覆盖动作游戏 90% 动画需求

### 时间安排建议

| 月份相对位置 | Phase | 备注 |
|---|---|---|
| #49 完成 + 0 周 | P1 启动 | 立即给 #46 Phase 2 / TAA 升级做基础设施 |
| +1 周 | P2 启动（P1 done） | ACL 集成可能稍慢；并行做 #46 Phase 2 落地 |
| +3 周 | P3 启动 | |
| +4 周 | P4 + P5 并行启动 | P5 较小可塞缝 |
| +6 周 | P4 完成；P6 启动 | |
| +9 周 | P7 收尾 | 全部完成约 10 周 |

---


## Issue #85 — TAA 读 handles.velocity（unjittered velocity 接口统一）✅ DONE
<!-- FrameUniforms +mat4 currUnjitteredViewProj（size 640→704），ApplyCameraToUniforms 写入；VelocityPrepass shaders rasterize 仍用 jittered viewProj（matches GBuffer depth），velocity 输出改用 currUnjitteredViewProj × prevViewProj → handles.velocity 接口语义冻结为 unjittered NDC velocity（对齐 UE5 nonJitteredProjMatrix / HDRP nonJitteredVP）；taa_resolve.frag 加 set=2 binding=3 t_Velocity，删 WorldPos+jitter 反偏移函数，prevUV = v_TexCoord - texture(t_Velocity).rg 直接 reproject → TAA 移动 rigid body / skinned mesh 不再 ghosting；VelocityPrepassFeature::AddPasses 门控扩展为 motionBlur OR TAA enabled（未来 #48 SSR 加 OR 条件即可） -->

---

## Issue #84 — Motion Blur Phase 2（Per-Object Velocity Writes）✅ DONE
<!-- VelocityPrepassFeature 独立 prepass 跑在 GBuffer 后、SSAO 前，仅当 motion blur enabled 时 AddPass（disabled 时 RG 不分配 velocity 物理槽）；per-draw push currModel+prevModel = 128B；skinned 路径走 set=3 (curr/skinData/prev) 通过 velocity_prepass_skinned.vert + skin_deform_dual.glsl 双 pose 采样；MotionBlurFeature 删除 MB_Velocity pass（被 prepass 取代）+ 删除 motion_blur_velocity.frag；SkinnedMeshComponent 加 skinMatricesBufferPrev + velocityDescSet + poseSeeded + lastEvalClipId 实现 PrevBonePose double-buffer；AnimationSystem::Update 内 pointer swap + 三个 force-reseed（first-write/mesh swap/clip swap）+ UPDATE_AFTER_BIND-safe 的 re-bind；EvaluateAll (scrubbing) 不动 prev；PrevTransformComponent auto-emplaced by Scene::CreateEntity，UpdateTransforms 顶部 snapshot + 末尾 first-frame seed → 静态物体也走 prepass 自动产生 camera velocity。吸收 #83 P1 的 PrevBonePose 子项（mat3x4/static skip/saskel 仍留 P1 残余）-->

---

## Issue #86 — ProgramCache：统一 GPU 程序持有层（重构）✅ DONE
<!-- Phase 1+2 完成：新增 Resource 层 `ProgramCache`（SceneRenderer 持有、经 FeatureInitContext::programs 注入）集中持有所有 ComputeProgram + ShaderProgram，按 holder key 缓存（不去重）、engine/project 双作用域、统一热重载。compute features（AutoExposure/Tonemap/SSR）改 `GetCompute` 持裸指针；MaterialType::shader 由按值改 `ShaderProgram*`（GetGraphics，单次 .refl 加载，merged 反射从 program 取）；4 个 feature skinned 变体 + MaterialManager 注册路径全迁入 cache；清理顺序 instances→types→programs 防双 free。ComputeProgram 外部 frameLayout 改占 set=1；frame 全局 sampler stage→All 供 compute 采样。Phase 3（ComputeDispatch）撤销——program 之上无单一抽象，按用例分叉，拆出独立的 ScreenEffect 后处理 issue（见 #88）。 -->

---
## Issue #87 — PSO 预编译 / pipeline cache 持久化（发布向优化）

**优先级：低（仅"若发布游戏"才需要；关联 #70 项目导出）**

引擎当前运行时惰性 `GetOrCreatePipeline`（create-on-miss），编辑器/研究用足够。发布游戏需消除首遇材质+state 组合时的 shader 编译卡顿：录制 PSO desc 清单 → 加载期 worker 线程预建暖 cache + 落 `VkPipelineCache` blob 跨运行复用。属 #70 发布管线的子项，待发布需求出现再展开。

---

## Issue #88 — ScreenEffect：声明式自定义后处理 ✅ DONE
<!-- 用户写 .saeffect(@Effect/@Stage/@Inject/@In/@Out + 内联 @Param)零 C++ 插入自定义后处理 pass = .saglsl→MaterialType 的后处理侧同级。Phase0 ShaderReflection 泛化为通用 metadata map;ScreenEffectType/ScreenEffectRegistry 落 function/material(仿 MaterialType/MaterialManager),ScreenEffectFeature 为 SceneRenderer nested feature(4 注入锚点 AfterLighting/AfterTAA/BeforeTonemap/AfterTonemap + RenderFrame redirect handles.hdr);cook 走 ShaderCookLib::CookEffects 产 <stem>.saeffect.{frag,comp}.{spv,refl}+metadata(标准 refl,机制同 shadingModel);资产集成 .saeffect→type Shader(自动 .sameta/脚本图标/文本 inspector/Shader 子菜单新建+NewEffect.saeffect 模板);激活模型=Unity Volume/UE Blendable:PostProcessSettings.screenEffects 每场景有序栈(序列化)+PostProcessPanel add/remove/enable/拖拽/参数+Add Effect 目录,ApplyWorldSettings 解析为 m_activeScreenEffects;ParamWidgets 公共反射 param 控件层(材质 MaterialOverrideDrawer 共用);GPU 安全 mid-frame 触发走 RequestProjectShaderReload 延迟到 RenderFrame 顶应用。遗留→#90:双 cook 目录/启动幽灵 effect/焦点自动 cook 仅 .cs。 -->

---

## Issue #89 — SSR Phase 2：Hi-Z 步进 + 空间去噪 ✅ DONE
<!-- 接续 #48。5 个 compute pass 内聚于 SSRFeature：HiZ_Copy(depth→R32F mip0) + HiZ_SPD(min 金字塔,复用 #94 SPD 的 min 变体 + SPD_SRC_IMAGE in-place 源) → SSR_Trace(ssr.comp,8-spp GGX 随机化,屏幕空间 Hi-Z DDA `HiZTrace`) → SSR_Resolve(轻 3×3 双边预去噪) → SSR_Temporal(velocity 重投影 + 自适应计数累积 alpha=count/(count+1) + mean±1.5σ 方差裁剪防 ghosting) → split-sum 合成替换 IBL 高光。traversal 血泪 5 修=透视正确视空间深度(消横条带)/cell 深度平面交点 tCross∈[t,tCell](消分形三角洞)/texel 比例 crossOffset(消竖条纹)/屏幕射线视口裁剪(消近平面假命中黑块)/viewR.z≥0 剔除朝相机射线(消下半采黑减 IBL 黑块)。新增 shader hiz_copy/hiz_spd/spd_reduce.glsl/ssr_resolve/ssr_temporal;SSRFeature +Hi-Z 纹理/历史 ping-pong/trace-resolve-temporal 三 set;VelocityPrepass gate 加 ssrEnabled。剩余截断/底面/off-screen 为 SSR 固有→回退 IBL。Phase D(PostProcess resolve 参数 UI + profiling)未做→ #95。 -->
---

## Issue #90 — 文件流操作收敛（File I/O 集中化 / 去重）✅ DONE
<!-- 新建 core/io/FileIO 门面(ReadText/WriteText/ReadBytes/WriteBytes/ReadJson/WriteJson 出参+json_fwd、Copy(避Win32宏)/Rename/Remove/EnsureDir、CopyTemplateReplacing;统一 error_code+SA_LOG)——全库不再手写 ifstream/ofstream(仅二进制 Cooked*.cpp 有意保留结构化 read/write)。MetaFile 从 tools/importer 下沉 src/resource(唯一 .sameta parser,删 AssetRegistry::ParseSameta + InputMapLoader::ReadUuidFromSameta 两份重复)。CreateNewFile/CommitRename/DeletePath/MoveAsset + SceneSerializer/SaProject/InputMapLoader/EditorShortcutConfig/InputMapImporter 全迁 IO::。cook 目录统一到 cook_cache/shaders(CookProjectShaders 不再写 BUILTIN_SHADER_DIR + 补 reimport RecompileDeferredLighting);PruneOrphanedEffectCookOutputs 两 cook 入口都调用(根治启动幽灵 effect);FileWatcher 焦点自动 cook 扩展 .saglsl/.saeffect(m_pendingShaderCook,同 .cs)。docs/architecture.md 增 File-Stream Flow Overview + File I/O layer + Cook-path unification 三段。遗留:.saglsl 材质孤儿(命名不派生于文件名)+ 其余 tools fs 点未迁,低优先级。 -->

---

## Issue #91 — ScreenEffect Phase 2：compute stage ✅ DONE
<!-- 核心 compute 执行路径完成:ScreenEffectFeature::AddPasses 拆 frag/compute 分支——compute @Out 瞬态加 UnorderedAccess、set=2 b0 EffectParams UBO/b1..K @In sampled/b(K+1) @Out storage、WriteUAV+SetComputePipeline+SetDescriptorSet(1,frame)(2,eff)+Dispatch((w+7)/8,(h+7)/8),@Out hdr 链式(镜像 SSRFeature)。comp 模板 NewEffectCompute.saeffect(8×8 + b0 UBO/b1 t_hdr/b2 out_hdr image2D rgba16f)+ AssetsPanel CreateKind::SaeffectCompute(Shader 子菜单 "Screen Effect — Compute" + EntityTemplateRegistry::EffectComputeTemplatePath)+ demo tint_compute.saeffect。ProgramCache::GetCompute 加 primaryDir/fallbackDir(空→builtin,SSR/AE 不受影响),registry Scan isCompute 传 ctx.shaderDir+engineShaderDir → 解锁项目目录 compute .saeffect(清 #88 遗留)。ResolveEffectHandle 已含 hdr/depth/gbuffer/velocity/ssaoTex/taaResolved。构建全绿;实机验证 opt-in 栈 "Tint (Compute)" 生效。LDR(@Out ldr 跨缓冲 + ldr 词汇)+ Tonemap→compute 移出→ #93(互相耦合、主驱动 tonemap 已延后)。 -->

---

## Issue #92 — 内建后处理 compute 重写（bloom）✅ DONE
<!-- BloomFeature 4 道 pass 全 frag→compute(引擎 feature 内部 MaterialType→ComputeProgram,仿 SSR):threshold/downsample 纯写 UAV(imageStore);upsample/composite 原硬件 Additive→imageLoad+add+imageStore(readwrite image2D rgba16f)。新增 bloom_{threshold,downsample,upsample,composite}.comp;删死文件 bloom_*.frag(5 个)。BloomFeature 4 个 ComputeProgram* 成员,OnInit/RebuildDescSets 从各 GetLayout(2) 分配 descSet(b0 sampler+b1 storage),AddPasses BindStorageImage+WriteUAV+SetComputePipeline+Dispatch((w+7)/8,(h+7)/8);mip 链+HDR_Color 加 UnorderedAccess usage(保留 RT|Sampled)。统计核实:不改变 texture/buffer 字节(仅 usage 标志;frag 类型零 texture/buffer 分配),变化仅 graphics program/pipeline→compute,GetMemoryStats 正确无泄漏。PostFX 保留 frag(直写 swapchain+无 RHI blit,compute 净负);Tonemap/SSR compute 化移出→留其它 issue(SSR 见 #89)。全库仅注释/RG pass 名提及 Bloom。 -->

---

## Issue #93 — ScreenEffect LDR / Tonemap-compute（接续 #91）

**优先级：中低（#91 分出;三步互相耦合,主驱动 Tonemap→compute 需先定机制）**

> 从 #91 移出(用户 2026-07-01):单独做 @Out ldr 无当前消费者,与 tonemap 一并推进更合理。

### 待展开(/plan)
- **ldr 词汇 + @Out ldr 跨缓冲**:`ResolveEffectHandle` 补 `ldr`;执行器分辨 `@Out hdr` vs `@Out ldr`(各自 redirect `handles.hdr`/`handles.ldr`);`@Out ldr` 的 compute 效果写 RGBA16F storage 瞬态(swapchain 不能作 storage image)后 redirect `handles.ldr`;回归 PostFX 开/关(PostFX 读 ldr 写 swapchain)与 present-copy。词汇表标注 swapchain 仅 frag-out。
- **Tonemap→compute**:C1 常驻内建效果(registry always-on 标志 + 注入序)/ C2 TonemapFeature 委托 compute 执行 helper / C3 引擎 feature compute 重写(仿 #92 bloom,最简)三选;`hdr→ldr` 跨缓冲(ldr 转 RGBA16F storage,见上);CG-LUT/exposure 迁移;LutTonemap 协调;像素等价。
- 关联:#92 已建立"内建 frag feature→compute"范式(C3 可直接复用);#91 已提供 compute .saeffect 执行路径(C1/C2 可复用)。

---

## Issue #94 — SPD 单趟降采样基础设施（Single Pass Downsampler / mip-chain 生成）✅ DONE
<!-- 两层基础设施：①per-mip 数组 UAV 绑定 `WriteDescriptorStorageImageArrayMip`(IRHIDevice/VulkanDevice，旧 `...Mip` 委托它 arrayElement=0) + FrameContext `BindStorageImageArrayMip`(m_pendingStorageArrayMips + FlushBindings)；storage-image 数组反射 arraySize→layout descriptorCount。②完整单趟 SPD `assets/shaders/spd_downsample.comp`：tile64/wg256 局部归约 mip1..6 + `coherent image2D u_mips[12]`+`coherent`原子计数器末组接力 mip7..12，单 dispatch 到 4096²，`SPD_REDUCE` 宏可定制(默认 avg，#89 用 min)，LDS-only(vulkan1.0)，无需 RG per-subresource。测试：rhi_interface_demo(接口断言) + examples/spd_test(真机 readback vs CPU box-average，256²/1024² 全 maxErr=0)。既有 mip 操作迁移评估=均不迁移(shadow 金字塔/运行时 mipmap 无实现；AutoExposure 直方图非 mip；Bloom 13-tap Karis 无法用 2×2 SPD 复现)。消费者：#89 Hi-Z(首个)、未来 #57 GPU-driven 遮挡剔除(HZB)。SPD 只服务归约型 mip。 -->
---

## Issue #95 — SSR Phase D：resolve 参数 UI + profiling（#89 收尾拆出）

**优先级：低（#89 已可用；本 issue 为参数暴露 + 性能）**

从 #89 拆出的收尾项（#89 核心已 DONE 并可视验收通过）：

- **PostProcess 参数暴露 + 序列化**：把当前硬编码的 SSR 去噪参数做成可调 —— resolve 空间半径、temporal 自适应计数上限(现 24)、方差裁剪 K(现 1.5)、每帧采样数(现 8-spp)。经 `PostProcessSettings` + `SceneSerializer` 双向 + `PostProcessPanel` UI（对齐现有 ssr* 字段模式）。
- **性能 profiling**：8-spp × 5 pass 的实测开销（Tracy scope），与 #48 对比；按需给采样数/半径提供质量档位（低/中/高）。
- **可选画质增强**：temporal 的 miss-帧 conf 兜底（防罕见闪烁）；neighborhood clamp 对移动反射物体的 ghosting 进一步调优；half-res trace + full-res resolve 省带宽。
