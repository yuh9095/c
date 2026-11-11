# Droidspaces Linux 容器集成 CI

本项目使用普通 GitHub-hosted runner 运行 Droidspaces Linux 后端，不需要 self-hosted Runner，也不需要 Android 手机参与 CI。

工作流：`.github/workflows/droidspaces-container.yml`

## 运行方式

每次向 `bootstrap/project-skeleton` 推送提交，或手动触发 `workflow_dispatch` 时，GitHub Actions 会分配一台全新的 `ubuntu-24.04` x86_64 Linux VM，并执行：

1. 安装 Droidspaces Linux x86_64 后端（固定使用 v7.2.0，并校验发布包 SHA256）。
2. 从 Linux Containers 官方镜像服务器下载预构建的 Ubuntu Noble amd64 rootfs，选择最新发布的镜像并保留 root 所有权、权限和特殊 mode bits；不再运行 debootstrap。
3. 通过 Droidspaces 启动这个现成 rootfs，先使用发行版自带的 init 进入容器。
4. 将 GitHub 工作区 bind-mount 到容器内相同的绝对路径，在容器内安装 GCC、CMake、Ninja 和 util-linux。
5. 在容器内部编译 Ninit，并将 `ninit-init`、`ninit-manager`、`ninitctl` 安装到容器的 `/usr/bin`。
6. 停止发行版 init，再启动同一个持久 rootfs，通过 Droidspaces `--init=/usr/bin/ninit-init` 将刚编译的 Ninit 作为真正的 PID 1。
7. 显式把工作区路径传给容器内命令（宿主机的 `GITHUB_WORKSPACE` 不会自动继承到 guest），验证 `/proc/1/exe`、工作区挂载和 Manager 可执行文件。
8. 在 guest 内运行完整 CTest。设置 `NINIT_REQUIRE_PID_NAMESPACE_TEST=1`，namespace 集成测试不能以 SKIPPED 结束。
9. 测试结束后停止容器；Runner 是临时 VM，不保留测试状态。

这不是用普通 `unshare` 或 Docker 替代 Droidspaces：Docker 只用于本工作流之外的 rootfs 构建已不再需要；工作流下载并执行 Droidspaces 自己的 Linux 后端，由它创建、启动、停止容器。rootfs 来自预构建的 Linux Containers 镜像。

## 两阶段启动的原因

Ninit 必须先在容器内编译出来，才能被指定为 PID 1。因此同一个 rootfs 分两阶段启动：

- **构建阶段：** 使用 rootfs 自带的发行版 init 启动容器，安装工具链并在 guest 中编译、安装 Ninit。
- **验证阶段：** 停止容器，再用 `--init=/usr/bin/ninit-init` 启动同一个 rootfs，验证 Ninit 的 PID 1 行为。

这里不使用 `--volatile`，因为构建阶段安装的工具和 Ninit 二进制必须在停止后保留，供第二阶段启动使用。

## 测试边界

- CI 主机：GitHub-hosted `ubuntu-24.04` x86_64。
- Droidspaces 后端：Linux x86_64，v7.2.0。
- Guest rootfs：Linux Containers 预构建 Ubuntu Noble amd64。
- Guest 编译器：容器内部安装的 GCC。
- Guest PID 1：本项目的 `ninit-init`（通过 Droidspaces `--init` 显式指定，并检查 `/proc/1/exe`）。
- Guest 中的 Manager：本项目的 `ninit-manager`。
- CTest 在 Guest 容器内部执行；PID namespace 集成测试还会尝试创建嵌套 mount/PID namespace。
- `--allow-sandboxing` 为嵌套 namespace 测试开放所需能力；能力仍不足时 job 必须失败。

## 安全与权限

Droidspaces 是特权容器运行时，需要在 Linux 主机上以 root 启动。工作流因此只在隔离、一次性的 GitHub-hosted VM 上执行，不使用持久 self-hosted runner，也不向工作流提供额外 secrets。

`--allow-sandboxing` 会放宽容器内创建 user namespace 的限制，仅用于本 CI 的可信测试代码。不要将此配置视为运行不可信容器的安全边界。

当前只在 `bootstrap/project-skeleton` 分支 push 和手动触发时运行，不对 fork 的 `pull_request` 执行特权容器代码。

## 与普通 CI 的关系

- `.github/workflows/ci.yml`：普通 GitHub-hosted runner 上的 glibc/musl 构建与单元测试。
- `.github/workflows/droidspaces-container.yml`：在 Droidspaces Linux 容器内编译 Ninit，将其作为 PID 1 启动，并运行完整测试套件。

两套 CI 分开报告。普通 CI 通过不代表 Droidspaces 容器集成测试已经通过。
