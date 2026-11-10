# 开发说明

## 当前阶段

当前仓库是 Ninit 的工程起点，不是可用于启动真实系统的 init 实现。

目前提供 CMake/C11 构建、公共日志 API、PID1—Manager IPC 消息帧、Unix SOCK_SEQPACKET 通道、Manager 子进程启动与握手，以及 glibc/musl CI。Manager 目前处理 PING/PONG、监听自己的 SIGCHLD self-pipe，并统一回收自己创建的子进程；PID1 侧已有独立的 Manager 监督状态机，支持崩溃后的有界指数退避重启。Unit、Job、服务生命周期、PID 1 主循环和完整故障恢复尚未实现。新增的 Boot Coordinator 目前只负责按顺序提交 sysinit、boot、default 三个目标，并接收目标完成/失败结果；它尚未连接真实 Job Manager。不要将当前程序配置为系统 PID 1。

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

PID 1 只回收自己的直接子进程（例如 ninit-manager）；Manager 对自己创建的服务子进程使用独立的 SIGCHLD self-pipe 和 waitpid(-1, WNOHANG) 路径。两个进程各自只回收自己的子进程，不存在跨进程 waitpid 竞争。Manager 的退出状态仍由 PID 1 回收后按 PID 路由到 Manager Supervisor。

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
- PID 1 统一使用一个 waitpid(-1, WNOHANG) 路径回收自己的直接子进程；Manager 也统一使用自己的 waitpid(-1, WNOHANG) 路径回收其直接子进程。模块不得在同一进程内另建竞争回收路径。
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

当前 Unit Manager 提供内存中的原生 Unit 注册表；独立的 Unit File Loader 可从单个 .service/.target 文件解析定义并注册到该表中。

- 支持 `service` 与 `target` 两种 Unit 类型。
- 每个 Unit 有唯一名称和可选 description；service 必须提供 ExecStart argv，target 不允许 ExecStart。
- ExecStart 按 argv 数组保存并交给后续执行器直接 exec，不经过 shell 字符串解析。
- Unit 可声明 Requires、Wants、After、Before 名称列表。
- 注册时深拷贝定义及依赖名称；调用者可在 add 返回后释放输入缓冲区。
- 重复名称返回 `EEXIST`，非法名称/定义返回 `EINVAL`。
- 查找返回 manager 所有的只读借用指针，生命周期截止于 manager 销毁。

名称目前限制为最多 255 个 ASCII 字母数字字符以及 `_`、`-`、`.`、`@`、`:`。依赖名称在注册时只做语法检查；是否存在、是否形成循环以及实际启动顺序由后续 Dependency Engine 负责。

Unit File Loader 当前支持 [Unit] 的 Description/Requires/Wants/After/Before，以及 [Service] 的 ExecStart；ExecStart 使用轻量引号/反斜杠分词，不调用 shell、不做变量展开。支持单个文件加载，也支持目录加载；目录中的 .service/.target 文件按文件名排序后注册，遇到错误会停止，但已注册的前序文件不会回滚。尚未实现 drop-in、环境变量、ExecStartPre/Post 或 systemd/OpenRC 兼容。Unit 状态、完整服务管理仍由后续模块负责。


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
- 父进程通过非阻塞读端调用 `ninit_service_executor_check_exec()`，由 Manager 事件循环等待握手完成。
- 执行器不调用 wait/waitpid。子进程退出状态由 Manager 的统一 child reaper 回收，再按 PID 路由到 Service Runtime。
- Target 不创建进程，应由 Job 调度层直接完成。

当前 Manager 主循环已接入 SIGCHLD self-pipe、Service Runtime 的 child-exit 回调，以及服务 exec-error 管道的 poll 监听。exec 成功/失败结果会反馈给 Start Job。尚未接入 Unit 加载和 Job 提交，因此主循环目前还没有自动启动服务；也未实现服务运行状态、停止信号、超时、重启策略或进程组管理。不要将此模块单独视为完整服务监督器。


## PID 1 启动配置传递

PID 1 侧提供 `ninit_manager_start_with_config()`，可在启动 Manager 时传递 Unit 目录和目标；旧的 `ninit_manager_start()` 保留并继续启动 IPC-only 模式。Manager Supervisor 可通过 `ninit_manager_supervisor_set_startup_target()` 设置同一配置，首次启动和崩溃退避后的重启都会使用该配置。

传入 Supervisor 的 `unit_dir` 和 `target` 字符串由调用者持有，必须在 Supervisor 生命周期内保持有效。配置必须同时提供或同时为 NULL。

## Manager 原生启动入口（第一版）

`ninit-manager` 现在接受可选参数：

```sh
ninit-manager --ipc-fd 3 --unit-dir /etc/ninit/units --target default.target
```

`--unit-dir` 与 `--target` 必须同时提供。Manager 在 IPC 握手前加载目录中的原生 Unit 文件，检查目标存在，构建 Requires/Wants 激活闭包和 After/Before 排序计划，并提交一个 Start Job（最大并发数暂定为 4）。握手成功后立即派发可运行任务；每次处理 exec/SIGCHLD/IPC 事件后继续派发下一批任务。Unit Manager、Dependency Plan 在整个 Job 生命周期内保持有效。

不提供这两个参数时，Manager 保持原有的仅 IPC/监督模式。当前 PID1 的 `ninit_manager_start()` 尚未提供参数转发，因此这条原生启动路径还不能由 PID1 配置自动启用；下一步需要给 PID1—Manager 启动配置增加显式的 Unit 目录和目标字段，并补充端到端测试。该入口也还没有 sysinit/boot/default 多阶段协调、正常 Stop Job、服务进程组或重启策略。

## Service Runtime（第一版）

`include/ninit/service_runtime.h` 将 Job Manager 与 Service Executor 连接起来：

- 从 Start Job 领取一个 Unit；Target 立即完成，Service 则创建子进程并登记 PID、exec 错误管道、Job ID 和 plan index。
- 事件循环可通过 `ninit_service_runtime_exec_pending_count()` 和 `ninit_service_runtime_exec_pending_at()` 枚举待处理的 PID/fd，将 exec-error 管道加入 poll；事件就绪后调用 `ninit_service_runtime_check_exec()`。
- exec 成功才将对应 Unit 的 Start Job 标记成功；exec 失败则把实际 errno 反馈给 Job Manager。
- 创建服务子进程的 Manager 必须负责回收它们，并将 PID 和 wait status 交给 `ninit_service_runtime_child_exited()`。PID 1 只回收自己的直接子进程（例如 Manager），运行时模块本身不会调用 wait/waitpid。
- 进程记录只有在 exec 握手和 child-exit 通知都完成后才释放，兼容短命进程在握手处理前就退出的竞态。

运行时对象借用 Job Manager；Job Manager 和 Unit Manager 必须比运行时对象活得更久。运行时销毁前，调用方必须先停止并回收所管理的子进程。`ninit_service_runtime_signal_all()` 可向所有已跟踪子进程发送信号，但不负责 waitpid；Manager 在退出清理阶段发送 SIGKILL，并继续通过 exec 握手 fd 与统一 child reaper 处理所有子进程，直到 Runtime 跟踪记录清空。当前尚未实现面向正常 Stop Job 的优雅停止、进程组、超时、重启策略、Unit 配置加载和 Job 提交。

## PID 1 executable

The `ninit-init` executable is the PID 1 entry point. It refuses to run
outside PID 1, installs the OpenRC-style self-pipe signal event source, starts
`ninit-manager`, and supervises that direct child. Its defaults are
`/usr/bin/ninit-manager`, `/etc/ninit/units`, and `default.target`; these
can be overridden with `--manager`, `--unit-dir`, and `--target`.

PID 1 handles SIGTERM, SIGINT, and SIGPWR as shutdown requests. It closes the
manager IPC channel and waits for the manager child to exit. SIGCHLD handling
always uses the centralized nonblocking waitpid loop, then routes the reaped
manager PID to the supervisor. PID 1 does not wait for or manage service
grandchildren; the manager owns those processes and its own cleanup.

Manager restart uses the supervisor's bounded exponential backoff. The PID 1
poll timeout is derived from the next restart deadline so restarts do not
require periodic busy polling. This is an initial operational entry point;
console setup, mount setup, sysinit stages, reboot/poweroff syscall handling,
and a complete shutdown policy remain future work.


## Execution environment detection (initial)

`include/ninit/environment.h` exposes an independent environment detector and a
conservative startup policy. The first implementation follows the layered
approach used by systemd's `detect_container()` and OpenRC's
`detect_container()`/`rc_sys` model:

- `/run/systemd/container` and the inherited `container` environment value;
- Podman and Docker marker files;
- OpenVZ procfs indicators;
- cgroup path indicators for Docker, Podman, Kubernetes, LXC and
  systemd-nspawn;
- WSL indicators and, as a final PID-1 fallback, the Linux initial PID
  namespace inode check.

The result records the environment kind, recognized container family and
evidence bits. Failure to inspect procfs is represented as UNKNOWN, not HOST.
UNKNOWN and CONTAINER receive a restrictive policy: no host reboot, no
host cgroup management, and no automatic replacement of proc/sys/dev mounts.
HOST-like environments receive permission flags for later mount and shutdown
stages; the flags are not proof that an operation is permitted, and every
mount operation must still check existing mounts and handle kernel errors.

PID 1 currently performs detection before console setup and reports the
selected environment. The policy is not yet connected to early mount setup,
service keyword filtering, or reboot/poweroff syscalls. The next work is to
add an OpenRC-style administrator override (`rc_sys` equivalent), broaden
Linux-VServer/UML and namespace/chroot detection, then make mount and shutdown
operations consume the policy. Do not treat the current detector as a complete
security boundary or assume that a detected container name proves a specific
capability set.


### Administrator environment override (rc_sys-style)

PID 1 accepts `--environment` as an explicit administrator override. Values
are `auto` (default), `host`, `container`, or a recognized container type
(`docker`, `podman`, `lxc`, `openvz`, `systemd-nspawn`, `wsl`,
`proot`, `oci`, `other`). The `container:TYPE` spelling is also accepted.
For example:

```sh
ninit-init --environment auto
ninit-init --environment lxc
ninit-init --environment host
```

An explicit value takes precedence over all automatic indicators, matching
OpenRC's administrator-controlled `rc_sys` concept. Unknown values are
configuration errors and cause startup to fail rather than silently selecting
a potentially unsafe host policy. `host` is an explicit trust decision: it
enables the host policy even if runtime markers say container, so it must only
be used by an administrator who controls the boot configuration.

The persistent configuration file is `/etc/ninit/environment.conf` and uses
one assignment:

```ini
Environment=auto
# or: Environment=container:lxc
```

The file is optional; when absent, `auto` is selected. A command-line
`--environment` value takes precedence over the file. Duplicate assignments,
unknown keys, and malformed values are rejected. Service-level environment
keywords are not implemented yet.


### Virtual machine versus container detection

Ninit classifies WSL as a virtual-machine guest, not as a container. A VM may
initialize guest-local procfs, sysfs, and devtmpfs when needed, while host
shutdown and host-level cgroup management remain disabled. Container policy
continues to preserve runtime-provided mounts. These policy flags are advisory:
future mount and shutdown implementations must inspect the current namespace,
mount table, capabilities, and syscall results before acting.


### Read-only mount and capability probing

`ninit_environment_probe_capabilities()` reads the current process'
`/proc/self/mountinfo` and `/proc/self/status`. It reports whether proc,
sysfs, devtmpfs, and cgroup filesystems are mounted, and whether the effective
capability set contains `CAP_SYS_ADMIN` or `CAP_SYS_BOOT`. Availability
flags distinguish an unavailable procfs/status source from a capability that
is present with value zero.

This is a read-only snapshot, not an authorization guarantee. It does not
attempt a mount or reboot, and the caller must still handle namespace changes,
capability changes, and syscall failures. PID 1 logs the snapshot and now uses
it to produce a read-only early mount plan.


### Mount Manager（只读规划与显式执行第一版）

`include/ninit/mount_manager.h` 提供三个早期伪文件系统目标的 mount plan：

- `/proc` → `proc`，`/sys` → `sysfs`，`/dev` → `devtmpfs`。
- 已存在且类型正确的挂载标记为 `skip`；目标已有其他文件系统时标记为 `conflict`，绝不覆盖。
- 目标缺失时，必须同时满足环境策略允许、mountinfo 可读、CapEff 可读且当前有效集包含 `CAP_SYS_ADMIN`，才会标记为 `mount`；否则标记为 `blocked`。
- mountpoint 目录必须已经存在。该模块不会自动创建目录、卸载挂载或替换 runtime 提供的挂载。
- `ninit_mount_manager_apply()` 只执行 plan 中的 mount 项；每次 mount 前重新读取 mountinfo 和有效 capability，拒绝过期计划，并校验目标、文件系统、source、flags 和 mount options 均属于内置白名单。
- 测试可注入 mount callback；生产调用传 NULL 时使用 Linux `mount(2)`。实际 syscall errno 是最终结果，能力位只是必要条件，不是成功保证。

PID 1 当前只输出 plan，不调用 apply；因此这轮代码不会在启动时改变挂载命名空间。容器/未知环境的策略保持保守，VM/host 只有在 capability 和 mount 状态满足条件时才会规划挂载。下一阶段需进一步验证 mount namespace 身份、挂载传播属性、只读根文件系统及容器运行时约束，再决定是否在 PID1 sysinit 阶段启用显式 apply。


### Mount namespace 安全检查

环境能力快照额外记录 `/proc/self/ns/mnt` 与 `/proc/1/ns/mnt` 的可见性、inode 和设备号比较结果。Mount Manager 的显式 apply 要求两者都可读取且指向同一个 namespace；若无法读取或与 PID 1 不一致，则拒绝执行。

这个比较只证明当前进程与 PID 1 处于同一 mount namespace，**不证明该 namespace 是宿主机初始 namespace**。Linux 没有一个可在所有容器/权限模型下可靠使用的通用“宿主机 mount namespace”标识；因此 Ninit 不会仅凭 inode 常量、namespace 名称或容器类型宣称自己处于 host namespace。环境分类仍由独立的 PID namespace、运行时标记和管理员 override 决定。当前 PID 1 仍然只记录 mount plan，不会自动 apply。
