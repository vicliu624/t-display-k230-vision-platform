# 快速开始

## 获取镜像或自行构建

当前 CPU1 集成分支为 `codex/cpu1-rtsmart-integration`。下载 Actions 产物时，
确认运行成功，并核对产物中的 manifest 和 `SHA256SUMS`。PR 构建可能使用
GitHub 生成的测试合并提交，因此产物名称中的提交号可能与分支 HEAD 不同。

自行构建使用 Ubuntu 24.04 x86_64，SDK 工作目录放在 Linux/WSL 的 ext4 文件系统上。
依赖和静态检查见 [Buildroot 说明](../buildroot/README.zh-CN.md)。

```sh
git clone --recurse-submodules --branch codex/cpu1-rtsmart-integration https://github.com/vicliu624/t-display-k230-vision-platform.git
cd t-display-k230-vision-platform
SDK_WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$SDK_WORKTREE"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$SDK_WORKTREE"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$SDK_WORKTREE"
bash buildroot/tools/collect-release-bundle.sh "$SDK_WORKTREE" tdvp-k230-labwc-desktop-local
```

收集脚本把压缩镜像、CPU1 固件、manifest 和校验文件写入仓库
`output/tdvp-k230-labwc-desktop-local/`。已有同名目录会被保留；再次收集需换一个名称。

## 烧录与首次启动

在 bundle 目录运行 `sha256sum -c SHA256SUMS`，然后使用支持 `.img.gz` 的烧录工具，
将压缩镜像写入已确认的整张 microSD 卡。请先备份卡上数据，并核对设备容量和名称。
完整文件清单见 [发布契约](release-contract.zh-CN.md)。

串口为 `ttyS0`，参数 `115200 8N1`。开发账户为 `tdvp` / `tdvp`，
恢复账户为 `root` / `tdvp`。设备离开可信开发网络前请更改默认密码。
登录页认证所选 Linux 账户，并启动该账户的桌面会话。

较大卡的空闲空间可在首次启动时用于扩展根分区和 ext4，过程中可能自动重启一次。
镜像没有 `/data` 分区；已有后续分区的卡会跳过自动扩展。检查：

```sh
systemctl --no-pager status greetd NetworkManager tdvp-rootfs-expand
cat /var/lib/tdvp/rootfs-expand.status
```

## 桌面、锁屏与网络

登录页和登录后的 Labwc 桌面都使用 VGLite。PCManFM 提供壁纸、桌面和 Files，
`wf-panel-pi` 提供顶部栏。LilyGO Menu 键打开应用菜单，桌面空白处长按打开右键菜单，
Fn 输入黄色字符，`Alt+F4` 关闭当前应用。

默认空闲 5 分钟后出现 gtklock 密码锁屏页，再过 30 秒关闭屏幕。按键或触摸唤醒后，
输入当前登录账户的密码解锁。详情见 [登录与锁屏](session-login-and-lock.zh-CN.md)。

NetworkManager 管理网络。可通过面板选择 Wi-Fi，或打开 **Edit Connections**。
终端连接方式：

```sh
nmcli device status
nmcli device wifi list
nmcli device wifi connect "SSID" password "PASSPHRASE"
```

基础镜像没有预装浏览器，也没有 Camera 菜单项。摄像头和 AI 由 CPU1 托管，
Linux 应用通过异步接口取得数据，接口说明见 [架构](architecture.zh-CN.md)。

## 软件源：暂缓安装和升级

**截至 2026-09-09，软件包安装与重启验收未通过。请先验证新镜像本身。**
上一次 NetSurf 安装拉入的运行库包含 CPU0 不支持的 RVV 指令，随后启动停在 systemd。
当前 `stable` 软件源的签名检查无法覆盖这类兼容性问题。

这阶段可以查看 `/etc/opkg/*.conf` 和本机已安装包记录；请暂缓从该源安装、升级或批量更新。
原因、验证边界和后续验收条件见 [软件源状态](package-feed-status.zh-CN.md)。
