# 发布契约

每个发行版交付一张可刷写的 T-Display K230 SD 卡镜像以及 Windows 可见的 release bundle。
它是低性能、全键盘 Linux 掌机的长期维护基线，不是实验性的 Launcher 镜像。

## 交付文件

```text
vicliu-pocket-linux-k230-<revision>.img
vicliu-pocket-linux-k230-<revision>.img.gz
tdvp-image-manifest
tdvp-sdk-baseline-manifest
README.txt
SHA256SUMS
```

最终 bundle 必须复制到本 Windows 仓库的 `output/`；只存在于 WSL 中的构建物不构成发布
交付。镜像包含 K230 启动 payload、内核、RM69A10 DTB、systemd、OpenSSH 恢复、
NetworkManager、seatd、greetd/gtkgreet、Labwc、PCManFM、Raspberry Pi `wf-panel-pi`
插件、Foot、Cog/WPE WebKit、`nm-connection-editor`、`opkg`、签名软件源 bootstrap 与
板级 package。

## 必须满足的镜像不变量

- U-Boot 使用固定 root `PARTUUID`，绝不能硬编码 `/dev/mmcblkN`。
- GPT 只有 boot 分区 1 与 root 分区 2；首次启动可安全扩展根文件系统，不存在 `/data`
  provisioner 或第三数据分区。
- Greeter 登录的账户获得其自身 home 和 runtime 目录。
- LilyGO/Menu 打开上游应用菜单；Fn 输出黄色字符；桌面空白处长按出现普通右键菜单。
- PCManFM 提供壁纸、Desktop 与 Files；唯一面板是 `wf-panel-pi`，只有一个 NetworkManager
  项和一个输出音量项。
- Cog 具备 GIO TLS 支持且 Labwc 标题栏控制可触摸。
- `libcanberra` 与 Freedesktop theme 提供输出音量的系统事件声音。
- NetworkManager 独占网络并启动上游 `nm-connection-editor`；不交付直接配置旧
  wpa_supplicant 的 UI。
- 默认应用软件源 ABI 固定，且在使用 `opkg` 前会校验其发行公钥与索引签名。

## 发布门禁

1. 准备锁定的 SDK，回放 SDK/Linux/Buildroot/package 补丁并执行 patch-only 断言。
2. 使用发行 defconfig 构建完整可启动镜像。
3. 执行 post-image SD 卡校验：检查分区身份、rootfs 文件、systemd 链接、桌面配置、TLS、
   事件声音及软件源信任物料，而不只依赖编译退出码。
4. 运行硬件构建预检并保留其 evidence report。
5. 实机检查 Greeter/会话、菜单键、键盘、触摸、Files、面板、Wi-Fi 编辑器和音量事件声音；
   再从已签名 feed 安装浏览器（`tdvp-netsurf`）并验证 HTTPS 浏览。
6. 通过 HTTPS 下载配置的软件源的 `Packages.gz`、`Packages.gz.asc` 与
   `release.json`；使用镜像内置公钥验证 detached signature，并要求每个已发布包精确依赖
   当前镜像 ABI。
7. 将镜像、压缩镜像、manifest 与 checksum 收集到 `output/`。

`tdvp-image-manifest` 记录源码版本、构建输入、文件系统/GPT 身份与镜像哈希；
`tdvp-sdk-baseline-manifest` 记录实际 staged SDK 输入；`SHA256SUMS` 覆盖全部交付文件。

## 签名软件发行源策略

标准镜像只配置：

```text
https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/r5/riscv64
```

公钥指纹为 `2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500`。镜像只含公钥；镜像本身是很小的
硬件/桌面种子，这个 ABI 匹配的软件源才是可扩展的用户态发行目录，承载库、工具、桌面程序
和设备应用。发布端必须用离线私钥为 ABI 匹配的软件包、`Packages`/`Packages.gz` 及
detached signature 签名。设备与本仓库不应持有私钥，发布说明也绝不能要求关闭签名校验。

该 feed 的可组合性是发行契约：对本发行版中的每一个软件组件，每一个非 ABI 动态运行时库、插件和
runtime helper 在同一 feed 修订版中都必须有且只有一个可独立安装的 IPK 所有者。应用是叶子，只能通过
精确版本依赖使用这些提供者；不得静态塞入通用库，也不得因为某个库恰好存在于基础镜像中就暗中依赖它。

release collector 会对在线 Pages endpoint 强制执行该策略；镜像里仅仅存在一个 URL 和公钥，
不能证明设备在现场一定可以通过软件源更新。
