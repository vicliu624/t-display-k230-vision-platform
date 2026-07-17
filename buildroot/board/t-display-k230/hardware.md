# T-Display K230 Hardware Facts

This file summarizes the hardware facts extracted from the LilyGO
`T-Display-K230_canmv_rt` repository and maps them to Linux/Buildroot design
needs.

## Board Identity

| Item | Fact | Status |
| --- | --- | --- |
| Board/project name | `T-Display-K230_canmv_rt` | confirmed |
| SoC | Kendryte/Canaan K230 | confirmed |
| CPU architecture | RISC-V | confirmed |
| Memory | 1 GiB LPDDR | confirmed |
| Primary removable storage | SD card | confirmed |
| Primary product purpose | K230 development board with AMOLED display | confirmed |
| Platform target in this repo | minimal Linux vision platform SDK | linux-decision |

## Display

| Item | Fact | Status |
| --- | --- | --- |
| Main display type | AMOLED | confirmed |
| Main display size | 4.1 inch | confirmed |
| Main display resolution | 568 x 1232 | confirmed |
| Main display controller | RM69A10 | confirmed |
| Main display interface | MIPI DSI | confirmed |
| RM69A10 lane count | 2-lane DSI in reference driver | confirmed |
| LCD reset | GPIO22 / `CONFIG_MPP_DSI_LCD_RESET_PIN=22` | confirmed |
| LCD enable/backlight | GPIO25; Linux `gpioinfo` shows `backlight_gpio` in use | linux-validated |

Reference code confirms an RM69A10 mode named
`RM69A10_MIPI_2LAN_568X1232_60FPS`.

2026-07-17 board validation confirmed `card0-DSI-1` is `connected`, the mode is
`568x1232`, and `tdvp-display-smoke` passed with the `RG24` format while the
panel showed red/green/blue/white bars.

Linux implication:

- Device tree must describe the DSI host, RM69A10 panel, reset GPIO, panel
  timing, and power/backlight control.
- BSP display API must expose logical display handles and frame presentation,
  not DRM/KMS internals.
- The platform default display target is the RM69A10 AMOLED panel.

## Touch

| Item | Fact | Status |
| --- | --- | --- |
| Touch controller | GT9895 | confirmed |
| Touch bus | I2C | confirmed |
| Touch I2C device in reference Kconfig | `i2c3` | confirmed |
| Touch I2C address in reference Kconfig | `0x5d` | confirmed |
| TP reset | GPIO24 | confirmed |
| TP interrupt | GPIO23 | confirmed |
| TP SCL | GPIO36 / IIC3_SCL | confirmed |
| TP SDA | GPIO37 / IIC3_SDA | confirmed |
| Linux I2C observation | address `0x5d` is visible on `/dev/i2c-1` | linux-observed |
| Linux input status | Goodix registered `/dev/input/event0`, `mice`, and `mouse0` | linux-observed |

Linux implication:

- The current 6.6.36 baseline registers an input device through the
  Goodix-compatible path. Next, validate physical touch coordinates and
  orientation with `evtest /dev/input/event0`.
- BSP input API must normalize touch events into platform events.
- Applications must not read evdev nodes directly.

## HDMI Bridge

| Item | Fact | Status |
| --- | --- | --- |
| HDMI bridge | LT9611 | confirmed |
| LT9611 display path | MIPI DSI to HDMI | confirmed |
| LT9611 DSI lane count | 4-lane DSI in reference driver | confirmed |
| HDMI reset | GPIO24 / `CONFIG_MPP_DSI_HDMI_RESET_PIN=24` | confirmed |
| HDMI interrupt | GPIO22 | confirmed |
| HDMI SCL/SDA | GPIO36/GPIO37 shared naming with touch pins | confirmed |
| Supported modes in reference driver | 640x480p60, 1280x720p50/60, 1920x1080p30/60 | confirmed |

Linux implication:

- HDMI and AMOLED share some named GPIO/I2C resources in the board reference.
  Treat HDMI as an alternate display path until the exact board population,
  muxing, and runtime coexistence rules are validated.
- The BSP display API should support multiple logical display targets, but the
  first Linux bring-up should prioritize the onboard RM69A10 panel.

## Camera and CSI

Reference board config enables three CSI devices and two sensor families.

| CSI | I2C / clock facts | Power/reset facts | Status |
| --- | --- | --- | --- |
| CSI0 | CAM0 I2C0 in schematic | power GPIO9, reset GPIO10 | confirmed |
| CSI1 | CAM1 I2C1 in schematic | power GPIO11, reset GPIO12 | confirmed |
| CSI2 | CAM2 I2C4, MCLK1/chip clock usage in config | reset GPIO21 | confirmed |

Enabled reference sensors:

| Sensor | Reference modes | Status |
| --- | --- | --- |
| OV5647 | 2592x1944p10, 1920x1080p30, 1280x960p45, 1280x720p60, 640x480p90, 10-bit | confirmed |
| GC2093 | 1920x1080p30, 1920x1080p60, 1280x960p90, 1280x720p90, 10-bit | confirmed |

The reference config sets `CONFIG_CANMV_DEFAULT_SENSOR_CSI_2=y`.

Linux implication:

- Device tree must describe CSI hosts, sensor connectors, I2C buses, reset and
  power GPIOs, MCLK routing, and sensor endpoints.
- Kernel config must include V4L2/media controller infrastructure and the
  selected K230 camera pipeline driver.
- BSP camera API must hide sensor identity, `/dev/video*`, V4L2 controls, media
  graph details, and any vendor MPP handle.

## SD Card

| Signal | GPIO | Status |
| --- | --- | --- |
| TFCARD_CMD | GPIO54 | confirmed |
| TFCARD_CLK | GPIO55 | confirmed |
| TFCARD_D0 | GPIO56 | confirmed |
| TFCARD_D1 | GPIO57 | confirmed |
| TFCARD_D2 | GPIO58 | confirmed |
| TFCARD_D3 | GPIO59 | confirmed |

Linux implication:

- The Linux device tree must describe the SD/MMC controller used for the root
  filesystem and boot media.
- The generated image must be flashable as `sysimage-sdcard.img`.

## UART and Console

| Item | Fact | Status |
| --- | --- | --- |
| U-Boot env console | `ttyS0,115200` | confirmed |
| Schematic UART0 pins | GPIO38/GPIO39 | confirmed |
| Schematic UART3 pins | present | confirmed |
| Generic CanMV note | two serial consoles may exist in the dual-core reference stack | reference-only |

Linux implication:

- Linux console default should start with `console=ttyS0,115200`.
- Any second serial port must be treated as board-specific until validated on
  the T-Display K230 hardware.

## WiFi

| Item | Fact | Status |
| --- | --- | --- |
| WiFi chip | RTL8189FTV | confirmed |
| WiFi bus | SDIO | confirmed |
| Reference SDIO device | `REALTEK_SDIO_DEV=0` / SDIO0 | confirmed |
| Linux boot observation | `mmc0` enumerates an SDIO card; `mmc1` is the SD card/rootfs path | confirmed |
| Linux network interface | `8189fs` auto-load produces `wlan0` and `wlan1` | linux-observed |
| Linux scan | `iw dev wlan0 scan` can scan nearby APs | linux-validated |

Linux implication:

- The first Linux image enables `mmc_sd0` for the RTL8189FTV SDIO device and
  keeps `mmc_sd1` as the SD-card/rootfs controller.
- Buildroot uses its standard `rtl8189fs` kernel-module package and installs
  `iw`, `wireless-regdb`, `wireless_tools`, `wpa_supplicant`, and `iproute2`
  for validation.
- Interface enumeration and scan have passed. AP association, DHCP, and stable
  throughput still need separate acceptance tests.
- Applications must not call wireless ioctls directly. Network management must
  later be owned by a TDVP system service or BSP-level network abstraction.

## USB Ethernet

| Item | Fact | Status |
| --- | --- | --- |
| Attached USB Ethernet | Realtek USB 10/100 LAN | linux-validated |
| Linux driver | `r8152` | linux-validated |
| Network interface | `eth0` | linux-validated |
| DHCP | obtained `192.168.31.86/24` from `192.168.31.1` | linux-validated |
| Ping | device->gateway, host->device, and device->host all passed | linux-validated |

Linux implication:

- USB Ethernet is the most stable network control plane for the current
   bring-up stage. The `be9c30f3...` image automatically runs DHCP on `eth0`
  and enables Dropbear SSH.
- This does not make the platform depend on an external USB Ethernet adapter;
  it is a hardware experiment and data collection entry point.
- Applications still must not depend directly on Linux network device names.
  Later network capability should be exposed through a TDVP system service or
  BSP-level abstraction.

## Keyboard

| Item | Fact | Status |
| --- | --- | --- |
| Keyboard controller | TCA8418 | confirmed |
| Keyboard I2C bus in LilyGO demo | `/dev/i2c4` | confirmed |
| Keyboard I2C address | `0x34` | confirmed |
| Keyboard reset pin in demo | board pin/GPIO 43 | confirmed |
| Keyboard SCL/SDA | demo calls `fpioa_set_function(46, IIC4_SCL)` / `fpioa_set_function(47, IIC4_SDA)` | confirmed |
| Keyboard module | detachable; the 2026-07-17 failure log was captured with the module installed | confirmed-by-test-context |
| Keyboard matrix | 7 rows x 10 columns in demo init sequence | confirmed |
| Keyboard backlight | PWM4 on board pin 52 | confirmed |
| Caps indicator helper | XL9555-compatible access at I2C address `0x20` | needs-validation |
| Linux IRQ line | not found in the reference source yet | missing-fact |

Linux implication:

- The first Linux image enables I2C4 and installs `tdvp-keyboard-smoke`, which
  follows the LilyGO polling model to detect and read the TCA8418 through
  Linux `i2c-dev` and can pulse GPIO43 high/low/high for reset as in the demo.
- 2026-07-17 board observation: Linux enumerated only `/dev/i2c-0` and
  `/dev/i2c-1`; `/dev/i2c-0` matches DTS alias `i2c0 = &i2c4`, but address
  `0x34` did not ACK yet. Since the test was run with the detachable keyboard
  module installed, missing module is no longer the primary explanation.
- The image now includes `tdvp-k230-iomux keyboard` as a validation-only helper.
  It mirrors the LilyGO `rt_fpioa.c` register write model by setting IO43 to
  GPIO and IO46/IO47 to I2C4.
- The latest log confirmed IO46/47 read back as I2C4 alt3 with `cfg=0x18f`, so
  "I2C4 IOMUX was not applied" is no longer the primary cause. The
  `c6808945...` image further aligns IO43 reset IOMUX with the vendor GPIO
  default `0x18f` and makes the smoke tool use the validated
  `/dev/gpiochip1 line 11` reset path; retesting still did not produce an ACK
  at `0x34`.
- The next layer is TCA8418 power, I2C pull-ups, GPIO43 physical continuity,
  and the detachable keyboard connector. Stable configuration should later
  move to kernel pinctrl and DTS.
- The upstream Linux `tca8418_keypad` driver requires an IRQ line. Do not add a
  stable Linux input DTS node until that IRQ is verified or a polling-capable
  driver strategy is selected.
- Applications must eventually consume normalized TDVP input events, not raw
  I2C or evdev internals.

## LED

| Item | Fact | Status |
| --- | --- | --- |
| LED signal found in schematic | `LEDBANK2_GPIO35` | confirmed |
| Linux GPIO number/pinctrl mapping | TBD | needs-validation |

Linux implication:

- Add LED support only after pinctrl and GPIO bank numbering are validated.
- LED must be exposed through BSP diagnostics or sysfs/debug-only tooling, not
  as an application dependency.

## Additional Board Capabilities

The reference repository and datasheets mention additional hardware capabilities
that may depend on board revision, assembly option, or external module
population:

| Area | Component/reference | Status |
| --- | --- | --- |
| LoRa | HPDTEK/SX1262 family, later README mentions LR2021 | needs-validation |
| BLE | nRF52840 | needs-validation |
| LTE-M/GNSS | nRF9151 | needs-validation |
| Keyboard | TCA8418; I2C4 address `0x34`; reset pin 43; PWM4 backlight | partial-linux-bringup |
| USB Ethernet | attached Realtek USB 10/100 LAN enumerates as `eth0` through `r8152`; DHCP and ping passed | linux-validated-control-plane |
| Battery/charger | BQ25896, BQ27220 | needs-validation |
| WiFi | RTL8189FTV on SDIO0 | first-linux-bringup |
| Sensor/environment | AHT20 mentioned in V1.3 notes | needs-validation |
| Fan/speaker | mentioned in V1.3 notes | needs-validation |

Linux implication:

- These capabilities are in scope for the T-Display K230 platform when present
  on the target board revision.
- They must not be ignored just because the platform is vision-first.
- They must enter Linux/device-tree/BSP planning after their pins, buses, power
  rails, and board revisions are verified.
- Bring-up may be ordered in waves, but ordering is not a scope exclusion.

## Boot Straps

| Item | Fact | Status |
| --- | --- | --- |
| BOOT0 | GPIO0 | confirmed |
| BOOT1 | GPIO1 | confirmed |
| BOOT0=0, BOOT1=0 | SPI NOR mode in schematic note | confirmed |
| BOOT0=1, BOOT1=0 | SPI NAND mode in schematic note | confirmed |

Linux implication:

- The shipped workflow still uses SD-card images, but boot ROM strap behavior
  must be checked during board bring-up.
- Do not assume that SD-card flashing alone describes the complete first-stage
  boot source.

## Hardware-Complete Bring-Up Scope

The first bootable Buildroot Linux image may validate hardware in waves, but the
platform target is a hardware-complete board baseline. "Minimal" means minimal
software complexity, not minimal hardware coverage.

Wave A: boot-critical baseline:

1. UART console on `ttyS0,115200`.
2. SD-card boot/rootfs.
3. GPIO/pinctrl.
4. I2C/SPI/MMC foundations.
5. deterministic BusyBox userspace.

Wave B: interactive and vision baseline:

1. RM69A10 AMOLED panel.
2. GT9895 touch input.
3. RTL8189FTV WiFi on SDIO0.
4. TCA8418 keyboard detection and polling validation on I2C4.
5. One camera path, preferably CSI2 because it is the reference default.
6. Enough DMA/CMA memory for camera to display streaming.
7. LED diagnostic after GPIO numbering is validated.

Wave C: full board capability baseline:

1. LT9611 HDMI, after DSI/I2C/GPIO coexistence is validated.
2. LoRa, after exact module and bus wiring are validated.
3. BLE.
4. LTE-M/GNSS.
5. Battery, charger, and fuel gauge.
6. Audio/speaker.
7. Fan.
8. AHT20 or other environmental sensor if populated.

The waves describe validation order only. They do not remove any populated
board capability from platform scope.
