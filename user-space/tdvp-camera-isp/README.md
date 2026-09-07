# CPU0 camera ISP integration work

This directory is **not yet enabled in the image**. It contains a scalar-ISP
sensor ABI adapter, a board-specific GC2093 transport, candidate device-tree
configuration and a VVCAM clock/reset patch. Physical chip identification
passed on 2026-09-07. **Frame capture has not passed**; neither chip detection
nor a scalar executable proves that the complete camera stack works.

## Pinned candidate and reason for the adapter

The SDK currently pinned by TDVP is `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7`.
Its ISP executables contain RVV instructions, while Linux executes on CPU0.
The newer official v0.8.1 binary was also inspected: 1,572 vector mnemonics were
found in executable-section disassembly, including unconditional vector stores
in `CamDeviceSensorIsiGetMetadataWindow`. Its official archive SHA256 is
`2ee5f9703eaa466e97b257a016f282d1b6cf1fe9a677e72ea53f22ede3a2eb69`.
See the [upstream v0.8.1 package](https://github.com/kendryte/k230_linux_sdk/blob/ac0ac1e1888f15ce2fe7713e6a587dc1268c57e2/buildroot-overlay/package/vvcam/isp-media-server/isp-media-server.mk).

An alternative exists in official release **v0.6.1**, commit
`155359af908a5fb38e870e81ee82c91bceb30800`:

- [ISP binary](https://github.com/kendryte/k230_linux_sdk/blob/155359af908a5fb38e870e81ee82c91bceb30800/buildroot-overlay/package/vvcam/isp_media_server)
- SHA256: `5d3e7bd8914cd2a3d9ca9da03a2743e7bd8ab24b9cb090929950b632ffb833ce`
- Git blob: `baec321f6f683e455d0a56914af988998e9b5dde` (download matched the pinned Git object)
- ELF architecture: RV64GC; executable-section disassembly found zero vector
  mnemonics. The metadata function uses scalar stores. This is static ISA
  evidence, not a successful camera runtime test.

Do **not** combine that binary with today's `libvvcam.so` unchanged. The
[legacy sensor header](https://github.com/kendryte/k230_linux_sdk/blob/155359af908a5fb38e870e81ee82c91bceb30800/buildroot-overlay/package/vvcam/include/vvcam_sensor.h)
has nine callbacks. The pinned modern header inserted four flip callbacks
before the gain/exposure callbacks without changing `VVCAM_API_VERSION=1`.

| Registration layout on LP64 | Legacy ISP | Current driver |
| --- | ---: | ---: |
| Entire `vvcam_sensor` size | 80 | 112 |
| Analog gain callback byte offset | 56 | 88 |
| Digital gain callback byte offset | 64 | 96 |
| Exposure callback byte offset | 72 | 104 |

`src/tdvp-vvcam-legacy-adapter.c` registers an explicitly mapped, static-lifetime
legacy table for GC2093. It preserves the typed callbacks, return values and
sensor context. It must be linked with the board GC2093 driver **instead of**
vendor `src/lib.c`. It performs no I2C, reset, clock or stream operation during
registration. It does not advertise the newer flip-control interface.

## Verification already performed

```sh
bash buildroot/tools/test-tdvp-camera-legacy-abi.sh
bash buildroot/tools/test-tdvp-camera-i2c.sh
```

The test uses the pinned current vendor header and a mock GC2093. It verifies
registration without side effects, all nine callback slots, context, integer,
boolean and floating-point arguments, return values, and layout assertions.
Modern flip slots deliberately abort if reached. The same test was cross-built
with the actual SDK CPU0 toolchain and passed on the board on 2026-09-07.
Target test SHA256:
`90178e764baed4aead8a30690328557269a13d4aa34e8890861bbd11a5039a76`.

The I2C test covers nine success/error cases, including wrong adapter identity,
permissions, a kernel-owned address, NACKs, short transfers and wrong chip ID.
It wraps both ordinary and Ubuntu 24.04 fortified `realpath` entry points.
It also applies the real transport patch to the pinned driver and dynamically
loads the complete plugin: API version, registration, allocation and mode
enumeration. It does not configure or stream a physical sensor.

The PR workflow runs both host regressions. It does not claim camera
acceptance, install this adapter, or replace the existing ISP binary.

## Board candidate and physical identification

Apply `patches/0001-*.patch` and `0002-*.patch` to a **private copy** of the
pinned SDK `buildroot-overlay/package/vvcam`; do not patch an existing SDK
build directory in place. Compile the shared plugin from:

- the patched `src/gc2093.c` and unchanged `src/version.c`;
- `src/tdvp-vvcam-legacy-adapter.c` and `src/tdvp-gc2093-i2c.c` here;
- include paths for this directory's `src` and the pinned vendor `include`.

Exclude vendor `src/lib.c` and the other sensors. `src/version.c` is required
for the ISP loader's `vvcam_api_version` export. Build the kernel modules
against the matching prepared kernel with
`BR2_PACKAGE_VVCAM_DEF_SENSOR=gc2093`, not the image's old OV5647 default.
The pinned kernel uses platform registration for the V4L2 ISP; it has no
`v4l2isp` DT label and does not consume the newer donor's sensor properties.

Append `board/tdvp-gc2093.dtsi` **after** the complete TDVP board DTS (including
CPU0, mailbox and UART1 patches). The candidate preserves touch on I2C0 and
dock/keyboard on I2C1, and explicitly assigns physical I2C4 to adapter 4.
The transport verifies the controller's `of_node`, uses address `0x37` without
`I2C_SLAVE_FORCE`, and validates registers `0x03f0/0x03f1` before exposing an
open handle. There is no fallback address or broad bus scan.

Only physical CSI2 is enabled. GPIO7/8 provide I2C4, GPIO13 provides MCLK1 and
GPIO21 supplies the donor's high-low-high reset sequence. The MIPI driver
keeps logical `/dev/vvcam-mipi.0`, as expected by the ISP. A declared GC2093
I2C node is **not** evidence of a bound kernel sensor driver or frame capture.

### MCLK finding and fail-closed protection

The first candidate put the mux, divider and gate into one generic K230
composite clock. Physical testing reported **594 MHz**, not the requested
23.76 MHz. The module was unloaded and the clock disabled; no sensor register
or stream test was attempted in that state. The kernel image on the board and
validation host matched SHA256
`cef59965b1d9349ad7a65a8d9224dae44a8524a6506f93e1897cb0e0309f3a42`.

In the pinned `drivers/clk/clk-k230.c`, mux nodes use
`__clk_mux_determine_rate_closest`, which takes precedence over the divider's
`round_rate` and selects the parent rate. The final DTS separates mux/gate
from divider, using the same provider's register lock. It does **not** alter
the shared CPU/GPU/SD/audio clock driver or any PLL frequency.

The MIPI patch explicitly requests the rate and rejects mismatches before
enabling the clock or releasing sensor reset. It also checks after enable and
uses managed cleanup. The rejection path was exercised on the first DTB:
`-ERANGE`, no MIPI device, prepare/enable counts zero. On the split-clock DTB,
the reported sensor rate was **23,760,000 Hz** (594 MHz / 25), and both physical
chip-ID checks returned **0x2093**. Unloading the module returned mux and
divider prepare/enable counts to zero and disabled the physical parent gate.

| Final candidate artifact | SHA256 |
| --- | --- |
| DTB | `2d273d6f9871f000922db3c8ab723d6ae48db40b42c156411c92dc85c6ac4ce2` |
| MIPI module with clock guard | `7ff2f41ba3bdac063f27fc596bf928bff720f417ca7e3fce24144dd44738e696` |
| Complete sensor plugin | `7c9d04c01283aae964867cc5c5649c9d08364bb2de3c972a5c975c2657c80c42` |
| Chip-ID utility | `874c2dc3bd99c27456db2a48038f130dd5e9383bf5ff81e6c711b8af5ff73ada` |

Ubuntu 24.04 / SDK GCC 14.1.1 cross-built the plugin, chip-ID utility and all
five VVCAM modules. Pinned vendor sources still emit existing const/unused/
format warnings; this is not a warning-free claim. The plugin's dynamic load
test and the nine-case I2C test also passed on the real CPU0.

For a scope check, decompile the pre-camera and candidate DTBs with
`dtc -q -s -I dtb -O dts`, then run
`python3 tests/compare-candidate-dts.py before.dts candidate.dts`. It resolves
reference phandles and rejects changes to existing properties outside the
explicit camera allowlist. The real comparison preserved **268 existing
nodes**. This is a scope check, not full device-tree binding validation.

The two diagnostic boots automatically restored the pre-camera DTB on the SD
card before testing. The running kernel retains the candidate DT until the
next reboot, but no VVCAM module or ISP daemon was left active. No production
ISP/plugin file or camera acceptance marker was installed. Raw local evidence:
`.tmp/device-validation/camera-chip-id-20260907.log`.

## Remaining integration requirements

1. Build an isolated, pinned scalar-ISP plus matching sensor-plugin package;
   verify dynamic dependencies and kernel event compatibility. The modern
   kernel added crop/selection and sensor controls that the older daemon may
   not implement; basic capture and unsupported operations need real testing.
2. Promote the validated I2C/clock/CSI2 candidate into the production package
   and ordered Linux queue only after capture compatibility is verified.
   Preserve CPU1's reservation and all Linux display, GPU, SD, keyboard and
   radio ownership, including on incremental builds.
3. Check the ISP/VVCAM buffer allocation and event ABI before loading the
   remaining modules; do not assume the old daemon implements modern controls.
4. Perform bounded stream start, frame acquisition and stop
   tests, then test camera preview through Wayland alongside VGLite and CPU1.
5. Only after this works, enable the service/package, update hardware status
   and produce a complete image. No fake camera node or acceptance marker.

Moving the camera to CPU1 is a separate fallback, not an action taken here.
The donor MPP startup initializes VO, audio, DMA and other shared peripherals;
simply enabling `RT_USING_MPP` would violate the current ownership contract.
