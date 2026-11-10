# Ninit

**Ninit** 是一个面向 Linux 的独立 Init 与系统服务管理项目，使用 C 语言开发。

Ninit 借鉴 Dinit、OpenRC 和 systemd 等成熟项目的设计经验，但定义自己的原生配置格式、内部模型和行为语义，而不是简单拼接或复制其他项目。

> **项目状态：早期开发 / 设计阶段**
>
> 当前仓库是工程起点，尚未实现可用于真实系统启动的 PID 1 或完整服务管理功能。请勿将当前程序配置为系统 init。

## 项目目标

- 提供独立的 Linux PID 1 实现和常驻服务管理器。
- 将 PID 1 的系统生存职责与服务管理职责分离。
- 采用模块化设计，明确各模块的职责和接口。
- 定义 Ninit 自己的 Unit、依赖关系、服务生命周期和 Job 语义。
- 提供服务状态查询、控制和日志查看能力。
- 必要时复用成熟的 systemd 库或其他第三方组件，但不依赖 systemd Manager。
- 使用一套源码和构建规则支持 musl、glibc 两种构建目标。

## 设计原则

1. **自主语义**：Ninit 原生配置和运行行为由 Ninit 自己定义。
2. **借鉴成熟经验**：参考 Dinit、OpenRC、systemd 的成熟设计，不盲目照搬。
3. **避免重复造轮子**：必要时采用成熟的库和 Linux 内核接口。
4. **模块化**：功能源码分离，通过明确的模块 API 交互。
5. **边界清晰**：第三方库通过适配层接入，不能决定 Ninit 的核心语义。
6. **可靠性优先**：PID 1 保持精简，服务管理器故障不能使系统失去 PID 1。
7. **统一构建**：一份源码、一套构建规则，分别验证 musl 和 glibc。

## 架构概览

Ninit 采用 PID 1 与服务管理器分离的进程架构。

### Ninit Init（PID 1）

负责系统最基础、必须可靠的职责：

- 早期系统初始化。
- 信号处理和系统子进程回收。
- 服务管理器进程监视。
- 与服务管理器之间的内部通信。
- 紧急故障处理和系统关机协调。

### Ninit Manager

负责常规系统服务管理：

- Unit 加载与配置解析。
- 依赖关系分析和启动顺序。
- Job 创建、调度和执行。
- 服务生命周期与进程监督。
- Target 管理。
- Socket、Timer、Path 等事件触发功能。
- 日志采集、存储和查询。

PID 1 与 Manager 通过内部 IPC 通信。当前已实现版本化消息帧、Unix SOCK_SEQPACKET 私有通道、Manager 子进程启动和 HELLO/HELLO_REPLY 握手；完整 PID1 主循环、服务管理和故障恢复仍在开发中。

## 计划中的功能模块

| 模块 | 职责 |
|---|---|
| Unit | 原生服务单元模型与配置加载 |
| Dependency | 依赖关系和启动顺序处理 |
| Job | Start、Stop、Restart 等操作的统一调度 |
| Service | 服务生命周期管理 |
| Process | 进程创建、监督和退出状态处理 |
| Target | 系统启动目标与运行级别组织 |
| Socket | Socket 激活 |
| Timer | 定时任务触发 |
| Path | 文件路径变化监视 |
| Mount | 文件系统挂载管理 |
| Logging | 服务日志采集、存储和查询 |
| Compatibility | systemd Unit 文件兼容层 |

上述列表是规划，不代表所有模块已经实现。

## 兼容性策略

### 原生配置

Ninit 将提供自己的原生 Unit 格式和配置语义。原生接口是项目长期维护的主要接口。

### systemd Unit

计划提供独立的 systemd Unit 兼容层，将受支持的配置映射到 Ninit 内部模型。对于无法正确映射的指令，应明确报告不支持或语义差异，不应静默忽略。

### OpenRC 与 SysV init 脚本

不计划执行 OpenRC init.d Shell 脚本或传统 SysV init 脚本。可以借鉴 OpenRC 的依赖组织和运行级别思想，但不实现脚本兼容执行器。

## 构建与 libc 支持

Ninit 使用一套源码和统一构建系统，不维护 musl 与 glibc 两套代码分支。

同一份源码和构建规则分别在对应工具链下构建，生成各自 ABI 兼容的程序和库。musl 与 glibc 构建产物通常不能直接互换。

优先使用标准 C、POSIX 和 Linux 接口。确有必要时，通过统一适配层处理 libc 差异。

## 第三方依赖

Ninit 可以在必要时使用 systemd 提供的独立库或其他成熟组件，例如服务通知、Journal 或 D-Bus 相关接口。

- 第三方依赖统一管理版本、补丁和构建规则。
- 使用同一份依赖源码，分别为 musl 和 glibc 目标构建。
- 只链接实际使用的组件，不要求把所有 systemd 库都链接进 Ninit。
- Ninit 核心不依赖 systemd Manager，也不将 systemd 的内部服务管理实现作为自身核心。

## 快速构建

需要 CMake、C 编译器和 Ninja 或 Make。

glibc：

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=gcc
cmake --build build
ctest --test-dir build --output-on-failure
```

musl（需要提供 `musl-gcc` 的工具链）：

```sh
cmake -S . -B build-musl -DCMAKE_C_COMPILER=musl-gcc
cmake --build build-musl
ctest --test-dir build-musl --output-on-failure
```

更多开发约束见 [开发说明](docs/DEVELOPMENT.md)，IPC 帧格式见 [IPC 协议文档](docs/IPC-PROTOCOL.md)。

## 开发路线

1. 建立统一构建系统、基础目录结构和测试框架。
2. 实现 PID 1 的最小可靠功能。
3. 实现 Ninit Manager 及 PID 1—Manager IPC。
4. 定义并实现 Unit、Dependency、Job、Service 和 Process 核心模块。
5. 实现 ninitctl 基础管理命令。
6. 增加日志、Socket、Timer、Mount 等扩展模块。
7. 逐步实现 systemd Unit 兼容层。
8. 建立 musl、glibc、不同架构和容器环境下的持续集成测试。

## 当前状态

本项目处于早期阶段。本文档描述的是当前设计方向和开发计划；尚未实现的功能不会被视为已经可用。随着接口和行为规范确定，本文档将持续更新。

## 许可证

项目许可证尚待确定。正式发布前将在仓库中补充许可证文件。
