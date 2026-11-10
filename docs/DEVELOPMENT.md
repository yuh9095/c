# 开发说明

## 当前阶段

当前仓库是 Ninit 的工程起点，不是可用于启动真实系统的 init 实现。

目前提供 CMake/C11 构建、公共日志 API、PID1—Manager IPC 消息帧编解码、Unix SOCK_SEQPACKET 通道封装及单元测试，以及 glibc/musl CI。IPC 通道目前只提供底层通信能力，不启动 Manager，也不执行控制命令。PID 1、Manager 守护进程、Unit、Job 和服务监督尚未实现。不要将当前程序配置为系统 PID 1。

详细帧格式和消息编号见 [IPC 协议规范](IPC-PROTOCOL.md)。

## IPC 协议与传输层

`include/ninit/ipc_channel.h` 提供 `socketpair()`、单帧发送和单帧接收接口。通道使用本地 `AF_UNIX/SOCK_SEQPACKET`，两个端点均设置 `CLOEXEC`。接收端检查完整数据包长度，超出接收缓冲区的消息会被丢弃并报告缓冲区不足。该 API 要求传入的 fd 确实是 SOCK_SEQPACKET 类型；PID 1 仍需负责创建通道、fork/exec 时的 fd 交接及关闭不需要的端点。

## IPC 协议第一版

协议定义位于 `include/ninit/ipc_protocol.h`，实现位于 `src/common/ipc_protocol.c`。

- 固定 24 字节头部，多字节整数使用大端序。
- Magic 为 `NIPC`，协议版本为 1。
- 头部包含消息类型、保留标志、Payload 长度和 Request ID。
- 单条消息 Payload 上限为 1 MiB。
- 当前版本要求 flags 为 0；未知消息类型和协议版本会被拒绝。
- 数据包长度必须与头部声明完全一致，拒绝截断和尾随数据。
- 解码后的 Payload 指针指向输入缓冲区，不转移所有权。

协议帧层不定义服务状态机。Socket、对端身份校验、权限、请求超时和断线恢复仍需单独实现。所有控制请求都必须进入 Manager 的 Job 层，不能由 Socket/Timer/Path 触发器直接启停服务。

## 构建

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=gcc
cmake --build build
ctest --test-dir build --output-on-failure
```

musl 使用同一份源码和配置，将编译器替换为发行版提供的 `musl-gcc`。

## 设计约束

- PID 1 优先保证信号处理、子进程回收和故障安全。
- Manager 内模块通过清晰 API 协作，不要求每个模块独立成进程。
- Job Manager 是服务控制操作的统一调度入口。
- systemd Unit 兼容层与 Ninit 原生 Unit 模型隔离。
- 第三方库经评估后接入，不提前引入整套 systemd 依赖。
