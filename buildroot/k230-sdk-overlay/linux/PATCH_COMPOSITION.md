# T-Display K230 Linux Patch Composition

This document defines the reviewed composition rules for the active K230
Linux `6.6.36` patch queue. The source identities and exact file hashes are in
[PATCHES.md](PATCHES.md).

## Application Order

```text
SDK 0001..0024
  -> SDK 0014 and 0017: GNNE/AI2D power domain and AI clock policy
  -> TDVP 0025: standard Canaan DRM GEM-DMA
  -> Lewis 0026..0035: RM69A10 DSI, VO, panel, DTS and GDMA XRGB input
  -> TDVP 0036..0040: TCA8418 keyboard and K230 pinctrl
  -> TDVP 0041: GT9895 touchscreen input
  -> TDVP 0042..0044: sound card, display backlight and keyboard backlight
  -> TDVP 0045..0046: BQ27220 telemetry profile and dock I2C devices
  -> TDVP 0047: LR2021 pin and power profile, with an optional K256-04 nRF9151 selector
  -> TDVP 0048..0049: LR2021 SPI0 transport, reset sequencing and bounded IRQ enumeration
  -> TDVP 0050: AHT20 hwmon binding
```

The Buildroot `linux/` package directory is the sole patch input. Buildroot
applies its `*.patch` files in lexical order; `BR2_LINUX_KERNEL_PATCH` remains
empty so the same directory is never replayed as an external local-patch
directory. `assert-k230-sdk-rm69a10-baseline.sh` checks the effective patched
source; it is the gate for patch applicability.

## Reviewed Interactions

| Area | Inputs | Effective contract | Validation |
| --- | --- | --- | --- |
| DRM file operations and GEM allocation | SDK `0005`; TDVP `0025` | SDK `0005` leaves display runtime-PM calls disabled. TDVP `0025` removes the Canaan-local FOPS, dumb-buffer allocation and mmap copies, then installs Linux 6.6 `DRM_GEM_DMA_DRIVER_OPS_VMAP` and `DEFINE_DRM_GEM_DMA_FOPS`. | Inspect patched `canaan_drv.c`; create XR24 dumb buffers with DRM smoke test. |
| AI power, clocks and KPU runtime | SDK `0014`, `0017`; TDVP kernel fragment and KPU package | The SDK declares the AI power domain and both AI clocks in the effective DTS, enables them through runtime PM, and sets the AI clock to 800 MHz for GNNE. TDVP selects the two drivers and packages the nncase runtime plus a versioned kmodel workload. | Check the patched DTS and drivers, both KPU character devices, then run the bundled workload on the board. |
| VO reset, timing and scanout | SDK `0005`, `0006`, `0013`, `0023`; Lewis `0027`, `0031`, `0034` | SDK provides the VO reset path, one-row timing correction, background handling and GDMA scanout address/pitch support. Lewis registers XR24 on OSD, defines its byte order, and selects the RGB scanout conversion state. | Cold boot on the RM69A10; XR24 colour bars; static and dynamic DRM frames. |
| DSI PHY, mode and panel sequence | Lewis `0026`, `0028`, `0030`, `0032` | Two data lanes, RM69A10 init/reset sequence, the board timing declaration and burst video mode are a single panel link configuration. The PHY parameters are board-specific values and must be evaluated together with the declared pixel clock. | Repeated cold boots, no DSI timeout, stable full-screen pattern. |
| XRGB scanout and hardware rotation | SDK `0023`; Lewis `0027`, `0031`, `0035`; TDVP `0025` | SDK `0023` performs plane rotation through K230 GDMA and scans out the rotated DMA buffer through VO. Lewis `0035` makes XR24, ABGR8888 and XBGR8888 valid 32-bit inputs to that GDMA code. TDVP `0025` supplies the standard GEM-DMA objects consumed by both direct scanout and the GDMA rotation buffer pool. | Query plane rotation properties; commit XR24 at 0/90/180/270 degrees; verify source/destination dimensions and image stability. |
| Console orientation | Kernel fragment `fbcon=rotate:3` | The framebuffer console renders its pixels in the enclosure's landscape orientation. It does not set a KMS plane `rotation` property and therefore does not start the GDMA rotation path. | Observe kernel console, BusyBox login and shell after a cold boot. |
| Graphical-client orientation | SDK `0023`; Lewis `0035` | A DRM atomic client selects a plane rotation property and renders with matching logical dimensions. A client uses either an unrotated plane or one plane rotation transform for a frame. | Atomic DRM test program with a labelled, asymmetric test image. |
| RM69A10 board DTS and keyboard DTS | Lewis `0032`, `0033`; TDVP `0037` | Lewis creates `k230-canmv-rm69a10.dts`, enables SDIO WiFi and introduces the display. TDVP adds the detachable keyboard transport and matrix, while leaving `i2c4` disabled in this shell profile. | Decompile produced DTB; probe WiFi and TCA8418; verify the full matrix including `0` and `Shift+0`. |
| TCA8418 driver extensions | TDVP `0036`, `0038`, `0039`, `0040` | The driver reset is performed before its first I2C transaction. FIFO polling is selected by the board DTS, controller debounce is selected by the board DTS, and the K230 pinctrl accepts the standard Schmitt property. | Boot with the keyboard fitted; use `evtest` or console input; test all yellow-key combinations. |
| Audio and backlights | SDK `0013`; TDVP `0042`, `0043`, `0044` | The internal-codec sound card, RM69A10 brightness path and keyboard PWM backlight each use their corresponding Linux class interface. | Enumerate ALSA playback/capture, change LCD brightness through `/sys/class/backlight`, and change keyboard brightness through its PWM backlight class. |
| Dock GPIO and fuel gauge | TDVP `0045`, `0046`; existing `i2c_gpio_keyboard` node | The XL9555 at `0x20` is a standard `pca953x` GPIO provider. The BQ27220 at `0x55` uses its own standard-command register profile: instantaneous current is `0x0c`, remaining capacity is `0x10`, and no data-memory programming path is available. The static nodes share the accepted keyboard I2C bus without modifying the TCA8418 transport. | On the fitted dock, verify the full keyboard regression, `gpioinfo` reports the XL9555 chip, and BQ27220 `/sys/class/power_supply` values match independent I2C reads during charge and discharge. |
| Dock environment sensor | TDVP `0050`; existing `i2c_gpio_keyboard` node | AHT20 at `0x38` uses the in-kernel `aosong,aht20` hwmon binding. The driver verifies the AHT20 CRC and publishes standard temperature and humidity attributes. | Compare `temp1_input` and `humidity1_input` with a direct CRC-checked I2C frame at two environmental conditions. |
| LoRa and optional nRF9151 serial path | TDVP `0047` and `0048`; UART2, SPI0, K230 pinctrl and GPIO descriptors | The LR2021 reset input and the K256-04 nRF9151 UART2 TX path share IO5. `tdvp-radio-mux` owns the relevant pin and power states; the K256-04-A baseline stays in `lora` and keeps the optional modem unselected. SPI0 owns IO14 through IO17 for the LR2021 transport. | K256-04-A: run `vpl-lora-probe` and require a nonzero, non-`0xff` LR2021 firmware version. K256-04: additionally switch to `nrf9151`, confirm `/dev/ttyS2` exchanges AT commands with the fitted modem, then return to `lora` and repeat the LR2021 probe. |

## Explicit Scope Boundaries

### Touch

Lewis `0032` provides the GT9895 DTS node. TDVP `0041` supplies the matching
four-byte-register driver, and `board/tdvp/fragment/linux.touch` builds it
into the kernel. The release gate checks the patched driver source, Kconfig,
final DTB and a physical touch interaction before accepting an image.

### Camera

Lewis `0036-add-gc2093-camera.patch` is excluded. Its own source notes that
MCLK and reset initialization remain incomplete. It also requests `i2c4`,
while TDVP `0037` disables `i2c4` for the accepted shell profile. A camera
enablement change must supply the complete electrical configuration and a
separate DTB/CSI capture acceptance test.

### Colour Conversion

Lewis `0034` disables the global VO YUV-to-RGB and OSD RGB-to-YUV controls
for the XR24 display path. This is covered by the current shell and DRM test
scope. Camera composition, YUV overlays and multi-plane video need their own
colour-pipeline acceptance tests before they are enabled in a product image.

## Queue Hygiene

The reconciliation script removes legacy Lewis filenames for the display,
touch, rotation and camera files before Buildroot applies patches. The staged
queue contains only the named files in [PATCHES.md](PATCHES.md), preventing a
second copy of a semantic patch from silently changing patch order.

## Required Acceptance Sequence

1. Stage a fresh SDK worktree from the pinned inputs.
2. Run `linux-patch` and the `--patch-only` assertion.
3. Build the candidate image and run the full assertion.
4. Cold boot the board and verify the console, login, WiFi, SSH and keyboard.
5. Run static XR24 bars, a labelled asymmetric XR24 frame, and a dynamic
   XR24 frame. Hardware rotation is accepted only after all three modes are
   stable at each selected rotation.
