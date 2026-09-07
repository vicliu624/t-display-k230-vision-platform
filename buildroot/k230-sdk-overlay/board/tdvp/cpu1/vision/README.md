# CPU1 vision migration — candidate implementation, not enabled in images

The intended production split is:

- CPU1: GC2093/CSI2, ISP/capture, private visual buffers, AI preprocessing,
  KPU inference and postprocessing.
- CPU0: Linux, applications, Wayland/VGLite, display, networking and presentation
  of asynchronous visual results.

This directory is an **in-progress migration**. Merely building it does not
  switch the production image to CPU1 camera ownership. Do not boot the
  candidate CPU1 firmware with the existing CPU0 camera device tree.

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
bash buildroot/tools/test-tdvp-cpu1-transport.sh
bash buildroot/tools/test-tdvp-cpu1-capture.sh /path/to/pinned/canmv_k230/src/rtsmart/mpp
```

The capture test compiles against the actual pinned MPI headers with mocked
operations: 25 lifecycle/failure cases. The transport test covers packed row
copying, backpressure, leases, stale epochs, invalid releases and overflow.
These tests do not prove physical cache coherency, camera operation or FPS.

On the LAN Ubuntu 24.04 validation container, the pinned CPU1 musl toolchain
cross-linked the real MPI/ISP libraries with both executables:

```sh
bash buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision/build-capture-probe.sh \
    /path/to/pinned/mpp /path/to/riscv64-unknown-linux-musl- /path/to/output
```

The camera-only RT-Smart kernel and a subsequent ROMFS/worker kernel also
linked. The Linux bridge compiled against the existing 6.6.36 scalar kernel
with that kernel tree mounted read-only. The upstream RT-Smart linker script
still emits an RWX LOAD-segment warning; no claim of hardened ELF permissions
is made. Nothing in this migration has been deployed to the board yet.

## Required before production activation

1. **GPIO arbitration:** RT-Smart GPIO21 sensor reset shares the GPIO0 data
   and direction registers with Linux. RT-Smart uses hardlock 0; Linux
   `gpio-k230.c`/bgpio currently uses only local spinlocks and cached port
   state. Simply disabling Linux CSI/I2C is insufficient: Linux writes may
   overwrite CPU1's reset bit, or CPU1 read/modify/write may lose Linux bits.
   Add cross-core protection preserving the remote-owned bit, including
   direction and suspend/resume paths, and test it before booting.
2. Configure only camera pins 7/8 (I2C4), 13 (MCLK1) and 21 (reset) on CPU1.
   Do not copy the full CanMV pinmux initializer: its function-selection helper
   clears alternate pins and can modify Linux-owned functions.
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
