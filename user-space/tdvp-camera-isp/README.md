# Historical CPU0 camera ISP integration

The current product profile disables these Linux camera packages and removes the
Camera menu entry. CPU1 owns GC2093, capture/ISP and vision buffers; Linux reads
frames through the asynchronous bridge. See [architecture](../../docs/architecture.md)
and [AI/vision records](../../docs/cpu1-ai-jobs.zh-CN.md).

The following record belongs to the 2026-09-07 CPU0 camera candidate. It preserves
the scalar-ISP sensor ABI adapter, board-specific transport, managed device tree
and VVCAM lifecycle work for reference. Its build and deployment results apply
only to that historical candidate.
**Physical GC2093 identification
and bounded 1920x1080 NV12 frame capture passed on 2026-09-07**, including two
complete module load/capture/unload cycles. This is not yet a production image
deployment, VGLite coexistence or long-duration acceptance. The subsequent
package build and bounded Wayland preview results are recorded below.

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

The PR workflow runs both host regressions. The I2C test applies all five real
candidate patches to pinned source without fuzz, dynamically tests the plugin,
and builds the capture checker while verifying that it rejects `/dev/null`.
It does not open a host camera, claim hardware acceptance, install this
adapter, or replace the existing ISP binary.

## Board candidate and physical identification

Apply all `src/patches/0001-*.patch` through `0005-*.patch` in order to a **private copy** of the
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

The chip-identification diagnostic boots automatically restored the pre-camera DTB on the SD
card before testing. The running kernel retains the candidate DT until the
next reboot, but no VVCAM module or ISP daemon was left active. No production
ISP/plugin file or camera acceptance marker was installed. Raw local evidence:
`.tmp/device-validation/camera-chip-id-20260907.log`.

## Physical frame capture and lifecycle corrections

The legacy/basic ISP event values and layouts, VB ioctls and V4L2 buffer
descriptors were compared with the pinned SDK. New crop/selection events are
not thereby proven compatible. The board's CMA pool is
`0x20000000-0x3fffffff`; CPU1 occupies `0x10000000-0x13ffffff`. VVCAM uses
`enable_cma=1` / `dma_alloc_coherent`, not a fixed MMZ pool. Actual allocation
logs fell in CMA. No CPU1 MPP or shared display ownership was enabled.

Three runtime defects were found and addressed before accepting capture:

1. **Clock ownership (`0003`).** The old ISP's `cb_IsiOpenIss` writes all
   three sensor clocks directly when the sensor mode's `clk` is nonzero.
   This was visible in the first stream test and bypassed the managed kernel
   clock. Disassembly confirmed that `clk=0` skips those writes. Both GC2093
   mode descriptors now use that branch; the sensor register tables, gain
   controls and ISP executable remain unchanged. A diagnostic reboot cleared
   the old writes. All subsequent ISP tests used systemd `DevicePolicy=closed`
   with only the camera/I2C/PTY devices allowed, **not `/dev/mem`**.
2. **Completion metadata (`0004`).** Vendor buffers had zero timestamps and
   zero public V4L2 frame sequences, causing FFmpeg to drop successive frames.
   The patch stamps the receipt of an ISP completion with `ktime_get_ns()`
   and assigns a per-pad sequence under the existing queue spinlock. This is
   a **completion timestamp**, not a sensor exposure timestamp. The separate
   vendor buffer-index field is unchanged. Sequences reset for a new stream.
3. **Resource ownership (`0005`).** The event-only V4L2 subdevice registered
   an unused `0x90000000-0x9000ffff` resource. Platform registration inserted
   it above the real ISP and MIPI claims; deleting it orphaned that subtree,
   causing their later managed release to report nonexistent resources.
   Remove the software subdevice's unused MMIO/IRQ declarations, not the
   real drivers' `devm_ioremap_resource` protection. Actual ownership is now
   ISP `0x90000000-0x90008fff` and MIPI `0x9000a800-0x9000afff`.

With `0001-0004`, two consecutive 60-frame streams passed at **30.006** and
**29.927 FPS**, with sequences `0..59`, strictly increasing monotonic
timestamps and exactly **3,110,400 bytes/frame**. The last NV12 frame was
decoded to PNG and visually inspected: a real keyboard/desk/cables scene,
not a blank frame or test pattern. No rotation or image-quality tuning was
applied. FFmpeg subsequently captured three full frames and exited normally;
their PTS were `0`, `33972`, `66658` microseconds and hashes differed.
`VIDIOC_G_PARM` remains unsupported and FFmpeg warns about unknown nominal
frame rate; this is not presented as complete V4L2 API compatibility.

The final `0001-0005` candidate then passed two **full module reload** cycles:

| Cycle | Complete NV12 frames | Sequence | Completion timestamps (us) | Measured FPS |
| --- | ---: | --- | --- | ---: |
| 1 | 60 | 0..59 | 944171454..946137775 | 30.005 |
| 2 | 60 | 0..59 | 953463834..955429962 | 30.008 |

Both capture services exited `0/SUCCESS`. No new nonexistent-resource,
WARNING, BUG, Oops or Call Trace message appeared during these cycles.
Clock register `0x9110006c` remained `0x003a7709` across each capture and
became `0x003a7708` on unload; mux enable/prepare counts returned to zero.
CMA free memory did not decline across the two final cycles, but this does
not prove long-term absence of leaks. The kernel and DTB were unchanged from
the successful chip-identification candidate. All five modules were built
fresh with the matching Ubuntu 24.04 / SDK CPU0 toolchain.

| Final runtime-v4 artifact | SHA256 |
| --- | --- |
| Scalar ISP (unmodified upstream) | `5d3e7bd8914cd2a3d9ca9da03a2743e7bd8ab24b9cb090929950b632ffb833ce` |
| GC2093 plugin | `df65b714eeac692cd7f3b1cea863d0a7807f71f01fa1e26cbf57b8473f6d4977` |
| ISP subdev module | `7db704f19d36f5e1a6841d310acc5d40f55700a86e1b5b8064d38989c0668768` |
| MIPI module | `3565e0262f2a1e1c08279222085ff750932f1abeb5dd2493330758bd69c9cf49` |
| Capture checker | `5f5c4e4ca7e44b039da5cd43ad14cc2f867595f34582e9fcf144fe39376eee3d` |

`src/tdvp-camera-capture-check.c` uses only QUERYCAP, S_FMT and the MMAP streaming
path. It refuses non-VVCAM devices, unexpected stride/size, error buffers,
invalid/non-increasing timestamps and discontinuous sequences. It captures
60 frames and optionally saves the last raw frame with exclusive creation.
Run it under an external service time limit as well, because setup/teardown
ioctls can wait on the ISP. It does not exercise crop, flip, DRM or Wayland,
and does not write a production acceptance marker.

Local raw evidence is under `.tmp/device-validation/camera-runtime-v3/` and
`camera-runtime-v4/`. The PNG SHA256 is
`37e9444745cb9a8e72a1c7d668cd53b61899262c62499210e7ae3a87000e5deb`.
Images and raw diagnostic files are not committed. The candidate is stored
under `/var/lib/tdvp-repair-backups/20260907/camera-candidate/` on the board,
not over the system ISP/plugin/modules. Test daemons and modules were stopped
and unloaded. The temporary boot-recovery unit was removed after verifying
that the SD boot DTB was restored to the UART1 baseline.

## Production packaging

`tdvp-camera-isp-runtime` downloads the exact upstream scalar executable through
Buildroot's normal hash-checked download path. It is a separate package because
Buildroot local-source packages skip the normal download/patch graph. The
profile excludes **only** `isp_media_server` from stripping so the audited SHA256
remains valid at startup and inside the final ext4 image.

`tdvp-camera-isp` stages this directory's `src`, checks the five patch hashes,
refreshes its own private VVCAM copy on every rsync (without rsync's `-u`), applies
the queue with zero fuzz, and uses Buildroot's actual kernel-module backend.
It installs only the GC2093 legacy plugin, five modules and diagnostic helpers.
The vendor `BR2_PACKAGE_VVCAM` must be disabled; its binary/plugin/deb recipe is
not another owner of the same output paths. Source changes participate in the
existing stage manifest and incremental package-clean contract.

Linux patch `0066-tdvp-riscv-dts-enable-gc2093-managed-clock.patch` includes the
validated camera DT configuration after the complete CPU0/mailbox/UART1 queue.
The post-build hook removes the vendor overlay's additive `S31canaan_isp`.
`tdvp-camera-modules.service` loads the guarded MIPI driver, verifies chip ID,
then loads the remaining modules. It rejects pre-existing VVCAM modules and
cleans up its own partially loaded queue on failure. The ISP service is only a
`multi-user.target` **want**, not a desktop/SSH requirement. Its closed device
policy permits named camera/I2C devices and never `/dev/mem` or whole classes.

The scalar daemon requires the VVCAM graph at `/dev/media0`; startup validates
its media-device model and fails closed if that assignment changes. Udev gives
the VVCAM nodes stable identities. `tdvp-camera-device` validates the primary
capture interface; it does not mistake `/dev/video0` (MVX codec) for a sensor.
The desktop launcher selects that device and Wayland SHM preview at NV12 1080p.
Discovery/service availability is not successful frame-capture acceptance.

Capture stays at 1920x1080 NV12, while the preview filter converts/copies it to
640x360 BGRA for the small display. The final launcher disables threaded
demuxing/readahead and uses separate `--demuxer-lavf-o-add` options: assigning
`--demuxer-lavf-o` would overwrite the low-latency profile's `fflags=+nobuffer`.
The original full-resolution path retained MMAP packet buffers and logged
`Bad file descriptor` while returning them after the V4L2 device had closed.
The final actual launcher completed a bounded 90-frame Wayland SHM run without
those ownership/descriptor warnings or MPV error-level messages. This is not
a 30 FPS preview or pixel-perfect visual-acceptance claim: playback took about
6.3 seconds excluding startup/shutdown, and the ISP still logged occasional
dequeue return `16` during slower consumer operation. G_PARM remains unsupported.

Run `bash buildroot/tools/test-tdvp-camera-package.sh` for source hashes,
production prepare-hook replay **twice**, and image/service contract checks.
Long-duration operation, cold boot and VGLite coexistence still require physical
validation; the bounded package/preview checks below do not replace those gates.

### Package validation on 2026-09-07

An independent copy of the prepared SDK output was used in Ubuntu 24.04.4;
the original SDK output was mounted read-only. Actual Buildroot Kconfig,
download/hash verification, generic-package and kernel-module backends built
and installed both packages. A clean package build and a subsequent
`tdvp-camera-isp-reconfigure` both passed. The final rootfs camera guard,
41-patch validation, real queue reconciliation, renderer stack lock and image
source contract passed. The real DTB passed the camera guard and all ten
negative mutations, as well as the UART1 guard. Its SHA256 is **identical** to
the physically validated candidate:
`2d273d6f9871f000922db3c8ab723d6ae48db40b42c156411c92dc85c6ac4ce2`.

Actual installed package artifacts were copied to a separate board directory,
not over system files. The exact units were loaded from `/run` with read-only
test bind mounts and an extra time limit. In the final two cycles, 60 complete
1080p NV12 frames passed at **29.956 / 30.013 FPS**, sequence `0..59`. The actual
desktop launcher then completed the 90-frame preview as user `tdvp`. Stopping
the module unit stopped the ISP first, unloaded all five modules and returned
MCLK gate state to `0x003a7708`. No new kernel lifecycle WARNING/BUG/Oops/Call
Trace appeared. CPU1 remained ready at sequence `1401/1401`; greetd and Labwc
were not restarted. Temporary units and udev rules were removed afterward.

Evidence: `.tmp/device-validation/camera-package/` and the board's
`/var/lib/tdvp-repair-backups/20260907/camera-candidate/package-v4/`. No SD boot
file or production acceptance marker was changed by these package tests.

## Remaining release requirements

1. Validate cold-boot startup in the complete image, including final stripping,
   rootfs and SD-image guards; the isolated package build is not that image.
2. Recheck CPU1/display/keyboard/radio ownership after persistent deployment,
   including a missing/disconnected camera and ISP restart behavior.
3. Implement truthful camera device discovery/status and test the old daemon's
   unsupported controls, crop/selection and frame-interval API behavior.
4. Test camera preview through Wayland alongside VGLite and CPU1, including
   process restart and longer-running buffer/clock lifecycle checks.
5. Only after this works, update hardware status and produce/deploy a complete
   image. No fake camera node or acceptance marker.

Moving the camera to CPU1 is a separate fallback, not an action taken here.
The donor MPP startup initializes VO, audio, DMA and other shared peripherals;
simply enabling `RT_USING_MPP` would violate the current ownership contract.
