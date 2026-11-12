# 组件缺口分析（Component gap analysis）

> 范围：`components/` 与 `layout/` 的公开组件，对照 Material Design 3，并补充产品设计与软件工程视角的缺口。
> 方法：先盘点仓库现状（66 个公开头文件、16 个 Gallery 页面、50 个测试），再对照 [Material 3 组件目录](https://m3.material.io/components)、
> [Compose Material3 API](https://developer.android.com/reference/kotlin/androidx/compose/material3/package-summary)、
> [M3 Expressive](https://m3.material.io/blog/building-with-m3-expressive) 与 [WCAG 2.2 新增准则](https://www.w3.org/WAI/standards-guidelines/wcag/new-in-22/)。
> 本文与 `docs/components.md`（已有目录）互补：那份回答"有什么"，这份回答"还缺什么、先做什么"。

---

## 1. 结论先行

库的**广度已经很好**（表单、日期、虚拟表格、覆盖层分层、主题与 RTL 都有），但还缺三类东西：

1. **基础控件的"最后一公里"**：`Switch`、`Divider`、`IconButton`、`Chip`、`FAB`、`BottomSheet`、`SearchBar`、`NavigationBar`。
   这些不是"锦上添花"，而是任何真实产品界面都会用到、且 M3 规范明确列出的组件。
2. **应用壳与容器**：底部导航/导航栏、可调整分栏、可编辑数据网格、步骤条（Stepper）、上传区。
   有了它们，库才能从"组件集合"变成"能搭应用"。
3. **工程能力**：**没有 install/export 规则**（外部工程无法 `find_package`）、**没有真正的无障碍实现**（只有 14 处 `setAccessibleName`，没有 `QAccessibleInterface`）、
   没有视觉回归基线、没有 API 文档站点、没有版本/变更日志。这些缺口不解决，组件再多也难以被外部采用。

一句话：**组件缺口约 18 个（P0 8 个、P1 7 个、P2 若干），工程缺口 7 项，其中 4 项建议先于新组件完成。**

---

## 2. 现状盘点

### 2.1 已有的能力（做得好的地方）

| 类别 | 组件 | 说明 |
| --- | --- | --- |
| 基础 | Button（5 变体）、CheckBox（三态）、Radio、Rate、Progress、Ring、SeekBar、Text、Typography、Icon | 状态层、涟漪、焦点环走统一 `View` 管线 |
| 输入 | TextInput（含 Email/Telephone/Regex 派生）、NumberInput、DropDown、Select、Complete、ColourPicker、NumberPicker、RingPicker | 校验、错误态、辅助文本齐全 |
| 表单 | Form、FormItem、FormRule | 集中校验、首个错误聚焦 |
| 日期时间 | DatePicker、DateTimePicker、TimePicker、Calendar | 六周网格、键盘、min/max |
| 数据 | Table（`QAbstractItemModel`）、VirtualTable、VirtualSelect、VirtualTreeSelect、TreeSelect | 万行不物化，冻结列，虚拟滚动 |
| 容器/布局 | Linear、Frame、Constraint、AppBar、Collapse、Coordinator | 自带测量/排布协议 |
| 覆盖层 | Dialog 家族、Drawer、Dropdown、PopUp、Modal（分层注册表）、Tooltip、Menu/SubMenu/MenuGroup、Notification（Snackbar）、Alert、Banner | 层级 `restack()`、焦点陷阱、Esc/遮罩关闭 |
| 导航 | Page、NavigationView、Pager、Drawer、Pagination、Anchor、Affix | 滚动联动、RTL 镜像 |
| 内容 | Card、Avatar、Image、Video（自绘播放面）、Carousel、Timeline、Tag、Skeleton、Marquee、Link、Badge、Scrollbar | 媒体与骨架屏 |
| 状态 | StateLayout（content/loading/empty/error/offline）、GuideView（引导） | 产品空态/错误态/引导已有雏形 |
| 主题 | Theme、ThemeScope、Font、Language、Store、ThemeSwitcher | 令牌驱动、运行时切换、Store 绑定 |
| 手感 | 降低动效（`reducedMotion`）、密度（Density）、涟漪、状态层 | 已在令牌层统一 |

### 2.2 当前规模

- 公开组件头文件：**66**
- Gallery 页面：**16**（含新的 Pool 页）
- 测试可执行文件：**50**，`ctest` 全绿
- CI：仅 `.github/workflows/msbuild.yml`（Windows/MSVC）

---

## 3. 对照 Material 3：组件缺口矩阵

图例：✅ 已有 · ◐ 部分具备 · ✗ 缺失。优先级：**P0** 产品必备 / **P1** 常见需求 / **P2** 锦上添花。

### 3.1 动作与选择

| M3 组件 | 现状 | 缺口 | 优先级 |
| --- | --- | --- | --- |
| Common buttons | ✅ `Button`（filled/tonal/elevated/outlined/text） | — | — |
| Icon buttons | ✗ | 只有文字/图标+文字按钮，缺 24dp 图标按钮（standard/filled/tonal/outlined）与切换态 | **P0** |
| FAB | ✗ | 缺浮动操作按钮（small/regular/large），含滚动隐藏与扩展态 | **P0** |
| Extended FAB | ✗ | 图标 + 标签的扩展态 | P1 |
| Segmented buttons | ✗ | 2–5 段单选/多选，带选中指示器 | P1 |
| Split button | ✗ | 主操作 + 下拉（M3 Expressive） | P2 |
| Button groups | ✗ | 成组按钮的连接/分离布局（M3 Expressive） | P2 |
| Switch | ✗ | **完全缺失**，只有 CheckBox/Radio；开关是设置页刚需 | **P0** |
| Chips | ◐ `Tag` | 只有 filled/tonal/outlined + 可选/可关；缺 assist、filter（带勾选）、input（可输入）、suggestion 四种语义与尺寸 | **P0** |
| Sliders | ◐ `SeekBar` | 单值滑块；缺双拇指区间滑块、刻度标签、离散吸附的可视化 | P1 |
| Menus | ✅ | 含子菜单、可勾选项 | — |
| Tooltips | ✅ | — | — |

### 3.2 容器与表面

| M3 组件 | 现状 | 缺口 | 优先级 |
| --- | --- | --- | --- |
| Bottom sheet（standard / modal） | ✗ | 拖拽把手、半展开/全展开、模态遮罩 | **P0** |
| Side sheet | ✗ | 侧边面板（检查器、详情） | P1 |
| Dialog | ✅ | 五种变体 + 自定义 | — |
| Divider | ✗ | 细线分隔符（列表/菜单/表单都需要） | **P0** |
| Cards | ✅ | — | — |
| Lists | ✅ `List` / `Recycler` | 缺 section header / sticky header / swipe action | P1 |
| Bottom app bar | ✗ | 底部应用栏 | P2 |
| Navigation bar（底部导航） | ✗ | 3–5 个目的地 + 选中指示器 | **P0** |
| Navigation rail | ✗ | 平板/桌面侧边窄导航 | P1 |
| Navigation drawer | ✅ | 模态 + 标准两种 | — |
| Top app bar | ✅ `ToolBar` / `AppBar` | 缺 medium/large 可折叠变体（`AppBar` 有折叠，语义未对齐） | P2 |
| Toolbars（浮动工具栏，Expressive） | ✗ | 富文本/选择场景的浮动工具条 | P2 |

### 3.3 输入与反馈

| M3 组件 | 现状 | 缺口 | 优先级 |
| --- | --- | --- | --- |
| Text fields | ✅ | 缺 multiline 自动增高、字符计数上限、前后缀插槽 | P1 |
| Search（search bar / search view） | ✗ | 搜索框 + 展开式搜索视图 + 建议列表；`Complete` 只是自动补全 | **P0** |
| Date / Time pickers | ✅ | 缺**日期区间**、快捷预设（今天/最近 7 天） | P1 |
| Snackbar | ✅ `Notification` | 队列与超时已有 | — |
| Progress indicators | ✅ | 缺 Expressive 的 wavy 进度 | P2 |
| Loading indicator（Expressive） | ✗ | 与 Progress 不同的形状动画指示器 | P2 |
| Badge | ✅ | — | — |
| Rating | ✅ | — | — |

### 3.4 M3 Expressive（2025）新增

M3 Expressive 在 2025 年补充了一批表达性组件与动效规范（button groups、split button、FAB menu、loading indicator、wavy progress、shape morph 等）。
这些**不建议现在做**：它们依赖新的动效与形状令牌，且产品收益低于 P0 缺口。等基础件补齐、令牌升级后再评估。

### 3.5 小结：组件层面的 P0 清单（8 项）

1. `Switch` — 设置/开关刚需
2. `Divider` — 到处都用
3. `IconButton` — 应用栏/工具栏/卡片操作
4. `Chip`（四种语义）— 筛选、输入、建议
5. `FAB` — M3 的主操作入口
6. `BottomSheet` — 移动端最常见的次级面板
7. `SearchBar` / `SearchView` — 桌面与移动都需要
8. `NavigationBar`（底部导航）— 移动端应用壳

---

## 4. 产品设计视角的缺口

组件是积木，产品要的是"页面模式"。以下缺口按真实产品出现频率排序：

| 场景 | 现状 | 建议补什么 | 优先级 |
| --- | --- | --- | --- |
| 设置页 | 有 Form、Switch 缺失 | SettingsList（分组 + 右侧控件 + 说明文字） | **P0** |
| 空/错/离线 | ✅ `StateLayout` | 补插图插槽与重试动作的标准布局 | P1 |
| 引导 | ◐ `GuideView` | 多步引导、完成态存储、可跳过 | P1 |
| 命令面板 | ✗ | `CommandPalette`（Ctrl/Cmd+K，模糊搜索 + 分组 + 快捷键提示） | P1 |
| 全局搜索 | ✗ | SearchView + 结果分组 + 最近搜索 | P1 |
| 通知中心 | ◐ `Notification` | 通知列表、已读/未读、分组 | P2 |
| 数据看板 | ✗ | 指标卡（KPI）、趋势缩略图；图表建议外接而非自研 | P1 |
| 上传/附件 | ✗ | `FileDropZone`、上传列表（进度/重试/取消） | P1 |
| 步骤流程 | ✗ | `Stepper`（横向/纵向、可跳过、错误定位） | P1 |
| 可调整分栏 | ✗ | `SplitPane`（拖拽分隔、最小尺寸、记忆比例） | P1 |
| 富文本/评论 | ◐ `Mention` | 富文本编辑器（建议基于 `QTextDocument` 封装，不自研排版） | P2 |
| 日历排程 | ◐ `Calendar` | 周/日视图、事件块、拖拽改期 | P2 |
| 看板/拖拽排序 | ✗ | Kanban/可排序列表（注意 WCAG 2.5.7：拖拽必须有单指替代操作） | P2 |

**产品设计上的一条重要约束**：以上任何"拖拽"交互都必须同时提供键盘或按钮替代（WCAG 2.5.7 Dragging Movements），
所以 `SplitPane`、Kanban、Carousel 这类组件在 API 设计时就要把"可拖动"与"可键盘调整"一起设计，而不是后补。

---

## 5. 软件工程视角的缺口

这一节比组件更关键：**没有这些，库无法被外部项目使用。**

### 5.1 打包与分发（P0，建议最先做）

- **现状**：`cmake/PeriodicityModule.cmake` 只设置了 `$<BUILD_INTERFACE:...>`，全仓库**没有任何 `install()` / `export()` / `configure_package_config_file`**。
- **后果**：外部工程无法 `find_package(Periodicity)`，只能把源码拖进自己的构建树；vcpkg/Conan 也无从打包。
- **要补**：`install(TARGETS ...)`、`install(DIRECTORY include/)`、`PeriodicityConfig.cmake`、版本文件、`periodicity::` 命名空间导出；
  可选加 CMake presets 的 `install` 预设。
- **验收**：一个空工程 `find_package(Periodicity REQUIRED)` + `target_link_libraries(app PRIVATE periodicity::components)` 能编译运行。

### 5.2 无障碍（P0）

- **现状**：只有 14 处 `setAccessibleName`；没有 `QAccessibleInterface` 实现，自绘控件对屏幕阅读器基本是"一个空白控件"。
- **WCAG 2.2 里与桌面控件直接相关的几条**（[新增准则](https://www.w3.org/WAI/standards-guidelines/wcag/new-in-22/)）：
  - 2.4.11 Focus Not Obscured：焦点不能被吸底栏/浮层遮住 → 需要"焦点滚动到可见区"的统一策略；
  - 2.5.7 Dragging Movements：拖拽必须有单指替代 → SplitPane/Kanban/Carousel 的键盘路径；
  - 2.5.8 Target Size：点击目标至少 24×24 → 图标按钮、菜单项、关闭按钮要核对；
  - 3.2.6 Consistent Help、3.3.7 Redundant Entry → 产品层约定，库提供容器即可。
- **要补**：
  1. `View` 层的 `QAccessibleInterface` 基类（role / name / state / action / child 遍历）；
  2. 每个组件的 role/state 映射表（Button→PushButton、Switch→CheckBox、Table→Table、Dialog→Dialog）；
  3. 焦点顺序与焦点陷阱的统一实现（Dialog 已有，需抽成通用 `FocusScope`）；
  4. 键盘交互规范（Enter/Space/Esc/方向键/Tab/Shift+Tab）与测试；
  5. 高对比度主题变体。
- **验收**：用 Windows 讲述人（Narrator）走查 Gallery 的 Basic / Feedback / Data 三页，能读出控件名与状态。

### 5.3 测试深度（P1，但成本最低、收益最高）

- **现状**：50 个 Qt Test 套件、覆盖行为正确性；但没有视觉基线、没有性能基准、没有无障碍断言。
- **要补**：
  - **视觉回归**：`periodicity_gallery --screenshot` 已有，把输出升级为 golden image 对比（基线入库，CI 比对），这一条几乎零成本；
  - **性能基准**：measure/arrange/paint 的单帧预算（例如 10k 行表格滚动的每帧耗时），防止"功能对但掉帧"；
  - **交互回放**：用 `QTest::mouseClick`/`keyClick` 覆盖键盘路径，而不是只调 API；
  - **模糊测试**：文本解析（`RegexInput`、Markdown/富文本若引入）、表格模型边界；
  - **内存**：池与虚拟滚动路径的泄漏检查（已有 pool，适合做断言）。

### 5.4 国际化与 RTL（P1）

- **现状**：`Language` 容器、`localized()`、部分组件 RTL 镜像；但缺复数规则、日期/数字本地化、伪语言测试。
- **要补**：`QLocale` 接入（数字/日期/货币格式）、复数与性数格（Qt 的 `tr(..., n)`）、伪本地化（长字符串+双向文本）用于布局回归、输入框 bidi 光标行为。

### 5.5 文档与发布（P1）

- **现状**：`framework.md`、`docs/components.md`、Gallery 可交互演示；**没有 API 文档生成、没有 CHANGELOG、没有版本号策略**。
- **要补**：Doxygen（或 QDoc）配置 + 站点发布；`CHANGELOG.md` 与 SemVer；废弃策略（`[[deprecated]]` + 迁移说明）；"从 Gallery 页面到代码"的示例索引。

### 5.6 CI 与质量门禁（P1）

- **现状**：只有 Windows/MSVC 工作流。
- **要补**：Linux（GCC/Clang）与 macOS 构建（Qt6 跨平台，库本身没有平台绑定）；`clang-tidy` + `/W4` 保持零警告；ASan/UBSan 任务；覆盖率报告；视觉回归任务（Windows runner 即可）。

### 5.7 API 一致性（P2）

- 属性命名（`setX`/`x()`/`xChanged`）、所有权（父对象 vs 智能指针）、错误传递（返回值 vs 信号）目前在组件间基本一致，但有少量例外（如回调式 Dialog action 与 modal result 并存）。
- 建议补一份 `docs/api-conventions.md`，并在 CI 里用 clang-tidy 自定义检查兜住命名。

---

## 6. 建议路线图

依赖关系：基础控件 → 应用壳 → 产品模式；工程能力可与组件并行。

### 第一波（P0，约 2–3 周）

| 项 | 类型 | 验收 |
| --- | --- | --- |
| Switch、Divider、IconButton、Chip | 组件 | Gallery 新卡片 + 单测 + 键盘 |
| FAB、BottomSheet、SearchBar、NavigationBar | 组件 | 演示页；BottomSheet 支持拖拽与 Esc |
| install/export + `PeriodicityConfig.cmake` | 工程 | 空工程 `find_package` 成功 |
| `View` 的 `QAccessibleInterface` 基类 + 6 个核心组件映射 | 工程 | Narrator 可读 |
| Gallery 截图 → golden image 基线 | 工程 | CI 比对失败即红 |

### 第二波（P1，约 3–4 周）

SegmentedButton、NavigationRail、SideSheet、RangeSlider、日期区间、Stepper、FileDropZone、Table 的排序/筛选/列宽、
SettingsList、CommandPalette、SplitPane；Doxygen + CHANGELOG；性能基准；i18n 与伪本地化；Linux/macOS CI。

### 第三波（P2，按需）

M3 Expressive 组件（button groups、split button、FAB menu、loading indicator、wavy progress）、图表/看板/日历排程、富文本、QR、签名、通知中心。

### 不建议自研

- **图表**：封装成熟库或宿主侧渲染，自研坐标系/缩放/图例是无底洞；
- **富文本排版**：基于 `QTextDocument` 封装编辑体验，不要重写排版引擎；
- **地图、PDF、视频解码**：保持"宿主提供内容，组件只负责表面"的现有约定（`Video` 就是这么做的）。

---

## 7. 速查清单（还缺什么）

**组件（P0）**：Switch · Divider · IconButton · Chip（assist/filter/input/suggestion）· FAB · BottomSheet · SearchBar/SearchView · NavigationBar

**组件（P1）**：SegmentedButton · NavigationRail · SideSheet · RangeSlider · DateRangePicker · Stepper · FileDropZone/UploadList · SplitPane · CommandPalette · SettingsList · 可编辑/可排序 Table · StickyHeader List

**组件（P2）**：Extended FAB · SplitButton · ButtonGroup · FloatingToolbar · LoadingIndicator · WavyProgress · Charts · CalendarScheduler · Kanban · RichText · QR · Signature · NotificationCenter

**工程（P0）**：install/export 打包 · QAccessibleInterface 无障碍基类 · 视觉回归基线

**工程（P1）**：性能基准 · API 文档站点 · CHANGELOG/SemVer · 伪本地化与 QLocale · Linux/macOS CI · 交互回放测试

**工程（P2）**：clang-tidy 规则 · 覆盖率 · 高对比度主题 · 动态取色（Material You/HCT）

---

## 8. 参考

- Material Design 3 — Components：<https://m3.material.io/components>
- Material 3 Expressive：<https://m3.material.io/blog/building-with-m3-expressive>
- Compose Material3 API（组件面）：<https://developer.android.com/reference/kotlin/androidx/compose/material3/package-summary>
- WCAG 2.2 新增准则：<https://www.w3.org/WAI/standards-guidelines/wcag/new-in-22/>
- WCAG 2.2 规范：<https://www.w3.org/TR/WCAG22/>
- 仓库内：`docs/components.md`（现有目录）、`docs/backlog.md`（批次计划）、`framework.md`（分层与约定）
