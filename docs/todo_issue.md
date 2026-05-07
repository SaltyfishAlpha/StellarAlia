~~1. rotation ui的y轴被卡在-90·+90~~
~~2. bloom pass当物体在画面上边缘时下边缘也会被bloom影响~~
~~2. 交换链格式（lighting->tonemap用hdr？GUI用RGBA？当前是BGRA？待确认）~~ 确认正确：HDR中间缓冲RGBA16F线性，Tonemap输出BGRA8_SRGB，硬件自动linear→sRGB，无需手动gamma。ImGui叠写同一swapchain，颜色精度问题低优先级。
~~3. 动画系统/物理tick~~
4. 资源导入（工作目录内->直接打开，外部->复制->打开）与导入按钮
5. 工作目录模板
6. 物体属性反射
7. 自定义材质feature的动态创建/文件化/动态载入
~~8. 窗口场景树父子结构错误~~
~~9. 网格->子网格拆分~~
~~10. tonemap失效（场景配置未定义）~~
11. ~~GameMode与运行GameMode按钮~~
12. 整理内建资产目录，删掉无用demo，删掉无用shader和材质模型等
13. InputSystem 支持组合键（Modifier+Key）：在 `BindingDef` 中添加 `Composite` kind（modifier path + key path），`ReadBindingFloat` 加对应分支；届时 SceneHierarchyPanel 的 Ctrl+D 可从 `GetDeviceButton` 双检测改为单一 `WasActivated("EntityDuplicate")`，同时解锁手柄组合键绑定能力
~~13. worldsettings配置面板（hdr贴图）~~
~~14. 鼠标选择物体与选中物体高亮描边~~
~~15. 编辑模式下相机线框显示~~

---

## 下一步规划

**优先级建议：**

### #6 物体属性反射（编辑器可编辑性基础）
在 InspectorPanel 中自动生成组件字段的 UI（position/rotation/scale/light params/camera params 等），无需为每个组件手写 imgui 代码。这是编辑器可用性的核心瓶颈——目前只能查看层级，无法改变任何参数。
- 技术方向：手写轻量反射宏（REFLECT_FIELD）或直接在 InspectorPanel 按组件类型分支绘制，无需第三方反射库。

### #4 资源导入（与 WorldSettings HDR/LUT 联动）
工作目录内文件直接读取 .sameta 获取 UUID；外部文件复制到 assets/ 并生成 .sameta。提供 InputText + Browse 按钮。完成后 WorldSettingsPanel 的 HDR 和 LUT 槽位才能从 UI 操作。
- 依赖：nfd-extended 或手写 InputText 临时方案。

### #12 内建资产目录整理（低风险，随时可做）
删除无用 demo 场景/shader/模型，统一 builtin 资产命名规范。

**推荐顺序：#6 → #4 → #12**（#6 对日常迭代提效最大，#4 解锁 HDR/LUT 工作流，#12 随时插空）
