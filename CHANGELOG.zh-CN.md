# 更新日志

本文件记录 Periodicity 的重要变更。格式遵循 [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)。项目尚未发布带标签的版本，因此每个小节对应一个提交，按从新到旧排列，标题包含短哈希与日期。

## [Unreleased]

### 新增

- **图表**：抗锯齿策略扩展到十一种。新增五种——MLAA、SMAA、CMAA、4× 抖动采样与 8× 抖动采样——与已提交的六种（无抗锯齿、光栅抗锯齿、2× 与 4× 超采样、FXAA、FMAA）并存；由 `ChartAntialiasing` 与 `ChartBase::setAntialiasing()` 选择。
- **GPU 合成器**：`md::GpuCompositor` 与 `md::GpuCanvas` 增加选择性开启的 OpenGL 路径，把已绘制帧作为纹理合成。默认仍是软件路径；缺少 `Qt6::OpenGLWidgets` 模块或 GL 上下文时，视图留在软件路径，原因记录在 `View::lastError()`。API、成本模型与回退规则见 `docs/gpu.md`。
- **View**：`setGpuAccelerated()`、`isGpuAccelerated()`、`setRenderBackend()`/`renderBackend()` 与 `gpuAccelerationChanged` 信号把开启标志放在内核上，因此所有组件都支持，无需逐组件 API。
- **测试**：`tests/view/tst_render_backend.cpp` 覆盖后端选择与回退；在工作区运行 `ctest` 会列出 100 个测试。
- **许可证**：项目改为 Apache License 2.0 许可；新增 `LICENSE`（完整文本）与 `NOTICE`（版权与归属声明）。

## [211998d] — 2026年11月12日

### 新增

- `ChartAntialiasing`，四种策略：`None`、Qt 灰度光栅覆盖（`Grayscale`）、2× 与 4× 超采样。
- `ChartBase::setAntialiasing()`/`antialiasing()`：只让绘图区失效，不影响缓存的静态图层；新增 `tst_chart_antialiasing` 测试套件。

## [0e2dd4b] — 2026年11月12日

### 修复

- 过期任务改为与 `Clock::time_point::max()` 截止哨兵比较，未显式设置截止时间的任务不再被丢弃；超预算路径有明确测试覆盖。
- 超预算的帧会为预算剩余工作安排后续轮次，而不是等待其他调用者再次调度。
- 绘制回调跨帧保留，不再被第一帧消耗。

## [a16c719] — 2026年11月12日

### 新增

- `mathematic` 模块：矩阵、分解、频谱、滤波器、统计，以及 Hilbert、Laplace、Mellin 与 Z 积分变换。
- 通过 `md::i18n` 与 `zh_CN` 翻译目录实现的嵌入式国际化，以及 `Font`、`Language` 配置容器。
- 十六个信号图表、`ChartAnimation`、`ContainerTransform`，以及 `UiThread` 与 `EventThread` 帧/输入运行时。

## [5a4f203] — 2026年11月12日

### 变更

- 为新页面刷新 Gallery 视觉回归基线，并加入新的 Charts 参考图对（顶部与底部）。

## [cde6b3c] — 2026年11月12日

### 新增

- `Canvas` 与 `Context1D`、`Context2D`、`Context3D` 绘制上下文。
- 基于新的 `ChartBase`/`MathematicsCoordination` 栈的九个图表，以及 `Clock`、`Schedule` 与 `Scheduler`。
- `structure` 中的 LRU 容器族，以及可视化相关的新 Gallery 页面。

## [4fe2fb2] — 2026年11月12日

### 修复

- 关闭边界审计发现的 26 个 P1 问题，覆盖事件分发、动画中断、对象池生命周期、调度预算与主题解析，并为修复后的边界补充回归测试。

## [557fa4d] — 2026年11月12日

### 移除

- 移除多余的、仅一字节的 `issue.md.fence` 临时文件。

## [0d14d5d] — 2026年11月12日

### 新增

- `issue.md` 中的逐模块边界审计：26 个 P1 与 59 个 P2 问题，含位置与测试覆盖情况。

### 修复

- 修正审计中暴露的 badge 测试。

## [f2ad36c] — 2026年11月12日

### 新增

- P0 缺口组件：`BottomSheet`、`Chip`、`Divider`、`Fab`、`IconButton`、`NavigationBar`、`SearchBar` 与 `Switch`。
- 可安装打包：`PeriodicityTargets` 导出集、`PeriodicityConfig.cmake`、版本文件与 `periodicity::` 命名空间。
- 无障碍支持、Gallery 视觉回归工具及其 CI 工作流。

## [8f3ccff] — 2026年11月12日

### 新增

- `pool` 模块：对象池、线程池、环形缓冲、IO 线程、跨线程信箱与进程通信。

### 变更

- 把对象池从 `structure` 移入 `pool`，并移除 `.tmp` 中内嵌的 PyYAML 目录。

## [bb5f808] — 2026年11月9日

### 变更

- 合并 11 月 9 日的两条导入线；该合并提交本身不引入文件变更。

## [ba680b3] — 2026年11月9日

### 新增

- 批量导入基础控件实现与 demo 外壳。

## [a12a593] — 2026年11月9日

### 新增

- 批量导入 CI 构建工作流、Gallery 源码及配套文件。

## [38846c1] — 2026年11月9日

### 移除

- 删除 `.github/workflows` 目录及其单平台 CMake 工作流。

## [aaea4b3] — 2026年11月9日

### 新增

- 面向 GitHub Actions 的单平台 CMake 构建工作流。

## [3f1abd0] — 2026年11月9日

### 变更

- 用英式英语重写 `README.md`，并新增简体中文翻译。

## [14955b1] — 2026年11月9日

### 新增

- 初始导入组件库：CMake 预设、分层模块骨架、core 的单位/几何/状态头文件、Gallery 占位、设计待办与架构说明。
