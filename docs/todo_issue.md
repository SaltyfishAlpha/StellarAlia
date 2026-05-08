---

## Issue #34 — 全局快捷键配置窗口（含配置缓存）✅ DONE
<!-- ActionDef::userConfigurable 过滤白名单；EditorShortcutConfig(Load/Save/Reload/Import/Export/ApplyTo) 直接存 BindingDef；ShortcutsPanel 动态枚举+按键捕获+Default/Reload/Import/Export 按钮+内置路径只读；InputSystem 严格 modifier 匹配(HasExtraModifiers)修复 Ctrl+Shift+S 落回 Ctrl+S 问题 -->

---

## 其余 backlog

35. 渲染资源内存统计（#16 前置）
16. RG 资源别名 — RenderGraph handle 复用（依赖 #35）

---

## Issue #17 — 双击交互升级：短双击聚焦相机 / 长双击重命名 ✅ DONE
<!-- DoubleClickClassifier + EditorCamera::FocusOn；Hierarchy/Assets 多选(Ctrl/Shift/Ctrl+A)；短双击→聚焦/长双击→内联重命名(0.20s 阈值)；Composite 快捷键(SelectAll/NewScene/SaveScene/EntityDuplicate)；TextInput map gate 修复 WASD/快捷键冲突 -->

---

## 待办issue

24. **[低优先级] 长耗时操作进度反馈**
    - **启动进度条**（难度高）：`OnAttach` 在渲染循环前同步执行，ImGui 无法渲染。需重构为两阶段延迟初始化或独立 splash screen 渲染通道，暂不做。
    - **Reimport All 进度条**（难度中，最有实际价值）：`ReimportDir` 同步阻塞 UI。改法：将其拆成逐帧 N 个文件的状态机，`OnDraw` 期间推进并用 `ImGui::ProgressBar` + modal 显示；或移入工作线程 + 原子进度计数器。
    - 前置条件：依赖 `AssetsPanel` 暴露异步迭代接口；待项目素材量增大后再做。
25. 项目结构模板，创建/打开项目面板
26. sence的保存/另存为 文件操作 基于nfd
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

## Issue #16 — RG 资源别名（RenderGraph Handle 复用）

**优先级：中**（依赖 #35）

### 目标

`RenderGraph::Compile()` 分析 transient texture 生命周期区间，用贪心区间着色将不重叠、格式兼容的资源分配到同一个物理 `RHITextureHandle`（slot）。`Execute()` 按 slot 映射使用物理 texture，slot 跨帧持久复用，无每帧分配/释放开销。预计节省 transient 显存 15–25%。

### 前置工作：SceneRenderer transient 化

当前 SceneRenderer 在 `Init()` 里预分配所有中间 texture 并通过 `ImportTexture()` 注入 RG。
aliasing 只对 `CreateTexture()`（transient）资源有效，需先将以下资源迁移：

| 资源 | 迁移优先级 | 说明 |
|------|-----------|------|
| `bloomA` / `bloomB` (各 mip) | 最先 | 格式相同，天然互相 alias |
| `selectMask` / `dilateH` | 次之 | R8，与 bloom 不兼容但生命周期不重叠 |
| `RT0` / `RT1` / `RT2` | 再次 | 在 Lighting pass 后不再使用 |
| `hdrColor` | 最后 | 贯穿多个 pass，alias 机会少 |
| `depth` / `shadowMap` | **保留 Import** | 有跨帧 resize 逻辑，不参与 aliasing |

### 设计

#### 数据结构

```cpp
// RenderGraph 内部新增：

struct RGTextureEntry {
    std::string    name;
    RHITextureDesc desc;
    bool           imported = false;
    // 生命周期（Compile 后填充）：
    int firstWritePass = -1;   // sorted pass index
    int lastReadPass   = -1;
    // slot 分配（Compile 后填充）：
    int slotIndex = -1;        // -1 = imported，不参与 aliasing
};

struct RGPhysicalSlot {
    RHITextureDesc      desc;
    RHITextureHandle    handle;  // 跨帧持久，格式/尺寸变化时重建
    int                 freeAfterPass = -1;  // 当前占用者的 lastReadPass
};

// 成员：
std::vector<RGTextureEntry> m_textures;
std::vector<RGPhysicalSlot> m_slots;       // 跨帧持久
```

#### Compile() 新增阶段（拓扑排序后执行）

```
Phase A — 生命周期分析：
  for each sorted pass p (index i):
    for each texture t written by p:
      if t.firstWritePass == -1: t.firstWritePass = i
      t.lastReadPass = i
    for each texture t read by p:
      t.lastReadPass = i

Phase B — 贪心区间着色（slot 分配）：
  按 firstWritePass 升序排列 transient textures
  for each texture t:
    for each existing slot s:
      if Compatible(s.desc, t.desc) && s.freeAfterPass < t.firstWritePass:
        assign t.slotIndex = s.index
        s.freeAfterPass = t.lastReadPass
        break
    if no slot found:
      create new slot s with t.desc
      t.slotIndex = s.index

Compatible(a, b):
  return a.format == b.format
      && a.width == b.width && a.height == b.height
      && a.mipLevels == b.mipLevels
      && (a.usage & b.usage) == b.usage  // a 的 usage 是 b 的超集
```

#### Execute() 修改

```cpp
// 在执行 pass 前，为 transient texture 的 slot 创建/复用物理 handle：
for (auto& slot : m_slots) {
    if (!slot.handle.IsValid()
        || slot.desc != m_slots[...].desc)  // 分辨率变化后重建
    {
        if (slot.handle.IsValid()) device->DestroyTexture(slot.handle);
        slot.handle = device->CreateTexture(slot.desc);
    }
}
// resolved 表：
for (auto& t : m_textures) {
    if (!t.imported)
        m_resolved[&t - m_textures.data()] = m_slots[t.slotIndex].handle;
}
```

#### 分辨率变化处理

`SceneRenderer` resize 时调用 `rg.InvalidateSlots()`，设 `slot.handle = {}` 强制下帧重建。
或在 Compile() 时检测 desc 变化（宽高与上帧不同）自动重建。

### ASCII 生命周期示例

```
Pass index:  0(Shadow) 1(GBuf) 2(Light) 3(SelMask) 4(Bloom0) 5(Bloom1) 6(Tonemap)
shadowMap    ██████████████████
RT0                   █████████████████
RT1                   █████████████████
RT2                   █████████████████
hdrColor                       █████████████████████████████████████████
selectMask                               █████████████████
bloomA                                             █████████████████████
bloomB                                                       ████████████

Slot 分配（贪心）：
  Slot 0 [D32F  2048²]:  shadowMap
  Slot 1 [RGBA8 1920×1080]: RT0
  Slot 2 [RGBA16F 1920×1080]: RT1
  Slot 3 [RGBA16F 1920×1080]: RT2, bloomA  ← RT2 lastRead=2，bloomA firstWrite=4，不重叠
  Slot 4 [RGBA16F 1920×1080]: hdrColor
  Slot 5 [R8 1920×1080]: selectMask
  Slot 6 [RGBA16F 960×540]: bloomB

节省：RT2 与 bloomA 共用 Slot 3 → 节省约 16 MB
```

### 实施步骤

- [ ] Step 1  `RGTextureEntry` 加生命周期字段 + `RGPhysicalSlot` 结构；`Compile()` 加生命周期分析
- [ ] Step 2  贪心区间着色算法（`Compatible` 检查 + slot 分配）
- [ ] Step 3  `Execute()` 使用 slot handle（替换当前 transient texture 的空 handle 逻辑）
- [ ] Step 4  `SceneRenderer` 迁移 bloom textures → `rg.CreateTexture()`（最小改动验证）
- [ ] Step 5  迁移 selectMask → rg.CreateTexture()
- [ ] Step 6  迁移 RT0/RT1/RT2 → rg.CreateTexture()
- [ ] Step 7  借助 #35 统计工具验证节省量

### 边界情况与约束

| 场景 | 行为 |
|------|------|
| 分辨率变化 | 检测 desc 变化，销毁旧 handle，下帧重建 |
| 条件性 pass（bloom disabled）| 对应 texture firstWritePass = -1，不参与 aliasing，不分配 slot |
| alias 的两个 texture 格式不同 | Compatible() 返回 false，分配独立 slot |
| slot pool 跨帧膨胀 | slot 只增不减（除非 resize）；管线稳定后 slot 数收敛 |

**不做**：usage flag 超集扩展（slot 自动升级 usage）；跨帧资源持久化（depth 保留 Import 方式）；VkMemory 级别 suballocation（留 #36）。

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
