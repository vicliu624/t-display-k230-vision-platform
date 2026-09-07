# CPU1 AI and vision migration — candidate implementation, not enabled in images

The intended production split is:

- CPU1: the AI subsystem (KPU/GNNE, AI2D, FFT, AI working memory and interrupt/
  driver ownership), GC2093/CSI2, ISP/capture, private visual buffers,
  preprocessing, inference, CPU model partitions and postprocessing.
- CPU0: Linux, applications, Wayland/VGLite, display, networking and presentation
  of asynchronous visual results.

The complete AI ownership requirement was confirmed on 2026-09-07. Do not
leave a second Linux AI driver/runtime owner or treat a camera-only firmware
as its completion. Dedicated AI resources belong to CPU1; shared system
PLLs, power controllers and DMA infrastructure need explicit allocation and
coordination, not wholesale CPU1 reset/init. VGLite/display remain on CPU0.
Audio capture/playback initially remains Linux-owned; a future ASR client
sends bounded PCM chunks to CPU1 and receives text. No ASR model is implemented
or accepted by these changes. See `docs/k230-offline-asr.md` and its Chinese
counterpart for the revised service boundary and memory constraints.

This directory is an **in-progress migration**. Merely building it does not
  switch the production image to CPU1 camera ownership. Do not boot the
  candidate CPU1 firmware with the existing CPU0 camera device tree.
  `build-rtsmart.sh` now builds the AI/vision firmware through its normal
  production entry point. Linux profile/bridge packaging has not yet switched;
  `post-image.sh` and the image verifier reject that mixed pair before it can
  be delivered. This intermediate revision is intentionally not releasable.

The former `vpl-camera` Linux V4L2 desktop demo is retired: no menu entry or
launcher is shipped, including in reused Buildroot targets. A replacement demo
has not been designed or selected. Future Linux previews must consume CPU1
results, not open the camera directly. Removing the old demo does not itself
complete the camera ownership migration described below.

## Implemented and cross-built

- A camera-only MPP kernel initializer with explicit strong dependencies and
  latched stage failures; no VO, connector, audio, codec, global PM or default
  media-frequency initialization.
- A curated kernel archive list, including the GC2093 sensor and media clock
  helpers. Other stale SDK archives cannot be whole-linked accidentally.
- A separate RT-Smart userspace MPI capture backend, fixed initially to
  GC2093 CSI2 1920x1080@30, NV12, six private VB buffers, header ISP database.
- A bounded 30-frame diagnostic and an asynchronous worker. Neither claims
  model inference. The worker waits for a Linux request; peer-heartbeat loss
  stops capture, faults are latched, and failed teardown retains the process
  and potentially DMA-owned buffers.
- Embedded ROMFS mounting and LWP launch glue; no CPU1 SD/USB/network startup.
- A noncached three-slot SPSC frame transport and Linux `poll/read` copy bridge.
  The bridge accepts no user physical addresses and exports no `mmap`.
- Camera-only pinmux for 7/8 (I2C4), 13 (MCLK1) and 21 (reset), preserving
  voltage/reserved bits and refusing an existing alternate I2C4 route.
- Opt-in Linux GPIO0 cross-core arbitration (`0067`): hardware semaphore 0,
  live register RMW, CPU1 GPIO21 preservation, timeout refusal, and no Linux
  reset/clock-off/system-suspend while the shared controller is active.
- Opt-in AI/DISP power retention (`0068`), including the ISP/display shared
  domain. Linux does not cycle already-on domains and rejects power-off while
  CPU1 ownership is enabled. Initialization failures are not silently ignored.
- Opt-in shared LS clock arbitration (`0069`): serialize Linux UART/I2C/GPIO
  CMU register writes with hardware semaphore 0, retain the shared LS APB
  parent and reject Linux-owned declarations for I2C4 fields.
- The paired RT-Smart BOARD hook (`0001-rtsmart-i2c4-early-clock.patch`)
  defers all I2C4 access until the explicit ownership-gated startup. The hook
  then prepares I2C4's exact 100 MHz functional clock before controller access,
  using hardware semaphore 0 and an independently mapped CMU. It observes,
  but never retunes, PLL0; it preserves the shared APB divider and all other
  peripherals. Unsupported PLL state, timeout, failed readback, controller
  mapping, bus-speed setup or bus registration is latched and blocks MPP.
  The hook requires I2C4 master-only ownership. Its flag defaults off and the
  unselected BSP entry is unchanged. The production firmware builder now opts
  in together with the other paired sources; the Linux image switch is pending.
- The paired component patch (`0002-rtsmart-defer-vision-components.patch`)
  prevents automatic MPP/GNNE/AI2D initialization before main. MPP is called
  explicitly after GRANT. The candidate now follows MPP with a strong,
  once-only AI startup chain: dedicated AI clock/reset preparation, GNNE,
  AI2D and the CPU1 hardware FFT driver. A failed stage blocks READY/worker
  launch, latches the failure and never retries or resets live sibling engines.
- AI clock setup requires Linux-held AI power, repaired SRAM, both ISP/AI DDR
  ports and the verified 1.6 GHz PLL0. CPU1 prepares only CMU `0x08` (800 MHz
  AI core/400 MHz AXI) and the dedicated AI reset at `0x91101014`, with readback
  checks and a bounded reset wait. It never writes shared PLL/power/DDR or GPU
  clock registers. The single startup reset affects GNNE/AI2D/FFT together;
  it is not a per-job recovery mechanism.
- `0003-rtsmart-ai-initialization-errors.patch` makes paired GNNE/AI2D
  initialization establish mappings, events, wait queues, file operations and
  interrupt handlers before device publication/unmasking. It propagates KPU
  hardlock reservation failures. It also fixes the BSP hardlock mapping check:
  a failed mapping must not become the apparently valid address `0xa0`.
  Unselected GNNE/AI2D initializer bodies remain unchanged. Their upstream
  runtime operations still require bounded/fault-aware integration before a
  model service is released; successful registration is not that acceptance.
- CPU1 hardware FFT uses the pinned MPI ioctl ABI with bounded, serialized
  PIO/FIFO transfers. The upstream FFT SDMA dependency is deliberately absent:
  system SDMA stays outside CPU1's exclusive AI allocation. Inputs are
  validated before MMIO, hardware/software waits are bounded, and hardware or
  ownership faults latch AI failure and propagate to the main owner heartbeat.
  Runtime readers use sequence-checked, fresh publications without advancing
  the main thread's state machine; an in-progress publication gets a bounded
  retry. No numerical FFT or model execution on the board is claimed yet.
- A shared, versioned ownership policy and RT-Smart startup adapter. Linux
  publishes OFFER only after preparing/retaining resources, CPU1 requires an
  advancing heartbeat before writing HELLO, Linux validates the matching
  live CPU1 cookie before GRANT, and CPU1 initializes only on that grant.
  The Linux adapter now validates both registered `no-map` reservations,
  disabled Linux camera/AI declarations and exact named supplier references.
  `0070` verifies actual GPIO/power/clock initialization, not just DT flags.
  Before OFFER it holds AI/DISP runtime-PM references, enables the three shared
  PLL divide-by-four suppliers plus ISP P1/AI P3 DDR port gates, and obtains
  rate-exclusive references (including their parent protection). The two DDR
  gates use Linux CCF's shared-register serialization; CPU1 never writes that
  register. Ownership contract 2 refuses contract-1 firmware which did not
  require these retained ports. No PLL retuning is performed. This remains a
  candidate path, not selected by the production image.
  Separate 128-byte records, sequence-checked snapshots and fences prevent
  mixed publications; boot/peer timeouts and identity changes latch faults.
  Startup is attempted once. Failure does not reset shared resources or free
  possibly DMA-owned buffers. The worker also monitors the kernel ownership
  heartbeat and stops on lost/invalid ownership.
- The Linux ownership heartbeat starts at probe and continues without an open
  frame reader. Reader close requests STOP but never releases boot ownership.
  Frame epochs now equal the granted CPU1 cookie, preventing a fresh Linux
  reader from accepting an old boot's valid-looking frame control block.
  Resources remain held after OFFER, including timeout/fault; safe quiesce and
  regrant are not implemented. The fixed boot DT must not be removed or changed
  by live overlays. The bridge pins its module, omits hot-unbind attributes,
  and rejects system suspend/hibernation. Screen DPMS/lock remains available.
  `0070` also hides GPIO/power hot-unbind attributes and pins AMP instances;
  it does not change the GPU renderer or display clock operations.
- Camera clock setup before MPP registration: explicitly prepare ISP CFG/core/
  HCLK, CSI2 pixel and sensor MCLK1 fields; verify clock writes and preserve
  CSI0/CSI1 and MCLK0/MCLK2 fields. Require powered DISP/ISP, DDR P1 access and
  the pinned board's PLL rates before writing. No shared PLL, power, DDR,
  I2C, GPU/VO clock or reset writes. The 23.76 MHz MCLK matches the selected
  GC2093 CSI2 mode table. This guard is not the missing Linux ownership-ready
  handshake: the paired Linux/RT-Smart adapters supply that protocol, but the
  complete paired image still requires production wiring and hardware tests.
- A compiled candidate device-tree wrapper and ownership include, with the
  new MMZ/transport reservations and GPIO/power policies. Linux camera/AI
  devices and dedicated clock providers are disabled. This wrapper is NOT
  selected by the production profile yet and is not a live DT overlay.

## Memory and wire contract

| Range (end exclusive) | Owner / use |
| --- | --- |
| `0x10000000–0x13ff0000` | Existing CPU1 OpenSBI/RT-Smart allocation |
| `0x13ff0000–0x14000000` | Existing CRC/ping mailbox, unchanged |
| `0x14000000–0x1c000000` | New 128 MiB CPU1 MMZ, never exposed to Linux applications |
| `0x1c000000–0x1e000000` | New 32 MiB transport reservation |
| `0x1c000000–0x1d800000` | Three 8 MiB frame slots within transport |
| `0x1dff0000–0x1e000000` | Transport control window, final 64 KiB |
| `0x1dff1000–0x1dff2000` | Ownership window inside control; first 256 bytes are the two owner records |
| `0x20000000–0x40000000` | Existing 512 MiB Linux CMA, unchanged |

All new regions must be reserved `no-map` in Linux before CPU1 starts. CPU1
and the bridge map transport noncached. Each side writes separate control
regions. CPU1 publishes a completed descriptor and data before its sequence;
Linux releases that sequence only after copying it. Three outstanding records
cause new frames to be dropped. No timeout reclaims an unread slot. Invalid
acknowledgements or sequence overflow fail closed. Epoch changes are not a
license for in-place CPU1 reset while DMA or Linux readers are active.

The ownership window is not the legacy user-writable ping mailbox. CPU1 maps
it noncached but does not write until observing a live OFFER. Existing or
malicious arbitrary physical writers are not authenticated by this protocol;
matched image/DT reservations and kernel-only access remain mandatory. Linux
must retain shared suppliers after any GRANT, including on timeout or module
teardown, until a real quiesce/reboot policy permits release. The candidate
therefore pins successful bridge/provider instances and prohibits hot unbind;
there is no supported in-place ownership release/regrant yet.

The initial Linux read record is a 64-byte `tdvp_vision_frame_header` followed
by packed NV12, exactly 3,110,400 payload bytes at the initial resolution.
One exclusive reader uses a buffer large enough for the whole record;
`O_NONBLOCK` returns `EAGAIN` until data is available. Short buffers receive
`EMSGSIZE`. Failed `copy_to_user` does not acknowledge the leased record.
This is bounded-copy asynchronous delivery, **not zero-copy or DMA-BUF**.

## Validation performed on 2026-09-07

Host regression tests:

```sh
bash buildroot/tools/test-tdvp-cpu1-vision-init.sh
bash buildroot/tools/test-tdvp-cpu1-vision-pins.sh
bash buildroot/tools/test-tdvp-cpu1-transport.sh
bash buildroot/tools/test-tdvp-cpu1-gpio-amp.sh
bash buildroot/tools/test-tdvp-cpu1-power-amp.sh /path/to/pristine/pinned/linux
bash buildroot/tools/test-tdvp-cpu1-clock-amp.sh /path/to/pinned/linux
bash buildroot/tools/test-tdvp-cpu1-i2c4-early.sh /path/to/pinned/maix3
bash buildroot/tools/test-tdvp-cpu1-ownership.sh /path/to/pinned/maix3
bash buildroot/tools/test-tdvp-cpu1-linux-owner.sh
bash buildroot/tools/test-tdvp-cpu1-camera-clock.sh /path/to/pinned/maix3 /path/to/pinned/mpp
bash buildroot/tools/test-tdvp-cpu1-ai.sh /path/to/pinned/maix3 /path/to/pinned/mpp
bash buildroot/tools/test-tdvp-cpu1-vision-dtb.sh /path/to/fully/patched/linux
bash buildroot/tools/test-tdvp-cpu1-capture.sh /path/to/pinned/canmv_k230/src/rtsmart/mpp
```

The capture test compiles against the actual pinned MPI headers with mocked
operations: 25 lifecycle/failure cases. The transport test covers packed row
copying, backpressure, leases, stale epochs, invalid releases and overflow.
These tests do not prove physical cache coherency, camera operation or FPS.
The ownership test executes the common policy and actual RT-Smart startup,
including 14 startup scenarios: absent/stale/invalid offers, live startup,
peer loss, each initialization/launch failure, grant loss during MPP and
mapping failure. It patches actual pinned component sources with zero fuzz,
compiles them and checks that automatic MPP/AI calls are absent while
unselected source bodies remain unchanged. Those RT-Smart tests do not prepare
real Linux resources, authenticate a mismatched image, or prove hardware behavior.
The separate Linux adapter test executes its production C with DT/PM/clock/MMIO
primitives mocked: 51 preparation refusals must issue no offer and leak no
references. It also checks Linux/RT-Smart publication compatibility, a complete
handshake, sequence wrap, torn snapshots and lifetime retention after boot/peer
timeouts. Bridge FD/PM lifecycle assertions are source checks, not runtime
hardware acceptance. The power and clock regressions now apply/validate `0070`
as well and execute actual provider-readiness logic, including an unregistered
or unprotected shared clock refusing readiness.
The early-I2C test applies the actual vision patch with zero fuzz to a temporary
copy of the pinned BSP, extracts its real BOARD entry and executes it with the
production clock helper and hook. All scenarios first require BOARD init and
an unauthorized explicit call to perform zero hardware accesses. Twenty-seven
cases then cover mapping, PLL state,
exact divider selection, write/readback failure, semaphore timeout/timer wrap,
controller initialization failure and repeat-call latching. Five illegal I2C
ownership configurations fail compilation. A read-only board PLL/CMU snapshot
is one successful no-write case, not evidence that this firmware has booted.
The existing real-firmware CI preflight runs this test after fetching its BSP;
it also structurally validates the real vision patch, not a substitute fixture.
The camera-clock test checks real pinned register-layout declarations and runs
22 successful/failure scenarios against the production helper. Every write is
restricted to camera clock fields, including checks that changing dividers
occurs with the affected clocks gated. Shared-resource reads, mapping failure,
power/PLL refusal, ignored writes and a changing prerequisite are covered.
One case uses the actual board's read-only CMU/PLL/power snapshot. This is not a
live register-write or camera test. CI obtains the MPP layout header from the
exact pinned Git commit even when its compute-only sparse checkout omits MPP.
The AI regression compiles against pinned BSP register/address headers and the
actual MPI FFT ABI. It runs 54 scenarios: 22 AI clock/reset cases, five startup
dependency outcomes, 18 FFT initialization/runtime cases (including 84 legal
size/layout/direction combinations), and nine GNNE/AI2D/hardlock initialization
cases. For the latter it applies the real patch with zero fuzz to temporary
BSP copies and executes the extracted initializer bodies, not a rewritten
fixture. The FFT FIFO model checks transfer counts and failure handling, not
FFT mathematics. CI fetches the exact pinned MPP type/ioctl headers for this
regression. A 32-bit DFS ioctl comparison bug was caught and fixed by the
strict-warning host build; transient ownership publication is also tested.
The GPIO model runs 100,000 concurrent updates per core against the actual
patch helper. Pinmux tests cover the four-pad whitelist and failure/conflict
paths; MPP initialization covers eight stages plus early-I2C and camera-clock
failure. The power regression runs
the complete production driver after applying `0068` to actual kernel source,
with Linux/MMIO mocked, including probe retry, no cycling of live domains,
power-off refusal and error cleanup. CI runs this against `linux-patch` output
with `--patched`, first checking that the patch can be reversed without fuzz.
The candidate DTB test compiles both the actual CPU0 board and the vision
wrapper, resolves phandles, and compares all 273 existing nodes with a narrow
property allowlist. Twenty-two invalid candidates (Linux ownership, missing
protection/reservation, overlap, mailbox/UART1 regression) are rejected. This
proves declarations and absence of unrelated DT drift, not runtime ownership.
The clock regression executes the actual patched CCF operations with a
read-to-acquire semaphore model, 100,000 concurrent iterations on each side,
timeout refusal, APB retention and unchanged non-AMP/GPU controls. The complete
clock driver compiled as a RISC-V object with `W=1` on Ubuntu 24.04. Neither
the model nor compilation proves physical clock timing or camera operation.

On the LAN Ubuntu 24.04 validation container, the pinned CPU1 musl toolchain
cross-linked the real MPI/ISP libraries with both executables:

```sh
bash buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision/build-capture-probe.sh \
    /path/to/pinned/mpp /path/to/riscv64-unknown-linux-musl- /path/to/output
```

The camera-only RT-Smart kernel and a subsequent ROMFS/worker kernel also
linked, including the four-pad initializer. The Linux bridge and GPIO driver
compiled against the existing 6.6.36 scalar kernel with that kernel tree
mounted read-only; the power-domain driver also compiled as a RISC-V object.
That object's `W=1` build retains the upstream missing-prototype warning for
`k230_pd_probe`. The GPIO validation module must never be loaded alongside the
built-in GPIO driver. The upstream RT-Smart linker script
still emits an RWX LOAD-segment warning; no claim of hardened ELF permissions
is made. Nothing in this migration has been deployed to the board yet.

The early-I2C revision also cross-linked into the actual camera-only RT-Smart
kernel, including the guarded BOARD entry and both public status/clock symbols.
Its `rtthread.elf` SHA-256 is
`27be0f41e07661a999218866993bde9ebd7be6f3d474e9e941cef212957da408`;
`rtthread.bin` SHA-256 is
`1356796781930e3de16dd4f643c12eed3fa8b6c3150b0a253cdac6efba2788ec`.
This records the earlier I2C-only cross-link check. Subsequent camera-clock
changes must be cross-linked and verified independently; a linked kernel does
not prove the paired image, AI initialization or physical camera acceptance.

The subsequent camera-clock kernel cross-link passed on the same pinned SDK:
`rtthread.elf` SHA-256
`b948e6ed6bf53f9a6fbac26087eacc8949470d687243b8b597660b8899990b6d`,
`rtthread.bin` SHA-256
`cc0df5e6c1db14e064321817d1b58dbf4cb170f684b043f6ea9d9b417ab66e37`.
The new clock helper is linked into the curated MPP path. The candidate DTB
still passes the 273-node baseline comparison and 22 invalid-candidate tests,
and no enabled Linux composite clock may write camera CMU registers.

## Required before production activation

The ownership-gated candidate also passed the real Ubuntu 24.04 cross-link
with the updated heartbeat-monitoring worker embedded in ROMFS:
`rtthread.elf` SHA-256
`797569e6805ddfb19d3737ed7d8590c13449be8334cd6ab71b06c59d70bb5057`,
`rtthread.bin` SHA-256
`fe5bae87cbc1e8b9c04f39aa01b52ce80171e65064b5c20bc98f86ac012a99b1`,
worker SHA-256
`7803cb5ae3a38dc4988694c71bc86341c07b979779fc0bb85c03b76b13a4f398`.
The kernel includes the explicit startup gate and no automatic AI initializer.
That earlier cross-link predates the Linux resource adapter. The subsequent
paired adapter revision passed a real patched Linux 6.6.36 `vmlinux`/modules
build and the bridge's `W=1` compilation/modpost on Ubuntu 24.04. All three
readiness exports exist in the linked kernel. The candidate DTB passed the
273-node comparison, 26 invalid-candidate tests and three camera-clock writer
refusals. Renderer lock and VGLite session-gate host regressions still pass.

The earlier contract-1 paired-source build evidence was:

- Linux bridge SHA-256: `eea1a87bfff9bdc51a3c5ccde15727b73981c11844ae20ed8cc57de81e2f5d2c`.
- CPU1 worker: `2d38d00966760922a9966c0f7435f95f5b4b04fd5b53b484df84c6c9ca205c36`.
- RT-Smart ELF with that worker in ROMFS: `0e5c1af45d403f6b06a576057d4106a29cabed423859fe6683329951cec3ec1b`.
- RT-Smart binary: `32725bfc201b2bbf4423cc20fa929417ed0a4cd454ea0925a4892ab64f968c9d`.

This is build evidence only, not model inference, FFT execution, full AI SRAM/
DMA ownership audit, complete image build or board deployment. Existing SDK
warnings (including the RT-Smart RWX LOAD segment) remain.

The contract-2 AI initializer/FFT candidate also passed the real Ubuntu 24.04
regressions. The bridge linked with `W=1` against the patched Linux kernel;
its SHA-256 is `515cf20bc94d8076a749f436325960aa6505ec72af95b999e2293cf92f2c63b1`.
The candidate DTB passes the 273-existing-node comparison, 28 invalid-candidate
tests and four forbidden AI/camera-clock writer tests. Renderer stack lock,
VGLite session gate, the real 45-patch Linux queue and stale-patch reconciliation
all pass. The final-source RT-Smart cross-link includes the ownership gate,
controlled GNNE/AI2D/FFT initializers and asynchronous worker in ROMFS:

- Worker SHA-256: `600fdd7c136c8a04880522ad509f1035bd59e5d5b34b17242325f5148f1ae2b6`.
- RT-Smart ELF: `3f1f2ead7d105ab92d788d19836e751b3f51d5b801a3dd9c8b9e95f20b890810`.
- RT-Smart binary: `c262d2652aacc4b473bf5e795409d93ab7ac63f3f34720109d19dbdf45a44780`.

This does not activate the production profile or deploy a board image; the
remaining release requirements still apply.

## Production firmware build and packaging-pair gate

`build-rtsmart.sh` now invokes `stage-build.sh` to apply the three vision
patches, install the curated kernel sources, select the explicit RT-Smart
drivers and set the GC2093 CSI2/MMZ configuration through real SDK syncconfig.
It rebuilds the sensor and media-clock archives (removing old archive members),
links the MPI worker, generates its embedded ROMFS and links the RT-Smart
kernel followed by OpenSBI. `verify-kernel.sh` requires the actual AI/vision
symbols and rejects automatic AI startup and Linux-owned display/audio/SDMA
initializers. The existing raw OpenSBI entry and 20 MiB slot checks remain.
`mpp-source-paths.txt` limits the partial checkout to real build dependencies,
not the SDK's unused audio/codec libraries and demos. Cached fixed commits do
not require an origin refresh. Pristine BSP regression inputs are extracted
with individual `git show` calls; `git archive` on the host Git version tried
to prefetch unrelated large blobs even when given narrow pathspecs.

The firmware manifest declares `resource_owner=cpu1-ai-vision`, ownership
contract 2, the exact MMZ/transport ranges, GC2093 CSI2, GNNE/AI2D/FFT and the
embedded worker hash. `verify-pair.sh` checks those fields against the actual
Linux DTB, its disabled Linux owners, memory reservations, GPIO protection,
DDR/PLL references and power-domain holds. Both post-image (before genimage)
and standalone image verification call this gate. CI compares the DT candidate
with the real preflight firmware manifest, rather than trusting a substitute
manifest to represent the built payload. Twenty-three mixed/missing-contract
cases are rejected by the packaging-pair regression.

The first real production-entry raw payload built on Ubuntu 24.04 is 3,560,632
bytes at entry `0x10000000`, SHA-256
`6d0a09077b2d3138229a80b425ea0727f4d51b33a5984654d7ff2b8ba2ab1789`.
It passed the boot guard against the built CPU0 U-Boot header and the candidate
DTB pair guard. It is not a complete SD image, not deployed, and not hardware
acceptance. The final entry also passed the complete CI preflight, then a
same-SDK/output rebuild with the container network disabled. An obsolete
sensor archive member deliberately inserted before rebuilding was removed.
That reused-output check is now part of the committed CI preflight as well:
it builds real firmware twice and still exercises all five RT-Smart/OpenSBI
failure-propagation cases. This is not a claim of bit-reproducible CPU1 builds;
upstream build timestamps/paths have not been audited for that property.

The remaining release requirements are:

1. Enable `tdvp,cpu1-gpio-mask = <0x00200000>` on GPIO0 and
   `tdvp,cpu1-vision-domains` on the power provider in the ownership device
   tree. The software guards are implemented, but their physical coexistence
   behavior has not been tested. Simply disabling Linux CSI/I2C is insufficient.
2. Enable `tdvp,cpu1-i2c4-clock-sharing` on CMU and retire Linux camera/AI
   clock providers without gating active CPU1 clocks,
   preserve Linux display/VGLite clocks, and validate CPU1 power/clock setup
   before sensor and KPU access. Do not import the full CanMV board initializer:
   it configures functions outside CPU1's ownership. `rt_hw_i2c_init` is a
   BOARD initializer: install both deferral patches, hook/header, startup and
   clock sources, and select `RT_USING_TDVP_CPU1_VISION` together. The hook's
   semaphore/100 MHz preparation follows GRANT and precedes controller access.
   The camera helper now prepares its dedicated clocks, but first requires
   shared domains/DDR/PLLs to be ready. Linux's half now validates BOTH
   reservations and real GPIO/power/clock suppliers, holds the named resources
   and drives ownership independently of frame opens. Validate its actual
   startup timing, PM/CCF holds and fault retention on the paired board image.
3. Activate the Linux ownership device tree and retire Linux camera, ISP,
   KPU/AI2D bindings and the CPU0 ISP/KPU acceptance services. The new pair gate
   already rejects a mixed firmware/DT; complete rootfs/profile checks with
   that Linux switch rather than weakening the gate to accept the old image.
4. Package the Linux bridge, boot loading and its `video` udev rule. Curated
   MPP, ROMFS and raw OpenSBI building are now in the production CPU1 entry;
   keep the raw entry/20 MiB slot and ownership guards intact.
5. Validate sensor CSI2 mode/MCLK/reset on hardware, then physical frame bytes
   through the Linux bridge, slow-reader/close/reopen/error paths, and
   CPU1+Wayland/VGLite coexistence with rollback prepared.
6. Complete exclusive CPU1 AI ownership, including KPU/GNNE, AI2D, FFT, their
   interrupts and AI memory allocations, verified clock/power preparation and
   error propagation. Remove Linux bindings/direct-runtime acceptance paths.
   Integrate CPU1 nncase, fixed-model execution, FFT reference checks and typed
   asynchronous results. Driver registration and a frame test are not AI
   subsystem acceptance. Shared SRAM/DMA/PLL users must be inventoried before
   reallocating or resetting them; do not take Linux display/VGLite resources.
7. Run the production staging/ownership/image checks, build the complete PR
   image, and perform hardware acceptance. Until then this is not a new
   validated CPU1-camera image, even if all host tests are green.
