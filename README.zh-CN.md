# T-Display K230 的 Vicliu Pocket Linux

Vicliu Pocket Linux 是面向 LILYGO T-Display K230 V1.3 键盘掌机的长期维护
Buildroot 发行版。本仓库会生成当前维护中的桌面发行版：它是一套原生 Wayland 系统，面向
性能受限的 K230，而不是裁剪版 PC 发行版，也不是自制 Launcher 外壳。

系统采用可维护的标准组件：

```text
Linux DRM/KMS + libinput + ALSA
        |
systemd + NetworkManager + seatd + D-Bus
        |
greetd / gtkgreet（认证实际登录的 Linux 用户）
        |
Labwc + PCManFM desktop + wf-panel-pi
        |
Foot、PCManFM、nm-connection-editor、已签名 opkg 软件源
```

K230 是低性能、键盘优先的掌机，因此桌面有意保持轻量：优先选择可靠、可触控的标准程序，
不引入完整 GNOME 或 Chromium，也不包含 GNOME Shell。

## 镜像包含的能力

- 通过 `greetd` 图形登录。登录后的会话永远使用该账户真实的 home，绝不强行落到
  `/home/tdvp`。
- Labwc、PCManFM，以及 Raspberry Pi 的上游 `wf-panel-pi` 插件。顶部栏包含应用菜单、
  NetworkManager 状态、一个输出音量控制、电池状态与时钟。
- 物理 LilyGO/Menu 键打开应用菜单。桌面空白处长按等价于右键，短按为正常指针输入。
  `Alt+F4` 可关闭普通或全屏程序。
- 真正的 XKB Fn 层。两个物理 Fn 键都是 `ISO_Level3_Shift`（Mod5），用于黄色字符；
  普通 Tab 仍是 Tab。
- Foot 终端、PCManFM Files，以及标准 NetworkManager 连接编辑器。基础镜像有意不
  内置通用浏览器，也不会留下失效的浏览器菜单项；浏览器属于软件源应用交付，而非板级
  固件的启动依赖。

## 当前发布状态

当前固件配置的是不可变的 `tdvp-k230-…-r1` 软件源。它只包含已经针对当前固件 ABI
签名并发布的软件包；尤其是，基础镜像目前**没有**可安装的浏览器包。浏览器候选包必须
发布到新的、不可变的软件源修订版中；绝不能修改既有已签名索引来追加软件包。

## 外放路由与验收

外接 MAX98357A 兼容功放复用现有的 `K230_I2S_INNO` ALSA 声卡，而不是创建第二张声卡。
内核将 IO32、IO33、IO35 分别配置为 I²S 的 BCLK、LRCK 与 data-out；GPIO34 的功放关断线由
K230 ASoC machine driver 管理。设备树默认选择外放，启动链没有
`tdvp-external-audio.service`，避免策略服务与声卡注册竞争。

需要人工切换时，唯一的 root 策略工具是 `tdvp-audio-route external|internal`；它不直接调用
`gpioset`。播放器、PulseAudio 和面板只作为普通 ALSA 客户端。刷写后可执行：

```sh
sudo tdvp-speaker-acceptance status
sudo tdvp-speaker-acceptance test
# 听到双声道正弦音后按 Ctrl-C：
sudo tdvp-speaker-acceptance confirm-audible
```

声卡枚举并不能证明扬声器实际发声；只有最后的人工确认才会将本次启动标记为已通过外放验收。

## SD 卡存储规则

镜像只有 GPT 的 boot 分区 1 和 root 分区 2，绝不创建 `/data`，也不会把剩余容量交给
第三分区。

在更大 SD 卡上的首次启动，`tdvp-rootfs-expand.service` 使用 Raspberry Pi 式流程：先移动
GPT 备份头、扩展分区 2、严格保留其 GPT `PARTUUID`，再自动重启一次；下次启动扩展已挂载
的 ext4 根文件系统。保留 UUID 极其重要，因为 U-Boot 使用 `root=PARTUUID=…` 启动，而不是
不稳定的 `/dev/mmcblkN` 枚举序号。已有后续用户分区的卡会保持不变。

## 网络与浏览器交付

NetworkManager 是有线与 Wi-Fi **连接策略**的唯一所有者。镜像不会启用竞争性的
`systemd-networkd`，也不会保留旧式按接口配置的 `wpa_supplicant@wlan0`。Wi-Fi 仍通过
正常的 D-Bus `wpa_supplicant` 后端工作；启动该后端不等于启动第二个网络管理器。可从
状态栏 Wi-Fi 项选择接入点，或点 **Edit Connections**；它打开上游
`nm-connection-editor`，不是自制网络对话框。

基础镜像有意不包含通用浏览器，避免重量级 WebKit/Chromium 依赖成为 K230 的固件启动
依赖。未来的浏览器包必须 ABI 匹配、经过签名并发布到新的软件源修订版后，才能出现正常的
桌面入口；它必须提供正常的地址栏和导航，不能只是 kiosk 壳。现代大型 Web 应用仍可能
超出 K230 的 CPU 与内存预算。

## 已签名的软件发行源与 Software Manager

内置 Software Manager 会启动 Foot 中的真实 `opkg`。固件有意只保留很小的硬件/桌面种子；
ABI 固定且不可变的软件源修订版则是可扩展的用户态软件目录，承载通用库、命令行工具、桌面程序
和设备应用：

```text
https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/r2/riscv64
```

每次从 Software Manager 或命令行开始操作前，受控的 `tdvp-opkg` wrapper 会把内置发行
公钥导入 `/etc/opkg/gpg`，并校验指纹：

```text
2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500
```

这有意是按需操作，而不是开机服务：网络软件源绝不能阻塞存储、登录或桌面。索引签名是
强制的。在 Software Manager 的终端执行：

```sh
sudo tdvp-opkg update
sudo tdvp-opkg list
sudo tdvp-opkg install <package>
```

当更新失败时不要关闭签名校验。软件源发布端必须先发布 ABI 匹配的软件包、
`Packages`/`Packages.gz` 及使用对应私钥生成的 detached signature。本固件与本仓库只保存
公钥；私钥绝不应放入设备或源码树。追加软件意味着发布新的软件源修订版、再让后续镜像
使用新 URL，绝不能覆盖任何已签名索引。通用库保留它们的上游包名（例如 `sdl2`、`sdl2-ttf`、
`libmgba`）；ABI 安全性来自精确的平台依赖和 `riscv64` 架构，而不是给库名强加前缀。

## 构建与发布

SDK 必须在 WSL/Linux 的 ext4 工作目录构建；交付前必须把产物收集到本 Windows 仓库的
`output` 目录，不能把只在 WSL 内可见的文件作为交付镜像。

```sh
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/collect-release-bundle.sh "$HOME/work/tdvp-k230-labwc" <release-name>
```

构建固定 SDK、Linux commit、ext4/GPT 身份；同时运行 patch-only 源码断言和 post-image
SD 卡契约。最终守卫会验证：U-Boot 的 PARTUUID 根参数、桌面程序、NetworkManager/连接编辑器、
GIO TLS 模块、签名源信任物料、Fn 布局、面板插件、事件声音资源、根分区扩展服务，以及旧
`/data` provisioner 确实不存在。

在写入 `output/` 之前，**release collector** 还会执行一次在线发布门禁：通过 HTTPS 下载配置
软件源的 `Packages.gz`、`Packages.gz.asc` 与 `release.json`，用镜像内置的同一把公钥验证签名，
并拒绝任何不精确依赖当前固件 ABI 的已发布包。因此，只要软件源尚未发布、签名无效或 ABI
不匹配，本地镜像即使构建成功也不能被当作正式发行包交付。

刷写前必须检查 image manifest 和 `SHA256SUMS`。collector 有意只在 Windows 的 `output/`
下交付 `<release-name>.img.gz`，不会在旁边保留数 GiB 的裸 `.img`。请使用支持 gzip 输入的
写卡器，或把压缩流直接解压到整张 SD 卡设备；不要先在 WSL 构建目录或 `output/` 中生成并
长期保留裸镜像。

## 恢复与现场检查

串口为 `ttyS0`，参数 `115200 8N1`。开发镜像提供 root 恢复账户 `root` / `tdvp`；设备离开
可信开发网前请修改凭据。

登录后的常用检查：

```sh
systemctl --no-pager status NetworkManager tdvp-rootfs-expand greetd
nmcli device status
sudo tdvp-opkg update
```

根分区扩展日志在 `/var/lib/tdvp/rootfs-expand.status`。含有既有第三分区的 SD 卡会被新服务
有意跳过，不会被重新分区。

更详细的发布契约与验证步骤请见 `docs/`。
