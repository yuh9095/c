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
- 每个 Unit 有唯一名称和可选 description；service 必须提供 ExecStart argv，可选提供 ExecStop argv；target 不允许 ExecStart/ExecStop。ExecStop 由 Runtime 异步执行，退出后才向主服务发送 SIGTERM。
- ExecStart 与可选 ExecStop 均按 argv 数组深拷贝保存，不经过 shell 字符串解析；ExecStop 已接入 Service Runtime：辅助进程通过 exec-error pipe 完成握手，由统一 Child Reaper 回收；辅助进程退出后终止主服务。
- Unit 可声明 Requires、Wants、After、Before 名称列表。
- 注册时深拷贝定义及依赖名称；调用者可在 add 返回后释放输入缓冲区。
- 重复名称返回 `EEXIST`，非法名称/定义返回 `EINVAL`。
- 查找返回 manager 所有的只读借用指针，生命周期截止于 manager 销毁。

名称目前限制为最多 255 个 ASCII 字母数字字符以及 `_`、`-`、`.`、`@`、`:`。依赖名称在注册时只做语法检查；是否存在、是否形成循环以及实际启动顺序由后续 Dependency Engine 负责。

Unit File Loader 当前支持 [Unit] 的 Description/Requires/Wants/After/Before，以及 [Service] 的 ExecStart/ExecStop；两者使用同一套轻量引号/反斜杠分词，不调用 shell、不做变量展开。支持单个文件加载，也支持目录加载；目录中的 .service/.target 文件按文件名排序后注册，遇到错误会停止，但已注册的前序文件不会回滚。尚未实现 drop-in、环境变量、ExecStartPre/Post 或 systemd/OpenRC 兼容。Unit 状态、完整服务管理仍由后续模块负责。


## Dependency Engine（第一版）

`include/ninit/dependency_plan.h` 根据指定 Target 生成激活计划：

- 从根 Target 迭代计算 Requires/Wants 闭包；Requires 缺失返回 `ENOENT`，Wants 缺失忽略。
- After/Before 只建立排序边，不会单独激活服务；引用的 Unit 不在激活闭包时忽略。
- `After=X` 表示 X 在当前 Unit 之前；`Before=X` 表示当前 Unit 在 X 之前。
- 使用稳定的 Kahn 分层拓扑排序；同一 wave 中的 Unit 在其所有前置排序约束满足后可并行启动。
- 排序环路返回 `ELOOP`。计划只保存 manager 所有的只读 Unit 指针，必须在 manager 销毁前释放计划。

该模块只生成计划，不创建 Job、不 fork/exec，也不跟踪服务状态。当前实现使用邻接矩阵表达排序边，适合当前小规模原生 Unit 集合；后续规模扩大时再评估稀疏图结构。


## Job Manager（Start/Stop Job）

`include/ninit/job_manager.h` 提供内存中的 Start/Stop Job 调度核心：

- 提交时复制 Dependency Plan 中的 Unit 借用指针和 wave 编号；Unit Manager 必须比 Job Manager/Job 活得更久。
- 每个 Job 可设置最大并发数；同一 wave 可并行派发，后续 wave 必须等待当前 wave 全部完成。
- Start Job 按正向依赖波次派发；Stop Job 反转 wave 顺序，并在同一 wave 内按计划注册顺序逆序派发。
- 执行器通过 dispatch 领取 Unit，再通过 complete 报告成功或失败；`ninit_job_manager_is_stop()` 用于区分事务方向。
- 任一 Unit 操作失败会将 Job 标记 FAILED，并跳过尚未派发的 Unit；已派发任务仍可回报完成。
- cancel 会跳过未派发任务；已运行任务仍由执行器完成/收尾。
- Job 状态可查询；Job 记录保留到 Job Manager 销毁。

Job Manager 只负责调度，不直接发送信号或执行进程操作。Restart/Reload、Job 合并和跨 Job 冲突处理尚未实现。



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

运行时对象借用 Job Manager；Job Manager 和 Unit Manager 必须比运行时对象活得更久。Stop Job 会按逆依赖顺序定位已跟踪的 Service，发送 SIGTERM，并将 Stop Job 项关联到进程记录；只有 child-exit 已由统一 reaper 回收、且 exec handshake 已处理后，才完成该停止项。未运行的 Service 和 Target 可立即完成停止项。Manager 退出时优先提交 Stop Job，等待服务退出；5 秒宽限期后仍存活的进程会收到 SIGKILL，随后仍通过 exec handshake 与统一 child reaper 清理。若没有可用的启动 plan，则使用 SIGTERM 广播和相同的宽限期/强制回收路径。该机制尚未提供 Unit 自定义 TimeoutStopSec、进程组/cgroup 管理或 Restart 策略。ExecStop 的退出状态目前记录为辅助进程退出事件，失败不会阻止主服务终止。

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
console setup and mount setup are implemented as early setup stages; a complete
sysinit/boot/default coordinator integration and process-group/cgroup shutdown
remain future work. PID 1 now performs policy-gated reboot/poweroff after the
Manager Stop Job has completed.


## Execution environment detection (initial)

`include/ninit/environment.h` exposes an independent environment detector and a
conservative startup policy. Host-level shutdown and guest-local shutdown are
separate permissions; a container may stop/restart its own PID namespace
without being authorized to reboot the host. The first implementation follows the layered
approach used by systemd's `detect_container()` and OpenRC's
`detect_container()`/`rc_sys` model:

- OpenVZ is checked first using `/proc/vz` and the absence of host-only `/proc/bc`;
- WSL kernel/interop markers identify a VM guest, not a container;
- Proot is detected through `TracerPid` and the tracer process command name;
- `/run/host/container-manager` is preferred because it is readable without
  privileged access;
- for PID 1, the inherited `container` variable is authoritative when set
  (an empty value means no container); non-PID-1 processes consult
  `/run/systemd/container` and then best-effort read PID 1's `container`
  variable from `/proc/1/environ`;
- `/run/.containerenv` and `/.dockerenv` are checked last, in that order, so
  generic image markers do not override a more specific manager hint;
- an `oci` hint is not treated as a runtime: Ninit checks the Podman/Docker
  marker files and otherwise classifies it as `OTHER`;
- cgroup path names are deliberately not used to infer a container family;
  they are not a reliable systemd container identity signal;
- as a final fallback, Ninit uses nsfs `NS_GET_NSTYPE`/`NS_GET_PARENT`
  to recognize a verifiably nested PID namespace. `EPERM`/`ENOTTY` is
  inconclusive and remains UNKNOWN rather than comparing inode constants.

The result records the environment kind, recognized container family and
evidence bits. Failure to inspect procfs or conclusively determine the PID
namespace is represented as UNKNOWN, not HOST. Procfs availability,
a root-looking cgroup path, or sharing a namespace with PID 1 does not prove
that the process is in the host's initial namespaces. `NS_GET_PARENT` can
return `EPERM` both for the initial PID namespace and when its parent is
outside the caller's namespace scope; Ninit treats this ambiguity
conservatively. Automatic detection therefore never promotes an inconclusive
result to HOST. An administrator can explicitly select `Environment=host`
(or `--environment host`) after verifying the boot environment. UNKNOWN and
CONTAINER receive a restrictive policy: no host reboot, no host cgroup
management, and no automatic replacement of proc/sys/dev mounts. Only an
explicit HOST override enables the host policy; even then, its permission
flags are not proof that an operation is permitted, and every mount operation
must still check existing mounts and handle kernel errors.

PID 1 performs detection before console setup and consumes the policy
for early pseudo-filesystem mount setup. `ninit_environment_authorize_host_shutdown()` authorizes host reboot/poweroff
only with an explicitly permissive host policy and observable effective
`CAP_SYS_BOOT`. Container and VM policies instead enable
`ninit_environment_authorize_guest_shutdown()`, which requires the same
capability but never authorizes a host-level operation; UNKNOWN denies both.
PID 1 handles SIGINT as an orderly restart request and SIGTERM/SIGPWR as an
orderly poweroff request. It first asks Manager to execute the dependency-aware
Stop Job (including ExecStop), and waits for Manager to finish. On a HOST
system, it re-probes current capabilities, requires the explicit host policy
and effective `CAP_SYS_BOOT`, syncs storage, and calls Linux `reboot(2)` with
`RB_AUTOBOOT` or `RB_POWER_OFF`. A VM guest uses the guest shutdown gate and
its own guest kernel reboot operation. A CONTAINER follows the systemd
container fallback model instead: after the same orderly Stop Job and `sync()`,
PID 1 exits successfully and lets the container runtime own the stop/restart
operation. It does not call `reboot(2)` from a container, because containers
may lack a private PID namespace or `CAP_SYS_BOOT`; a privileged container
must not be able to turn an init request into an accidental host reboot.
UNKNOWN environments authorize neither host nor guest shutdown. The startup
capability snapshot is diagnostic only; host/VM shutdown always performs a
fresh probe immediately before the syscall. Do not treat
environment detection as a complete security boundary or infer capabilities
from a container name.


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


### PID 1 启动时的伪文件系统挂载

PID 1 完成环境检测和只读 capability probe 后，使用同一份环境策略生成 /proc、/sys、/dev 挂载计划，并只对计划中标记为 APPLY 的缺失目标调用 Mount Manager。HOST 和完整 VM guest 可在策略允许且 capability、mount namespace、根挂载传播/只读状态均满足时补齐挂载；CONTAINER、UNKNOWN 不会接管运行时提供的挂载。已有正确挂载保持不变，错误 filesystem、符号链接、非目录目标均不覆盖。执行失败只记录错误，不使用传统 pathname mount(2) 回退，也不擅自改变传播属性。

### Mount Manager（只读规划与显式执行第一版）

`include/ninit/mount_manager.h` 提供三个早期伪文件系统目标的 mount plan：

- `/proc` → `proc`，`/sys` → `sysfs`，`/dev` → `devtmpfs`。
- 已存在且类型正确的挂载标记为 `skip`；目标已有其他文件系统时标记为 `conflict`，绝不覆盖。
- 目标缺失时，必须同时满足环境策略允许、mountinfo 可读、CapEff 可读且当前有效集包含 `CAP_SYS_ADMIN`，才会标记为 `mount`；否则标记为 `blocked`。
- mountpoint 目录必须已经存在。该模块不会自动创建目录、卸载挂载或替换 runtime 提供的挂载。
- `ninit_mount_manager_apply()` 必须接收调用方基于当前环境检测生成的非空 environment policy；即使旧 plan 中存在 `APPLY` 项，只要当前 policy 不再允许对应目标，apply 就以 `EPERM` 拒绝，避免容器/未知环境沿用此前主机环境生成的计划。它只执行完整且结构有效的 plan 中的 mount 项；每次 mount 前重新读取 mountinfo、有效 capability、根挂载只读状态及传播属性，打开并固定目标目录 FD 后再次读取 mountinfo，拒绝已出现的挂载；若计划生成后根挂载变为只读或变为 shared，也会拒绝执行，并校验目标、文件系统、source、flags 和 mount options 均属于内置白名单。Linux 没有通用的原子 compare-and-mount 接口，因此第二次 mountinfo 检查仍不能阻止另一个有权限的进程在最后检查与 `move_mount()` 之间并发挂载；fd-bound API 解决的是目标路径重新解析问题，不代表 mount namespace 中不存在并发竞争。
- 测试可注入 mount callback；生产调用传 NULL 时使用 `fsopen()`、`fsconfig()`、`fsmount()`、`mount_setattr()` 和 `move_mount()`：文件系统先创建为 detached mount，再通过已验证的目标目录 FD 安装，不再将目标路径字符串交给 `mount(2)` 重新解析。`/dev` 的固定 `mode=0755` 通过 `fsconfig()` 设置，VFS `nosuid`、`nodev`、`noexec` 属性通过 `mount_setattr()` 设置。若构建时缺少所需 syscall 编号，或运行内核不支持这些接口，apply 会失败（通常为 `EOPNOTSUPP`/内核返回的 errno），不会回退到路径字符串挂载。实际 syscall errno 是最终结果，能力位只是必要条件，不是成功保证。

对已经存在且 filesystem 类型正确的挂载，plan 还会报告 mountinfo 第六字段中的有效 VFS 安全选项：`nosuid`、`nodev`、`noexec`。这些值通过 `existing_mount_options` 位标志提供给调用者。`/proc` 和 `/sys` 推荐同时具备三项，`/dev` 至少推荐 `nosuid`；由于运行时可能有意提供不同选项，Ninit 只报告差异，不自动 remount，也不把已有挂载替换掉。新建挂载仍通过固定的 mount flags 设置这些约束。

PID 1 在早期启动阶段调用 `ninit_mount_manager_apply_report()`，逐目标执行计划中标记为 APPLY 的挂载并记录独立结果。一个目标失败不会阻止后续目标尝试；已成功挂载不会回滚。旧的 `ninit_mount_manager_apply()` 接口仍保留 fail-fast 语义。容器/未知环境策略保持保守，所有实际操作仍受当前环境策略、capability、mount namespace、传播属性和根挂载只读状态约束。 每轮执行后 PID 1 会重新探测 capability 并重新读取 `/proc/self/mountinfo`，输出内核确认的最终状态。挂载状态不写入独立状态文件：mountinfo 是权威来源，下一次 PID 1 启动会重新规划，已存在且文件系统类型正确的目标自然变为 `skip`，部分成功也能被恢复识别。


### Mount namespace 安全检查

环境能力快照额外记录 `/proc/self/ns/mnt` 与 `/proc/1/ns/mnt` 的可见性、inode 和设备号比较结果。Mount Manager 的显式 apply 要求两者都可读取且指向同一个 namespace；若无法读取或与 PID 1 不一致，则拒绝执行。

这个比较只证明当前进程与 PID 1 处于同一 mount namespace，**不证明该 namespace 是宿主机初始 namespace**。Linux 没有一个可在所有容器/权限模型下可靠使用的通用“宿主机 mount namespace”标识；因此 Ninit 不会仅凭 inode 常量、namespace 名称或容器类型宣称自己处于 host namespace。环境分类仍由独立的 PID namespace、运行时标记和管理员 override 决定。当前 PID 1 仍然只记录 mount plan，不会自动 apply。


Mount Manager 的路径构造器要求 root 和目标均为绝对路径，并拒绝 `.`、`..` 路径组件；root 末尾的多余斜杠会被规范化。路径解析从 `/` 开始逐级使用 `openat(O_PATH|O_NOFOLLOW)` followed by `fstat()`，每一级都相对于已打开的父目录解析并必须是实际目录。规划阶段会将符号链接和非目录组件标记为 conflict；apply 阶段则直接失败并保留 `ELOOP`/`ENOTDIR`。生产挂载会保留最终目标目录 FD，并通过 `move_mount(..., MOVE_MOUNT_F_EMPTY_PATH)` 安装 detached mount，避免在检查后重新解析目标路径字符串。

能力快照还从根挂载记录的 mount options 字段读取 `ro`/`rw`，报告 `root_mount_read_only`。若根挂载为只读，规划阶段会将缺失的伪文件系统目标标记为 `blocked`；apply 阶段也会重新探测并拒绝过期计划。已经存在且类型正确的挂载仍标记为 `skip`，不会因为根挂载只读而尝试覆盖或重挂载。

Mount Manager 还会从 mountinfo 检查根挂载点 `/` 的传播属性。若找不到根挂载记录，或根挂载带有 `shared:` propagation tag，则缺失的伪文件系统挂载目标会被标记为 `blocked`；apply 阶段也会重新检查。原因是直接在 shared mount tree 下创建挂载可能向 peer namespace 传播。Ninit 当前不会擅自把整个根挂载树改成 private，也不会在未审查传播语义前自动改变系统挂载拓扑。
