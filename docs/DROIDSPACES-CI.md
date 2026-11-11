# Droidspaces Linux 容器集成 CI

本项目使用普通 GitHub-hosted runner 运行 Droidspaces Linux 后端，不需要 self-hosted Runner，也不需要 Android 手机参与 CI。

工作流：`.github/workflows/droidspaces-container.yml`

## 运行方式

每次向 `bootstrap/project-skeleton` 推送提交，或手动触发 `workflow_dispatch` 时，GitHub Actions 会分配一台全新的 `ubuntu-24.04` x86_64 Linux VM，并在 VM 内执行：

1. 安装 Droidspaces Linux x86_64 后端（固定使用 v7.2.0，并校验发布包 SHA256）。
2. 使用 debootstrap 创建干净的 Ubuntu 24.04 rootfs。
3. 编译 Ninit，并将 `ninit-init`、`ninit-manager`、`ninitctl` 安装进 rootfs。
4. 通过 Droidspaces 的 `--init=/usr/bin/ninit-init` 显式指定自定义 init，不能只依赖 `/sbin/init` 软链接。
5. 使用 `--allow-sandboxing` 开启容器内嵌套 user namespace 能力，供 PID/mount namespace 集成测试使用。
6. 将工作区 bind-mount 到容器的 `$GITHUB_WORKSPACE` (宿主与容器内保持相同绝对路径)，在容器内安装 CTest 和 util-linux。
7. 验证 `/proc/1/exe` 确实指向 `/usr/bin/ninit-init`，随后在 Droidspaces 容器内部运行整个 CTest 套件。
8. 测试结束后停止容器；Runner 是临时 VM，不保留测试状态。

这不是用普通 `unshare` 或 Docker 替代 Droidspaces：工作流会下载并执行 Droidspaces 自己的 Linux 后端，调用它创建、启动和停止容器。

## 测试边界

- CI 主机：GitHub-hosted `ubuntu-24.04` x86_64。
- Droidspaces 后端：Linux x86_64，v7.2.0。
- Guest rootfs：Ubuntu Noble amd64。
- Guest PID 1：本项目的 `ninit-init`（通过 Droidspaces `--init` 显式指定，并检查 `/proc/1/exe`）。
- Guest 中的 Manager：本项目的 `ninit-manager`。
- CTest 在 Guest 容器内部执行；PID namespace 集成测试还会尝试创建嵌套 mount/PID namespace。
- 设置 `NINIT_REQUIRE_PID_NAMESPACE_TEST=1`，因此 namespace 集成测试不能以 SKIPPED 结束；能力不足时 job 必须失败。

## 安全与权限

Droidspaces 是特权容器运行时，需要在 Linux 主机上以 root 启动。工作流因此只在隔离、一次性的 GitHub-hosted VM 上执行，不使用持久 self-hosted runner，也不向工作流提供额外 secrets。

`--allow-sandboxing` 会放宽容器内创建 user namespace 的限制，仅用于本 CI 的可信测试代码。不要将此配置视为运行不可信容器的安全边界。

当前只在 `bootstrap/project-skeleton` 分支 push 和手动触发时运行，不对 fork 的 `pull_request` 执行特权容器代码。

## 与普通 CI 的关系

- `.github/workflows/ci.yml`：普通 GitHub-hosted runner 上的 glibc/musl 构建与单元测试。
- `.github/workflows/droidspaces-container.yml`：在 Droidspaces Linux 容器内启动 Ninit 为 PID 1，并运行完整测试套件。

两套 CI 分开报告。普通 CI 通过不代表 Droidspaces 容器集成测试已经通过。
