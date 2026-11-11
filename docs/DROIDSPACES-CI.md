# Droidspaces 容器集成 CI

本仓库有两套互相独立的 CI：

- `.github/workflows/ci.yml`：GitHub-hosted runner 上的 glibc/musl 构建和普通单元测试。
- `.github/workflows/droidspaces-container.yml`（工作流名称：`Droidspaces Linux integration`）：在真实 Droidspaces Linux 容器内运行的 ARM64 集成测试，必须成功创建嵌套 mount/PID namespace；不能像普通 CI 一样把该测试标记为 skipped。

## 运行模型

Droidspaces CI 使用注册在本仓库的 self-hosted GitHub Actions runner。Runner 必须安装并运行在 Droidspaces 管理的 Linux 容器内部，而不是 Android 的 Termux/PRoot shell，也不是仅仅在 GitHub runner 上执行一个模拟脚本。

当前开发测试环境是 Droidspaces v7.1.0 的 Artix Linux aarch64 容器 `armtix2`，容器内由 OpenRC 作为 PID 1。可以先在这个容器里试运行 runner；GitHub Actions 支持 Linux ARM64 runner，但官方列出的 Linux 发行版没有明确列出 Artix，因此 Artix 上 runner 的兼容性需要实际验证。如果 runner 自身不能正常启动，可在 Droidspaces 中使用 Debian/Ubuntu ARM64 rootfs 作为 runner 容器。

## 一次性配置 Runner

1. 在 Droidspaces 中启动 Linux 容器，并以普通用户进入容器。不要把 GitHub runner 注册到 Android 宿主机。
2. 安装构建和运行依赖。Artix 示例：

   ```sh
   sudo pacman -S --needed base-devel cmake ninja git curl tar util-linux icu libunwind openssl zlib krb5
   ```

3. 打开 GitHub 仓库的 **Settings → Actions → Runners → New self-hosted runner**，选择 Linux / ARM64，按 GitHub 页面给出的命令下载并解压 runner。
4. 在 runner 配置步骤中添加自定义标签 `droidspaces`。默认的 `self-hosted`、`linux`、`ARM64` 标签也要保留。注册命令中的临时 token 只在终端使用，不要提交到仓库或粘贴到日志。
5. 以普通用户运行 runner：

   ```sh
   ./run.sh
   ```

   首轮先在 Droidspaces 容器的持久终端会话（例如 tmux）中前台运行，确认 GitHub 页面显示 runner 为 Idle。不要先把 runner 配成 root 服务。

## 先验证嵌套 namespace

在 runner 用户身份下执行：

```sh
unshare --mount --pid --fork --mount-proc true
```

如果容器不允许直接创建 namespace，再验证 rootless 路径：

```sh
unshare --user --map-root-user --mount --pid --fork --mount-proc true
```

至少有一种必须成功。CI 会再次通过实际集成测试验证，而不是只依赖这个探测命令。若两种都失败，先检查 Droidspaces 容器的 nested-container / user-namespace 权限，不要通过关闭测试或把 skip 当成通过来绕过。

## 工作流行为

- 触发方式：仅 `bootstrap/project-skeleton` 分支的 push，或手动 `workflow_dispatch`。如需测试其他可信分支，应显式修改工作流的 `push.branches`，不要开放给 fork PR。
- 不监听 `pull_request`：self-hosted runner 位于有权限的 Droidspaces 容器内，不能让来自 fork 的不可信代码自动获得执行机会。
- Runner 标签：`[self-hosted, linux, ARM64, droidspaces]`。
- 工作流首先检查 ARM64、Linux 基础信息和必需工具，再实际探测嵌套 PID/mount namespace（先直接模式，失败后尝试 rootless user namespace）；两种方式都失败则立即失败。随后编译 Ninit，运行全部 CTest，并设置 `NINIT_REQUIRE_PID_NAMESPACE_TEST=1`。如果 PID/mount namespace 测试无法启动，整个 job 失败，不会报告绿色的 skipped。
- 真实集成测试会让 Ninit 在嵌套 PID namespace 中成为 PID 1，触发 Manager 崩溃，验证 PID 1 清理遗留服务后代并重启 Manager，最后通过容器式 SIGTERM 路径退出。它不会调用宿主机 reboot。

## 安全边界

Self-hosted runner 会执行仓库中的任意构建脚本。只允许可信的仓库写入者向触发该工作流的分支推送代码；不要为 fork PR 启用此 runner。Runner 使用普通用户运行，只有在 Droidspaces 容器允许 rootless nested namespace 时才可执行集成测试。不要把 runner 注册 token、PAT 或其他凭据写入仓库。
