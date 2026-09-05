# T-Display K230 Linux Patch Queue

This directory is copied into the K230 Linux SDK Buildroot `linux/` package by
`buildroot/tools/prepare-k230-sdk-worktree.sh`. The SDK applies the files in
lexical order to Linux `6.6.36` at
`7d4e1f444f461dbe3833bd99a4640e7b6c2cd529`.

Chinese version: [PATCHES.zh-CN.md](PATCHES.zh-CN.md).

[PATCH_COMPOSITION.md](PATCH_COMPOSITION.md) records the reviewed interactions,
ordering constraints and validation gates for this queue.

## Queue

| Files | Owner | Contract |
| --- | --- | --- |
| `0001` through `0024` | K230 Linux SDK | SDK kernel enablement required by the K230 Buildroot profile. `0014` binds GNNE and AI2D to the AI power domain and clocks; `0017` applies the 800 MHz AI clock policy and preserves the clock gate while changing its parent. |
| `0025-tdvp-drm-canaan-standard-gem-dma.patch` | TDVP | Canaan DRM uses Linux 6.6 GEM-DMA helper operations and FOPS. |
| `0026` through `0034` | Lewis patch set | RM69A10 DSI PHY, VO XRGB8888, panel sequence, burst mode, colour conversion and DTS. |
| `0035-lewis-gdma-add-xrgb8888-rotation.patch` | Lewis patch set | Registers XRGB8888, ABGR8888 and XBGR8888 as 32-bit K230 GDMA inputs. |
| `0036-tdvp-input-tca8418-reset-gpios.patch` | TDVP | Standard optional active-low reset GPIO support in the TCA8418 driver. |
| `0037-tdvp-riscv-dts-canaan-add-t-display-k230-keyboard.patch` | TDVP | T-Display K230 detachable-keyboard bus and matrix configuration. |
| `0038-tdvp-input-tca8418-polling.patch` | TDVP | Optional driver FIFO polling for boards selecting `poll-interval-ms`. |
| `0039-tdvp-input-tca8418-hardware-debounce.patch` | TDVP | Optional TCA8418 hardware debounce property. |
| `0040-tdvp-pinctrl-k230-support-standard-schmitt-enable.patch` | TDVP | Standard `input-schmitt-enable` support in K230 pinctrl. |
| `0041-tdvp-input-add-gt9895-touchscreen.patch` | TDVP | GT9895 Type-B multitouch driver for the board DTS node at I2C address `0x5d`. |
| `0042-tdvp-riscv-dts-canaan-enable-k230-platform-services.patch` | TDVP | K230 internal-codec sound-card binding. |
| `0043-tdvp-panel-canaan-universal-add-standard-backlight.patch` | TDVP | RM69A10 backlight is exposed through the Linux backlight class. |
| `0044-tdvp-riscv-dts-canaan-add-keyboard-backlight.patch` | TDVP | Keyboard backlight is exposed through the PWM backlight class. |
| `0045-tdvp-power-bq27xxx-add-bq27220.patch` | TDVP | Adds a BQ27220-specific, read-only standard-command register profile to the Linux `bq27xxx` power-supply driver. |
| `0046-tdvp-riscv-dts-canaan-add-dock-power-devices.patch` | TDVP | Adds the dock XL9555 GPIO expander and BQ27220 fuel-gauge nodes to the accepted keyboard I2C bus. |
| `0047-tdvp-misc-add-radio-profile-selector.patch` | TDVP | Enables UART2 and provides an exclusive standard pinctrl/GPIO profile selector for LR2021 LoRa and the optional nRF9151 LTE-M/NB-IoT/GNSS modem. |
| `0048-tdvp-radio-lr2021-spi-transport.patch` | TDVP | Enables SPI0 with an LR2021 spidev transport and adds power/reset control to the radio profile selector. |
| `0049-tdvp-k230-spi-bound-irq-enumeration.patch` | TDVP | Enumerates only the interrupt resources declared by K230 SPI0. |
| `0050-tdvp-hwmon-aht20-standard-binding.patch` | TDVP | Adds the dock AHT20 at `0x38` and its standard `aosong,aht20` hwmon binding. |
| `0051` through `0052` | TDVP | Route the accepted external I2S amplifier through the existing K230 sound card and its managed ALSA switch. |
| `0053-tdvp-drm-canaan-page-flip-lifecycle.patch` | TDVP | Makes Canaan DRM page-flip/vblank event ownership explicit across CRTC disable and VO IRQ delivery. |
| `0054` through `0062` | TDVP | VGLite per-client ownership, submission serialization, watchdog, interrupt, single-context and completion-idle lifecycle fixes. |
| `0063-tdvp-cpu1-rtsmart-mailbox.patch` | TDVP | Reserves CPU1's RT-Smart RAM, exposes only a non-cacheable 64 KiB mailbox at `/dev/tdvp-cpu1`, and deliberately leaves CPU1 outside Linux SMP. |
| `0064-tdvp-riscv-dts-use-scalar-cpu0.patch` | TDVP | Describes physical CPU0 without RVV, with 128 KiB L2, and reserves UART3 for the CPU1 console. |

The numbering follows the imported display queue. The lexical ordering is a
build input and is checked by the baseline assertion.

## CPU1 Coprocessor Contract

The K230 Linux device tree intentionally declares only local hart `cpu@0`;
this name alone does not select a physical core. The pinned SDK normally
runs U-Boot on physical CPU1. TDVP overrides `CONFIG_LINUX_RUN_CORE_ID=0`
so resetting CPU1 cannot reset U-Boot itself. `0064`, the kernel fragment
and scalar `-mcpu=c908` userspace flags match physical CPU0. `0063`
reserves `0x10000000..0x13ffffff` for the CPU1 OpenSBI/RT-Smart runtime and
uses the last 64 KiB (`0x13ff0000`) for a versioned shared-memory mailbox.
Linux maps only that mailbox through `/dev/tdvp-cpu1` with non-cacheable page
attributes; it neither starts CPU1 nor makes it a schedulable Linux core.

The image post-processing stage compiles the pinned LilyGO RT-Smart source,
places the firmware in the raw SD-card 10--30 MiB slot, and changes U-Boot to
launch CPU1 before `blinux`. This slot contains raw `fw_payload.bin`, linked
at `0x10000000` with RT-Smart at offset `0x20000`, not the vendor K230 wrapper:
`boot_baremetal` only sets a reset vector. A build-time guard verifies core
selection, entry, format, size and digest. RT-Smart's allocatable RAM ends
before the mailbox (`0x03ff0000` bytes), uses UART3, disables shared board
peripheral drivers and replaces the SD/USB-initializing CanMV `main`.
Linux retains UART0 and owns SD, WiFi, display and other board peripherals.
Cold-boot and VGLite hardware gates must be repeated after this core/ISA change.
The bounded first ABI services are `ping` and
`crc32`, available through `tdvp-cpu1ctl` and `libtdvp_cpu1.so.1`.  Future
CPU1 jobs must extend this versioned ABI, keep cache maintenance on both cores,
and must not expose the remainder of the reserved RAM to Linux userspace.

## AI Power and Clock Contract

The K230 SDK owns the AI power and clock implementation in its `0014` and
`0017` patches. `0014` attaches the GNNE and AI2D device-tree nodes to
`K230_PM_DOMAIN_AI`, `ai_clk`, and `ai_aclk`, then uses runtime-PM and bulk
clock handling in both drivers. `0017` sets `ai_clk` to 800 MHz before a GNNE
workload is used and prevents an unintended parent reconfiguration.

TDVP does not duplicate these patches. The release assertion requires their
presence in the staged SDK queue and checks their resulting DTS and driver
code before a candidate image can be built. The target KPU gate remains the
existence of both character devices plus a successful bundled kmodel workload.

## Lewis Patch Integrity

`0026` through `0035` preserve the source patch text from
[`lewisxhe/k230-t-display-linux-patches`](https://github.com/lewisxhe/k230-t-display-linux-patches)
commit `cef00a992975ea4ed687bf44f04f633dad5087e6`, canonicalized to LF in the
staged ext4 worktree. The assertion checks the canonical LF SHA-256 values
below. This keeps the selected board behaviour reproducible while the TDVP
GEM-DMA and keyboard integrations have explicit ownership.

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
GEM:         Linux DRM GEM-DMA helpers
console:     fbcon=rotate:3
atomic rotation: SDK GDMA rotation pipeline with XRGB8888 input support
```

`0025` supplies `DEFINE_DRM_GEM_DMA_FOPS(canaan_drm_fops)` and
`DRM_GEM_DMA_DRIVER_OPS_VMAP`. The assertion rejects Canaan-specific GEM-DMA
allocation and mmap implementations in the effective `canaan_drv.c`.

The SDK's `0023-add-gdma-vo-rotation.patch` creates the K230 GDMA rotation
pipeline: an atomic plane commit requests a rotation, GDMA produces a rotated
scanout buffer, and the VO plane scans out that buffer. `0035` supplies the
XRGB8888 format registration required by that pipeline. Framebuffer-console
rotation renders the early console in the enclosure orientation. DRM clients
set the plane `rotation` property when they use the GDMA path and render their
content with the matching logical dimensions.

## Detachable Keyboard Contract

```text
controller: TCA8418 at 0x34
matrix:     7 rows x 10 columns
delivery:   Linux input event device
```

The release DTB owns the selected I2C transport, reset line, interrupt or
polling method, and matrix map. The release manifest records the decompiled
DTB values together with a physical input-event trace. The standard Linux
input subsystem publishes the resulting key events. The rootfs loads the
T-Display console map through `tdvp-keyboard-layout`; physical `0` emits `0`
and `Shift+0` emits `)`.

The complete matrix, modifiers, yellow layer, navigation keys, backlight, and
shell shortcuts are the keyboard regression set. A patch that changes any
keyboard electrical, driver, or mapping input is accepted only after this set
passes on T-Display K230 V1.3.

## Detachable Dock Power Contract

```text
controller: i2c-gpio keyboard bus at Linux I2C adapter 1
XL9555:     0x20, nxp,pca9555, standard pca953x GPIO provider
BQ27220:    0x55, ti,bq27220, standard bq27xxx power supply
BQ25896:    0x6b, present on the bus and intentionally unbound
AHT20:      0x38, aosong,aht20, standard hwmon sensor
```

`0046` extends the existing `i2c_gpio_keyboard` node after the board display
and keyboard DTS files have created it. It does not change the TCA8418 node,
its GPIO transport, matrix, reset path, backlight, or keymap.

`0045` defines the BQ27220 command map directly: `Current()` at `0x0c`,
`RemainingCapacity()` at `0x10`, `FullChargeCapacity()` at `0x12`,
`CycleCount()` at `0x2a`, `StateOfCharge()` at `0x2c`, and
`DesignCapacity()` at `0x3c`. Its data-memory entries are invalid, so the
driver has no unseal or data-memory write path. The resulting power-supply
device must be accepted against physical charge and discharge readings.

The BQ25896 charger is excluded from the static DTS until its interrupt route
and all required charge-policy values are measured on the V1.3 dock. The
XL9555 is exposed as a standard GPIO controller; line ownership is assigned
only after the dock schematic and physical line traces identify each function.

The AHT20 node binds to the in-kernel AHT10/AHT20 hwmon driver. It publishes
temperature through `temp1_input` in millidegrees Celsius and humidity through
`humidity1_input` in millipercent. The driver validates each AHT20 frame with
its CRC and enforces the sensor's minimum sampling interval.

## Touch Contract

```text
controller: GT9895 at 0x5d on i2c3
transport:  four-byte event register address
delivery:   Linux Type-B multitouch input event device
coordinates: controller-native 1060 x 2400
```

`0041` binds the DTS node from `0032` to `tdvp_gt9895`. The release assertion
checks the effective driver source, kernel configuration and final DTB. The
physical acceptance record contains a touch trace and a Wayland pointer/touch
interaction on the selected output transform.

## Required Checks

```sh
buildroot/tools/prepare-k230-sdk-worktree.sh "$WORKTREE"
buildroot/tools/build-k230-sdk-rm69a10.sh "$WORKTREE"
buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

The static assertion checks patch integrity, effective GEM-DMA source, DTB
content, kernel configuration, target rootfs and image artifacts. The image
then proceeds through the physical gates in
[`docs/hardware-baseline-validation.md`](../../../docs/hardware-baseline-validation.md).
