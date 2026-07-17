# T-Display K230 Bring-Up 验证

本文定义 Buildroot Linux system 成为 T-Display K230 platform baseline 前必须证明的事项。

英文版本：

- [validation.md](validation.md)

## 验证规则

不能因为 raw Linux device node 存在，就认为一个功能已被接受。

成功意味着：

1. 所选 K230 Linux baseline 可以驱动硬件；
2. TDVP BSP 可以抽象该硬件；
3. runtime 可以使用 BSP path；
4. applications 不依赖 Linux 或 vendor internals。

## Phase 0：可复现输入

| Check | Pass condition |
| --- | --- |
| Buildroot | official submodule 已固定 |
| SDK baseline | exact `k230_linux_sdk` commit 已固定 |
| Kernel | exact external kernel commit 已固定 |
| OpenSBI/U-Boot | exact source 或 binary provenance 已固定 |
| Defconfig | build 从 Git-tracked TDVP defconfig 开始 |
| Rootfs | target files 来自 Buildroot packages 或 tracked overlay |
| Image | `sysimage-sdcard.img` 从 tracked/pinned inputs 生成 |

### 当前 Phase 0 记录

2026-07-17 WSL ext4 构建状态：

| Item | Result |
| --- | --- |
| TDVP WSL workspace | `/home/vicliu/projects/t-display-k230-vision-platform` |
| SDK workspace | `/home/vicliu/projects/k230_linux_sdk` |
| SDK commit | `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7` |
| SDK minimal targets | `opensbi` 和 `uboot` 已构建 |
| Boot artifacts | 已生成并作为本地 ignored files 导入 |
| Buildroot defconfig | 已被接受 |
| Full TDVP Buildroot build | passed；2026-07-17 `linux-dirclean` 后重建通过，并应用 Canaan DRM RGB888 fbdev 和 no-vblank console patch |
| Validation tools | 已安装 `libdrm`、`modetest`、`tdvp-display-smoke`、`tdvp-keyboard-smoke`、`tdvp-k230-iomux`、Dropbear、`v4l2-ctl`、`media-ctl`、ALSA tools、SPI tools、network diagnostics、`wireless-regdb`、WiFi/I2C/GPIO tools |
| Generated image | `output/t_display_k230_vision/images/sysimage-sdcard.img` |
| Image size | `268455936` bytes，约 256 MiB |
| Image SHA256 | `be9c30f3ca7004c375970dae48f26aea5cb802e159645a660183153b4ed24c27` |
| Hardware validation | Phase 1、Phase 2A、Phase 2B 和 Phase 2C 已在板上通过；USB Ethernet DHCP/SSH control plane、WiFi scan、Goodix input device、24bpp fbdev/fbcon、no-vblank DRM commits 和屏幕 console 输出已验证；keyboard TCA8418 仍待 ACK |

这条记录只证明本地 WSL ext4 workspace 上的构建可复现。它不证明显示、摄像头或 runtime
已经验收。

## Phase 1：Boot And Console

| Check | Pass condition |
| --- | --- |
| SD flash | board 从 generated SD image 启动 |
| Boot chain | SPL/U-Boot/OpenSBI 到达 K230 big-core Linux |
| UART | Linux console 出现在所选 serial port |
| Init | BusyBox init 运行 platform startup scripts |
| Rootfs | expected root filesystem 已挂载 |
| Reboot | reboot 回到同一状态 |

### 当前 Phase 1 记录

2026-07-16，generated SD image 在板上的启动状态：

| Item | Result |
| --- | --- |
| Kernel | `Linux tdisplay-k230 6.6.36 riscv64` |
| Command line | `root=/dev/mmcblk1p2 ... console=ttyS0,115200 earlycon=sbi` |
| Rootfs | `/dev/root` 以 ext4 read/write 挂载 |
| Partitions | `mmcblk1p1` boot partition 和 `mmcblk1p2` rootfs |
| Console | 到达 BusyBox login prompt；root shell 可用 |
| Device tree model | `LILYGO T-Display K230` |
| DRM node | `/dev/dri/card0` 存在 |
| DSI connector | `/sys/class/drm/card0-DSI-1` 存在 |
| Touch input | Goodix driver 已注册 `/dev/input/event0`；尚未通过物理触摸事件验收 |

Phase 1 对 boot、rootfs、serial console 和 T-Display board identity 已接受。
旧 Phase 1 记录使用的是 CanMV LCD DTB；新的接受条件必须以 `/proc/device-tree/model`
报告 `LILYGO T-Display K230` 为准。

## Phase 1A：Debug Control Plane

这个阶段的目标不是把板子做成通用 Linux 服务器，而是为后续硬件实验建立可重复的控制面：

```text
serial console -> recover/debug
eth0 DHCP      -> SSH/control/data collection
```

目标板命令：

```sh
ip link set eth0 up
udhcpc -i eth0 -n -q -t 5
ip addr show eth0
ping -c 3 192.168.31.1
```

主机侧命令：

```sh
ping 192.168.31.86
ssh root@192.168.31.86
```

### 当前 Phase 1A 记录

2026-07-17，上板测试显示：

| Item | Result |
| --- | --- |
| Serial control | Windows `COM56`，115200 8N1，可进入 root shell |
| Ethernet driver | Realtek USB 10/100 LAN 使用 `r8152`，interface 为 `eth0` |
| DHCP | `eth0` 从 `192.168.31.1` 获取 `192.168.31.86/24` |
| Device to gateway | `ping 192.168.31.1` 通过 |
| Host to device | Windows 主机可 ping `192.168.31.86` |
| Device to host | 设备可 ping Windows 主机 `192.168.31.210` |
| Historical image limitation | 旧的 `c6808945...` 镜像没有 Dropbear/OpenSSH/telnetd/netcat，只能通过串口控制 |
| Generated image policy | 当前 `be9c30f3...` 镜像自动启动 `eth0` DHCP，启用 Dropbear，root 密码固定为 `tdvp` |

Phase 1A 已接受到链路和 IP 层。下一张镜像烧录后，优先通过串口确认 `eth0`
地址，然后使用 `ssh root@<eth0-ip>` 接管设备。这个 SSH 入口只作为 bring-up/debug
控制面，不改变 application API 边界。

## Phase 2：Core Hardware

| Hardware | Pass condition |
| --- | --- |
| SD/MMC | rootfs 在预期访问下保持稳定 |
| GPIO/pinctrl | reset、power、interrupt 和 LED lines 由 drivers 控制 |
| I2C | touch、camera 和 power devices 启用时可 probe |
| DMA/CMA | camera/display/AI buffer allocation 稳定 |

## Phase 2A：Display Smoke

Phase 2A 是板级 bring-up 步骤。因为 BSP display API 还不存在，所以这里允许 validation-only
工具直接使用 DRM/KMS。这个例外只限 Buildroot 安装的验证工具，不能扩散到 application API。

目标板命令：

```sh
cat /proc/device-tree/model
cat /sys/class/drm/card0-DSI-1/status
cat /sys/class/drm/card0-DSI-1/modes
modetest -M canaan-drm
tdvp-display-smoke --device /dev/dri/card0 --seconds 5
```

通过条件：

- `card0-DSI-1` 报告可用 mode。
- mode 对应 RM69A10 panel，第一轮预期尺寸是 `568x1232`。
- `modetest` 能列出 Canaan DRM connector、CRTC、plane 和 supported formats。
- `tdvp-display-smoke` 打印 `PASS`。
- 板载 RM69A10 panel 在测试窗口内显示彩条。

### 当前 Phase 2A 记录

2026-07-17，上板测试显示：

| Item | Result |
| --- | --- |
| Board model | `LILYGO T-Display K230` |
| Connector status | `card0-DSI-1` 为 `connected` |
| Mode | `568x1232` |
| Smoke tool | `tdvp-display-smoke --device /dev/dri/card0 --seconds 5` |
| Format | `RG24` |
| Result | `PASS`，屏幕显示红/绿/蓝/白彩条 |

Phase 2A 已接受。旧镜像中 kernel fbdev emulation 会打印 `bpp/depth value of 32/24 not supported`
和 `fbdev: Failed to setup generic emulation`，但 DRM/KMS plane path 已可显示。临时的
`XRGB8888`/32bpp 实验可以创建 `/dev/fb0`，但屏幕保持紫色，并且 DRM commits 会等待不存在的
vblank event。最终接受路径是 Phase 2B/2C 验证通过的 RGB888/24bpp fbdev 加 no-vblank/fake-vblank
DRM commit 修复。

失败处理：

- 如果 `modetest` 不能列出 planes，检查 DRM/KMS client capabilities 和 kernel config。
- 如果 connector 存在但没有 usable mode，检查 DTS panel timing、DSI lane count、reset 和
  power sequencing。
- 如果 format negotiation 失败，把 plane formats 与 K230 SDK display examples 对比，并调整
  smoke-test candidate order。
- 如果彩条能显示但红蓝互换，display path 可以先接受，同时把 byte-order detail 记录给后续 BSP
  实现。

## Phase 2B：fbdev/fbcon Bring-Up

这个阶段的目标是让内核能基于 Canaan DRM 创建 generic fbdev，从而具备传统 Linux 屏幕 console
的基础条件。它不替代最终 BSP display API，只服务于启动可见性和板级调试。

2026-07-17 已接受的构建变更：

| Item | Result |
| --- | --- |
| Kernel patch | `0002-drm-canaan-use-rgb888-fbdev-format.patch` |
| Driver path | `drivers/gpu/drm/canaan/canaan_drv.c`、`drivers/gpu/drm/canaan/canaan_crtc.c` |
| Change | generic fbdev 请求 24bpp RGB888；T-Display DSI path 不再注册不可靠的 DRM vblank，而是让 atomic helpers 使用 fake vblank events |
| Generated image | `output/t_display_k230_vision/images/sysimage-sdcard.img` |
| Image SHA256 | `be9c30f3ca7004c375970dae48f26aea5cb802e159645a660183153b4ed24c27` |
| Board status | boot artifacts 已通过 SSH 复制到运行中 SD 卡的 boot 分区，重启后完成验证 |

目标板命令：

```sh
dmesg | grep -Ei 'canaan-drm|fbdev|fbcon|fb0|Console'
ls -l /dev/fb0 /sys/class/graphics/fb0 2>/dev/null || true
cat /proc/consoles
modetest -M canaan-drm | grep -Ei 'RG24|RGB888|AR24|ARGB8888'
```

通过条件：

- `dmesg` 不再出现 `No compatible format found` 和 `fbdev: Failed to setup generic emulation`。
- `/dev/fb0` 和 `/sys/class/graphics/fb0` 存在。
- `modetest` 能看到 primary OSD plane 支持 `RG24`。
- 如需屏幕显示启动日志，还必须在 bootargs 中加入 `console=tty0`，并保留 `console=ttyS0,115200`
  作为 recovery console。
- 如需屏幕最终出现 login shell，还必须让 rootfs 在 `tty1` 启动 getty，并验证可用输入设备
  （USB HID keyboard 或修复后的 TCA8418 keyboard）。

### 当前 Phase 2B 记录

2026-07-17，上板测试显示：

| Item | Result |
| --- | --- |
| Kernel | `Linux tdisplay-k230 6.6.36 #2 SMP Fri Jul 17 17:39:25 CST 2026 riscv64` |
| fbdev error check | `No compatible format`、`Failed to setup generic emulation`、`bpp/depth` 均未再出现 |
| fbdev node | `/dev/fb0` 存在 |
| fbdev sysfs | `/sys/class/graphics/fb0` 存在 |
| fbdev name | `canaan-drmdrmfb` |
| fbdev mode | `U:568x1232p-0` |
| fbdev virtual size | `568,1232` |
| fbdev bpp | `24` |
| fbdev stride | `1704` |
| fbcon log | `Console: switching to colour frame buffer device 71x77` |
| DRM connector | `card0-DSI-1` 为 `connected`，mode 为 `568x1232` |
| DRM commit behavior | `modetest` 可正常退出；`tdvp-display-smoke --device /dev/dri/card0 --seconds 5` 以 `RG24` 返回 `PASS`；未再出现 `vblank wait timed out`、`flip_done timed out` 或 `commit wait timed out` |
| Network control | 重启后 `eth0` DHCP 获取 `192.168.31.155/24`，Dropbear SSH 已可用 |

Phase 2B 已接受到 fbdev/fbcon 基础能力。此前的 32bpp `XRGB8888` 实验不接受为最终方案：
它会让屏幕保持紫色，并依赖错误的 alpha 解释。当前接受的 console baseline 是
24bpp RGB888 加 DRM fake-vblank completion。

## Phase 2C：Screen Console And Login

这个阶段的目标是让板载屏幕从“有 framebuffer”推进到“Linux 启动日志可见，并最终有 login shell”。
它仍是系统 bring-up 能力，不代表 application 可以直接依赖 fbdev、VT 或 evdev。

2026-07-17 构建变更：

| Item | Result |
| --- | --- |
| U-Boot env source | `buildroot/board/t-display-k230/uboot-linux.env` |
| bootargs | `root=/dev/mmcblk1p2 loglevel=8 rw rootdelay=4 rootfstype=ext4 console=tty0 console=ttyS0,115200 earlycon=sbi consoleblank=0` |
| Rootfs post-build | `buildroot/board/t-display-k230/post-build.sh` |
| Added getty | `tty1::respawn:/sbin/getty -L tty1 0 vt100` |
| Kernel console config | `CONFIG_VT=y`、`CONFIG_VT_CONSOLE=y`、`CONFIG_FRAMEBUFFER_CONSOLE=y`、`CONFIG_DRM_FBDEV_EMULATION=y` |
| USB keyboard config | `CONFIG_HID_GENERIC=y`、`CONFIG_USB_HID=y` |
| Generated image | `output/t_display_k230_vision/images/sysimage-sdcard.img` |
| Image SHA256 | `be9c30f3ca7004c375970dae48f26aea5cb802e159645a660183153b4ed24c27` |
| Board status | 通过 live boot-partition update 和 reboot 完成验证 |

目标板命令：

```sh
cat /proc/cmdline
cat /proc/consoles
ps | grep -E '[g]etty.*tty1|[g]etty.*ttyS0'
dmesg | grep -Ei 'Console:|fb0|fbcon|tty0|tty1|usbhid|hid-generic'
ls -l /dev/tty0 /dev/tty1 /dev/fb0 /dev/input/event*
```

通过条件：

- `/proc/cmdline` 包含 `console=tty0` 和 `console=ttyS0,115200`。
- `/proc/consoles` 同时包含 screen console 和 serial console。
- BusyBox init 同时 respawn `ttyS0` getty 和 `tty1` getty。
- 屏幕上能看到 kernel log 或最终 login prompt。
- 插入 USB keyboard 时，内核产生 HID/input event，并能在 `tty1` login。

### 当前 Phase 2C 记录

2026-07-17，上板测试显示：

| Item | Result |
| --- | --- |
| Upgrade method | 通过 SSH 把新的 `Image` 和 `tdisplay-k230.dtb` 复制到 `mmcblk1p1`，随后重启 |
| SSH address before reboot | `192.168.31.106` |
| SSH address after reboot | `192.168.31.155` |
| Command line | 包含 `console=tty0`、`console=ttyS0,115200` 和 `consoleblank=0` |
| Consoles | `/proc/consoles` 同时列出 `ttyS0` 和 `tty0` |
| Getty | BusyBox getty 运行在 `ttyS0` 和 `tty1` |
| fbdev | `/sys/class/graphics/fb0/bits_per_pixel` 为 `24` |
| DRM/KMS | `modetest -M canaan-drm` 可正常退出 |
| Display smoke | `tdvp-display-smoke --device /dev/dri/card0 --seconds 5` 打印 `PASS plane=33 format=RG24` |
| Console write | SSH 已向 `/dev/tty1` 和 `/dev/tty0` 写入可读状态文字，过程中没有 DRM 错误 |
| DRM errors | no-vblank 修复后未再出现 `vblank wait timed out`、`flip_done timed out` 或 `commit wait timed out` |

Phase 2C 已接受为 boot-visible screen console plumbing。要从板载屏幕完成物理 login
仍依赖可用键盘路径；USB HID 已启用，可拆卸 TCA8418 keyboard 仍在 Phase 3B 继续验证。

## Phase 3：Display And Input

| Check | Pass condition |
| --- | --- |
| RM69A10 | panel 初始化并显示 test frame |
| Touch | GT9895 产生 normalized BSP input events |
| Abstraction | test app 不直接打开 DRM/KMS 或 evdev |

LT9611 HDMI 在 onboard panel path 稳定后继续保持范围内。

## Phase 3A：WiFi Smoke

WiFi 属于当前 bring-up 集合，因为板级参考和启动观察都指向 RTL8189FTV on SDIO0。

目标板命令：

```sh
dmesg | grep -Ei 'mmc0|8189|rtl|cfg80211|mac80211|wlan'
lsmod | grep -Ei '8189|cfg80211|mac80211'
ip link
iw dev || true
modprobe 8189fs || modprobe rtl8189fs || true
wpa_passphrase '<ssid>' '<passphrase>' > /tmp/wpa.conf
wpa_supplicant -B -i wlan0 -c /tmp/wpa.conf
udhcpc -i wlan0
ip addr show wlan0
```

通过条件：

- `mmc0` 仍是 SDIO device，`mmc1` 仍是 SD-card/rootfs device。
- Realtek module 已加载或可以加载。
- 出现 `wlan*` network interface。
- 可以关联 AP 并获取 IP address。

### 当前 Phase 3A 记录

2026-07-17，上板测试显示：

| Item | Result |
| --- | --- |
| SDIO controller | `mmc0` 枚举出 high speed SDIO card |
| Rootfs controller | `mmc1` 枚举出 SDHC card 和 `mmcblk1p1/p2` |
| Module load | `modprobe 8189fs` 成功加载 out-of-tree module |
| Interfaces | `wlan0` 和 `wlan1` 出现 |
| `iw dev` | 两个 managed interfaces 可见 |
| USB Ethernet | 插入的 Realtek USB 10/100 LAN 枚举为 `eth0`，driver 为 `r8152` |
| USB Ethernet DHCP | `eth0` 获取 `192.168.31.86/24`，网关为 `192.168.31.1` |
| USB Ethernet ping | device->gateway、host->device、device->host 均通过 |
| WiFi scan | `iw dev wlan0 scan` 能扫描到周边 AP |

Phase 3A 已通过到 WiFi interface enumeration 和 scan。AP association、DHCP 和稳定吞吐仍需单独测试。
USB Ethernet 已通过链路、DHCP 和 ping，可作为后续上板实验的主控制面；吞吐和长期稳定性仍需
`iperf3`/长时间 ping 测试。

## Phase 3B：Keyboard Smoke

LilyGO demo 证明 TCA8418 通过 I2C4 address `0x34` 轮询。Upstream Linux input
driver 仍需要 verified IRQ line，所以本阶段使用 TDVP validation tool。

目标板命令：

```sh
i2cdetect -l
i2cdetect -y 0
i2cdetect -y 1
tdvp-k230-iomux dump 43 46 47
tdvp-k230-iomux keyboard
tdvp-k230-iomux dump 43 46 47
tdvp-keyboard-smoke --reset-gpio 43 --bus /dev/i2c-0 --seconds 10
```

通过条件：

- `tdvp-keyboard-smoke` 在 address `0x34` 检测到 TCA8418-compatible device。
- polling window 内按键会打印 key press/release events。
- 这个结果只作为 board hardware proof，不作为最终 application input API。

### 当前 Phase 3B 记录

2026-07-17，上板测试显示：

| Item | Result |
| --- | --- |
| Linux I2C adapters | `/dev/i2c-0` 和 `/dev/i2c-1` |
| Expected keyboard bus | `/dev/i2c-0`，对应 DTS alias `i2c0 = &i2c4` |
| TCA8418 address | `0x34` 未 ACK |
| Touch address cross-check | `/dev/i2c-1` 上 `0x5d` 可见 |
| Keyboard module population | 本次日志来自已安装键盘模块的板卡；不能再把未插模块作为主要解释 |
| IOMUX preset | `tdvp-k230-iomux keyboard` 已执行；IO46/IO47 readback 为 I2C4 alt3、`cfg=0x18f` |
| GPIO43 reset path | `tdvp-keyboard-smoke` 已能通过 `/dev/gpiochip1 line 11` pulse GPIO43 |
| Keyboard smoke | 未检测到 TCA8418-compatible device |

LilyGO demo 还执行 `fpioa_set_function(46, IIC4_SCL)` 和
`fpioa_set_function(47, IIC4_SDA)`，并通过 GPIO43 执行 high/low/high reset。
当前日志已经排除了“未执行 I2C4 IOMUX”和“GPIO43 reset path 不可用”这两个主因：
`c6808945...` 镜像已把 IO43 reset IOMUX 调整为 vendor FPIOA helper 的 GPIO 默认
`0x18f`，并且 smoke tool 已能通过 `/dev/gpiochip1 line 11` pulse reset，但
`0x34` 仍然不 ACK。下一层应集中验证 TCA8418 电源、I2C 上拉、reset 物理连通和可拆卸
键盘接口。长期方案仍应迁移到 kernel pinctrl 和 DTS。

## Phase 3C：Board Capability Probe

参考源码还提到 battery/fuel gauge、LR2021、GNSS/LTE、fan、speaker 和 AHT20。它们仍在
范围内，但第一步必须先确认 bus/address/pin。

目标板命令：

```sh
i2cdetect -l
for bus in /dev/i2c-*; do echo "== $bus =="; i2cdetect -y "${bus##*-}"; done
gpiodetect || true
gpioinfo || true
dmesg | grep -Ei 'bq27|bq258|aht|pwm|gpio|i2c|spi|sound|audio'
```

通过条件：

- 加 DTS node 前先记录 candidate I2C addresses。
- 启用 fan、keyboard backlight 或 indicator LEDs 前先搞清 GPIO/PWM ownership。
- Audio/speaker 只有通过真实 playback test 后才能接受。

## Phase 4：Camera

| Check | Pass condition |
| --- | --- |
| Sensor | first selected sensor probe 成功 |
| Capture | BSP `camera_read` 返回 frames |
| Mode | 扩展 modes 前先接受一个 stable mode |
| Memory | continuous capture 不泄漏 buffers |
| Abstraction | test app 不直接调用 V4L2/ioctl |

## Phase 5：Runtime And AI

| Check | Pass condition |
| --- | --- |
| Pipeline | camera -> preprocess -> optional AI -> overlay -> display 运行 |
| Buffers | ownership 确定 |
| Event loop | camera、input、AI 和 display scheduling 由 runtime 拥有 |
| AI | 一个 model 通过稳定 TDVP API 加载并运行 |
| Shutdown | runtime 退出后硬件不处于未知状态 |

## Phase 6：Additional Board Capabilities

实际存在的其他板载能力仍在范围内：

- LoRa；
- BLE；
- LTE-M/GNSS；
- WiFi；
- keyboard；
- battery/charger/fuel gauge；
- audio/speaker；
- fan；
- AHT20/environment sensor。

这些能力只有通过 BSP/system service APIs 才算接受，不能通过 application-level GPIO、SPI、
I2C、ALSA 或 ioctl access 接受。
