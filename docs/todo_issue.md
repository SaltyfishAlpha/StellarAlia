~~1. rotation ui的y轴被卡在-90·+90~~
~~2. bloom pass当物体在画面上边缘时下边缘也会被bloom影响~~
~~2. 交换链格式（lighting->tonemap用hdr？GUI用RGBA？当前是BGRA？待确认）~~ 确认正确：HDR中间缓冲RGBA16F线性，Tonemap输出BGRA8_SRGB，硬件自动linear→sRGB，无需手动gamma。ImGui叠写同一swapchain，颜色精度问题低优先级。
~~3. 动画系统/物理tick~~
~~4. 资源导入与 AssetID 选择器~~ 已完成：AssetRegistry（src/resource/）扫描 .sameta；DrawAssetIDField 选择器；AssetsPanel 拖放/Import 导入；WorldSettingsPanel HDR/LUT 选择器；SkeletonComponent 移除。
~~5. 工作目录模板~~ 已完成：目录规范确定，demo_project 建立，.saproject 读取，Assets 面板，路径常量分离。
~~6. 物体属性反射~~ 已完成：值类型字段（Transform/灯光/PBR参数/物理参数）、Add/Remove Component；AssetID 选择器字段（albedoMap、normalMap、materialSlots）已通过 #4 DrawAssetIDField 解锁；skeleton 组件已移除（纯派生数据）；clip 字段目前只读（无独立动画资源格式）。
~~7. 材质组件重设计（MaterialOverrideComponent）~~ 已完成：MaterialOverrideComponent 替换 PBRSurfaceComponent + MaterialParamComponent；MaterialOverrideDrawer 含 materialAsset 选择器 + 动态参数 UI；SceneSerializer 兼容旧格式迁移；BuildDrawList 只读新组件。
20. 数据驱动模板系统（进行中）
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
24. **[低优先级] 长耗时操作进度反馈**
    - `OnAttach` 约 130 行，结构清晰无需拆分；唯一耗时入口是 `AssetsPanel::RunInitialScan()`（cook 所有项目资产）。
    - **启动进度条**（难度高）：`OnAttach` 在渲染循环前同步执行，ImGui 无法渲染。需重构为两阶段延迟初始化或独立 splash screen 渲染通道，工作量大，暂不做。
    - **Reimport All 进度条**（难度中，最有实际价值）：`ReimportDir` 同步阻塞 UI。改法：将其拆成逐帧 N 个文件的状态机，`OnDraw` 期间推进并用 `ImGui::ProgressBar` + modal 显示；或移入工作线程 + 原子进度计数器。
    - **单文件 Import** 已瞬时完成，不需要进度。
    - 前置条件：Reimport All 改造依赖 `AssetsPanel` 暴露异步迭代接口；待项目素材量增大后再做。
25. 项目结构模板

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

~~**推荐顺序：#5 → #4 → #7 尾部**~~ 已全部完成

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

### ~~#7 尾部（#4 完成后）~~ 已完成

~~实现 `MaterialOverrideDrawer` 的 `materialAsset` 选择器；完成 `PBRSurfaceComponent` → `MaterialOverrideComponent` 迁移；序列化兼容读写。~~

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

---

## Issue #20：数据驱动模板系统

**核心原则**：模板 = 文件，不是代码。增删改模板只操作文件，引擎扫描文件夹动态生成菜单。

**目录结构**（`engineAssetsDir/templates/`）：
```
assets/templates/
├── entities/                  ← EntityTemplateRegistry 扫描（子目录名 = 菜单分类）
│   ├── 3D Object/
│   │   ├── Cube.sascene
│   │   └── Plane.sascene
│   ├── Light/
│   │   ├── Directional Light.sascene
│   │   ├── Point Light.sascene
│   │   ├── Spot Light.sascene
│   │   └── Area Light.sascene
│   └── Camera.sascene         ← 无子目录 = 顶层条目
├── scenes/
│   └── default.sascene        ← New Scene 起始模板（相机 + 平行光）
└── materials/                 ← Create Material 模板列表
    ├── PBR Default.samat
    └── PBR Emissive.samat
```

**修改模板举例**：
- 改默认点光源强度 → 编辑 `entities/Light/Point Light.sascene`，不动代码
- 加球体 → 新建 `entities/3D Object/Sphere.sascene`
- 加新材质预设 → 新建 `materials/Unlit.samat`

### 技术实现

**1. 实体模板**（已实现）

`SceneSerializer::SpawnFromTemplate(Scene&, path) → vector<entt::entity>`：
- 保存当前 WorldSettings + 场景名
- 调 `LoadFromFile` 追加实体
- 恢复 WorldSettings + 场景名
- 返回新增的根实体列表

`EntityTemplateRegistry`（`editor/resource/`）：
- `Scan(engineAssetsDir)` — 遍历 `templates/entities/` 一层；子目录名 = category
- `Entries()` → `vector<TemplateEntry>{category, label, path}`
- `DefaultScenePath()` → `templates/scenes/default.sascene`

**2. New Scene**

`EditorUI` File 菜单 → `onNewScene` 回调 → `EditorMode`：
```cpp
scene.Clear();
SceneSerializer::LoadFromFile(scene, m_templateRegistry.DefaultScenePath());
app.RebuildDrawList();
```

**3. Create Material**（延后至 #4 后实现）
AssetsPanel "+" → "Create Material" → 弹出模板选择 → 复制 `.samat` + 新建 `.sameta` + 重扫。

### 实施进度

- [x] Step 1  模板文件（`.sascene` × 7 + `.samat` × 2 + `default.sascene`）
- [x] Step 2  `SceneSerializer::SpawnFromTemplate`（~25 行）
- [x] Step 3  `EntityTemplateRegistry::Scan`（`editor/resource/EntityTemplateRegistry.hpp/.cpp`）
- [x] Step 4  `SceneHierarchyPanel` 菜单由 Registry 驱动（删除 `CreateKind` 枚举）
- [x] Step 5  `EditorMode` 扫描模板、传入面板；`EditorUI` File 菜单（New Scene / Save Scene）
- [ ] Step 6  AssetsPanel Create Material 入口（依赖 #7 完成后）

---

## Issue #7 深度分析：MaterialOverrideComponent

### 现状问题

当前系统有两个松耦合的材质组件：
- `PBRSurfaceComponent`：硬编码 baseColor/roughness/metallic/albedoMap/normalMap，只能表达标准 PBR
- `MaterialParamComponent`：通用 `map<string, ParamValue>`，但 Inspector 无法发现有效 key（编辑器盲目）

二者都耦合到具体 shader，无法支持自定义材质（纯自发光、卡通、透明等）。

### 目标设计

```cpp
// 移除 PBRSurfaceComponent、MaterialParamComponent

struct MaterialOverrideComponent {
    AssetID                    materialAsset;          // 可选：覆盖 slot 0 材质资产
    std::map<std::string, ParamValue> scalars;         // 参数覆盖（同 MaterialParamComponent）
    std::map<std::string, AssetID>    textures;        // 贴图覆盖
};
```

### Inspector 流程

`MaterialOverrideDrawer` → 通过 `ResourceManager` 加载有效材质的 `MaterialAsset` → 从 `ShaderReflection` 读取所有 `ParamDef` / `TextureDef` → 动态生成 UI：
- 已覆盖的参数：高亮显示实际值 + 重置按钮
- 未覆盖的参数：灰色显示材质默认值

### 序列化迁移

`SceneSerializer` 兼容读取旧格式 `pbrSurface` + `materialParams` → 合并写入新 `materialOverride`；`BuildDrawList` 只处理 `MaterialOverrideComponent`。

### 依赖与实施顺序

1. **MaterialAsset 资源格式**（`.samat` cook pipeline 已有，但缺 `ShaderReflection`）
   - `ShaderReflection` 描述 PBR shader 的参数列表（`ParamDef`, `TextureDef`）
   - 硬编码一份 `kPBRShaderReflection` 即可启动，将来接反射系统

2. **`MaterialOverrideComponent` 组件替换**
   - 删 `PBRSurfaceComponent` + `MaterialParamComponent`
   - 加 `MaterialOverrideComponent`
   - `BuildDrawList`/`SceneRenderer` 改读新组件

3. **Inspector Drawer**
   - `MaterialOverrideDrawer`：读 ShaderReflection，动态渲染参数列表
   - `materialAsset` 字段接 `DrawAssetIDField`（#4 已完成，可直接用）

4. **序列化兼容**
   - 旧 `pbrSurface` / `materialParams` key → 读取后写入 `MaterialOverrideComponent`
   - `SaveToFile` 只写 `materialOverride`

**估算工作量**：中等，约 300 行。不依赖外部系统，可独立推进。
**推荐时机**：#20 完成后立即做，因为材质模板 (`.samat`) 复制功能在 #7 完成后意义才完整。

---

## Issue #21：Shader/Material 完整管线重设计

### 总体架构思路

```
.saglsl（统一 shader 源）
    ↓ ShaderCookTool（构建期 / 编辑器内触发）
.spv + .refl + .shader.json（派生产物，cook cache）
    ↓ MaterialManager::RegisterTypesFromShaderDir（运行期）
MaterialType（已注册到 MaterialManager，含参数布局）
    ↑ 引用
.mat（材质资产，JSON，声明 type + 参数默认值）
    ↓ MaterialManager::LoadMaterial
MaterialInstance（运行时实例，含 UBO 数据）
    ↑ 挂载
MaterialOverrideComponent（实体级覆盖层，可选）
```

---

### .saglsl 统一 Shader 源格式

```glsl
// @ShaderName  "Simple Albedo"       人类可读名（编辑器显示）
// @ShadingModel SimpleAlbedo         CamelCase，唯一类型 ID，写入 .mat 的 "type" 字段
// @VertShader   deferred_geometry    可选，默认值 deferred_geometry

#pragma sa_section gbuffer
#version 450
// 完整 GLSL fragment shader（GBuffer 写入）
layout(set=1,binding=0) uniform MaterialParams {
    vec4 baseColorFactor; // @Color4("Base Color") = 1,1,1,1
} u_Mat;
layout(set=1,binding=1) uniform sampler2D t_BaseColor; // @Texture("Albedo Map")
void main() { /* 写 GBuffer RT0~RT3 */ }
#pragma sa_end_section

#pragma sa_section lighting
// 光照评估函数，被 shading_dispatch.glsl 内联
vec3 EvaluateShading(GBufferData gbuf) { return gbuf.albedo; }
#pragma sa_end_section
```

`// @` 注释只在文件头部（第一个 `#pragma sa_section` 之前）生效。参数注释（`@Color4`, `@Texture`）写在 UBO 成员或 sampler 定义的同行，供 ShaderReflectTool / UI 自动解析——**不需要手写 C++ 注册代码**。

---

### ShaderCookTool（`tools/shader_cook/`）

**输入**：一个 `--scan-dir`（`<project>/assets/shaders/`）下的所有 `.saglsl` 文件。

**输出**（写入 `--spv-out` = `bin/assets/shaders/`）：

| 文件 | 说明 |
|------|------|
| `<snake_name>.gbuffer.frag` | 提取的 gbuffer section GLSL（临时中间产物） |
| `<snake_name>.gbuffer.frag.spv` | 编译后 SPIR-V |
| `<snake_name>.gbuffer.frag.refl` | ShaderReflectTool 输出的反射 JSON（v4 格式） |
| `<snake_name>.shader.json` | 类型描述 sidecar，供运行期自动注册 |

**输出**（写入 `--dispatch-out` = `build/generated/shaders/`）：

| 文件 | 说明 |
|------|------|
| `shading_model_ids.glsl` | `#define SHADING_MODEL_<NAME> <id>u` |
| `shading_dispatch.glsl` | `DispatchShadingModel(uint, GBufferData, out vec3)` |
| `evaluators/<snake>.lighting.glsl` | 各 shader 的 EvaluateShading 实现 |

**`.shader.json` 格式**：
```json
{
  "shaderName":   "Simple Albedo",
  "shadingModel": "SimpleAlbedo",
  "vertShader":   "deferred_geometry",
  "fragShader":   "simple_albedo.gbuffer"
}
```
`fragShader` 的值对应 `RegisterTypeFromShaders` 中的 `desc.fragShader`，即 `.frag.spv` 的 stem 前缀。

**触发时机**：
- 构建期：`CookDemoShaders` CMake custom target，依赖 `.saglsl` 源文件 mtime。
- 未来：编辑器内保存 / reimport `.saglsl` 文件时，通过 `AssetsPanel` 回调触发子进程调用 `StellarAliaShaderCook --force`。

---

### 运行期材质类型注册

**内建类型**（PBR、Skybox 等）：`GBufferFeature::OnInit` 等处手写 `ctx.matMgr->RegisterTypeFromShaders(...)` 调用。

**项目类型**（`.saglsl` 编译产物）：通过 `.shader.json` 自动注册，无需手写 C++ 代码：

```cpp
// MaterialManager.hpp 新增：
void RegisterTypesFromShaderDir(const std::string& shaderDir,
                                 const FeatureInitContext& ctx);

// 在 GBufferFeature::OnInit 末尾调用：
ctx.matMgr->RegisterTypesFromShaderDir(ctx.shaderDir, ctx);
```

`RegisterTypesFromShaderDir` 实现：
- 扫描 `shaderDir` 下所有 `*.shader.json`
- 解析 `shaderName / shadingModel / vertShader / fragShader`
- 跳过已注册的类型（避免重复）
- 调用 `RegisterTypeFromShaders({shadingModel, vertShader, fragShader}, ctx)`

---

### 目录规范

```
<engine>/assets/
├── shaders/          builtin .spv + .refl（flat，无子目录）
├── materials/        builtin .mat（default_pbr.mat 等）
└── templates/
    └── materials/    Create Material 用的初始模板（#20 Step 6）

<project>/assets/
├── shaders/          *.saglsl（用户创建的自定义 shader）
└── materials/        *.mat（引用 saglsl 编译出的类型）

build/bin/assets/shaders/   ← cook 输出（.spv + .refl + .shader.json），运行期读取
build/generated/shaders/    ← dispatch GLSL（只在构建期被 glslc 包含）
```

---

### MeshRendererComponent vs MaterialOverrideComponent

两个组件职责不同，**保持分离**：

| 组件 | 职责 |
|------|------|
| `MeshRendererComponent` | 声明渲染的网格资产（`meshAsset`）和每个 sub-mesh 对应的材质资产 ID（`materialSlots[]`）。这是"该物体用什么材质"的静态声明。 |
| `MaterialOverrideComponent` | 对 `materialSlots[0]` 的参数进行运行时覆盖（颜色、贴图等），不改变材质资产本身。等价于 Unity 的"Renderer.material"（克隆）。 |

`BuildDrawList` 逻辑：
1. 读 `MeshRendererComponent.materialSlots[i]` 获取 AssetID
2. `MaterialManager::LoadMaterial(assetId)` 获取 cached `MaterialInstance*`
3. 若存在 `MaterialOverrideComponent`，克隆 instance，应用覆盖参数
4. 提交 draw call，附带最终 instance

---

### Asset Inspector（.mat 文件）

点击 Assets 面板中的 `.mat` 文件 → Inspector 面板显示材质资产视图（而非实体组件视图）：
- 顶部：`Type: SimpleAlbedo`，`Shader: simple_albedo.gbuffer`
- 参数区：从 `MaterialType` 读取 `ParamDef` 列表，对照 `.mat` JSON 的 `params` 字段显示当前值，可编辑后立即保存回 `.mat` 文件

实现方式：`AssetsPanel` 点击时检查扩展名，若为 `.mat` 则调 `InspectorPanel::ShowAssetInspector(path)` 而非 `ShowEntityInspector(entity)`。

---

### 当前实现状态 & 差距

| 功能点 | 状态 | 差距 |
|--------|------|------|
| `.saglsl` 统一格式 | ✅ 已设计 + 文件存在 | — |
| `ShaderCookTool` 编译 .spv + .refl | ✅ 已实现 (`tools/shader_cook/main.cpp`) | — |
| `CookDemoShaders` CMake target | ✅ 已接入构建 | — |
| `.shader.json` sidecar 输出 | ❌ 未实现 | `CompileEntry()` 中添加 JSON 写入 |
| `RegisterTypesFromShaderDir` | ❌ 未实现 | `MaterialManager` 新增方法 |
| 调用 `RegisterTypesFromShaderDir` | ❌ 未接入 | `GBufferFeature::OnInit` 末尾添加 |
| Asset Inspector (.mat) | ❌ 未实现 | `AssetsPanel` + `InspectorPanel` 扩展 |
| 编辑器内 auto-cook | ❌ 未实现 | `AssetsPanel` reimport 回调 → 子进程 |
| `templates/materials/` 目录 | ❌ 未实现 | 依赖 #20 Step 6 |
| `MeshRendererComponent` materialSlots | ✅ 已有 | — |
| `MaterialOverrideComponent` | ✅ 组件存在 | Drawer 动态参数 UI 待完成（#7） |

### 实施顺序（优先级排序）

1. **[高] `.shader.json` sidecar**：`CompileEntry()` 写出 JSON（~15 行）
2. **[高] `RegisterTypesFromShaderDir`**：`MaterialManager` 扫描 JSON，调 `RegisterTypeFromShaders`（~30 行）
3. **[高] 接入 GBufferFeature**：`OnInit` 末尾一行调用，修复"选 mat 无效果"问题
4. **[中] Asset Inspector**：`AssetsPanel` 点击 `.mat` → 静态参数视图
5. **[中] 编辑器 auto-cook**：保存 `.saglsl` 时触发 `StellarAliaShaderCook --force`
6. **[低] templates/materials/**：#20 Step 6 一并做
