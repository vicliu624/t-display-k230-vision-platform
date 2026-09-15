# 发布契约

本文规定 T-Display K230 镜像的交付物与验收范围。代码集成、CI 构建和实机验收
分别记录；一次构建通过只证明该次构建检查覆盖的内容。

## 交付文件

`collect-release-bundle.sh` 生成以下文件，`<release-name>` 由调用者指定：

```text
<release-name>.img.gz
tdvp-image-manifest
tdvp-cpu1-rtsmart.bin
tdvp-cpu1-rtsmart.manifest
tdvp-sdk-baseline-manifest
tdvp-image-base.json
tdvp-opkg-status
tdvp-opkg-info.tar.gz
tdvp-buildroot-packages.json
<release-name>-cpu0-sdk.tar.gz
tdvp-sdk-manifest.json
README.txt
SHA256SUMS
```

最终 bundle 位于仓库 `output/<release-name>/`；本地 WSL 构建应收集到用户可见的
仓库目录，CI 则上传这个目录。bundle 只交付压缩镜像。CPU1 固件同时嵌入整卡镜像，
单独附带的文件用于核对配对关系与校验值。
CI 在隔离 SDK 测试通过后追加 `tdvp-sdk-validation.log` 及其 SHA-256。
应用 SDK 包含配套编译器、开发 sysroot 和镜像包记录，使用方法见
[CPU0 应用 SDK](cpu0-application-sdk.zh-CN.md)。

镜像包含 CPU0 Linux、CPU1 RT-Smart/OpenSBI、systemd、OpenSSH、NetworkManager、
seatd、greetd/gtkgreet、Labwc/VGLite、PCManFM、wf-panel-pi、Foot、
nm-connection-editor、gtklock、板级服务及 opkg 签名信任材料。
浏览器和 Linux Camera demo 已从基础桌面移除。

## 镜像不变量

- U-Boot 通过固定 root `PARTUUID` 找到根分区。
- GPT 包含 boot 分区 1 和 root 分区 2；启动固件另占 raw 区域，CPU1 槽位为 10–30 MiB。
  首次启动可扩展根分区，保留已有后续分区，不创建 `/data`。
- Linux 运行在 CPU0，使用 CPU0 支持的标量指令集。CPU1 独占 GC2093、采集/ISP、
  KPU、AI2D、FFT 和相关 AI 内存；Linux 通过 `/dev/tdvp-vision`、`/dev/tdvp-ai` 异步交互。
- 登录页和桌面均使用 VGLite；渲染异常会结束会话并留下诊断状态，禁止切换到 Pixman。
- Greeter 认证所选账户并使用其 home/runtime。gtklock 使用当前会话账户密码，
  默认空闲 300 秒锁定、330 秒关闭屏幕，唤醒后显示密码窗口。
- PCManFM 提供壁纸、桌面与 Files；wf-panel-pi 提供面板。Menu、Fn、触摸和键盘背光
  必须在实物上检查。
- NetworkManager 管理网络；nm-connection-editor 提供连接编辑。
- 软件源保留签名校验。签名有效和 ABI 元数据一致还需配合 CPU 指令集、文件所有权、
  依赖闭包及冷启动验证，才能确认可安全安装。

## 检查与验收

| 阶段 | 检查内容 | 结论范围 |
| --- | --- | --- |
| 构建前 | 锁定 SDK、补丁结构和回放、源码契约、硬件预检 | 已检查的输入与配置 |
| 完整构建 | 发行 defconfig、CPU1 配对固件、post-image verifier | 产物布局、rootfs 文件和配置符合断言 |
| SDK 交付 | 文件哈希、镜像配对、两个隔离路径、C/C++/GTK/CMake 与 ELF 属性 | 应用编译器及已检查依赖可脱离原构建目录使用 |
| 软件源静态门禁 | HTTPS、索引签名、release 元数据、包 ABI 依赖、必需包名 | 发布索引与信任材料 |
| 新卡实机 | 启动/重启、CPU1 数据与数值、VGLite、登录锁屏、输入、网络、音频 | 该镜像与该硬件的运行结果 |
| 软件包实机 | 安装依赖闭包、启动应用、卸载/升级边界、重启 | 包管理与软件源的端到端可用性 |

实机步骤见 [硬件基线验证](hardware-baseline-validation.zh-CN.md)。
历史热部署结果应注明基线镜像和替换文件；每张候选整卡镜像仍需单独验收。

`tdvp-image-manifest` 记录源码、构建输入、分区身份和镜像哈希；
CPU1 manifest 记录配对固件信息；`tdvp-sdk-baseline-manifest` 记录 staged 输入；
`tdvp-sdk-manifest.json` 将应用 SDK 与镜像、包清单绑定；
`SHA256SUMS` 覆盖其余交付文件。

## 软件源的当前状态与待满足条件

镜像配置的是 `stable` 可变通道，完整 URL 与公钥见
[软件源状态](package-feed-status.zh-CN.md)。平台标识末尾的 `r1` 是 ABI 标签的一部分；
2026-09-09 观察到的 feed 修订版为 `r6`。

**软件包端到端验收未通过，安装与升级暂缓。** NetSurf 安装涉及的运行库含 CPU0
不支持的 RVV 指令，随后设备启动失败。镜像发布现在独立检查最终 ext4 的文件归属和
预装包数据库，并导出相应清单。线上软件源签名/元数据检查单独运行；每个 IPK 的
兼容性与端到端验收仍需完成。配套 SDK/sysroot 发布后，才能建立对外软件源的构建基线。

恢复软件交付前，至少要保证：

- 所有 Linux 可执行文件和运行库均兼容 CPU0。
- 基础镜像与 feed 的公共运行库具有明确、可核对的版本和包所有权。
- 每个运行库、插件与 helper 的提供者唯一，依赖闭包完整。
- 签名持续开启；私钥留在发布端，设备和镜像只携带公钥。
- 在配对的候选镜像上完成 NetSurf 安装、HTTPS 页面访问和重启验收。
