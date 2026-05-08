~~1. rotation ui的y轴被卡在-90·+90~~
~~2. bloom pass当物体在画面上边缘时下边缘也会被bloom影响~~
~~2. 交换链格式（lighting->tonemap用hdr？GUI用RGBA？当前是BGRA？待确认）~~ 确认正确：HDR中间缓冲RGBA16F线性，Tonemap输出BGRA8_SRGB，硬件自动linear→sRGB，无需手动gamma。ImGui叠写同一swapchain，颜色精度问题低优先级。
~~3. 动画系统/物理tick~~
~~4. 资源导入与 AssetID 选择器~~ 已完成：AssetRegistry（src/resource/）扫描 .sameta；DrawAssetIDField 选择器；AssetsPanel 拖放/Import 导入；WorldSettingsPanel HDR/LUT 选择器；SkeletonComponent 移除。
~~5. 工作目录模板~~ 已完成：目录规范确定，demo_project 建立，.saproject 读取，Assets 面板，路径常量分离。
~~6. 物体属性反射~~ 已完成：值类型字段（Transform/灯光/PBR参数/物理参数）、Add/Remove Component；AssetID 选择器字段（albedoMap、normalMap、materialSlots）已通过 #4 DrawAssetIDField 解锁；skeleton 组件已移除（纯派生数据）；clip 字段目前只读（无独立动画资源格式）。
7. 材质组件重设计（MaterialOverrideComponent）
~~8. 窗口场景树父子结构错误~~
~~9. 网格->子网格拆分~~
~~10. tonemap失效（场景配置未定义）~~
11. ~~GameMode与运行GameMode按钮~~
~~12. 整理内建资产目录，删掉无用demo，删掉无用shader和材质模型等~~ 完成：demo assets 迁移到 demo_project/，无用素材删除，builtin/ 子目录扁平化。examples/ 中的路径引用待后续修复。
13. InputSystem 支持组合键（Modifier+Key）：在 `BindingDef` 中添加 `Composite` kind（modifier path + key path），`ReadBindingFloat` 加对应分支；届时 SceneHierarchyPanel 的 Ctrl+D 可从 `GetDeviceButton` 双检测改为单一 `WasActivated("EntityDuplicate")`，同时解锁手柄组合键绑定能力
~~13. worldsettings配置面板（hdr贴图）~~
~~14. 鼠标选择物体与选中物体高亮描边~~
~~15. 编辑模式下相机线框显示~~
16. 资源别名
17. 提升输入质量：短双击让编辑器相机聚焦物体，长双击重命名
18. 提升输入质量：输入文字的时候或其他快捷键仍会触发awsd的相机移动，用进出栈和组合键修正

---

## Issue #7：材质组件重设计（MaterialOverrideComponent）

**问题**：当前 `PBRSurfaceComponent`（硬编码 PBR 字段）+ `MaterialParamComponent`（通用 map，编辑器无法发现有效 key）两组件耦合到具体 shader，无法支持自定义材质（纯自发光、卡通等）。

**目标设计**：

```
移除: PBRSurfaceComponent、MaterialParamComponent

新增: MaterialOverrideComponent
    ├── AssetID materialAsset   // 可选：覆盖实体 slot 0 的材质资产
    ├── map<string, ParamValue> scalars    // 参数覆盖
    └── map<string, AssetID>    textures   // 贴图覆盖
```

**编辑器流程**：Inspector 中 `MaterialOverrideDrawer` 通过 `ResourceManager` 加载当前有效材质的 `MaterialType`，从 `ShaderReflection` 读取所有 `ParamDef` / `TextureDef`，动态生成参数编辑 UI。已覆盖的参数高亮，未覆盖的显示材质默认值（灰色），每项带重置按钮。

**迁移**：`SceneSerializer` 读旧格式 `pbrSurface` + `materialParams` → 合并写入新 `materialOverride`；`BuildDrawList` 只处理 `MaterialOverrideComponent` 一个组件。

**依赖**：`materialAsset` 字段的 AssetID 选择器 UI 需要 #4 完成后才能实现；参数覆盖编辑部分可先独立完成。

---

## 下一步规划

**#6 当前瓶颈**：AssetID 类字段（材质贴图、网格、动画 clip 等）在 Inspector 中只能显示 UUID，无法通过 UI 赋值，解锁条件是 #4 的资源文件浏览器。

**推荐顺序：#5 → #4 → #7 尾部**

### ~~#5 工作目录模板~~ 已完成

**确认的目录结构**：
```
<project-root>/
├── <name>.saproject        ← 项目入口（JSON），编辑器启动时读取
├── assets/                 ← 用户可见的全部项目内容（Assets 面板根）
│   ├── scenes/             .sascene
│   ├── models/             .gltf / .glb   (+ 隐藏的 .sameta)
│   ├── textures/           .png / .hdr    (+ 隐藏的 .sameta)
│   ├── materials/          .samat         (+ 隐藏的 .sameta)
│   └── shaders/            用户自定义 .vert / .frag
└── cook_cache/             派生产物，.gitignore，Assets 面板不可见

<engine-root>/assets/       ← 引擎内建资产（独立搜索路径）
├── shaders/                builtin shaders (flat, no subdirs)
├── models/                 cube.gltf, plane.gltf + .sameta
├── textures/               default textures
├── hdri/                   brdf_schilk, grasslands_sunset
└── materials/              default_pbr.mat
```

**.saproject 格式（JSON，最小集）**：
```json
{ "name": "DemoProject", "version": 1, "startupScene": "assets/scenes/foo.sascene" }
```

**引用语义**：用户看到相对路径（`models/BoomBox.glb`），系统内部查 `.sameta` → UUID → cook_cache。  
`.sameta` 与源文件同目录，Assets 面板过滤隐藏，用户永远不需要看到或输入 UUID。

**已实现（步骤 ①②⑤）**：
- 物理迁移：demo assets → `demo_project/`，无用素材删除，`builtin/` 子目录扁平化
- `AppDesc` 增加 `engineAssetsDir` + `projectDir`；`main.cpp` 传入对应常量
- `SA_PROJECT_DIR` CMake cache 变量（默认 `demo_project/`，可 `-DSA_PROJECT_DIR=` 覆盖）
- `demo_project/CMakeLists.txt` 用 `sa_cook_directory()` cook 项目资产到共享 cook cache
- `src/engine/SaProject.hpp/.cpp`：`LoadSaProject` / `SaveSaProject`
- `EditorMode::OnAttach`：扫描 `projectDir/*.saproject`，读 `startupScene` 并加载场景
- `AssetsPanel`：显示 `projectDir/assets/` 文件树，过滤 `.sameta`

**剩余（步骤 ③④⑥ 延后至 #4）**：
- ③ ResourceManager 双路径搜索（engine assets + project assets）—— 目前共享 cook cache 规避
- ④ SceneRenderer `cookCacheDir` 已来自 AppDesc（无需修改）
- ⑥ Assets 面板 + 开项目对话框（File > Open / New Project）—— #4 阶段实现

### ~~#4 资源导入与 AssetID 选择器~~ 已完成

- `AssetRegistry`（`src/resource/`）：扫描 `.sameta`，UUID/路径双索引，`FindByID()` / `ResolveID()` / `EntriesByType()`
- `DrawAssetIDField`（`editor/ui/ComponentDrawers.hpp`）：inline 选择器 widget，按钮显示资产名，点击弹出过滤列表，"×" 清除
- `AssetsPanel`（`editor/ui/panels/AssetsPanel.cpp`）：Import 按钮 + GLFW drop callback 批量导入；自动生成 `.sameta`，触发 registry 重扫
- `WorldSettingsPanel`：HDR 和 LUT 槽均已接入选择器
- `SkeletonComponent` 移除：`AnimationSystem` 改为内联推导 `DeriveSkinID`
- clip 字段只读（无独立动画资源格式，延后）
- 剩余：ResourceManager 双路径搜索（步骤 ③）仍延后；原生文件对话框（nfd-extended）仍延后

### #7 尾部（#4 完成后）

实现 `MaterialOverrideDrawer` 的 `materialAsset` 选择器；完成 `PBRSurfaceComponent` → `MaterialOverrideComponent` 迁移；序列化兼容读写。

---

~~## Issue #19：骨骼与动画资源系统重设计~~  已完成 Phase 1 + Phase 2；Phase 3 延后

~~### 现状~~

| 资源 | ~~现状~~ 现状（已完成） |
|------|------|
| `CookedSkeleton` | `.glb` import 自动生成 `.saskel` + `.sameta`；cook → `<uuid>.saskelc`；`DeriveSkinID` 兼容层保留 |
| `CookedAnim` | `.glb` import 为每个 clip 生成 `.sanim` + `.sameta`；cook → `<uuid>.saanim` |
| `AnimatorComponent.clipAsset` | Inspector 中 `DrawAssetIDField` picker，Filter="Animation" |
| 骨架可视化 | `GetBoneGlobalPoses` + `GetBoneSkeleton`；EditorMode xray overlay（球关节 + 线骨骼） |
| Editor 自动刷新 | 修改 `SkinnedMeshComponent.meshAsset` → `Scene::MarkSkinnedMeshDirty()` → 下一帧 `PrepareAnimatedEntities` |

---

### Phase 3 — 完整骨骼绑定（低优先级 / backlog）
- `SkinnedMeshComponent.skeletonAsset` picker（独立指定骨骼资产）
- 多 mesh 共用同一骨骼
