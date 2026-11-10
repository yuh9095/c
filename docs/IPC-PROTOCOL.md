# Ninit IPC 协议（版本 1）

本文定义 PID 1 与 Ninit Manager 之间内部 IPC 的消息帧格式。它只定义单条消息的编码，不规定 Unix 域套接字的创建、进程生命周期或业务状态机。

## 传输约束

第一版使用本地 Unix 域 `SOCK_SEQPACKET`，以保留消息边界。`ninit_ipc_socketpair()` 创建两个带 `CLOEXEC`、`NONBLOCK` 的私有端点；`ninit_ipc_send()` 发送一个完整帧，`ninit_ipc_receive()` 接收并验证一个完整帧。不能将此格式当作字节流并假定一次读取一定返回完整数据；传输层仍须处理截断、系统调用错误和对端关闭。

PID 1 与 Manager 之间的通道应由 PID 1 创建并通过受控的文件描述符传递给 Manager，而不是开放一个可被普通用户连接的公共控制套接字。普通 `ninitctl` 客户端通道属于另一套权限边界，不应复用为 PID1—Manager 私有通道。

## 帧格式

所有多字节整数均为大端序（网络字节序），不直接发送 C 结构体内存布局。

| 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `NIPC`，数值 `0x4e495043` |
| 4 | 2 | Version | 当前为 1 |
| 6 | 2 | Type | 消息类型 |
| 8 | 4 | Flags | 当前必须为 0，非零拒绝 |
| 12 | 4 | Payload size | 后续 Payload 字节数 |
| 16 | 8 | Request ID | 请求关联标识 |
| 24 | N | Payload | 最多 1 MiB |

头部长度固定为 24 字节。完整帧长度必须严格等于 `24 + payload_size`。拒绝错误 Magic、未知版本、未知类型、非零保留标志、超长 Payload、截断帧和含尾随字节的帧。

## 消息类型

| 值 | 名称 | 方向/用途（初步） |
|---:|---|---|
| 1 | HELLO | Manager 向 PID 1 报告协议握手 |
| 2 | HELLO_REPLY | PID 1 对握手作出响应 |
| 3 | PING | 存活探测请求 |
| 4 | PONG | 存活探测响应 |
| 5 | STATUS | 状态同步/查询请求 |
| 6 | STATUS_REPLY | 状态同步/查询响应 |
| 7 | SUBMIT_JOB | 提交控制任务 |
| 8 | JOB_REPLY | 任务受理或结果响应 |
| 9 | ERROR | 协议或请求错误 |

消息类型仅定义初步编号。各消息 Payload 的字段布局、幂等性、超时、重试、状态恢复语义必须在实现对应业务前另行规范；不能把“收到 JOB_REPLY”误解为服务已经达到 Ready 状态。

## Request ID

Request ID 用于关联请求和响应。零值可保留给非请求型事件；具体生成、重连后的作用域以及重复请求处理规则由传输/会话层规范。不能仅凭 Request ID 推断请求具备幂等性。

## 安全与故障边界

- 私有通道由 PID 1 创建，避免普通客户端访问。
- Manager 重启后应建立新会话；旧会话中的未完成请求不能自动假定成功。
- PID 1 必须独立于 Manager 保留信号处理、子进程回收最终保障和紧急输出能力。
- 协议解析失败应拒绝该帧并记录诊断；不能因 Manager 发来的非法消息而执行任意操作。
- 所有服务控制请求最终交由 Manager Job 层处理。Socket、Timer、Path 等触发器不得绕过 Job 层直接改变服务状态。

## 当前实现范围

`include/ninit/ipc_protocol.h` 与 `src/common/ipc_protocol.c` 实现版本 1 的帧编码/解码和基本校验；`include/ninit/ipc_channel.h` 与 `src/common/ipc_channel.c` 实现本地 SOCK_SEQPACKET socketpair 通道及单帧发送/接收。当前仍没有握手状态机、对端身份策略、重连、Manager 监督或 PID 1 实现。

## HELLO 握手 Payload（版本 1）

Manager 首先发送 HELLO，Request ID 必须为 0，Payload 固定 8 字节：

| 偏移 | 长度 | 字段 | 当前值 |
|---:|---:|---|---|
| 0 | 2 | 最低协议版本 | 1 |
| 2 | 2 | 最高协议版本 | 1 |
| 4 | 2 | 角色 | 1（Manager） |
| 6 | 2 | 保留 | 0 |

PID 1 当前只接受最低/最高版本均为 1 的 Manager 握手，并返回 HELLO_REPLY。响应 Payload 固定 4 字节：选定版本（2 字节，值 1）和保留字段（2 字节，值 0）。协议版本也必须与帧头中的 Version 一致。

握手超时由双方分别执行，当前默认 3000 毫秒。此握手确认私有通道两端使用兼容协议并识别 Manager 角色；由于通道由 PID 1 创建并通过 posix_spawn 文件动作交接，它不是通用 socket 服务，也不提供针对恶意同 UID 进程的密码学认证。

## Manager 进程交接

PID 1 侧的 ninit_manager_start() 创建 socketpair，将子进程端点交接到 fd 3，再执行 ninit-manager --ipc-fd 3。父进程保留另一端，只有握手成功后才把 Manager PID 和 IPC fd 返回调用者。握手失败时启动器终止并回收子进程。ninit_manager_disconnect() 关闭父端通道；Manager 收到关闭事件后退出。PID 1 必须继续负责 SIGCHLD 处理和子进程回收，不能把它交给 Manager。
