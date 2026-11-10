# 开发说明

## 当前阶段

当前仓库是 Ninit 的工程起点，不是可用于启动真实系统的 init 实现。

目前提供：

- CMake 构建入口和 C11 编译约束。
- `ninit-common` 公共静态库。
- `ninit-manager` 与 `ninitctl` 的命令行占位程序。
- 日志级别名称的最小公共 API 和单元测试。
- glibc / musl 两种 libc 构建目标的 CI 矩阵。

PID 1、Manager 守护进程、IPC、Unit、Job 和服务监督尚未实现。不要将当前程序配置为系统的 PID 1。

## 构建

glibc：

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=gcc
cmake --build build
ctest --test-dir build --output-on-failure
```

musl（以提供 `musl-gcc` 的发行版为例）：

```sh
cmake -S . -B build-musl -DCMAKE_C_COMPILER=musl-gcc
cmake --build build-musl
ctest --test-dir build-musl --output-on-failure
```

这两种构建使用相同源码和 CMake 配置。生成的二进制和库分别面向对应 libc ABI，不能假定可以跨 libc 直接混用。

## 设计约束

- PID 1 必须保持最小化，并优先保证信号处理、子进程回收和故障安全。
- Manager 的功能通过模块 API 组织；模块分离不意味着每个模块都要单独运行成进程。
- Job Manager 是服务控制操作的统一调度入口。
- Timer、Socket、Path 等触发器向 Job 层提交请求，不直接修改服务生命周期。
- systemd Unit 兼容层与 Ninit 原生 Unit 模型隔离。
- 第三方库经评估后接入；在功能真正需要之前，不提前引入整套 systemd 依赖。
