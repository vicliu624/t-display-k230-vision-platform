# 39d065a 配对候选：构建与成品审计

日期：2026-09-08。代码提交：`39d065a`，分支：
`codex/cpu1-rtsmart-integration`。本候选通过本地完整构建和镜像内容审计，
**尚未在设备上冷启动，不是摄像头、KPU 或蓝牙硬件验收通过报告。**

## 构建内容与环境

本候选相对已经烧录的 `a57e99c` 包含：

- `4314911`：有界、手动 CPU1 frame probe 与验收说明；不安装到桌面菜单。
- `0d34463`：启动解压器 SDMA/SRAM 交接保护。
- `c9627dd`：恢复以前未提交的键盘背光修复，并增加回读与回归测试。
- `a5e7862`：所有权握手约束下的 CPU1 初始化阶段记录及 Linux 只读报告。
- `39d065a`：修正生产 SDK 布局下启动校验器的参考目录。

构建在 `vicliu@192.168.31.42` 上的 Ubuntu 24.04 容器
`tdvp-ci-prepared:20260907-hardware` 中完成。外层主机不是 Ubuntu 24.04；
实际编译环境是该容器。SDK 复用专用工作目录，使用完整 `all` 目标，
未启用跳过包更新的 `TDVP_IMAGE_REBUILD` 模式。

构建日志：`cpu1-full-image.5HD1oh/image-39d065a.49VZC7.log`。
独立完整审计：`cpu1-full-image.5HD1oh/audit-39d065a.U6fMQQ/`。
这些目录位于 LAN 验证根目录的 `hardware-fixes-20260907/` 下。
原 a57e99c 压缩镜像和配对元数据已保存在 `baseline-a57e99c/`，未删除。

## 成品身份

交付文件名：`tdvp-ai-vision-39d065a.img.gz`。

| 成品 | 字节数 | SHA-256 |
| --- | ---: | --- |
| 压缩 SD 镜像 | 124486988 | `eb2518b0cea69a32b2fb9aa32d57a20f7b8abb0d0522350d798502eee4fdeebe` |
| 原始 SD 镜像 | 1744850944 | `cb6134ca8d9d36ddea4ed2374458aefc7d33a978e4b096e336c6ae0dce3931e3` |
| CPU1 raw payload | 3561160 | `b5bd14b8ddbed2caccd770efad18f1cda1a7ce68b868575a0da42ebb894efe81` |
| Linux DTB | 62664 | `88ccb69494e6c544631d139bd1f9927196be6ce35c8ef14781d091e0ef84dae1` |

配套 `tdvp-image-manifest` 记录内核、两个文件系统、U-Boot 和 CPU1 元数据的
完整哈希；`sdk-baseline-manifest` 记录 staged 输入。其 SHA-256 为
`1ad43fa71cf43155ca74e1334cc81c27c68aec14dbc6bd9d76915c692394fb9e`。
本机下载后重新计算压缩文件哈希，与 LAN 成品一致。

此前独立 CPU1 编译的 payload 哈希不是本镜像的 payload 哈希。两次构建的
RT 配置、startup trace 头与 service 源码一致，worker ELF 哈希均为
`600fdd7c136c8a04880522ad509f1035bd59e5d5b34b17242325f5148f1ae2b6`；
ELF 中可以看到各自的绝对源码路径。本记录不声称不同构建路径下整个 CPU1
二进制已经达到逐字节可复现；识别本候选必须使用上表和配套 manifest。

## 已完成的验证

1. 生产完整构建退出码 0，包括 RT-Smart/OpenSBI、Linux、桌面与镜像打包。
2. 保留全部检查的 boot contract 对实际 U-Boot 源码、SPL/U-Boot 二进制
   及新 CPU1 raw payload 通过。不是仅验证待复制的 overlay。
3. 独立运行 `TDVP_FULL_ROOTFS_IMAGE_COMPARE=1` 的生产 image guard：
   整个 rootfs 分区与原始 SD 镜像逐字节比较通过，不是默认 4 MiB 抽样。
   两个 ext4 的文件系统检查、启动布局、CPU1 raw slot、DTB 配对通过。
4. `gzip -t` 通过；压缩镜像解压后的全量 SHA-256 与原始镜像一致。
5. 从实际 ext4 提取 `vpl-hardwared`、`vpl-hwctl`、`tdvp_cpu1_vision.ko`，
   与本次 target 文件逐字节相同；实际硬件包编译源码与项目来源相同。
6. DTB 中键盘引脚是 `io52`/`alt2`，亮度表为反向 PWM 的降序值；程序中
   包含键盘 pwm-backlight 接管路径和 `pwm3_5`，背光修复进入了镜像。
7. 提取出的 Linux module 含有三个 startup trace 字段；实际 RT-Smart ELF
   存在强符号 `tdvp_cpu1_startup_trace`。MPP 与 I2C4 编译目录的 trace 头
   均与项目源文件相同，CPU1 内核归属契约通过。
8. 镜像 guard 对 VGLite-only 桌面启动策略、greetd/PAM/locker 权限、CPU1
   bridge 自动加载、非 Linux ISP/KPU 所有者及 UART1 DTB 检查通过。

从 ext4 提取的程序 SHA-256：

| 程序 | SHA-256 |
| --- | --- |
| `vpl-hardwared` | `74ed6429108befa3fc8f45f73c2e97947c74d5c189793146ad10390dca2a55bd` |
| `vpl-hwctl` | `dc5154a6a96dd38e86d6ae6cc1af1000e6e1f9441b99e0a44a40406af3ceec0c` |
| `tdvp_cpu1_vision.ko` | `fbe0e67c956830bed0dd4111e04150435fc74607509e182322d43b3eda705713` |

## 尚未通过的真实设备条件

当前设备仍运行 a57e99c 配对卡，仅背光程序做过独立部署：

- CPU1 停在 STARTING，Linux ownership 为 fault/-110，帧数为 0。
  新 trace 是下一次启动定位工具，本身不代表问题已解决。
- 真实 GC2093 帧、KPU/AI2D/FFT 数值、AI 与 VGLite 共存仍须验证。
- 背光控制与 PWM 回读已验证，实际三档亮度等待用户确认。
- 蓝牙仍未识别出 UART 对端或 HCI；参见
  [nRF52840 集成缺口](nrf52840-integration-gap.zh-CN.md)。未刷写 nRF 固件。
- LoRa 的传输/控制枚举不能代替射频收发验收；没有发送射频测试数据。

下一步使用保留回退卡的完整配对冷启动，不单独热替换 CPU1 或启动固件。
Linux 登录后只读 `/sys/class/misc/tdvp-vision/status`，记录
`startup_trace_version`、`startup_stage`、`startup_result` 和 ownership 状态。
在 ownership fault 或心跳停止时不得启动 frame probe、热重启 CPU1，
或回收可能仍受 DMA 使用的共享缓冲。
