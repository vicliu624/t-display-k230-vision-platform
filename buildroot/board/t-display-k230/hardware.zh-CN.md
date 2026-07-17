# T-Display K230 硬件事实

本文总结从 LilyGO `T-Display-K230_canmv_rt` 仓库提取出的硬件事实，并把它们映射到
Linux/Buildroot 设计需求。

英文版本：

- [hardware.md](hardware.md)

## 板卡身份

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| 板卡/项目名 | `T-Display-K230_canmv_rt` | `confirmed` / 已确认 |
| SoC | Kendryte/Canaan K230 | `confirmed` / 已确认 |
| CPU 架构 | RISC-V | `confirmed` / 已确认 |
| 内存 | 1 GiB LPDDR | `confirmed` / 已确认 |
| 主要可移除存储 | SD card | `confirmed` / 已确认 |
| 产品主要用途 | 带 AMOLED 显示屏的 K230 开发板 | `confirmed` / 已确认 |
| 本仓库的平台目标 | 最小 Linux 视觉平台 SDK | `linux-decision` / Linux 平台决策 |

## Display

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| 主显示类型 | AMOLED | `confirmed` / 已确认 |
| 主显示尺寸 | 4.1 inch | `confirmed` / 已确认 |
| 主显示分辨率 | 568 x 1232 | `confirmed` / 已确认 |
| 主显示控制器 | RM69A10 | `confirmed` / 已确认 |
| 主显示接口 | MIPI DSI | `confirmed` / 已确认 |
| RM69A10 lane 数量 | 参考 driver 中为 2-lane DSI | `confirmed` / 已确认 |
| LCD reset | GPIO22 / `CONFIG_MPP_DSI_LCD_RESET_PIN=22` | `confirmed` / 已确认 |
| LCD enable/backlight | GPIO25；Linux `gpioinfo` 显示 `backlight_gpio` 已被使用 | `linux-validated` / Linux 已验证 |

参考代码确认存在名为 `RM69A10_MIPI_2LAN_568X1232_60FPS` 的 RM69A10 模式。

2026-07-17 上板验证确认：`card0-DSI-1` 为 `connected`，mode 为 `568x1232`，
`tdvp-display-smoke` 使用 `RG24` format 通过，屏幕能显示红/绿/蓝/白彩条。

Linux 影响：

- Device tree 必须描述 DSI host、RM69A10 panel、reset GPIO、panel timing 和
  power/backlight control。
- BSP display API 必须暴露逻辑 display handle 和 frame presentation，而不是 DRM/KMS 内部细节。
- 平台默认 display target 是 RM69A10 AMOLED panel。

## Touch

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| Touch controller | GT9895 | `confirmed` / 已确认 |
| Touch bus | I2C | `confirmed` / 已确认 |
| 参考 Kconfig 中的 touch I2C device | `i2c3` | `confirmed` / 已确认 |
| 参考 Kconfig 中的 touch I2C address | `0x5d` | `confirmed` / 已确认 |
| TP reset | GPIO24 | `confirmed` / 已确认 |
| TP interrupt | GPIO23 | `confirmed` / 已确认 |
| TP SCL | GPIO36 / IIC3_SCL | `confirmed` / 已确认 |
| TP SDA | GPIO37 / IIC3_SDA | `confirmed` / 已确认 |
| Linux I2C observation | `/dev/i2c-1` 上可见 address `0x5d` | `linux-observed` / Linux 已观察 |
| Linux input status | Goodix driver 已注册 `/dev/input/event0`、`mice`、`mouse0` | `linux-observed` / Linux 已观察 |

Linux 影响：

- 当前 6.6.36 baseline 使用 Goodix-compatible path 完成了 input device 注册；下一步必须用
  `evtest /dev/input/event0` 验证真实触摸坐标和方向。
- BSP input API 必须把 touch event 归一化为 platform event。
- Application 不得直接读取 evdev node。

## HDMI Bridge

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| HDMI bridge | LT9611 | `confirmed` / 已确认 |
| LT9611 display path | MIPI DSI to HDMI | `confirmed` / 已确认 |
| LT9611 DSI lane 数量 | 参考 driver 中为 4-lane DSI | `confirmed` / 已确认 |
| HDMI reset | GPIO24 / `CONFIG_MPP_DSI_HDMI_RESET_PIN=24` | `confirmed` / 已确认 |
| HDMI interrupt | GPIO22 | `confirmed` / 已确认 |
| HDMI SCL/SDA | GPIO36/GPIO37，与 touch pin 命名共享 | `confirmed` / 已确认 |
| 参考 driver 支持的模式 | 640x480p60、1280x720p50/60、1920x1080p30/60 | `confirmed` / 已确认 |

Linux 影响：

- HDMI 和 AMOLED 在板级参考中共享部分具名 GPIO/I2C 资源。除非精确的板卡焊接、
  mux 和运行时共存规则已经验证，否则把 HDMI 视为 alternate display path。
- BSP display API 应支持多个逻辑 display target，但第一次 Linux bring-up 应优先支持板载
  RM69A10 panel。

## Camera 和 CSI

参考 board config 启用了三个 CSI device 和两类 sensor。

| CSI | I2C / clock 事实 | Power/reset 事实 | 状态 |
| --- | --- | --- | --- |
| CSI0 | schematic 中 CAM0 使用 I2C0 | power GPIO9，reset GPIO10 | `confirmed` / 已确认 |
| CSI1 | schematic 中 CAM1 使用 I2C1 | power GPIO11，reset GPIO12 | `confirmed` / 已确认 |
| CSI2 | CAM2 使用 I2C4，config 中使用 MCLK1/chip clock | reset GPIO21 | `confirmed` / 已确认 |

参考中启用的 sensor：

| Sensor | 参考模式 | 状态 |
| --- | --- | --- |
| OV5647 | 2592x1944p10、1920x1080p30、1280x960p45、1280x720p60、640x480p90、10-bit | `confirmed` / 已确认 |
| GC2093 | 1920x1080p30、1920x1080p60、1280x960p90、1280x720p90、10-bit | `confirmed` / 已确认 |

参考 config 设置了 `CONFIG_CANMV_DEFAULT_SENSOR_CSI_2=y`。

Linux 影响：

- Device tree 必须描述 CSI host、sensor connector、I2C bus、reset 和 power GPIO、
  MCLK routing 以及 sensor endpoint。
- Kernel config 必须包含 V4L2/media controller infrastructure 和所选 K230 camera pipeline driver。
- BSP camera API 必须隐藏 sensor identity、`/dev/video*`、V4L2 control、media graph 细节和
  vendor MPP handle。

## SD Card

| Signal | GPIO | 状态 |
| --- | --- | --- |
| TFCARD_CMD | GPIO54 | `confirmed` / 已确认 |
| TFCARD_CLK | GPIO55 | `confirmed` / 已确认 |
| TFCARD_D0 | GPIO56 | `confirmed` / 已确认 |
| TFCARD_D1 | GPIO57 | `confirmed` / 已确认 |
| TFCARD_D2 | GPIO58 | `confirmed` / 已确认 |
| TFCARD_D3 | GPIO59 | `confirmed` / 已确认 |

Linux 影响：

- Linux device tree 必须描述用于 root filesystem 和 boot media 的 SD/MMC controller。
- 生成的镜像必须能以 `sysimage-sdcard.img` 的形式烧录。

## UART 和 Console

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| U-Boot env console | `ttyS0,115200` | `confirmed` / 已确认 |
| Schematic UART0 pins | GPIO38/GPIO39 | `confirmed` / 已确认 |
| Schematic UART3 pins | 存在 | `confirmed` / 已确认 |
| 通用 CanMV 说明 | dual-core 参考栈中可能存在两个 serial console | `reference-only` / 仅供参考 |

Linux 影响：

- Linux console 默认应从 `console=ttyS0,115200` 开始。
- 任何第二串口都必须先当作板级特性处理，直到在 T-Display K230 硬件上验证。

## WiFi

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| WiFi 芯片 | RTL8189FTV | `confirmed` / 已确认 |
| WiFi 总线 | SDIO | `confirmed` / 已确认 |
| 参考 SDIO device | `REALTEK_SDIO_DEV=0` / SDIO0 | `confirmed` / 已确认 |
| Linux 启动观察 | `mmc0` 枚举出 SDIO card；`mmc1` 是 SD card/rootfs 路径 | `confirmed` / 已确认 |
| Linux network interface | `8189fs` 自动加载后出现 `wlan0` 和 `wlan1` | `linux-observed` / Linux 已观察 |
| Linux scan | `iw dev wlan0 scan` 能扫描到周边 AP | `linux-validated` / Linux 已验证 |

Linux 影响：

- 第一版 Linux image 启用 `mmc_sd0` 作为 RTL8189FTV SDIO 设备通道，并保留
  `mmc_sd1` 作为 SD-card/rootfs controller。
- Buildroot 使用标准 `rtl8189fs` kernel-module package，并安装 `iw`、
  `wireless-regdb`、`wireless_tools`、`wpa_supplicant` 和 `iproute2`
  作为验证工具。
- 当前已通过 interface enumeration 和 scan；AP association、DHCP 和稳定吞吐仍需单独验收。
- Application 不能直接调用 wireless ioctl。后续网络管理必须由 TDVP system service 或
  BSP-level network abstraction 拥有。

## USB Ethernet

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| 外接 USB Ethernet | Realtek USB 10/100 LAN | `linux-validated` / Linux 已验证 |
| Linux driver | `r8152` | `linux-validated` / Linux 已验证 |
| Network interface | `eth0` | `linux-validated` / Linux 已验证 |
| DHCP | 从 `192.168.31.1` 获取 `192.168.31.86/24` | `linux-validated` / Linux 已验证 |
| Ping | device->gateway、host->device、device->host 均通过 | `linux-validated` / Linux 已验证 |

Linux 影响：

- USB Ethernet 是当前 bring-up 阶段最稳定的网络控制面。`be9c30f3...` 镜像会自动对
  `eth0` 执行 DHCP，并启用 Dropbear SSH。
- 这不意味着平台依赖外接网卡；它是硬件实验和数据采集入口。
- Application 仍不能直接依赖 Linux network device 名称。后续网络能力应通过 TDVP system
  service 或 BSP-level abstraction 暴露。

## Keyboard

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| Keyboard controller | TCA8418 | `confirmed` / 已确认 |
| LilyGO demo 中的 keyboard I2C bus | `/dev/i2c4` | `confirmed` / 已确认 |
| Keyboard I2C address | `0x34` | `confirmed` / 已确认 |
| Keyboard reset pin | board pin/GPIO 43 | `confirmed` / 已确认 |
| Keyboard SCL/SDA | demo 中 `fpioa_set_function(46, IIC4_SCL)` / `fpioa_set_function(47, IIC4_SDA)` | `confirmed` / 已确认 |
| Keyboard module | 可拆卸；2026-07-17 的失败日志是在模块已安装时采集 | `confirmed-by-test-context` / 测试上下文确认 |
| Keyboard matrix | demo init sequence 中为 7 rows x 10 columns | `confirmed` / 已确认 |
| Keyboard backlight | PWM4 on board pin 52 | `confirmed` / 已确认 |
| Caps indicator helper | XL9555-compatible access at I2C address `0x20` | `needs-validation` / 需验证 |
| Linux IRQ line | reference source 中尚未找到 | `missing-fact` / 缺事实 |

Linux 影响：

- 第一版 Linux image 启用 I2C4，并安装 `tdvp-keyboard-smoke`。该工具按 LilyGO
  polling model 通过 Linux `i2c-dev` 检测和读取 TCA8418，并可按 demo 对 GPIO43 执行
  high/low/high reset。
- 2026-07-17 上板观察：Linux 只枚举 `/dev/i2c-0` 和 `/dev/i2c-1`；其中 `/dev/i2c-0`
  对应 DTS alias `i2c0 = &i2c4`，但 address `0x34` 尚未 ACK。由于测试时键盘模块已安装，
  不能再把未安装模块作为主要解释。
- 当前加入 `tdvp-k230-iomux keyboard` 作为 validation-only 工具，按 LilyGO `rt_fpioa.c`
  的寄存器写法把 IO43 设为 GPIO、IO46/IO47 设为 I2C4。
- 最新日志已确认 IO46/47 readback 为 I2C4 alt3、`cfg=0x18f`，所以“未执行 I2C4
  IOMUX”不再是主因。`c6808945...` 镜像进一步把 IO43 reset IOMUX 对齐到 vendor
  GPIO 默认 `0x18f`，并让 smoke tool 使用已验证的 `/dev/gpiochip1 line 11`
  reset path；复测后 `0x34` 仍然不 ACK。
- 下一层应验证 TCA8418 电源、I2C 上拉、GPIO43 物理连通和可拆卸键盘接口。后续应把
  稳定配置替换为 kernel pinctrl/DTS。
- Upstream Linux `tca8418_keypad` driver 需要 IRQ line。只有在 IRQ 被验证，或选定
  polling-capable driver 策略后，才加入稳定 Linux input DTS node。
- Application 最终只能消费归一化后的 TDVP input events，不能直接依赖 raw I2C 或 evdev
  internals。

## LED

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| schematic 中发现的 LED signal | `LEDBANK2_GPIO35` | `confirmed` / 已确认 |
| Linux GPIO number/pinctrl 映射 | 待定 | `needs-validation` / 需验证 |

Linux 影响：

- 只有在 pinctrl 和 GPIO bank numbering 验证后再加入 LED 支持。
- LED 应通过 BSP diagnostic 或仅调试用途的系统工具暴露，不应成为 application dependency。

## 额外板载能力

参考仓库和 datasheet 提到了额外硬件能力。这些能力可能取决于 board revision、焊接选项或外接模块是否实际存在：

| 领域 | 组件/参考 | 状态 |
| --- | --- | --- |
| LoRa | HPDTEK/SX1262 family，后续 README 提到 LR2021 | `needs-validation` / 需验证 |
| BLE | nRF52840 | `needs-validation` / 需验证 |
| LTE-M/GNSS | nRF9151 | `needs-validation` / 需验证 |
| Keyboard | TCA8418；I2C4 address `0x34`；reset pin 43；PWM4 backlight | `partial-linux-bringup` / 部分进入 Linux bring-up |
| USB Ethernet | 插入的 Realtek USB 10/100 LAN 通过 `r8152` 枚举为 `eth0`；DHCP 和 ping 已通过 | `linux-validated-control-plane` / Linux 控制面已验证 |
| Battery/charger | BQ25896、BQ27220 | `needs-validation` / 需验证 |
| WiFi | RTL8189FTV on SDIO0 | `first-linux-bringup` / 进入第一轮 Linux bring-up |
| Sensor/environment | V1.3 notes 提到 AHT20 | `needs-validation` / 需验证 |
| Fan/speaker | V1.3 notes 提到 | `needs-validation` / 需验证 |

Linux 影响：

- 当目标 board revision 上实际存在这些能力时，它们属于 T-Display K230 平台范围。
- 不能因为平台是 vision-first，就忽略这些能力。
- 在 pins、buses、power rails 和 board revisions 验证后，它们必须进入 Linux/device-tree/BSP 规划。
- Bring-up 可以分波次执行，但执行顺序不是 scope 排除。

## Boot Straps

| 项目 | 事实 | 状态 |
| --- | --- | --- |
| BOOT0 | GPIO0 | `confirmed` / 已确认 |
| BOOT1 | GPIO1 | `confirmed` / 已确认 |
| BOOT0=0, BOOT1=0 | schematic note 中为 SPI NOR mode | `confirmed` / 已确认 |
| BOOT0=1, BOOT1=0 | schematic note 中为 SPI NAND mode | `confirmed` / 已确认 |

Linux 影响：

- 交付工作流仍然使用 SD-card image，但 boot ROM strap 行为必须在板级 bring-up 时检查。
- 不要假设“烧 SD 卡”就完整描述了 first-stage boot source。

## 硬件完整 Bring-Up 范围

第一版可启动 Buildroot Linux image 可以分波次验证硬件，但平台目标是 hardware-complete board baseline。
这里的“最小”指最小软件复杂度，不是最少硬件覆盖。

Wave A：boot-critical baseline：

1. `ttyS0,115200` UART console。
2. SD-card boot/rootfs。
3. GPIO/pinctrl。
4. I2C/SPI/MMC foundations。
5. deterministic BusyBox userspace。

Wave B：interactive and vision baseline：

1. RM69A10 AMOLED panel。
2. GT9895 touch input。
3. RTL8189FTV WiFi on SDIO0。
4. TCA8418 keyboard detection and polling validation on I2C4。
5. 一条 camera path，优先 CSI2，因为它是参考默认路径。
6. 足够支持 camera to display streaming 的 DMA/CMA memory。
7. GPIO numbering 验证后的 LED diagnostic。

Wave C：full board capability baseline：

1. LT9611 HDMI，在 DSI/I2C/GPIO 共存关系验证后支持。
2. LoRa，在精确 module 和 bus wiring 验证后支持。
3. BLE。
4. LTE-M/GNSS。
5. Battery、charger 和 fuel gauge。
6. Audio/speaker。
7. Fan。
8. 如果实际焊接，则支持 AHT20 或其他 environmental sensor。

这些 wave 只描述验证顺序。它们不把任何实际存在的板载能力移出平台范围。
