# T-Display K230 Linux Bring-Up 计划

本文定义当前 bring-up 计划：通过官方 K230 Linux SDK baseline 适配 T-Display K230。

英文版本：

- [bringup-plan.md](bringup-plan.md)

## 目标

产出第一张 TDVP SD-card image，使 T-Display K230 启动到最小 Buildroot Linux
userspace，并为 hardware-complete support 建立受控路径。

第一版可接受 boot path 是：

```text
SD card
  -> SPL / U-Boot / OpenSBI compatible with the Canaan Linux SDK path
  -> K230 big-core Linux
  -> BusyBox rootfs
  -> TDVP platform init
```

## 主要参考源

下面几个来源各有角色：

| Source | Role |
| --- | --- |
| `kendryte/k230_linux_sdk` | 主要 Linux boot/kernel/image 参考 |
| `Xinyuan-LilyGO/T-Display-K230_canmv_rt` | T-Display K230 硬件事实 |
| TDVP Buildroot external tree | 平台自有 minimal rootfs、packages、image policy 和 API boundary |

## 起始策略

从官方 Linux SDK 形态开始，再裁剪并适配：

```text
k230_linux_sdk reference
  -> select closest K230/CanMV Linux profile
  -> extract kernel, DTS, U-Boot, OpenSBI, image-layout facts
  -> compare with T-Display K230 hardware facts
  -> create TDVP board delta
  -> build minimal TDVP rootfs and image
```

第一轮实现不要从 clean upstream kernel 开始。也不要从 LilyGO RT-Smart application
behavior 开始定义平台模型。

## 第一批工作项

### 1. 固定 SDK Baseline

状态：第一版 baseline 已完成。

已固定输入：

```text
repository: https://github.com/kendryte/k230_linux_sdk
branch: dev
commit: 5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7
```

见 [sources.lock](sources.lock) 和
[k230-linux-sdk-baseline.zh-CN.md](k230-linux-sdk-baseline.zh-CN.md)。

### 2. 选择最接近的官方 Profile

从下面这个 profile 开始分析：

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

提取并记录：

- kernel repo and commit；
- kernel defconfig；
- DTS names；
- OpenSBI version and build mode；
- U-Boot board name and version；
- genimage layout；
- boot environment；
- boot 和硬件验证所需 packages。

状态：已提取到 [k230-linux-sdk-baseline.zh-CN.md](k230-linux-sdk-baseline.zh-CN.md)。

### 3. 定义 T-Display Delta

对比官方 K230/CanMV profile 和 LilyGO T-Display K230 facts：

- RM69A10 AMOLED panel。
- GT9895 touch。
- CSI camera route and first sensor。
- UART console。
- SD/MMC boot device。
- GPIO reset、power、interrupt 和 backlight lines。
- Optional board peripherals。

输出应该是 TDVP board delta，而不是完整 SDK behavior 的 fork。

### 4. 创建最小 Buildroot Profile

TDVP profile 必须包括：

- 与所选 SDK baseline 兼容的 RISC-V target 和 toolchain policy。
- 选定的 K230 Linux kernel source。
- 选定的 DTS 或 TDVP DTS delta。
- BusyBox rootfs。
- 确定性的 SD-card image generation。
- platform init files。

默认必须排除：

- desktop packages；
- distro package managers；
- app-specific packages；
- full vendor demo package set；
- application 对 Linux devices 的直接访问。

### 5. 构建第一张镜像

第一张镜像要证明：

```text
kernel + dtb + rootfs + boot artifacts are reproducible from pinned inputs
```

镜像输出名保持：

```text
sysimage-sdcard.img
```

### 6. 分波次验证硬件

Wave A：

- SPL/U-Boot/OpenSBI handoff。
- serial console。
- SD/MMC rootfs。
- BusyBox init。
- debug control plane：`COM56` 串口兜底，USB Ethernet `eth0` DHCP 后用于 SSH/数据采集。

Wave B：

- RM69A10 display。
- GT9895 touch。
- RTL8189FTV WiFi on SDIO0。
- TCA8418 keyboard detection on I2C4。
- GPIO/pinctrl basics。

2026-07-17 状态：

- RM69A10 display 已通过上板验证：`card0-DSI-1` connected，mode `568x1232`，
  `tdvp-display-smoke` 通过，屏幕显示彩条。
- RTL8189FTV 已通过到 interface enumeration：启动脚本加载 `8189fs` 后出现 `wlan0`
  和 `wlan1`；`iw dev wlan0 scan` 能扫描到周边 AP；AP association/DHCP 仍待测。
- 插入的 Realtek USB 10/100 LAN 已通过 `r8152` 枚举为 `eth0`；`eth0` 已从
  `192.168.31.1` 获取 `192.168.31.86/24`，device->gateway、host->device、
   device->host ping 均通过。`be9c30f3...` 镜像已启用 Dropbear，并自动对 `eth0` 执行 DHCP。
- GT9895/Goodix 已注册 `/dev/input/event0`；物理触摸坐标和方向仍待 `evtest`
  验证。
- TCA8418 在 `/dev/i2c-0` 上尚未看到 `0x34` ACK；失败日志是在可拆卸键盘模块已安装时采集。
  `tdvp-k230-iomux keyboard` 已确认 IO46/47 为 I2C4 alt3；IO43 reset path 已能通过
  `/dev/gpiochip1 line 11` pulse，但复测后 `0x34` 仍不 ACK。下一步集中检查 TCA8418
  电源、I2C 上拉、GPIO43 物理连通和可拆卸键盘接口。

Wave C：

- CSI camera path。
- DMA/CMA/reserved memory。
- first BSP camera/display/input smoke tests。

Wave D：

- AI/KPU userspace path。
- LoRa、BLE、LTE/GNSS、audio、battery、fan、AHT20 和其他实际存在的 board capabilities。

这些 wave 只定义验证顺序，不把硬件移出范围。

## 第一轮非目标

- 不做 desktop Linux。
- 不引入 Debian/Ubuntu runtime assumptions。
- 不要求 target device 上安装 packages。
- 不导入完整 vendor demo environment。
- 不让 application 拥有 driver setup。
- 不让 app 直接使用 V4L2、DRM、evdev、ioctl、sysfs、procfs 或 vendor MPP handles。

## 验收标准

本 bring-up 阶段满足以下条件才算接受：

- SDK baseline 已固定；
- kernel baseline 已固定；
- T-Display K230 board delta 已记录；
- image 可以从 SD card 可复现启动；
- Linux 通过 serial 进入 BusyBox userspace；
- rootfs 由 Buildroot 生成；
- active DTB 是 `tdisplay-k230.dtb`，不是 CanMV ST7701/FT5306 DTB；
- application-facing code 保持在 TDVP BSP/runtime APIs 之后。

## 当前扩展适配集合

第一轮 hardware-complete 方向不应只做 display。当前 Linux 适配应覆盖这些板级关键硬件：

| Hardware | 第一轮实现方式 |
| --- | --- |
| RM69A10 display | `tdisplay-k230.dtb` 中加入 RM69A10 timing/init sequence；2026-07-17 已通过显示 smoke |
| GT9895 touch | I2C3 address `0x5d` 上使用 Goodix-compatible DTS node；Goodix input device 已出现，物理触摸事件待验收 |
| RTL8189FTV WiFi | 启用 SDIO0，并加入 Buildroot `rtl8189fs`、`iw`、`wireless-regdb`、`wireless_tools`、`wpa_supplicant`；interface 已出现 |
| USB Ethernet | `r8152` 已能把插入的 Realtek USB 10/100 LAN 枚举为 `eth0`；DHCP 和 ping 已通过；`be9c30f3...` 镜像自动 DHCP 并启用 Dropbear |
| TCA8418 keyboard | 启用 I2C4，并加入 `tdvp-k230-iomux` 和带 GPIO43 reset 的 `tdvp-keyboard-smoke`；IO46/47 IOMUX 已验证，`0x34` ACK 仍待通过 |
| CSI camera foundation | MIPI/CSI2 path 保持与所选 K230 Linux SDK baseline 对齐 |
| GPIO/I2C/SPI/MMC/PWM | 启用 kernel foundations 和 validation tools |
| Battery/fuel gauge、LR2021、GNSS/LTE、fan、speaker、AHT20 | 仍在范围内，但 DTS nodes 等 bus/address/pin 证据完整后再加入 |
