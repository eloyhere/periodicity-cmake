# Periodicity

[English](README.md) | 简体中文

Periodicity 是一个跨平台的 C++ 桌面应用 UI 组件库，以 Material Design 3 为设计基准，构建在
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
| `tokens/` | L1 — 颜色、字体、形状、高度、动效、状态 | `periodicity::tokens` |
| `theme/` | L2 — 主题包与作用域解析 | `periodicity::theme` |
| `event/` | L3 — 事件值、分发、传播、手势 | `periodicity::event` |
| `drawable/` | L3a — 绘制原语 | `periodicity::drawable` |
| `layer/` | L3b — 背景、高度与前景层 | `periodicity::layer` |
| `animation/` | L3c — 由动效令牌驱动的补间 | `periodicity::animation` |
| `view/` | L4 — 测量/排布/绘制内核与状态机 | `periodicity::view` |
| `layout/` | L5 — 容器、间距、窗口尺寸分级 | `periodicity::layout` |
| `components/` | L6 — 公开的 Material 控件 | `periodicity::components` |
| `apps/gallery/` | 示例应用 | `periodicity::gallery` |
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

Periodicity 需要 **Qt 6.8.3**（MSVC 2022 x64 构建），安装于 `R:/Qt/6.8.3/msvc2022_64`，以及
Visual Studio 2022（工具集 14.51）和随其附带的 CMake、Ninja。项目本身要求 CMake 3.21 或更高版本，
以 C++20 编译。

Qt 的安装路径固定在 `CMakePresets.json` 中。若你的安装位置不同，最干净的做法是新建一个继承自
`windows-base` 的 `CMakeUserPresets.json`，在其中设置自己的 `CMAKE_PREFIX_PATH`；该文件已被 Git
忽略，因此提交出去的预设对其他人依然通用。

## 构建

请从 Visual Studio 开发者环境配置并构建，以保证 `cl.exe` 在 `PATH` 中：

```bat
call "R:\visual\ide\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset x64-debug
cmake --build --preset x64-debug
```

若在普通 shell 中操作，把同样的命令包一层即可：

```powershell
cmd /c "call R:\visual\ide\VC\Auxiliary\Build\vcvars64.bat >nul && R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --preset x64-debug && R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build --preset x64-debug"
```

两个预设都构建到 `out/build/<preset>`。`x64-release` 编译同样的目标，开启优化并去掉调试信息。

## 测试

测试是 Qt Test 套件，每个模块一个目录，由 CTest 统一驱动：

```powershell
cmd /c "call R:\visual\ide\VC\Auxiliary\Build\vcvars64.bat >nul && R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir out/build/x64-debug --output-on-failure"
```

测试套件以 `QT_QPA_PLATFORM=offscreen` 运行，因此不需要显示器，在桌面和构建机上的表现完全一致。

## 分层检查

`framework.md` 中的依赖规则由一个小的 CMake 脚本强制执行：

```powershell
R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -DSOURCE_DIR=R:/project/periodicity-cmake -P tools/check_layering.cmake
```

只要某个公开头文件包含了它所在层次不允许依赖的模块，或者 `core`、`tokens` 伸手去碰 Qt Widgets，
这项检查就会失败。

## 状态

Periodicity 仍在建设中，正逐层向上推进。`framework.md` §3 中记录的令牌值已对照 Google 的参考令牌
来源核验过。动态配色与 Material 3 Expressive 被有意推迟，待其下层稳定之后再行处理。
