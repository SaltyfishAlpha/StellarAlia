13. InputSystem 支持组合键（Modifier+Key）：在 `BindingDef` 中添加 `Composite` kind（modifier path + key path），`ReadBindingFloat` 加对应分支；届时 SceneHierarchyPanel 的 Ctrl+D 可从 `GetDeviceButton` 双检测改为单一 `WasActivated("EntityDuplicate")`，同时解锁手柄组合键绑定能力
16. 资源别名
17. 提升输入质量：短双击让编辑器相机聚焦物体，长双击重命名
18. 提升输入质量：输入文字的时候或其他快捷键仍会触发 WASD 的相机移动，用进出栈和组合键修正
24. **[低优先级] 长耗时操作进度反馈**
    - **启动进度条**（难度高）：`OnAttach` 在渲染循环前同步执行，ImGui 无法渲染。需重构为两阶段延迟初始化或独立 splash screen 渲染通道，暂不做。
    - **Reimport All 进度条**（难度中，最有实际价值）：`ReimportDir` 同步阻塞 UI。改法：将其拆成逐帧 N 个文件的状态机，`OnDraw` 期间推进并用 `ImGui::ProgressBar` + modal 显示；或移入工作线程 + 原子进度计数器。
    - 前置条件：依赖 `AssetsPanel` 暴露异步迭代接口；待项目素材量增大后再做。
25. 项目结构模板

---

## Issue #19 — Phase 3 backlog（低优先级）

- `SkinnedMeshComponent.skeletonAsset` picker（独立指定骨骼资产）
- 多 mesh 共用同一骨骼
