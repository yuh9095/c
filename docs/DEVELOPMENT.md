# 开发说明

## 当前阶段

当前仓库是 Ninit 的工程起点，不是可用于启动真实系统的 init 实现。

目前提供 CMake/C11 构建、公共日志 API、PID1—Manager IPC 消息帧、Unix SOCK_SEQPACKET 通道、Manager 子进程启动与握手，以及 glibc/musl CI。Manager 目前只处理 PING/PONG 并在私有通道关闭后退出；PID1 侧已有独立的 Manager 监督状态机，支持崩溃后的有界指数退避重启。Unit、Job、服务生命周期、PID 1 主循环和完整故障恢复尚未实现。新增的 Boot Coordinator 目前只负责按顺序提交 sysinit、boot、default 三个目标，并接收目标完成/失败结果；它尚未连接真实 Job Manager。不要将当前程序配置为系统 PID 1。

## PID1—Manager 启动流程

ninit_manager_start() 位于 include/ninit/manager_process.h，由未来 PID 1 调用：

1. 创建 AF_UNIX/SOCK_SEQPACKET 私有 socketpair，端点为 NONBLOCK 和 CLOEXEC。
2. 为子进程端点创建 CLOEXEC 副本，并通过 posix_spawn file actions 将其交接为 fd 3。
3. 启动指定路径的 ninit-manager --ipc-fd 3。
4. Manager 发起 HELLO，PID1 端验证版本/角色并回复 HELLO_REPLY。
5. 握手成功后返回 Manager PID 和 PID1 持有的 IPC fd。

子进程继承的 fd 3 是唯一有意跨 exec 保留的 IPC 描述符。管理器启动失败或握手失败时，启动函数终止并回收该子进程。正常断开时，PID1 关闭 IPC fd，Manager 在收到通道关闭后退出；PID1 仍须通过 SIGCHLD 路径回收它。

## PID 1 信号事件源

`include/ninit/pid1_runtime.h` 提供 OpenRC self-pipe 风格的信号事件源：

1. 创建 NONBLOCK、CLOEXEC 管道。
2. 为 SIGCHLD、SIGTERM、SIGINT 和平台提供时的 SIGPWR 安装轻量处理函数。
3. 处理函数只向管道写入信号编号，并保存/恢复 errno。
4. PID 1 主事件循环将管道读端加入 poll/ppoll；醒来后在普通上下文 drain 信号并分发。
5. 每次事件循环醒来后都调用 `ninit_pid1_reap_children()`，统一执行 `waitpid(-1, WNOHANG)`。

当前实现提供一次 poll-based 事件循环迭代：等待信号管道、在普通上下文分发信号，并在每次唤醒后统一回收子进程。它尚未接入 Manager IPC、监督器定时器或 Boot/Shutdown Coordinator，因此不是完整 PID 1 主循环。事件源每个进程只允许一个实例；初始化/销毁由单线程 PID 1 控制。回调不得假定每个标准信号都一一对应：标准信号可能合并，self-pipe 满时处理函数的非阻塞写入也可能失败，因此 SIGCHLD 仅是唤醒提示，统一回收函数才是事实来源。

Manager process API 不再暴露单独的 child reap 接口，避免 Manager 生命周期模块与 PID 1 的全局回收路径竞争。Manager 的退出状态必须由 PID 1 回收后按 PID 路由到 Manager Supervisor。

## IPC 协议与传输层

include/ninit/ipc_channel.h 提供 socketpair()、单帧发送和单帧接收接口。通道使用本地 AF_UNIX/SOCK_SEQPACKET，两个端点均设置 CLOEXEC 和 NONBLOCK。发送/接收遇到 EAGAIN 时返回 NINIT_IPC_WOULD_BLOCK，由上层事件循环稍后重试。

握手 API 位于 include/ninit/ipc_handshake.h。当前协议版本 1 的 HELLO 只协商版本范围和 Manager 角色，HELLO_REPLY 返回选定版本。握手超时默认 3 秒。该握手用于验证进程通道和协议兼容性，不是对抗恶意同 UID 进程的身份认证。

## 构建

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=gcc
cmake --build build
ctest --test-dir build --output-on-failure
```

musl 使用同一份源码和配置，将编译器替换为发行版提供的 musl-gcc。

## 设计约束

- PID 1 优先保证信号处理、子进程回收最终保障和紧急输出。
- PID 1 统一使用一个 waitpid(-1, WNOHANG) 路径回收所有子进程；Manager 和其他模块不得自行 waitpid 竞争回收。
- PID 1 信号处理函数只向 NONBLOCK self-pipe 写入信号编号；事件循环在普通上下文中分发信号并执行回收/关机协调。
- self-pipe 即使因信号突发而满，事件循环每次醒来仍必须调用统一子进程回收函数，避免丢失 SIGCHLD 通知造成僵尸进程。
- Manager 内模块通过清晰 API 协作，不要求每个模块独立成进程。
- Job Manager 是服务控制操作的统一调度入口。
- systemd Unit 兼容层与 Ninit 原生 Unit 模型隔离。
- 第三方库经评估后接入，不提前引入整套 systemd 依赖。


## Boot Coordinator

`include/ninit/boot_coordinator.h` 提供启动目标协调状态机：

1. 调用 `ninit_boot_coordinator_start()` 提交 `sysinit`。
2. Job Manager 完成目标后，调用 `ninit_boot_coordinator_target_complete()`。
3. 成功时按顺序提交 `boot`、`default`；任一目标失败则进入 FAILED，不再提交后续目标。
4. `ninit_boot_coordinator_shutdown()` 阻止继续推进启动序列。

当前模块只定义协调接口和状态转换，submit 回调仍需由未来 Job Manager 实现；它不解析 Unit、不执行服务，也不代表系统已经具备可启动能力。


## Native Unit Manager

当前第一版 Unit Manager 提供内存中的原生 Unit 注册表，不负责解析配置文件或启动进程。

- 支持 `service` 与 `target` 两种 Unit 类型。
- 每个 Unit 有唯一名称和可选 description；service 必须提供 ExecStart argv，target 不允许 ExecStart。
- ExecStart 按 argv 数组保存并交给后续执行器直接 exec，不经过 shell 字符串解析。
- Unit 可声明 Requires、Wants、After、Before 名称列表。
- 注册时深拷贝定义及依赖名称；调用者可在 add 返回后释放输入缓冲区。
- 重复名称返回 `EEXIST`，非法名称/定义返回 `EINVAL`。
- 查找返回 manager 所有的只读借用指针，生命周期截止于 manager 销毁。

名称目前限制为最多 255 个 ASCII 字母数字字符以及 `_`、`-`、`.`、`@`、`:`。依赖名称在注册时只做语法检查；是否存在、是否形成循环以及实际启动顺序由后续 Dependency Engine 负责。

该模块是数据模型和注册表，不是完整服务管理器。配置文件解析、Unit 状态、Job 调度、进程监督和 systemd/OpenRC 兼容均未实现。


## Dependency Engine（第一版）

`include/ninit/dependency_plan.h` 根据指定 Target 生成激活计划：

- 从根 Target 迭代计算 Requires/Wants 闭包；Requires 缺失返回 `ENOENT`，Wants 缺失忽略。
- After/Before 只建立排序边，不会单独激活服务；引用的 Unit 不在激活闭包时忽略。
- `After=X` 表示 X 在当前 Unit 之前；`Before=X` 表示当前 Unit 在 X 之前。
- 使用稳定的 Kahn 分层拓扑排序；同一 wave 中的 Unit 在其所有前置排序约束满足后可并行启动。
- 排序环路返回 `ELOOP`。计划只保存 manager 所有的只读 Unit 指针，必须在 manager 销毁前释放计划。

该模块只生成计划，不创建 Job、不 fork/exec，也不跟踪服务状态。当前实现使用邻接矩阵表达排序边，适合当前小规模原生 Unit 集合；后续规模扩大时再评估稀疏图结构。


## Job Manager（Start Job 第一版）

`include/ninit/job_manager.h` 提供内存中的 Start Job 调度核心：

- 提交时复制 Dependency Plan 中的 Unit 借用指针和 wave 编号；Unit Manager 必须比 Job Manager/Job 活得更久。
- 每个 Job 可设置最大并发数；同一 wave 可并行派发，后续 wave 必须等待当前 wave 全部完成。
- 执行器通过 dispatch 领取 Unit，再通过 complete 报告成功或失败。
- 任一 Unit 启动失败会将 Job 标记 FAILED，并跳过尚未派发的 Unit；已派发任务仍可回报完成。
- cancel 会跳过未派发任务；已运行任务仍由执行器完成/收尾。
- Job 状态可查询；Job 记录保留到 Job Manager 销毁。

当前只实现 Start Job 的调度状态机，不执行 fork/exec，不负责服务进程监督，也未实现 Stop/Restart/Reload、Job 合并或跨 Job 冲突处理。失败后已派发的任务由上层执行器负责停止或清理。


## Service Executor（第一版）

`include/ninit/service_executor.h` 提供 Service Unit 的 fork/exec 启动适配器：

- 只接受 Service Unit，并直接使用 argv 数组调用 `execvp()`；不经过 shell。
- 使用 CLOEXEC 错误管道区分 fork 成功与 exec 成功：exec 成功时 close-on-exec 使父进程读到 EOF；失败时子进程写回 errno 并以 127 退出。
- 父进程通过非阻塞读端调用 `ninit_service_executor_check_exec()`，可在 PID 1 事件循环中等待握手完成。
- 执行器不调用 wait/waitpid。子进程退出状态必须由 PID 1 的统一 `ninit_pid1_reap_children()` 回收并按 PID 路由。
- Target 不创建进程，应由 Job 调度层直接完成。

当前版本尚未把 PID、exec 握手 fd 和 Job ID 集成进 Manager 主循环，也未实现服务运行状态、停止信号、超时、重启策略或进程组管理。不要将此模块单独视为完整服务监督器。
