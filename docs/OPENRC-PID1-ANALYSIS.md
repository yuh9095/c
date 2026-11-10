# OpenRC PID 1 实现研究

本文以随项目提供的 OpenRC 0.64.1 源码为依据，主要分析
`src/openrc-init/openrc-init.c`，作为 Ninit PID 1 Runtime 的设计输入。

## OpenRC 的实际结构

OpenRC 的 PID 1 并不是完整的服务管理器本体。它负责维持 PID 1
进程、执行启动阶段、接收控制请求、处理信号、回收子进程和执行最终
关机操作；具体 Runlevel 工作由它启动独立的 `openrc` 程序完成。

### 身份检查

`main()` 首先检查 `getpid() != 1`，不是当前 PID namespace 的
PID 1 就拒绝以 init 模式运行。

这只能证明进程是当前 PID namespace 的 init，不能证明它是宿主机
PID namespace 的 init。容器内部的 PID 1 同样满足该检查。

### 启动阶段

`init()` 按顺序执行：

1. `sysinit`
2. `boot`
3. 命令行指定或配置指定的默认 Runlevel（默认 `default`）

每个阶段由 `do_openrc()` 启动独立的 `openrc <runlevel>`，并等待
对应子进程结束。OpenRC PID 1 本身不直接解析全部服务依赖。

### 信号和事件循环

OpenRC 创建 NONBLOCK、CLOEXEC 的 self-pipe。SIGINT、SIGTERM 和
SIGPWR 的处理函数只向管道写入信号编号；主循环通过 `poll()` 读取
管道并执行关机逻辑。

SIGCHLD 则由独立处理函数调用非阻塞 `waitpid(-1, ...)` 循环回收
可回收的子进程。FIFO 控制命令与信号管道一起由主循环监视。

### 关机

`handle_shutdown()` 先执行指定的 shutdown/reboot Runlevel，再发送
SIGTERM，给予进程退出宽限期，之后发送 SIGKILL、执行 `sync()`，
最后调用 Linux `reboot()` 系统调用。该实现展示了 OpenRC 的最终
电源操作由 PID 1 执行，而不是由 Runlevel 管理子进程执行。

Ninit 不能直接照搬 `kill(-1, ...)` 或固定等待时间：Ninit 必须明确
自身管理的进程范围，并等待 Manager 的停止事务和服务依赖逆序停止完成。
容器环境下还必须确保最终动作只作用于当前 PID namespace。

## OpenRC 与 Ninit 的架构映射

| OpenRC | Ninit 设计 |
|---|---|
| `getpid() == 1` | PID 1 启动身份门禁 |
| `do_openrc()` | Boot/Shutdown Coordinator 与 Manager Job |
| `sysinit → boot → default` | 启动阶段/Target 编排 |
| self-pipe + `poll()` | PID 1 信号事件通道和事件循环 |
| SIGCHLD + `waitpid(-1, WNOHANG)` | 唯一的全局子进程回收入口 |
| init FIFO | 独立、受权限保护的 ninitctl 控制通道 |
| `handle_shutdown()` | Shutdown Coordinator + PID 1 最终电源动作 |
| `handle_reexec()` | 经验证的 PID 1 re-exec 流程（后续设计） |

## Ninit 当前实现

`include/ninit/pid1_runtime.h` 与 `src/init/pid1_runtime.c` 先提供两项
可独立测试的基础能力：

- 检查当前进程是否为当前 PID namespace 的 PID 1。
- 通过唯一的非阻塞 `waitpid(-1, ...)` 路径回收所有已退出子进程，
  并将 PID 和 wait status 回调给调用方。

当前已增加 OpenRC 风格的 self-pipe 信号事件源：处理函数只写入信号编号，
由普通上下文 drain 回调；事件源负责 SIGCHLD、SIGTERM、SIGINT 和可用时
SIGPWR。它仍不是完整 PID 1，也没有主事件循环、启动流程或电源操作。未来 PID 1 必须由单一事件循环调用该回收函数，并根据 PID 将
Manager 退出事件交给 Manager Supervisor；其他子进程事件也必须由
调用方明确处理，不能静默丢弃。

## 安全约束

- `getpid() == 1` 不等于宿主机 PID 1。
- 容器关机/重启必须限定在当前容器 PID namespace。
- chroot 本身不是 PID namespace，也不能据此推断独立电源控制边界。
- 不能让 Manager 和 PID 1 两处同时调用 `waitpid(-1)`。
- 不能在信号处理函数中执行复杂服务管理、分配内存或进行阻塞 IPC。
- 真实的 reboot/poweroff 逻辑必须等完整关机协调与环境策略实现后再接入。
