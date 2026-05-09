---

## Issue #34 — 全局快捷键配置窗口（含配置缓存）✅ DONE
<!-- ActionDef::userConfigurable 过滤白名单；EditorShortcutConfig(Load/Save/Reload/Import/Export/ApplyTo) 直接存 BindingDef；ShortcutsPanel 动态枚举+按键捕获+Default/Reload/Import/Export 按钮+内置路径只读；InputSystem 严格 modifier 匹配(HasExtraModifiers)修复 Ctrl+Shift+S 落回 Ctrl+S 问题 -->

---

---

## Issue #17 — 双击交互升级：短双击聚焦相机 / 长双击重命名 ✅ DONE
<!-- DoubleClickClassifier + EditorCamera::FocusOn；Hierarchy/Assets 多选(Ctrl/Shift/Ctrl+A)；短双击→聚焦/长双击→内联重命名(0.20s 阈值)；Composite 快捷键(SelectAll/NewScene/SaveScene/EntityDuplicate)；TextInput map gate 修复 WASD/快捷键冲突 -->

---

## 待办issue

24. **[低优先级] 长耗时操作进度反馈**
    - **启动进度条**（难度高）：`OnAttach` 在渲染循环前同步执行，ImGui 无法渲染。需重构为两阶段延迟初始化或独立 splash screen 渲染通道，暂不做。
    - **Reimport All 进度条**（难度中，最有实际价值）：`ReimportDir` 同步阻塞 UI。改法：将其拆成逐帧 N 个文件的状态机，`OnDraw` 期间推进并用 `ImGui::ProgressBar` + modal 显示；或移入工作线程 + 原子进度计数器。
    - 前置条件：依赖 `AssetsPanel` 暴露异步迭代接口；待项目素材量增大后再做。
27. 美化编辑器：骨骼改用球+锥绘制而不是线
28. 美化编辑器：灯光、相机等不可视物体添加icon贴图作为标识，素材由我提供
29. 引入挂载脚本 并提炼现有运行时库暴露给脚本调用
30. 屏幕空间射线求交：编辑器中点击选中物体，拖拽物体到场景
31. animator编辑器 imguizmo
32. 材质可视化编程 imguizmo
33. 场景物体：贝塞尔曲线 imguizmo
36. **[极低优先级] RHI VkMemory 级别别名（Approach A）**


---

## Issue #35 — 渲染资源内存统计 ✅ DONE
<!-- RGStats 结构体（Execute() 末尾填充）+ CalcTextureBytes/FormatName static helpers + SceneRenderer::GetRenderGraph() + SettingsPanel "Render Stats" 折叠区（counts + MB + per-texture detail table） -->

---

## Issue #16 — RG 资源别名（RenderGraph Handle 复用）✅ DONE
<!-- RGPhysicalSlot + Compile Phase A/B 生命周期分析 + 贪心区间着色 + Execute per-slot barrier 状态跟踪修复 Vulkan layout 验证错误；RGStats 加 importedBytesLogical/Entry::slotIndex；IRHIDevice::GetMemoryStats + RHIMemoryStats（VMA 堆预算 + 逻辑字节统计）；PlatformMemory.hpp 跨平台 CPU RSS；PerformancePanel 替代 SettingsPanel 性能统计展示（默认关闭）；Windows 菜单加 Open All / Close All -->


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

## Issue #25 — 项目结构模板，创建/打开项目面板 ✅ DONE
新增 `ProjectManager` + `ProjectBrowserPanel`（启动模态，含创建/打开/最近三区）；`EditorMode::LoadProject` 热切换项目；VFS 升级为双路径（引擎缓存固定 + 项目缓存动态）；`Application::UpdateProjectPaths` 同步传播至 VFS 和 SceneRenderer；`SA_DEBUG_PROJECT` CMake 选项绕过浏览器直接加载 `SA_PROJECT_DIR`。

---

## Issue #37 — Core 日志接入编辑器 Console 面板 ✅ DONE
新增 `EditorLogCapture`（RAII `spdlog::base_sink` 包装，2000 条环形缓冲，线程安全 `Drain()`）；`EditorMode::OnAttach` 注入 sink，`OnDetach` 自动移除；`ConsolePanel` 扩展为两 Tab — Diagnostics（原有，`EditorDiagnostics`）+ Engine Logs（被动镜像 `SA_LOG_*`，T/D/I/W/E/C 级别独立过滤，默认 INFO+ 开启）。

---

## Issue #38 — 切换项目时清空材质/纹理缓存

**优先级：中**

### 问题

`MaterialManager` 和 `ResourceManager` 以 `AssetID`（UUID）为键缓存材质和纹理。  
切换项目后旧的缓存条目不会被清空：若新旧项目存在 UUID 碰撞（小概率但理论上可能），
或旧项目材质实例仍被 `DrawItem` 持有（Scene 未完全重建），可能导致错误的材质显示。

### 设计方向

在 `EditorMode::LoadProject` 中，执行 `scene.Clear()` 之后、`ApplyWorldSettings` 之前：

```cpp
m_app->GetResourceManager().ClearProjectCache();  // 清空材质、纹理（保留内置）
m_app->GetMaterialManager().ClearInstanceCache();  // 清空 m_cachedInstances
```

- `ResourceManager::ClearProjectCache()`：销毁并清除 `m_textures` 和 `m_meshes`（保留 `m_white1x1` 和 `m_fileTextures`）
- `MaterialManager::ClearInstanceCache()`：清除 `m_cachedInstances`

内置纹理（`m_white1x1`）和文件纹理（`m_fileTextures`，用于 ImGui 图标等）不受影响。

**不做**：跨项目共享缓存（当前规模下无必要）。

---

## Issue #39 — 新项目首次 Cook：项目内资产 cook_cache 为空时的提示

**优先级：低**

### 问题

用户通过 `ProjectBrowserPanel` 创建新项目后立即加载，`cook_cache/` 为空，
所有 `ResolveCookedPath` 调用均回落至引擎 cook cache——项目自定义资产无法显示，
但没有任何提示，用户不知道需要运行 cook 步骤。

### 设计方向

- 在 `EditorMode::LoadProject` 完成后，检查项目 `cook_cache/` 是否为空。
- 若为空且 `assets/` 下有用户资产（`.sameta` 文件存在），在 Console 面板打印  
  `WARN: Project cook cache is empty — run "CookAssets" to import project assets.`
- 不阻断加载流程，仅给出提示。

---

## Issue #40 — 规范化现有后处理参数（PostProcessSettings）

**优先级：高（后续所有 PP issue 的前置）**

### 目标

将后处理参数从 `RendererConfig`（init-time）和零散 `WorldSettings` 字段统一到 `PostProcessSettings` 结构体，实现运行时开关和参数热更新，不需要重建 Renderer。消除 BloomFeature 内三处硬编码常量（threshold=1.0, strength=0.4, radius array）。

### 问题现状（调研结果）

| 参数 | 当前位置 | 问题 |
|------|---------|------|
| `bloomEnabled` | `RendererConfig` (init-time) | 不能运行时切换 |
| `bloomMipCount` | `RendererConfig` (init-time) | 影响 descriptor set 数量，保留 |
| `bloomThreshold=1.0` / `knee=0.1` | `SceneRenderer.cpp:1539` 硬编码 | 无法调节 |
| `bloomStrength=0.4` | `SceneRenderer.cpp:1627` 硬编码 | 无法调节 |
| upsample `radius` per-mip array | `SceneRenderer.cpp:1596` 硬编码 | 无法调节 |
| `exposure` / `gamma` / `lutStrength` | `WorldSettings` + `TonemapFeature::m_*` | 已可热更，但入口分散 |
| `tonemapMode` / `tonemapLut` | `WorldSettings` | 已可热更（ApplyWorldSettings feature 替换） |
| `builtinTonemap` | `RendererConfig` (init-time) | 与 tonemapMode 重复，可删除 |

### 设计

#### PostProcessSettings 结构体

位置：`src/function/scene/Scene.hpp`，定义在 `WorldSettings` 之前，由 `WorldSettings` 内嵌。

```cpp
struct PostProcessSettings {
    // ── Bloom ───────────────────────────────────────────────────────────
    bool  bloomEnabled   = true;
    float bloomThreshold = 1.0f;   // 亮度截止（knee width 固定为 threshold×0.1）
    float bloomStrength  = 0.4f;   // composite 权重
    float bloomRadius    = 1.0f;   // 最宽一级 upsample radius；后续每级 ×0.85

    // ── Tonemap ─────────────────────────────────────────────────────────
    // 注：tonemapMode/tonemapLut 迁移自 WorldSettings
    enum class TonemapMode { Builtin, LUT } tonemapMode = TonemapMode::Builtin;
    AssetID tonemapLut;
    float exposure    = 1.0f;
    float gamma       = 2.2f;   // 保留字段，当前 shader 未用（swapchain 走 sRGB）
    float lutStrength = 1.0f;

    // ── 后续效果占位（enabled=false → pass 跳过，无实现） ──────────────
    bool ssaoEnabled       = false;
    bool taaEnabled        = false;
    bool dofEnabled        = false;
    bool motionBlurEnabled = false;
};
```

#### WorldSettings 变化

```cpp
struct WorldSettings {
    // Background（不变）
    ...

    // 删除以下字段（迁移至 pp）：
    // - TonemapMode tonemapMode
    // - AssetID     tonemapLut
    // - float       exposure
    // - float       gamma
    // - float       lutStrength

    PostProcessSettings pp;   // 新增
};
```

#### RendererConfig 变化

```cpp
struct RendererConfig {
    bool     shadowEnabled  = true;
    uint32_t shadowMapSize  = 2048;
    // 删除：bool bloomEnabled（迁移至 WorldSettings::pp）
    int      bloomMipCount  = 3;   // 保留（影响 descriptor set layout）
    // 删除：bool builtinTonemap（由 pp.tonemapMode 取代）
};
```

#### BloomFeature 变化

```cpp
class BloomFeature final : public RenderFeature {
public:
    // 新增运行时可更新字段
    bool  m_enabled   = true;
    float m_threshold = 1.0f;
    float m_strength  = 0.4f;
    float m_radius    = 1.0f;   // 最宽一级；每级乘 0.85
    // AddPasses 检查 m_enabled，返回 early 跳过所有 pass
    // ThresholdPC 用 m_threshold（knee = m_threshold × 0.1）
    // CompositePC 用 m_strength
    // UpsamplePC 用 m_radius × pow(0.85f, layerIndex)
    ...
};
```

#### ApplyWorldSettings 变化

```cpp
void SceneRenderer::ApplyWorldSettings(WorldSettings& ws, bool updateIBL) {
    const auto& pp = ws.pp;

    // Bloom
    if (auto* bloom = FindFeature<BloomFeature>()) {
        bloom->m_enabled   = pp.bloomEnabled;
        bloom->m_threshold = pp.bloomThreshold;
        bloom->m_strength  = pp.bloomStrength;
        bloom->m_radius    = pp.bloomRadius;
    }

    // Tonemap（现有 feature 替换逻辑迁移，从 ws.tonemapMode → pp.tonemapMode）
    ...exposure/lutStrength 同样改读 pp.*...
}
```

#### SceneSerializer 变化

`WorldSettings` JSON block 新增 `"postProcess"` 子对象，旧的 `tonemapMode/exposure/gamma/lutStrength/tonemapLut` key 从顶层移入，保持向后兼容（读取时先尝试 `postProcess`，再 fallback 到顶层 key）。

#### PostProcessPanel（新建独立窗口）

位置：`editor/ui/panels/PostProcessPanel.hpp/.cpp`，注册为独立 `IEditorWindow`（默认开启）。

```cpp
class PostProcessPanel : public IEditorWindow {
public:
    PostProcessPanel(Scene* scene, SceneRenderer* renderer);
    std::string_view GetName() const override { return "Post Process"; }
    void OnDraw() override;
private:
    Scene*         m_scene    = nullptr;
    SceneRenderer* m_renderer = nullptr;
};
```

`OnDraw` 布局：
- **Bloom** CollapsingHeader：`Checkbox("Bloom")` → disabled group：threshold / strength / radius sliders
- **Tonemap** CollapsingHeader：RadioButton(Builtin/LUT) + exposure / gamma (Builtin) / lut picker + lutStrength (LUT)

任意参数改动后调用 `m_renderer->ApplyWorldSettings(m_scene->GetWorldSettings(), false)` 热更新。

#### WorldSettingsPanel 变化

删除现有 Tonemap 区域（exposure/gamma/lutStrength/tonemapMode/tonemapLut），这些参数迁移至 `PostProcessPanel`。`WorldSettingsPanel` 只保留 Background（SolidColor/Skybox）和 IBL 相关控制。

#### 数据流关系图

```
WorldSettings::pp (PostProcessSettings)
        │
        ├── WorldSettingsPanel (编辑 → ApplyWorldSettings)
        │
        └── SceneRenderer::ApplyWorldSettings()
                │
                ├── BloomFeature::m_enabled/threshold/strength/radius
                │       └── AddPasses → ThresholdPC / UpsamplePC / CompositePC
                │
                └── TonemapFeature::m_exposure / m_gamma
                    LutTonemapFeature::m_exposure / m_lutStrength
                        └── AddPasses → TonemapPC / LutTonemapPC
```

### 实施步骤

- [ ] 1. `Scene.hpp`：定义 `PostProcessSettings`；`WorldSettings` 删除 tonemap 散落字段，内嵌 `pp`
- [ ] 2. `SceneRenderer.hpp`：`RendererConfig` 删除 `bloomEnabled`/`builtinTonemap`；`BloomFeature` 加 `m_enabled/m_threshold/m_strength/m_radius`
- [ ] 3. `SceneRenderer.cpp`：`Init()` 从 `m_config.bloomMipCount` 构造 BloomFeature（不再用 `bloomEnabled`）；`ApplyWorldSettings` 读 `ws.pp.*` 更新 Bloom + Tonemap feature 字段；`BloomFeature::AddPasses` 改用成员字段替换三处硬编码
- [ ] 4. `SceneSerializer.cpp`：`SaveWorldSettings` 输出 `"postProcess"` 子对象；`LoadWorldSettings` 先读子对象 fallback 到旧顶层 key
- [ ] 5. 新建 `PostProcessPanel.hpp/.cpp`（Bloom + Tonemap 控制）；`WorldSettingsPanel` 删除 Tonemap 区；`EditorMode` 注册 PostProcessPanel
- [ ] 6. `PerformancePanel.cpp`：Render Stats 区添加当前 PP 开关状态一行摘要

### 边界情况与约束

| 场景 | 处理 |
|------|------|
| `bloomMipCount` 变化 | 仍需重建 descriptor set；保留在 RendererConfig，不暴露运行时修改 |
| `bloomEnabled = false` | `BloomFeature::AddPasses` 检查 `m_enabled`，直接 return；不从 feature list 移除（避免 reload overhead） |
| 旧 `.sascene` 文件（无 `postProcess` key）| Serializer fallback 读顶层 `tonemapMode/exposure` 等字段，保持兼容 |
| `pp.tonemapMode` 切换 | 复用现有 ApplyWorldSettings 的 feature-slot 替换逻辑，只是读取路径从 `ws.tonemapMode` → `ws.pp.tonemapMode` |
| `gamma` 字段实际未在 shader 使用 | 保留字段（swapchain sRGB），不删除；slider 范围 1.0–3.0 不变 |

**不做**：实现 SSAO/TAA/DoF/MotionBlur pass（仅添加占位 bool）；修改 bloom shader 代码（push constant 布局已兼容）；修改 `bloomMipCount` 为运行时。

### 受益 issues

- **#41 SSAO**：实现时只需在 ApplyWorldSettings 加读 `pp.ssaoEnabled` 的逻辑，入口已统一
- **#42 TAA**：同上 `pp.taaEnabled`
- **#44 Color Grading**：LUT pipeline 已在 LutTonemapFeature 中，参数统一后扩展更容易
- **#45 DoF / #46 MotionBlur**：占位 bool 已就位

---

## Issue #41 — SSAO（屏幕空间环境光遮蔽）

**优先级：高（依赖 #40）**

### 目标

在 DeferredLighting 前插入 GTAO（Ground-Truth Ambient Occlusion）pass，输出 R8 AO 纹理，DeferredLighting shader 将其乘入环境光项，提升接触阴影质感。

### 设计

```
GBuffer → SSAO_Main [读 depth + normal(RT1)] → SSAO_Blur [横向] → SSAO_BlurV [纵向]
        → AO 纹理(R8) → DeferredLighting 读取
```

- 新增瞬态纹理：`ao` (R8, viewport)，`aoBlurH` (R8, viewport)——可 alias 到同一 slot
- GTAO 采样：8 ray × 4 step（可配置），interleaved noise 降噪
- Blur：双边滤波（depth-aware），保留锐利边缘
- DeferredLighting shader 新增 `aoTex` 输入，`ambient *= ao`
- `PostProcessSettings::ssaoEnabled` 控制，disabled 时 ao 固定为 1.0

### 参数（PostProcessSettings）

```cpp
bool  ssaoEnabled  = true;
float ssaoRadius   = 0.5f;   // 世界空间采样半径
float ssaoStrength = 1.0f;   // AO 强度
int   ssaoSamples  = 8;      // ray 数（质量档位）
```

---

## Issue #42 — TAA（时间性抗锯齿）

**优先级：高（为 SSAO/SSR 降噪提供基础，依赖 #40）**

### 目标

Jitter + 历史帧混合，消除锯齿，同时为后续 SSAO/SSR 提供时域降噪基础。

### 设计

- **Velocity Buffer**：GBuffer pass 新增 RG16F `velocity` 附件，存储每像素屏幕空间速度（当前帧 MVP × 上一帧 MVP 反推）
- **History Buffer**：1 张 RGBA16F viewport-sized 持久纹理（跨帧，不参与 aliasing）
- **TAA Resolve pass**（Tonemap 之前）：
  - Jitter 当前帧投影矩阵（Halton 序列，8/16 帧循环）
  - 重投影历史帧，neighborhood clamping 消除 ghost
  - 混合比：`lerp(history, current, 0.1)`（静止）/ `0.5`（运动区域）
  - 输出写回 hdrTex 并更新 history buffer
- Resize 时 invalidate history（同 depth/shadowMap 逻辑）

### 参数（PostProcessSettings）

```cpp
bool  taaEnabled        = true;
float taaBlendStatic    = 0.1f;  // 静止区混合比
float taaBlendMotion    = 0.5f;  // 运动区混合比
bool  taaAntiGhosting   = true;  // neighborhood clamp
```

---

## Issue #43 — Auto Exposure（自动曝光 / 眼适应）

**优先级：中（依赖 #40）**

### 目标

每帧从 HDR buffer 计算亮度直方图，平滑插值目标曝光值（模拟人眼适应），替代手动 exposure 参数。

### 设计

- **Histogram compute pass**（HDR → 256-bin R32 buffer，1 dispatch）
- **Exposure resolve compute pass**（读直方图 → 写 1×1 R32F 持久 buffer）
  - 加权平均排除极端值（可配置低/高截断百分位）
  - `targetEV = lerp(currentEV, measuredEV, deltaTime * adaptSpeed)`
- Tonemap push constant 读取 exposure buffer 替代 WorldSettings::exposure

### 参数

```cpp
bool  autoExposureEnabled = false;
float evMin        = -3.0f;  // 最小曝光值（EV100）
float evMax        =  5.0f;  // 最大曝光值
float adaptSpeed   =  2.0f;  // 适应速度（秒）
float lowPercent   =  0.45f; // 直方图低截断
float highPercent  =  0.95f; // 直方图高截断
```

---

## Issue #44 — Color Grading（独立色彩校正）

**优先级：中（依赖 #40，可与 Tonemap 合并成 LUT bake）**

### 目标

在 Tonemap pass 之后（或合并其中）提供 Lift/Gamma/Gain 和 HSL 控制，最终通过 LUT bake 合并为一次采样，零额外开销。

### 设计

- `PostProcessSettings` 增加 color grading 参数
- **LUT bake pass**（首帧或参数变化时执行，非每帧）：将 tonemap + color grading 烘焙进 32³ RGBA16F 3D LUT
- Tonemap pass 改为直接采样烘焙 LUT，参数变化时标脏重烘焙
- `WorldSettingsPanel` 新增色彩校正区：Lift/Gamma/Gain 颜色拾取器 + Saturation/Contrast 滑条

### 参数

```cpp
glm::vec3 lift    = {0,0,0};  // 阴影偏移
glm::vec3 gamma   = {1,1,1};  // 中间调
glm::vec3 gain    = {1,1,1};  // 高光
float saturation  = 1.0f;
float contrast    = 1.0f;
```

---

## Issue #45 — Depth of Field（景深）

**优先级：中（依赖 #40，可选依赖 #42 TAA 降噪）**

### 目标

基于 CoC（Circle of Confusion）的近景/远景 Bokeh 模糊，支持手动焦距和自动对焦。

### 设计

- **CoC pass**：depth → CoC（R16F，viewport），公式 `coc = (depth - focusDist) * aperture / depth`
- **近景 blur**（分离式，两趟）+ **远景 blur**（同）：利用 CoC 大小控制卷积核半径
- **Composite pass**：CoC 混合近/远/对焦三层，写回 hdrTex（Tonemap 之前）
- 新增瞬态纹理：`cocTex`(R16F)、`dofNear`(RGBA16F)、`dofFar`(RGBA16F)——可参与 aliasing

### 参数

```cpp
bool  dofEnabled      = false;
float focusDistance   = 5.0f;
float aperture        = 1.4f;   // f/N
float focalLength     = 50.0f;  // mm
int   dofSamples      = 16;     // 质量
bool  autoFocus       = false;
```

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

## Issue #50 — 面板全局隐藏/恢复快捷键 ✅ DONE
<!-- EditorUI::m_panelsHidden + TogglePanelsHidden()；DrawPanels() loop 加 || m_panelsHidden 短路不改变 isOpen；"TogglePanels" action 默认 F8 可配置；Windows 菜单加 Toggle Panels 带对勾项 -->
