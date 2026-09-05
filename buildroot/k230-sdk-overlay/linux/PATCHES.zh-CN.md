# T-Display K230 Linux 补丁队列

本目录由 `buildroot/tools/prepare-k230-sdk-worktree.sh` 复制到 K230 Linux SDK Buildroot 的
`linux/` package。SDK 按词法顺序将文件应用到 Linux `6.6.36` 的
`7d4e1f444f461dbe3833bd99a4640e7b6c2cd529`。

英文版本：[PATCHES.md](PATCHES.md)。

[PATCH_COMPOSITION.zh-CN.md](PATCH_COMPOSITION.zh-CN.md) 记录该队列已经核对过的
交叉修改、顺序约束与验证关卡。

## 队列

| 文件 | 归属 | Contract |
| --- | --- | --- |
| `0001` 到 `0024` | K230 Linux SDK | K230 Buildroot profile 所需的 SDK kernel enablement。`0014` 将 GNNE 和 AI2D 绑定到 AI power domain 与 clock；`0017` 应用 800 MHz AI clock policy，并在切换 parent 时保持 gate 状态。 |
| `0025-tdvp-drm-canaan-standard-gem-dma.patch` | TDVP | Canaan DRM 使用 Linux 6.6 GEM-DMA helper operation 与 FOPS。 |
| `0026` 到 `0034` | Lewis patch set | RM69A10 DSI PHY、VO XRGB8888、panel sequence、burst mode、colour conversion 与 DTS。 |
| `0035-lewis-gdma-add-xrgb8888-rotation.patch` | Lewis patch set | 将 XRGB8888、ABGR8888 与 XBGR8888 注册为 K230 GDMA 的 32-bit input format。 |
| `0036-tdvp-input-tca8418-reset-gpios.patch` | TDVP | TCA8418 driver 中的标准可选 active-low reset GPIO support。 |
| `0037-tdvp-riscv-dts-canaan-add-t-display-k230-keyboard.patch` | TDVP | T-Display K230 detachable-keyboard bus 与 matrix configuration。 |
| `0038-tdvp-input-tca8418-polling.patch` | TDVP | 选择 `poll-interval-ms` 的 board 使用可选 driver FIFO polling。 |
| `0039-tdvp-input-tca8418-hardware-debounce.patch` | TDVP | 可选 TCA8418 hardware debounce property。 |
| `0040-tdvp-pinctrl-k230-support-standard-schmitt-enable.patch` | TDVP | K230 pinctrl 中的标准 `input-schmitt-enable` support。 |
| `0041-tdvp-input-add-gt9895-touchscreen.patch` | TDVP | 为 board DTS 中 `0x5d` 的 GT9895 提供 Type-B multitouch driver。 |
| `0042-tdvp-riscv-dts-canaan-enable-k230-platform-services.patch` | TDVP | K230 internal-codec sound-card binding。 |
| `0043-tdvp-panel-canaan-universal-add-standard-backlight.patch` | TDVP | 通过 Linux backlight class 暴露 RM69A10 backlight。 |
| `0044-tdvp-riscv-dts-canaan-add-keyboard-backlight.patch` | TDVP | 通过 PWM backlight class 暴露 keyboard backlight。 |
| `0045-tdvp-power-bq27xxx-add-bq27220.patch` | TDVP | 向 Linux `bq27xxx` power-supply driver 加入 BQ27220 专用、只读 standard-command register profile。 |
| `0046-tdvp-riscv-dts-canaan-add-dock-power-devices.patch` | TDVP | 将扩展坞 XL9555 GPIO expander 与 BQ27220 fuel-gauge node 加入已验收的 keyboard I2C bus。 |
| `0047-tdvp-misc-add-radio-profile-selector.patch` | TDVP | 启用 UART2，并通过标准 pinctrl/GPIO profile selector 为 LR2021 LoRa 与可选 nRF9151 LTE-M/NB-IoT/GNSS modem 提供互斥硬件状态。 |
| `0048-tdvp-radio-lr2021-spi-transport.patch` | TDVP | 启用 SPI0 的 LR2021 spidev 传输路径，并为 radio profile selector 增加电源和复位控制。 |
| `0049-tdvp-k230-spi-bound-irq-enumeration.patch` | TDVP | 仅枚举 K230 SPI0 在设备树中声明的 interrupt resource。 |
| `0050-tdvp-hwmon-aht20-standard-binding.patch` | TDVP | 加入扩展坞 `0x38` AHT20 与标准 `aosong,aht20` hwmon binding。 |
| `0051` 到 `0052` | TDVP | 通过现有 K230 sound card 及其受控 ALSA switch 路由已验收的 external I2S amplifier。 |
| `0053-tdvp-drm-canaan-page-flip-lifecycle.patch` | TDVP | 明确 Canaan DRM 在 CRTC 禁用和 VO IRQ 交付过程中的 page-flip/vblank 事件所有权。 |
| `0054` 到 `0062` | TDVP | VGLite 的每客户端资源所有权、提交串行化、看门狗、中断、单上下文和完成空闲生命周期修复。 |
| `0063-tdvp-cpu1-rtsmart-mailbox.patch` | TDVP | 保留 CPU1 的 RT-Smart RAM，仅将非缓存的 64 KiB mailbox 暴露为 `/dev/tdvp-cpu1`，并明确不把 CPU1 纳入 Linux SMP。 |

编号遵循导入的 display queue。词法顺序是 build input，并由 baseline assertion 检查。

## CPU1 协处理器约定

K230 Linux 设备树刻意只声明 `cpu@0`。`0063` 保留
`0x10000000..0x13ffffff` 给 CPU1 的 OpenSBI/RT-Smart runtime，其中最后 64 KiB
（`0x13ff0000`）是带版本的共享 mailbox。Linux 仅通过 `/dev/tdvp-cpu1` 以非缓存
页属性映射该 mailbox：它既不启动 CPU1，也不把 CPU1 变成可调度的 Linux CPU。

镜像后处理会编译已固定 commit 的 LilyGO RT-Smart 源码，将固件放入 SD 卡原始
10--30 MiB slot，并让 U-Boot 在 `blinux` 前启动 CPU1。第一版受限 ABI 提供 `ping`
和 `crc32`，由 `tdvp-cpu1ctl` 与 `libtdvp_cpu1.so.1` 暴露。后续 CPU1 service 必须
扩展这个带版本 ABI、维持两侧 cache maintenance，且不得把其余保留 RAM 暴露给 Linux 用户态。

## AI 电源与时钟约定

K230 SDK 在 `0014` 和 `0017` 中拥有 AI power 与 clock 的实现。`0014` 将 GNNE、AI2D 的
device-tree node 连接到 `K230_PM_DOMAIN_AI`、`ai_clk`、`ai_aclk`，并在两个 driver 中使用
runtime-PM 和 bulk clock。`0017` 在 GNNE workload 使用前将 `ai_clk` 设为 800 MHz，并避免
非预期的 clock parent reconfiguration。

TDVP 不复制这两份补丁。release assertion 要求 staged SDK queue 中存在它们，并在 candidate
image build 前检查实际 DTS 与 driver code。target KPU gate 仍要求两个 character device 存在，
并成功运行镜像内置的 kmodel workload。

## Lewis Patch Integrity

`0026` 到 `0035` 保留
[`lewisxhe/k230-t-display-linux-patches`](https://github.com/lewisxhe/k230-t-display-linux-patches)
commit `cef00a992975ea4ed687bf44f04f633dad5087e6` 的 source patch text，并在 staged ext4
worktree 中 canonicalize 为 LF。assertion 检查以下 canonical LF SHA-256。这样可复现选定的
board behaviour，同时 TDVP GEM-DMA 与 keyboard integration 保持明确 ownership。

| Queue file | Canonical LF SHA-256 |
| --- | --- |
| `0026-lewis-drm-canaan-fix-2lan-dsi-phy-with-timeout.patch` | `765a6987abecedadd36556e5f538493ec35b578267f28c14c063de2f8683bfc8` |
| `0027-lewis-drm-canaan-vo-add-xrgb8888-format.patch` | `1006e63966f521e2385626fe05f3418b90c67533ebc6e0f560ebd0da3c8017be` |
| `0028-lewis-panel-canaan-universal-enable-reset-in-prepare.patch` | `b433be3501b30d800cd8319256b505182875e11afa678c0acfd44c510ecbc73e` |
| `0030-lewis-drm-canaan-fix-video-mode-to-burst.patch` | `5a7c111352e501017568f6bb48ed5dec8021016b3dd19a9d1dcde9d73234aba2` |
| `0031-lewis-drm-canaan-fix-xrgb8888-opaque-and-rb-swap.patch` | `cafb2c2bfa98d015c9e79ace47b07aa81d51c323117484b6cc0aaf7eb192d7d1` |
| `0032-lewis-riscv-dts-canaan-add-rm69a10-display.patch` | `796b5db17f8a04a4b1e4d7e86d0cb509dfd22fca6adb695674dd89d4ba643c6a` |
| `0033-lewis-riscv-dts-canaan-add-k230-rm69a10-dts.patch` | `1725b1530bfc078494f11acc3e4f1cbe3a1a0eaf3e66863303ba24885537006e` |
| `0034-lewis-drm-canaan-fix-rgb2yuv-color-swap.patch` | `53a769d7718184afa983e78142eca7d7bf284ed3cfce1e75418a88a3e392bda2` |
| `0035-lewis-gdma-add-xrgb8888-rotation.patch` | `3bd5cd3e589e90159ccdc1a4665446603c69370a09fdbc6a9ddfdbfa531285f7` |

## Display Contract

```text
panel:       RM69A10, two-lane MIPI DSI
DRM:         canaan-drm
scanout:     DRM_FORMAT_XRGB8888 (XR24)
GEM:         Linux DRM GEM-DMA helper
console:     fbcon=rotate:3
atomic rotation: SDK GDMA rotation pipeline with XRGB8888 input support
```

`0025` 提供 `DEFINE_DRM_GEM_DMA_FOPS(canaan_drm_fops)` 与
`DRM_GEM_DMA_DRIVER_OPS_VMAP`。assertion 会拒绝实际 `canaan_drv.c` 中的 Canaan-specific
GEM-DMA allocation 与 mmap implementation。

SDK 的 `0023-add-gdma-vo-rotation.patch` 建立 K230 GDMA rotation pipeline：atomic plane
commit 请求 rotation，GDMA 产出 rotated scanout buffer，VO plane 扫描输出该 buffer。`0035`
提供该 pipeline 所需的 XRGB8888 format registration。Framebuffer-console rotation 将 early
console 渲染为 enclosure orientation。DRM client 使用 GDMA path 时设置 plane 的 `rotation`
property，并按相应 logical dimension 渲染内容。

## Detachable Keyboard Contract

```text
controller: TCA8418 at 0x34
matrix:     7 rows x 10 columns
delivery:   Linux input event device
```

发布 DTB 负责选定的 I2C transport、reset line、中断或 polling 方式和 matrix map。发布 manifest
记录反编译后的 DTB 值以及真实 input-event trace。标准 Linux input subsystem 发布生成的 key event。
rootfs 通过 `tdvp-keyboard-layout` 加载 T-Display console map；physical `0` 输出 `0`，`Shift+0`
输出 `)`。

完整矩阵、修饰键、黄字层、导航键、背光和 Shell 快捷键构成键盘回归集。任何改变键盘电气、
driver 或 mapping 输入的补丁，都必须在 T-Display K230 V1.3 上通过这套回归集才可接受。

## 可拆卸扩展坞电源约定

```text
controller: Linux I2C adapter 1 上的 i2c-gpio keyboard bus
XL9555:     0x20，nxp,pca9555，标准 pca953x GPIO provider
BQ27220:    0x55，ti,bq27220，标准 bq27xxx power supply
BQ25896:    0x6b，已在总线上响应，当前不绑定
AHT20:      0x38，aosong,aht20，标准 hwmon sensor
```

`0046` 在 board display 与 keyboard DTS 创建 `i2c_gpio_keyboard` node 后扩展该 node。它不改变
TCA8418 node、GPIO transport、matrix、reset path、backlight 或 keymap。

`0045` 直接定义 BQ27220 的 command map：`Current()` 位于 `0x0c`、
`RemainingCapacity()` 位于 `0x10`、`FullChargeCapacity()` 位于 `0x12`、
`CycleCount()` 位于 `0x2a`、`StateOfCharge()` 位于 `0x2c`，
`DesignCapacity()` 位于 `0x3c`。其 data-memory entry 均为 invalid，因此 driver 不包含
unseal 或 data-memory write path。生成的 power-supply device 必须通过实机充放电数值验收。

BQ25896 charger 在 V1.3 扩展坞的 interrupt route 与全部必需的 charge-policy value 完成实测前
不加入静态 DTS。XL9555 通过标准 GPIO controller 暴露；只有在扩展坞 schematic 与实机 line trace
确认功能后才为各 line 分配 owner。

AHT20 node 绑定内核的 AHT10/AHT20 hwmon driver。它通过 `temp1_input` 以毫摄氏度提供温度，
通过 `humidity1_input` 以万分之一百分点提供湿度。driver 会校验每个 AHT20 frame 的 CRC，
并遵守传感器的最小采样间隔。

## Touch Contract

```text
controller: GT9895，位于 i2c3 的 0x5d
transport:  使用四字节 event register address
delivery:   Linux Type-B multitouch input event device
coordinates: controller-native 1060 x 2400
```

`0041` 将 `0032` 中的 DTS node 绑定到 `tdvp_gt9895`。release assertion 检查实际 driver source、
kernel configuration 和最终 DTB。物理验收记录包含 touch trace，以及在选定 output transform
上的 Wayland pointer/touch interaction。

## Required Check

```sh
buildroot/tools/prepare-k230-sdk-worktree.sh "$WORKTREE"
buildroot/tools/build-k230-sdk-rm69a10.sh "$WORKTREE"
buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

static assertion 检查 patch integrity、实际 GEM-DMA source、DTB content、kernel configuration、
target rootfs 与 image artifact。镜像随后执行
[`docs/hardware-baseline-validation.zh-CN.md`](../../../docs/hardware-baseline-validation.zh-CN.md)
中的 physical gate。
