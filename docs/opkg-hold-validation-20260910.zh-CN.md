# opkg hold 修复验证记录（2026-09-10）

## 结论与边界

预装包状态应为 `Status: install hold installed`。生成器和最终产物校验器已同步修正，
新增回归能够拒绝旧格式；Ubuntu 24.04 与板上 opkg 0.7.0 的隔离测试通过。

以下是提交前的验证记录。验证阶段没有触发 GitHub Actions、生成新镜像、发布 Release 或修改线上软件源。
提交后的构建进度以 [PR #1 的检查结果](https://github.com/vicliu624/t-display-k230-vision-platform/pull/1/checks) 为准。
板上测试全部使用 `/tmp` 下独立的包数据库、配置和文件副本，真实系统仍保留原来的包数据库。
因此，当前板上真实包管理状态尚未修复，不能继续使用旧软件源安装 NetSurf。

## 检查对象

| 项目 | 值 |
| --- | --- |
| 代码分支 | `codex/cpu1-rtsmart-integration` |
| 修复前 HEAD | `d8617b272b8a4fdacc2d5dfb9dfb53f3df27c552` |
| 已烧录候选镜像标识 | `0e68645191925abe5acd38cd21c0c2c4789d922c` |
| 对应 CI | [run 34437564995](https://github.com/vicliu624/t-display-k230-vision-platform/actions/runs/34437564995) |
| 候选 artifact ID | `10139209497` |
| 板上 opkg | `0.7.0` |
| 本机测试环境 | Ubuntu 24.04 容器，root 和 UID 1000，测试时无网络 |

候选镜像的包元数据已经与 CI 导出记录核对，共 159 条预装包记录。
本轮修改仅涉及状态生成/校验、测试、CI 检查顺序和文档；CPU1、VGLite、内核及启动配置未改动。

## 根因与修复

opkg 的 `Status` 三列依次是安装意图、标志、安装状态。
旧生成器把 `hold` 写在第一列，旧校验器也按相同错误字符串比对，内部一致性检查因此通过。
真实 opkg 读取时打印 `Internal error`，并显示 `Status: unknown ok installed`。
部分命令仍返回 0，仅检查退出码无法发现这个问题。

修复后的测试独立检查列顺序，并调用真实 opkg 检查解析后的状态、版本和 `Essential`。
测试对 `Internal error`、`Status: unknown` 直接判失败。
CI 的原生 opkg 测试已移到完整镜像构建之前，先运行 `opkg-extract` 准备源码，
再从源码编译主机原生 opkg，检查通过后才进入完整镜像构建。
原有的最终 ext4 文件/权限/数据库校验仍保留。

## 已完成的测试

| 检查 | 结果与范围 |
| --- | --- |
| 旧实现反向验证 | 两项新增测试在旧生成器上均失败，捕获错误列顺序和原生解析错误 |
| CI 原生测试脚本 | 从锁定的 opkg 0.7.0 源码重新编译，root、UID 1000 各通过 20 项，无跳过 |
| 全新解包目录 | 使用现有锁定 Buildroot 源码、配置副本和缓存下载包，在空临时输出中执行 `opkg-extract`，随后原生入口 20 项通过；未构建目标 opkg 或镜像 |
| SDK 回归 | 18 项通过，覆盖配对清单、权限、重定位及发布入口约束 |
| CI YAML | actionlint 1.7.12 通过；此轮该调用未启用其 ShellCheck 集成 |
| 板上最小生产 seed | 3 条包记录，真实 opkg 正确解析 hold/Essential |
| 命名升级与全局升级 | 存在真实的较新本地候选包时，held 包被跳过，原库和版本保持不变 |
| 显式安装与删除 | 较新同名 IPK 被 hold 拦截；同版本 IPK 被拒绝；Essential 包删除被拒绝 |
| 升级正向对照 | 仅在临时目录解除 hold，同一个较新候选包实际升级成功，版本及内容发生预期变化 |
| 板上 159 条元数据副本 | 所有记录正确解析；精确依赖预装基线的测试应用成功安装并删除 |
| merged-/usr 文件冲突 | 经 `/usr/lib`、`/lib` 两种路径安装不同名冲突包均失败，副本中的 libmount 字节保持不变 |
| 实际系统检查 | 真实 status/libmount 哈希未变，未重启；无 failed systemd unit；CPU1 idle/error=0；renderer profile 为 VGLite |
| Wayland/VGLite 绘制 | 解锁后，普通窗口/layer-shell × XR24/AR24 × 全面刷新/16 像素损伤，共 8 组，每组 640×360、60 帧，全部通过 |

本机 20 项测试还包含生产 post-fakeroot hook、内容及数据库篡改拒绝、ext4 特殊权限检查。
板上使用惯常的 `opkg -f <临时配置> -o <临时根目录>`，测试 IPK 无维护脚本，源仅为本地 `file://`。
159 条记录测试使用设备元数据副本，仅在副本中修正状态，配合复制的真实 libmount 验证文件归属。
3 条记录测试使用当前生产 seed 生成的合成最小 rootfs；两个样本的用途分别记录，不冒充完整新镜像。

Wayland 检查共完成 480 次提交、回调和 buffer release，Labwc PID 始终为 367，
VGLite profile 与 5000 ms watchdog 检查均通过。该项覆盖有界 SHM 绘制及会话存活，
未启用额外 renderer diagnostics，没有宣称完成像素截图比对或 DMA-BUF 验收。
源码解包验证以 root 读取旧 SDK 中 root-only 的 Config.in，原生测试另以 UID 1000 独立通过。
SDK 构建包装脚本的完整同步流程未在本轮重跑；解包目标和后续原生入口分别做了真实执行检查。

## 实际系统未被改写的证据

测试前后核对以下 SHA-256，结果一致：

```text
ab55ae65785806d330861e7c4952342b7bafb0ae78902180989aed7546236d8b  /var/lib/opkg/status
f648a7c5130ff9e41827e80ea658751a01f37b5b6e403312941f99e12f80ae9e  /usr/lib/libmount.so.1.1.0
```

启动 ID 保持 `bd486b5c-2689-4428-9108-0d349c7cec10`。
板上测试目录为 `/tmp/tdvp-validation-20260910.hold.W5arr8`，重启后可能消失。
完整测试日志已下载到本地忽略目录 `.tmp/device-validation/newcard-0e686451-20260910/`：

- `legacy-negative-control.log`：旧实现两项失败记录。
- `fresh-native-ci-entry-root.log`、`fresh-native-ci-entry-user.log`：CI 原生入口测试输出。
- `fresh-extract-and-native.log`：空临时输出解包和原生回归。
- `sdk-regression.log`：SDK 回归。
- `opkg-hold-board-test.log`：板上每一步的命令结果和解析后包记录。
- `hold-board-regression.json`：板上退出码、真实系统哈希及服务状态。
- `hold-vglite-session-matrix.json`：8 组 Wayland/VGLite 会话检查。

## 尚待完成

1. 本轮修复生成的最终镜像与配套 SDK 的产物校验、整卡启动验证。
2. 公开发布可下载的镜像、SDK、哈希与包/文件清单，再让候选软件源绑定该发布版本。
3. 软件源完整依赖闭包、CPU0 指令集检查、NetSurf 安装运行及重启验收。

本次测试不承诺拦截管理员强制覆盖或包维护脚本直接改写系统文件。
软件源和镜像的后续发布仍需保留签名校验，并独立检查这些风险。
