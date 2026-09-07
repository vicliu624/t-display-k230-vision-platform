# CPU1 vision migration — candidate implementation, not enabled in images

The intended production split is:

- CPU1: GC2093/CSI2, ISP/capture, private visual buffers, AI preprocessing,
  KPU inference and postprocessing.
- CPU0: Linux, applications, Wayland/VGLite, display, networking and presentation
  of asynchronous visual results.

This directory is an **in-progress migration**. Merely building it does not
  switch the production image to CPU1 camera ownership. Do not boot the
  candidate CPU1 firmware with the existing CPU0 camera device tree.

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
  prepares I2C4's exact 100 MHz functional clock before controller access,
  using hardware semaphore 0 and an independently mapped CMU. It observes,
  but never retunes, PLL0; it preserves the shared APB divider and all other
  peripherals. Unsupported PLL state, timeout, failed readback, controller
  mapping, bus-speed setup or bus registration is latched and blocks MPP.
  The hook requires I2C4 master-only ownership. Its flag defaults off and the
  unselected BSP entry is unchanged. The production build does not yet opt in.
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
| `0x20000000–0x40000000` | Existing 512 MiB Linux CMA, unchanged |

All new regions must be reserved `no-map` in Linux before CPU1 starts. CPU1
and the bridge map transport noncached. Each side writes separate control
regions. CPU1 publishes a completed descriptor and data before its sequence;
Linux releases that sequence only after copying it. Three outstanding records
cause new frames to be dropped. No timeout reclaims an unread slot. Invalid
acknowledgements or sequence overflow fail closed. Epoch changes are not a
license for in-place CPU1 reset while DMA or Linux readers are active.

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
bash buildroot/tools/test-tdvp-cpu1-vision-dtb.sh /path/to/fully/patched/linux
bash buildroot/tools/test-tdvp-cpu1-capture.sh /path/to/pinned/canmv_k230/src/rtsmart/mpp
```

The capture test compiles against the actual pinned MPI headers with mocked
operations: 25 lifecycle/failure cases. The transport test covers packed row
copying, backpressure, leases, stale epochs, invalid releases and overflow.
These tests do not prove physical cache coherency, camera operation or FPS.
The early-I2C test applies the actual vision patch with zero fuzz to a temporary
copy of the pinned BSP, extracts its real BOARD entry and executes it with the
production clock helper and hook. Twenty-seven cases cover mapping, PLL state,
exact divider selection, write/readback failure, semaphore timeout/timer wrap,
controller initialization failure and repeat-call latching. Five illegal I2C
ownership configurations fail compilation. A read-only board PLL/CMU snapshot
is one successful no-write case, not evidence that this firmware has booted.
The existing real-firmware CI preflight runs this test after fetching its BSP;
it also structurally validates the real vision patch, not a substitute fixture.
The GPIO model runs 100,000 concurrent updates per core against the actual
patch helper. Pinmux tests cover the four-pad whitelist and failure/conflict
paths; MPP initialization covers eight stages plus early-I2C failure. The power regression runs
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
This is a cross-link check only: ISP/MCLK/AI power and clock initialization,
paired image integration and physical camera acceptance remain outstanding.

## Required before production activation

1. Enable `tdvp,cpu1-gpio-mask = <0x00200000>` on GPIO0 and
   `tdvp,cpu1-vision-domains` on the power provider in the ownership device
   tree. The software guards are implemented, but their physical coexistence
   behavior has not been tested. Simply disabling Linux CSI/I2C is insufficient.
2. Enable `tdvp,cpu1-i2c4-clock-sharing` on CMU and retire Linux camera/AI
   clock providers without gating active CPU1 clocks,
   preserve Linux display/VGLite clocks, and validate CPU1 power/clock setup
   before sensor and KPU access. Do not import the full CanMV board initializer:
   it configures functions outside CPU1's ownership. `rt_hw_i2c_init` is a
   BOARD initializer and immediately touches I2C registers: install the matched
   hook/header/clock source and select `RT_USING_TDVP_CPU1_VISION` together.
   Its semaphore/100 MHz preparation precedes controller access, not MPP.
3. Atomically add Linux MMZ/transport reservations and retire Linux camera,
   ISP, KPU/AI2D bindings and the CPU0 ISP service. Add an ownership gate that
   rejects mixed firmware/DT/profile combinations and checks GPIO protection.
4. Wire the curated MPP and ROMFS build into `build-rtsmart.sh`, preserve the
   raw OpenSBI entry/20 MiB slot guard, package the bridge and its `video` udev
   rule, and strengthen DT reservation checks before installing the module.
5. Validate sensor CSI2 mode/MCLK/reset on hardware, then physical frame bytes
   through the Linux bridge, slow-reader/close/reopen/error paths, and
   CPU1+Wayland/VGLite coexistence with rollback prepared.
6. Integrate the CPU1 AI2D/nncase/KPU model path and typed result delivery;
   driver registration and a frame transport test are not KPU inference.
7. Run the production staging/ownership/image checks, build the complete PR
   image, and perform hardware acceptance. Until then this is not a new
   validated CPU1-camera image, even if all host tests are green.
