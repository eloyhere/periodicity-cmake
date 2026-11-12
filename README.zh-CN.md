# Periodicity

[English](README.md) | 简体中文

Periodicity 是一个跨平台的 C++20 桌面应用组件库，构建在 Qt 6.8 Widgets 和一套声明式设计基线之上。每个组件都用 `QPainter` 自行绘制：库既不使用 Qt Style Sheets，也不使用 `.ui` 文件，更不回退到平台样式引擎。它从宿主系统获取的只有字体和原生窗口边框。开发仓库位于
[github.com/eloyhere/periodicity-cmake](https://github.com/eloyhere/periodicity-cmake)。

库按模块自下而上分层，最底层是单位与几何，最顶层是公开控件。[framework.md](framework.md) 是架构契约：分层、各模块职责、核心抽象以及允许的依赖矩阵，改动模块边界之前请先阅读它。组件目录见 [docs/components.md](docs/components.md)。

## 这个库是什么

- C++20 / Qt 6.8 Widgets 库。需要 Qt 的 `Core`、`Gui`、`Widgets` 以及 `Network`（`Avatar` 的 URL 加载器使用）；`OpenGLWidgets` 与 `LinguistTools` 为可选组件。
- 纯 `QPainter` 渲染。每个视觉元素都由组件自己绘制，而不是交给宿主平台换肤，因此在 Windows 上正确的布局，在 Linux 和 macOS 上同样正确。
- 分层架构。每个模块是一个静态 CMake 目标 `periodicity_<module>`，导出为 `periodicity::<module>`，构建系统强制依赖方向。模块顺序是构建顺序，不是拓扑排序；权威依赖边列表见 [framework.md](framework.md) §13.1。
- 只安装公开头文件。模块的 `include/md/<module>/` 是对外安装的接口，`src/` 保持私有，`detail/` 下的头文件不安装。
- 单一绘制路径。`View::paintEvent()` 是 `final`，按固定顺序绘制：elevation 阴影、背景、`paintContent()`、前景图层、焦点环。组件无法跳过状态层或焦点指示。
- 不可变设计令牌。颜色、字体、形状、elevation、动效与状态都是值类型；`Theme` 是值语义的主题包，`ThemeScope` 沿控件父链解析。动态配色与设计基线的表达性扩展被有意推迟。

## 快速开始

### 环境要求

- Windows x64。仓库中的预设由 `Windows` 宿主条件保护。
- **Qt 6.8.3**（MSVC 2022 x64 构建），安装于 `D:/Qt/6.8.3/msvc2022_64`。项目要求 Qt 6.8 或更高版本，预设固定使用上述路径。
- **MSVC 2022**（v143）及“使用 C++ 的桌面开发”工作负载，外加 Visual Studio 自带的 CMake（3.21 或更高）与 Ninja。`vs2022` 和 `vs2026` 预设分别覆盖两种 Visual Studio 工具集。
- 开发者命令提示符，确保 `cl.exe` 在 `PATH` 中。

若 Qt 安装在别处，可新增一个继承 `windows-base` 的 `CMakeUserPresets.json`，设置自己的 `CMAKE_PREFIX_PATH`。该文件已被 Git 忽略，因此提交的预设对其他人仍然通用。

### 构建与测试

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug
```

构建目录为 `out/build/x64-debug`。`x64-release` 用优化、无调试信息的方式编译相同目标。Visual Studio 解决方案预设为 `vs2022` 与 `vs2026`；`cmake --build --preset vs2022-debug` 构建 `periodicity_demo` 及其依赖，demo 已是解决方案的启动项目。

### 运行示例

两个示例应用都构建到同一目录：

```bat
out\build\x64-debug\periodicity_gallery.exe
out\build\x64-debug\periodicity_demo.exe
```

`periodicity_gallery` 是交互式测试台：左侧按组件类别切换页面，标题栏按钮切换明暗主题，每个页面都实例化真实控件而非截图。它还支持 `--screenshot <dir>` 做无人值守渲染。

`periodicity_demo` 是文档式展示窗口。两个可执行文件都由 `periodicity_deploy_qt_runtime()` 部署 Qt 运行时，因此也能从资源管理器或普通 shell 启动。

第三个可执行文件 `out\build\x64-debug\periodicity_ipc_echo.exe` 是 Gallery 的 Pool 页面和 IPC 测试使用的子进程。

## 仓库布局

模块按依赖顺序排列，与根 `CMakeLists.txt` 一致。

| 路径 | 目标 | 用途 |
| --- | --- | --- |
| `core/` | `periodicity::core` | L0：密度与单位换算、`MeasureSpec`、`ViewStates`、几何辅助、日志分类与 i18n 设施。不含控件，也不含绘制策略。 |
| `mathematic/` | `periodicity::mathematic` | L0：纯计算的信号数学——矩阵、分解、频谱、滤波器、窗函数、统计，以及 Hilbert、Laplace、Mellin、Z 积分变换。仅用标准库。 |
| `structure/` | `periodicity::structure` | L1a：无界面的节点、树、森林、跳表、分配器、LRU 容器族，以及表格/虚拟数据源契约。不含控件、令牌或绘制器。 |
| `store/` | `periodicity::store` | L1b：进程级的命名存储注册表；每个存储用有界 LRU 映射保存值。无控件，仅 Qt Core。 |
| `pool/` | `periodicity::pool` | L1p：对象池、线程池、SPSC 环形缓冲、IO 线程、跨线程信箱与同机进程通信。 |
| `schedule/` | `periodicity::schedule` | L1：任务优先级队列，以及 `UiThread` 帧调度器和 `EventThread` 输入管线。 |
| `tokens/` | `periodicity::tokens` | L1：不可变的颜色方案、字体比例、形状比例、elevation、动效与状态层数值。绝不包含 Qt Widgets。 |
| `theme/` | `periodicity::theme` | L2：令牌包、作用域解析、明暗切换与 `QPalette` 桥接。 |
| `event/` | `periodicity::event` | L3：事件值、捕获/拦截/冒泡分发、手势识别与 Qt 事件桥接。 |
| `drawable/` | `periodicity::drawable` | L3a：可复用的绘制原语——形状、阴影、图标、状态层与图层绘制器。 |
| `layer/` | `periodicity::layer` | L3b：合成绘制阶段——背景、elevation、前景与涟漪图层，以及图层栈。 |
| `animation/` | `periodicity::animation` | L3c：可中断的 `Animator`，由动效令牌驱动。 |
| `ripple/` | `periodicity::ripple` | L3：以指针位置为原点的一次性悬停与按压反馈。 |
| `view/` | `periodicity::view` | L4：组件内核——测量/排布/绘制模板、状态机、无障碍、异常保护与可选的 GPU 合成器。 |
| `layout/` | `periodicity::layout` | L5：`Linear`、`Frame`、`Constraint`、`AppBar`、`Collapse` 与 `Coordinator` 容器、间距与窗口尺寸分级。 |
| `components/` | `periodicity::components` | L6：公开控件、25 个图表组件、`Canvas` 及其上下文，以及 Clock/Schedule/Scheduler 组件。 |
| `tests/` | — | Qt Test 测试套件，每个模块一个目录，另含 `tests/visual/` 下的视觉回归基线。 |
| `apps/` | — | `gallery`（交互式测试台）与 `ipc_echo`（Pool 页面和 IPC 测试的子进程）。 |
| `demo/` | — | 文档式演示窗口，目标为 `periodicity_demo`。 |
| `tools/` | — | 品牌与分层检查（`check_branding.cmake`、`check_layering.cmake`）以及 `visual_regression.py`。 |
| `translations/` | — | `periodicity_zh_CN.ts`，编译后嵌入 `:/i18n`。 |

每个模块的内部结构一致：

```
<module>/
├── CMakeLists.txt
├── include/md/<module>/*.h
└── src/*.cpp
```

## 功能亮点

### 组件与图表

- `components/include/md/components/` 下有 **112 个公开头文件**，覆盖基础控件、输入与选择器、日期与级联、表单、展示与导航、媒体、浮层、数据表格与虚拟数据源、容器、时间与日程，以及图表栈。
- **25 个图表组件**，全部派生自 `ChartBase`：九个通用图表（`LineChart`、`ScatterChart`、`PieChart`、`BarChart`、`HistogramChart`、`BoxPlotChart`、`HeatmapChart`、`RadarChart`、`FunnelChart`）与十六个信号图表（`AutocorrelationChart`、`BodeChart`、`ChordDiagram`、`CoherenceChart`、`ConnectivityChart`、`EpicycleChart`、`HilbertChart`、`LaplaceChart`、`MellinChart`、`PolarPlotChart`、`SpectrogramChart`、`SpectrumChart`、`TopographyChart`、`WaterfallChart`、`WaveformChart`、`ZTransformChart`）。
- **图表动画**。每个图表通过 `ChartAnimation` 为数据标记播放入场动画，由 `Animator` 按动效阶梯驱动。首次绘制淡入；数据变化时以较短的更新时长重新显示；启用减弱动效时立即稳定。标题、图例、网格、坐标轴与刻度等静态元素缓存在一帧设备像素比感知的 `QPixmap` 中重复使用，因此每帧只绘制数据标记。
- **抗锯齿策略**。`ChartBase::setAntialiasing()` 可选十一种策略，分属四类：硬边与灰度覆盖、2×/4× 超采样、4×/8× 抖动样本累积，以及 FXAA、FMAA、MLAA、SMAA、CMAA 后处理滤波。默认使用 Qt 的灰度光栅覆盖；昂贵模式需要显式开启。

### Canvas 与绘制上下文

`Canvas` 承载三种上下文。`Context1D` 绘制标量迷你折线图。`Context2D` 是保留式 `QPainter` 命令封装。`Context3D` 是带相机的保留式图元场景：默认用 `QPainter` 投影，软件路径始终参与编译，也是无界面测试所走的路径。当 components 目标在包含 Qt `OpenGLWidgets` 模块的环境中构建时（`PERIODICITY_HAS_OPENGL`），`Context3D` 可嵌入 `QOpenGLWidget` 后端；若绘制时拿不到 GL 上下文，则回退到软件路径并只记录一次日志。`Canvas::requestDraw()` 负责安排重绘，上下文自己不安排。

### 国际化

`md::i18n` 管理位于 `:/i18n/periodicity_<locale>.qm` 的、每种语言一个的已编译 Qt 翻译。仓库提供 `translations/periodicity_zh_CN.ts`，CMake 用 `qt_add_lrelease` 编译并嵌入 `:/i18n`；当前随附的 `zh_CN` 翻译目录覆盖 `Language` 与 `Schedule` 两个上下文。`install("zh_CN")` 加载它，`install("en")` 回到源语言；未知或未构建的语言不会改变当前语言。`LinguistTools` 是可选依赖：没有它时库不携带翻译目录，源语言保持不变。`md::i18n` 只依赖 Qt Core，因此也能用于没有 GUI 的工具。

### 并发运行时

`md::schedule::UiThread` 把延迟工作组织成有预算的帧，并显式划分阶段。`md::schedule::EventThread` 负责输入解码与合并：保留每个按下、抬起与按键事件，对每个目标只保留最后的指针位置与滚轮增量，并在下一个帧边界把一批有序输入交给 UI 线程。`md::schedule::TaskQueue` 为直接投递任务的代码保留五级优先级；`md::pool` 提供工作线程池、IO 线程、环形缓冲、信箱与进程通信，承接所有必须离开 UI 线程的工作。完整模型见 [docs/threading.md](docs/threading.md)。

### 配置存储与容器

`md::Store` 是进程级的命名值映射注册表：`Store::open("md.theme")` 总是返回同一对象，存储会发出变更信号。`Font`、`Language` 与主题作用域容器分别发布到 `md.font`、`md.language` 与 `md.theme`，任何 `View` 都可以用 `bindStore()` 绑定存储；框架负责应用 `theme` 键，子类可扩展 `onStoreChanged()`。存储内部使用 `structure` 的 LRU 映射，因此也能像其他缓存一样设定上限。

### LRU 容器

`structure` 提供 `UnorderedLRUMap`/`Set`（索引放在哈希表）与 `OrderedLRUMap`/`Set`（索引放在有序表），它们共享同一套提升与淘汰内核。提升为 O(1)，缓存满时先淘汰最旧条目再插入，`setCapacity()` 按最旧优先收缩，`setEvictionHandler()` 在被淘汰项离开两个容器之后运行。`ArrayLRUList` 用一个可增长数组保存顺序槽位；`LinkedLRUList` 用侵入式节点保存，是默认的顺序容器。

### 可选的 GPU 合成器

GPU 路径是**选择性开启**的；默认始终是软件 `QPainter` 路径。`GpuCompositor` 承载子视图：每个启用的子视图先用普通 `QPainter` 管线绘制到离屏图像，再作为纹理上传，由 GPU 合成这些纹理。`GpuCanvas` 是单视图变体，在 `QOpenGLWidget` 表面上打开 `QPainter`。`View::setGpuAccelerated(true)` 是开启开关；它作用于该视图及其当前 `View` 后代，且因为开关位于内核而非某个组件的 API 上，所有组件都支持。`setRenderBackend(RenderBackend::OpenGL)` 是用后端表达的同一请求。

它加速的范围说得很清楚：加速的是**已绘制帧的合成与位块拷贝**，而不是组件自身的绘制。变更控件的首次绘制仍在 CPU 上光栅化；布局、文本整形与路径构建仍留在 CPU 绘制管线中。若构建时没有 `Qt6::OpenGLWidgets`，或平台无法创建 GL 上下文，请求会留在软件路径，并由 `View::lastError()` 记录原因。API、成本模型与回退规则见 [docs/gpu.md](docs/gpu.md)。

## 线程与渲染模型

UI 线程拥有控件、布局、绘制以及所有事件目标。事件线程拥有输入解码，绝不触碰控件：它的载荷携带不透明的目标令牌，而不是 `QWidget` 指针。工作线程通过队列信号、`TaskQueue` 或 `UiThread::postTask()` 返回结果。线程拥有自己的状态，队列是跨越线程边界的唯一方式，UI 线程永不阻塞。

`UiThread` 按以下阶段推进一帧：

| 阶段 | 工作 |
| --- | --- |
| `Idle` | 帧与帧之间的静止状态；`postIdleTask()` 可在此运行。 |
| `PreFrame` | 一次性的 `addFrameCallback()` 回调，包括事件批次的交接。这是帧边界。 |
| `Build` | `Priority::UserInput` 与 `Priority::UiUpdate` 工作。 |
| `Layout` | `Priority::UiEvent` 工作，包括投递输入批次。 |
| `Paint` | `addPaintCallback()` 钩子；`recordPaint()` 记录绘制统计。 |
| `PostFrame` | `Priority::Background` 与 `Priority::Idle` 工作。 |

优先级阶梯映射到这些阶段，而不是排空一个扁平队列：

| 优先级 | 数值 | 阶段 | 典型工作 |
| --- | ---: | --- | --- |
| `UserInput` | 0 | `Build` | 按下、点击、按键激活、滚动、涟漪启动 |
| `UiUpdate` | 1 | `Build` | 布局与绘制失效 |
| `UiEvent` | 2 | `Layout` | 悬停、合成通知、动画刻度、输入批次 |
| `Background` | 3 | `PostFrame` | 解码、预取、缓存预热 |
| `Idle` | 4 | `PostFrame` 与 `postIdleTask()` | 清理、指标 |

同一优先级内按 FIFO 顺序；跨优先级保持严格顺序，因此在 `UiUpdate` 批次排空期间投递的按压任务会先于该批次的剩余工作运行。`requestFrame()` 把一次突发合并为一帧，尺寸调整请求对每个目标只保留最后的大小，每个排队任务都有截止时间（默认一秒），过期任务被丢弃并计数，而不会在迟到的帧里重放。`setFrameBudget()` 默认 8000 微秒；超预算的帧仍会到达 `PostFrame`，只记录一次超时，并把剩余工作安排到稍后的事件循环轮次。

`View` 通过请求参与：安装了帧运行时后，`requestUpdate()` 经帧运行时合并重绘；未安装时回退到 `QWidget::update()` 加一个合并的 `TaskQueue` 条目。[docs/threading.md](docs/threading.md) 记录了阶段、合并规则、输入管线与诊断数据。

## 测试

```bat
ctest --preset x64-debug
```

测试是 Qt Test 套件，每个模块一个目录，控件测试集中在 `tests/components`；已提交的代码树中共注册 **99 个测试可执行文件**。CTest 以 `QT_QPA_PLATFORM=offscreen` 运行它们，因此不需要显示器，在桌面与构建机上的表现一致。

`tools/` 下还有两项检查：

- `tools/check_branding.cmake` 在源码、注释或文档中出现被禁止的产品名时失败。可直接运行：

  ```bat
  cmake -DSOURCE_DIR=D:/project/periodicity-cmake -P tools/check_branding.cmake
  ```

- `tools/check_layering.cmake` 执行 `framework.md` §13 的依赖矩阵：公开头文件只能包含其所在层允许的模块，`core`、`structure`、`pool`、`tokens` 与 `event` 必须不依赖 Qt Widgets，未知模块名同样失败。

视觉回归不在 CTest 中。`tests/visual/baseline/` 保存 36 张参考 PNG——18 个 Gallery 页面各一张顶部截图和一张底部截图——`tools/visual_regression.py` 将新截图与该基线比较，并为失败项输出红色差异图。详见 [docs/visual-regression.md](docs/visual-regression.md)。

## 安装

每个模块都通过 `cmake/PeriodicityModule.cmake` 贡献安装规则：静态库安装到 `${CMAKE_INSTALL_LIBDIR}`，公开头文件保持 `md/<module>/` 布局安装到 `${CMAKE_INSTALL_INCLUDEDIR}`，目标加入 `PeriodicityTargets` 导出集。根 `CMakeLists.txt` 以 `periodicity::` 命名空间和 `install(EXPORT PeriodicityTargets)` 安装该导出集：

```cmake
install(EXPORT PeriodicityTargets
  DESTINATION "${PERIODICITY_INSTALL_CMAKEDIR}"
  NAMESPACE periodicity::
  FILE PeriodicityTargets.cmake)
```

因此安装树会提供 `periodicity::core`、`periodicity::view`、`periodicity::components` 等导入目标，并在 `${prefix}/lib/cmake/Periodicity` 下提供 `PeriodicityConfig.cmake` 与 `PeriodicityConfigVersion.cmake`。使用方这样写：

```cmake
find_package(Periodicity 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE periodicity::components)
```

执行 `cmake --install out/build/x64-debug --prefix <prefix>` 即可安装。demo、gallery 与测试仅用于开发，不参与安装。

## 当前状态

Periodicity 的版本为 0.1.0，仍在建设中：十六个模块都能构建并通过测试覆盖。动态配色与设计基线的表达性扩展仍被推迟。上文有两项功能位于工作区、尚未提交，已记录在 [CHANGELOG.md](CHANGELOG.md) 的 `Unreleased` 小节：可选的 GPU 合成器及 `View` 的开启标志，以及把图表抗锯齿从四种扩展到十一种的七个新模式。`LICENSE` 与 `NOTICE` 同样是工作区中的新增文件。由于工作区还新增了 `tests/view/tst_render_backend.cpp`，在工作区运行 `ctest` 会列出 100 个测试，而不是提交树中的 99 个。

## 许可证与归属

Periodicity 采用 **Apache License 2.0** 许可。完整文本见 [LICENSE](LICENSE)，版权与归属声明见 [NOTICE](NOTICE)。本项目依据 Apache License 2.0 授权。

> Licensed under the Apache License, Version 2.0.

该许可证覆盖整个仓库：除非文件另有说明，仓库中的每个文件都在其覆盖范围内。为避免干扰源码阅读，文件不再逐一添加许可证头；根目录的 `LICENSE` 与 `NOTICE` 是权威声明。

Qt 6.8 是外部依赖，仍受其自身许可证约束；仓库不内嵌 Qt。`translations/` 中的翻译目录属于本仓库的一部分。

视觉设计遵循公开的设计基线。`framework.md` §3 中的令牌值已对照上游参考令牌导出核验，源码只按“它是什么”描述该基线，而不按“它是谁的产品”描述；`tools/check_branding.cmake` 是这一约定的执行环节。

## 文档

- [framework.md](framework.md) — 架构、分层与依赖矩阵。
- [docs/components.md](docs/components.md) — 组件目录，含头文件与测试。
- [docs/threading.md](docs/threading.md) — 帧阶段、优先级与输入管线。
- [docs/gpu.md](docs/gpu.md) — 可选的 GPU 合成器及其回退规则。
- [docs/visual-regression.md](docs/visual-regression.md) — Gallery 基线工作流。
- [issue.md](issue.md) — 逐模块边界审计。
- [CHANGELOG.md](CHANGELOG.md) — 逐提交历史，最新在前。
