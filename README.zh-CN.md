# Periodicity

[English](README.md) | 简体中文

Periodicity 是一个跨平台的 C++ 桌面应用 UI 组件库，遵循一套声明式设计基线，构建在
Qt Widgets 之上。库中的每一个视觉元素都由 `QPainter` 亲自绘制；它既不用 Qt Style Sheets 给控件
换皮，也不回退到平台主题引擎。它向宿主系统索取的只有字体和原生窗口边框，因此一处布局在 Windows
上看着对，在 macOS 和 Linux 上也一样对。开发仓库位于
[github.com/eloyhere/periodicity-cmake](https://github.com/eloyhere/periodicity-cmake)。

整个库自下而上分层排列，最底层是单位与几何，最顶层是公开控件。[framework.md](framework.md)
完整阐述了这套架构：分层方式、各模块的职责、核心抽象，以及允许的依赖矩阵。改动模块边界之前，请
先读它。

## 目录布局

| 路径 | 层次 | 目标 |
| --- | --- | --- |
| `core/` | L0 — 单位、几何、状态标志 | `periodicity::core` |
| `structure/` | L1a — 无控件的数据结构（节点、树、森林、对象池）与表格/虚拟数据源契约 | `periodicity::structure` |
| `pool/` | L1p — 对象池、线程池、环形缓冲、IO 线程、信箱与进程通信 | `periodicity::pool` |
| `tokens/` | L1 — 颜色、字体、形状、高度、动效、状态 | `periodicity::tokens` |
| `theme/` | L2 — 主题包与作用域解析 | `periodicity::theme` |
| `event/` | L3 — 事件值、分发、传播、手势 | `periodicity::event` |
| `drawable/` | L3a — 绘制原语 | `periodicity::drawable` |
| `layer/` | L3b — 背景、高度与前景层 | `periodicity::layer` |
| `animation/` | L3c — 由动效令牌驱动的补间 | `periodicity::animation` |
| `view/` | L4 — 测量/排布/绘制内核与状态机 | `periodicity::view` |
| `layout/` | L5 — 容器、间距、窗口尺寸分级 | `periodicity::layout` |
| `components/` | L6 — 公开组件 | `periodicity::components` |
| `apps/gallery/` | 示例应用 | `periodicity::gallery` |
| `apps/ipc_echo/` | Pool 页面与 IPC 测试启动的子进程 | `periodicity_ipc_echo` |
| `tests/` | Qt Test 测试套件，每个模块一个目录 | — |
| `tools/` | 分层检查及其他工具 | — |

每个模块的内部结构都是一致的：

```
<module>/
├── CMakeLists.txt
├── include/md/<module>/*.h
└── src/*.cpp
```

`include/` 下的头文件是模块对外的公开接口，也是其他模块唯一可以包含的部分；`src/` 下的源码则保持
私有。`apps/gallery/` 中的示例应用只使用公开目标拼装，从而让这条边界始终成立。

## 环境要求

Periodicity 需要 **Qt 6.8.3**（MSVC 2022 x64 构建），安装于 `D:/Qt/6.8.3/msvc2022_64`，以及
Visual Studio 2022 或更新版本的工具链（`msvc2022_64` 的 Qt 二进制与后续 MSVC 14.x ABI 兼容）
和随其附带的 CMake、Ninja。项目本身要求 CMake 3.21 或更高版本，以 C++20 编译。
`CMakePresets.json` 同时为 Visual Studio 2022 与 Visual Studio 2026 提供解决方案预设，详见
[Visual Studio](#visual-studio)。

Qt 的安装路径固定在 `CMakePresets.json` 中。若你的安装位置不同，最干净的做法是新建一个继承自
`windows-base` 的 `CMakeUserPresets.json`，在其中设置自己的 `CMAKE_PREFIX_PATH`；该文件已被 Git
忽略，因此提交出去的预设对其他人依然通用。

## 构建

请从 Visual Studio 开发者环境配置并构建，以保证 `cl.exe` 在 `PATH` 中：

```bat
call "D:\visual studio\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset x64-debug
cmake --build --preset x64-debug
```

若在普通 shell 中操作，把同样的命令包一层即可：

```powershell
cmd /c "call ""D:\visual studio\VC\Auxiliary\Build\vcvars64.bat"" >nul && ""D:\visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --preset x64-debug && ""D:\visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build --preset x64-debug"
```

两个预设都构建到 `out/build/<preset>`。`x64-release` 编译同样的目标，开启优化并去掉调试信息。

## Visual Studio

仓库提供两个解决方案预设，因为 Visual Studio 生成器绑定 IDE 自带的工具集：

| 预设 | IDE / 工具集 | 解决方案 |
| --- | --- | --- |
| `vs2026` | Visual Studio 2026，MSVC v180 | `out/build/vs2026/Periodicity.slnx` |
| `vs2022` | Visual Studio 2022，MSVC v143 | `out/build/vs2022/Periodicity.sln` |

选与已安装 IDE 匹配的那个。在只有 v180 的机器上使用 v143 预设会在配置阶段报 `MSB8020`，这正是在提示换另一个预设。

```bat
cmake --preset vs2026
cmake --build --preset vs2026-debug     :: 只构建 periodicity_demo 及其依赖
```

随后打开 `out/build/vs2026/Periodicity.slnx`，按 **F5** 即可。`periodicity_demo` 已经是解决方案的启动项目（由根 `CMakeLists.txt` 的 `VS_STARTUP_PROJECT` 指定），不需要手工在解决方案资源管理器里选择；`vs2026-all-debug` 会构建全部目标。

有两处细节让 demo 可以直接从 IDE 运行：

* `cmake/PeriodicityModule.cmake` 里的 `periodicity_deploy_qt_runtime()` 在每次链接后调用 `windeployqt`，把 Qt 的 DLL 和平台插件复制到 `periodicity_demo.exe` 旁边，因此从资源管理器或普通 shell 启动同样可用；
* 同一个函数设置 `VS_DEBUGGER_ENVIRONMENT` 与 `VS_DEBUGGER_WORKING_DIRECTORY`，即使首次运行、部署步骤尚未产生任何文件，调试器也能找到 Qt。

Gallery 采用同样的处理。如果 Qt 安装没有 `translations` 目录（开发者版构建），`windeployqt` 会提示翻译不可用；该提示仅是信息性的，不影响构建。

Visual Studio 的“文件夹”模式同样可用：**文件 → 打开 → 文件夹**，选择仓库根目录，再选择 `vs2026`（或 `vs2022`）配置预设。

## 测试

测试是 Qt Test 套件，每个模块一个目录，由 CTest 统一驱动：

```powershell
cmd /c "call ""D:\visual studio\VC\Auxiliary\Build\vcvars64.bat"" >nul && ""D:\visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"" --test-dir out/build/x64-debug --output-on-failure"
```

测试套件以 `QT_QPA_PLATFORM=offscreen` 运行，因此不需要显示器，在桌面和构建机上的表现完全一致。

## 分层检查

`framework.md` 中的依赖规则由一个小的 CMake 脚本强制执行：

```powershell
D:\visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -DSOURCE_DIR=D:/project/periodicity-cmake -P tools/check_layering.cmake
```

只要某个公开头文件包含了它所在层次不允许依赖的模块，或者 `core`、`tokens` 伸手去碰 Qt Widgets，
这项检查就会失败。

## 示例 Gallery

`apps/gallery/` 是一个可交互的组件测试台，只使用公开 target 组装：左侧按类别切换页面，右上角切换
明暗主题，每个页面都把一类控件以真实实例摆出来。它同时支持无人值守截图，可作为构建机上的冒烟检查：

```powershell
out\build\x64-debug\periodicity_gallery.exe
out\build\x64-debug\periodicity_gallery.exe --screenshot .tmp\shots          # 每页顶部一张
out\build\x64-debug\periodicity_gallery.exe --screenshot .tmp\shots --dark   # 深色主题
```

`--screenshot` 会为每页额外保存一张 `*-bottom.png`（滚动到底），以便覆盖折叠线以下的卡片。

## 状态

Periodicity 仍在建设中。目前 `core` 到 `components` 的整条链路已经可以编译、测试并渲染：基础控件、
输入与选择器、日期/级联/提及、布局容器、工具栏/徽章/状态页以及展示类组件都有真实实现与测试。
`framework.md` §3 中记录的令牌值已对照上游参考令牌来源核验过。动态配色与设计基线的表达性扩展
被有意推迟；安装与导出（`find_package(Periodicity)`）尚未实现，目前适合在源码树内集成。
