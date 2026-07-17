# T-Display K230 Vision Platform Architecture

This document defines the system architecture for the T-Display K230 Vision
Platform SDK.

Chinese version:

- [architecture.zh-CN.md](architecture.zh-CN.md)

The project is not an application, a demo collection, or a general-purpose
Linux distribution. It is a stable embedded vision platform that gives
developers a deterministic system image, a strict hardware abstraction layer,
a reusable vision runtime, and a small SDK surface for building camera and AI
workloads.

## 0. Current Adaptation Baseline

The first Linux baseline is derived from the official K230 Linux path:

```text
kendryte/k230_linux_sdk
  -> closest K230/CanMV Linux profile
  -> T-Display K230 hardware delta
  -> TDVP Buildroot board profile
```

The reference roles are intentionally separated:

- `kendryte/k230_linux_sdk` defines the first Linux boot, kernel, DTS,
  OpenSBI, U-Boot, Buildroot-overlay, and image-layout reference.
- `Xinyuan-LilyGO/T-Display-K230_canmv_rt` provides T-Display K230 hardware
  facts only.
- Upstream Linux is a later comparison and forward-port target, not the first
  bring-up baseline.

This prevents earlier exploratory boot experiments from becoming platform
architecture. Historical probe notes may remain as evidence, but they do not
define the current platform baseline.

## 1. Architecture Contract

The platform follows one fixed rule:

```text
Applications must depend on platform public APIs only: SDK, runtime, and BSP.
Applications must never depend on Linux device internals.
```

That means application code may use public `tdvp_*` APIs, including direct BSP
APIs for hardware-focused examples, but it must not call V4L2, DRM/KMS, evdev,
ioctl, sysfs, procfs, or kernel driver interfaces directly. Those details
belong below the BSP boundary.

The required stack is:

```text
+--------------------------------------------------+
| Layer 4: Applications                            |
| Optional user apps and demos                     |
| Uses SDK/runtime/BSP public APIs only            |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| Layer 3: Developer SDK                           |
| examples, templates, CLI tools, docs             |
| Stable developer workflow                        |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| Layer 2: Vision Runtime                          |
| pipeline, buffer manager, AI wrapper, event loop |
| Camera -> preprocess -> AI -> overlay -> display |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| Layer 1: BSP Hardware Abstraction Layer          |
| camera, display, input, lora, audio, storage     |
| Hides kernel, device tree, bus, and driver detail |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| Layer 0: Buildroot Linux                         |
| Linux kernel, BusyBox rootfs, init, firmware     |
| Deterministic minimal system image               |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| T-Display K230 Hardware                          |
| K230 SoC, camera sensor, display, input, I/O      |
+--------------------------------------------------+
```

## 2. Non-Goals

The platform must not drift into any of these shapes:

- A camera application.
- An AI application.
- A desktop Linux image.
- A Debian, Ubuntu, or package-manager based runtime.
- A generic Linux board support package exposed directly to users.
- A collection of unrelated demos.
- A split ecosystem where each app owns its own drivers and pipeline.

Applications may exist as examples, but they are not the platform's center of
gravity. The platform is the system, BSP, runtime, SDK, and API contract.

## 3. Layer Responsibilities

### Layer 0: Buildroot Linux

Buildroot provides the deterministic operating system base.

Responsibilities:

- Build the kernel, boot assets, root filesystem, and system image.
- Provide the minimum user space needed to start the runtime and SDK examples.
- Provide BusyBox-based init and shell utilities.
- Package platform libraries and demo binaries into the root filesystem.
- Keep image generation repeatable.

Allowed contents:

- Linux kernel.
- Device tree.
- U-Boot integration artifacts where needed.
- BusyBox.
- libc and runtime linker required by platform binaries.
- Platform libraries, runtime service, examples, and scripts.
- Required firmware blobs or model files when explicitly versioned.

Forbidden contents:

- Desktop environments.
- Window systems that are not required by the platform display path.
- Distribution package managers as runtime dependencies.
- Unbounded distro assumptions such as apt, systemd user services, udev rules
  owned by applications, or dynamic package installation during device use.
- App-specific system customization.

### Layer 1: BSP Hardware Abstraction Layer

The BSP is the platform's main portability asset. It owns all hardware access
and hides all kernel/device details from the layers above it.

Responsibilities:

- Open, configure, start, stop, and read from camera devices.
- Initialize and present frames to display devices.
- Read input events in a normalized format.
- Provide access to populated board capabilities such as LoRa, audio, storage,
  GPIO, SPI, I2C, power, radios, and environmental sensors through
  platform-owned APIs.
- Translate Linux driver behavior into stable platform semantics.
- Normalize error codes and device capabilities.

The BSP may use V4L2, DRM/KMS, evdev, ioctl, mmap, DMA buffers, sysfs, or
device-specific kernel APIs internally. Those APIs must not cross the BSP
boundary.

### Layer 2: Vision Runtime

The runtime is the execution engine for vision applications.

Responsibilities:

- Own the frame pipeline:

```text
camera -> preprocess -> AI -> overlay -> display
```

- Manage frame buffers and their lifecycle.
- Prefer zero-copy data movement where the BSP and hardware support it.
- Fall back to explicit copies only behind runtime APIs.
- Provide a unified AI inference wrapper.
- Provide a single event loop for camera, input, AI, display, and app callbacks.
- Isolate application code from device timing, buffer ownership, and hardware
  acceleration details.

The runtime is not a UI framework and not an app framework. It is a compact
vision execution model.

### Layer 3: Developer SDK

The SDK is the developer-facing layer.

Responsibilities:

- Provide examples that compile and run against the stable APIs.
- Provide application templates.
- Provide CLI tools for build, flash, package, and run workflows.
- Provide public headers and generated API documentation.
- Provide model conversion and deployment helpers when needed.

The SDK must teach a new developer this path:

```text
clone -> build -> flash -> run demo -> create app
```

### Layer 4: Applications

Applications are optional consumers of the platform. They are not part of the
platform design itself.

Allowed:

- Demo camera app.
- Demo AI app.
- User applications that use SDK, runtime, or BSP public APIs.

Forbidden:

- Direct V4L2, DRM/KMS, evdev, ioctl, sysfs, or procfs use.
- Per-app hardware initialization.
- Per-app kernel module selection.
- Per-app rootfs customization.
- Per-app pipeline ownership that bypasses the runtime.

## 4. Data Flow Model

The standard runtime data flow is:

```text
+---------+     +------------+     +---------+     +---------+     +---------+
| Camera  | --> | Preprocess | --> | AI      | --> | Overlay | --> | Display |
| BSP     |     | Runtime    |     | Runtime |     | Runtime |     | BSP     |
+---------+     +------------+     +---------+     +---------+     +---------+
     |                 |                |               |               |
     +-----------------+----------------+---------------+---------------+
                       |
                 Buffer Manager
```

The buffer manager is the shared ownership mechanism for all frame movement.
Every frame has one owner at a time. A frame may be borrowed by a stage, but
ownership must be returned or transferred explicitly through runtime APIs.

Required frame stages:

- Capture: acquire a frame from the camera BSP.
- Normalize: convert or describe frame format in runtime-owned terms.
- Preprocess: resize, crop, color convert, or normalize for AI input.
- Infer: execute an AI model through the runtime AI wrapper.
- Compose: produce display overlays or final display frames.
- Present: submit the final frame through the display BSP.
- Release: return buffers to the correct pool.

## 5. Control Flow Model

The platform uses a single runtime-owned event loop as the default control
model.

```text
+--------------------------------------------------+
| vision_runtime_run()                             |
|                                                  |
| 1. poll camera readiness                         |
| 2. acquire frame                                 |
| 3. run enabled pipeline stages                   |
| 4. poll input events                             |
| 5. dispatch app callbacks                        |
| 6. present frame if display output is enabled    |
| 7. release or recycle buffers                    |
+--------------------------------------------------+
```

The runtime may use worker threads internally for capture, inference, or
display pacing, but applications see a deterministic event/callback contract.

Applications should not create competing camera, display, or inference loops.
If an application needs custom behavior, it should register pipeline stages or
callbacks with the runtime.

## 6. Buildroot Design

The Buildroot layer is intentionally small.

### 6.1 Minimal Configuration Strategy

Buildroot configuration must be maintained as board-specific defconfigs under:

```text
buildroot/configs/
```

Each defconfig must produce a deterministic image for one board target. The
configuration should be reviewed as platform infrastructure, not app code.

Required strategy:

- Keep one canonical K230 target defconfig.
- Keep kernel config pinned and versioned.
- Keep device tree pinned and versioned.
- Keep package selection minimal.
- Treat any new package as a platform-level decision.
- Prefer static configuration over runtime discovery.

Forbidden strategy:

- Installing packages on the device after flashing.
- Requiring a distribution package manager.
- Adding packages because one demo app happens to need them.
- Letting application code modify rootfs layout.

### 6.2 Root Filesystem Design

The root filesystem must contain only the runtime environment required for the
platform.

Expected layout:

```text
/bin, /sbin, /lib, /usr/bin, /usr/lib
/etc/init.d/
/etc/vision-platform/
/opt/vision/
/opt/vision/bin/
/opt/vision/lib/
/opt/vision/models/
/opt/vision/examples/
/var/log/
/tmp/
```

Required files:

- Init script that starts the platform runtime or configured demo.
- Platform configuration file.
- Public runtime libraries.
- BSP implementation libraries.
- Optional demo binaries.
- Optional model files used by demos.

The rootfs overlay lives under:

```text
buildroot/rootfs_overlay/
```

The overlay must not become an app-specific dumping ground. Files added to the
overlay must be part of platform boot, runtime configuration, or documented
demo assets.

### 6.3 Kernel Module Selection Rules

Kernel configuration must be driven by platform needs only.

Required kernel subsystems:

- DRM/KMS for display.
- V4L2 media stack for camera.
- Input subsystem for buttons or touch.
- SPI and I2C for board peripherals.
- DMA and contiguous memory support where required by camera/display/AI paths.
- ALSA when populated board audio/speaker support is part of the target
  profile.
- USB only when required by the board or developer workflow.

Selection rules:

- Enable the smallest driver set that supports the target hardware.
- Prefer built-in drivers for boot-critical platform hardware.
- Use modules only when load order and dependency behavior are deterministic.
- Do not expose module management as an app responsibility.
- Do not enable broad subsystems without a platform use case.

## 7. Runtime Design

### Pipeline Model

The runtime pipeline is a fixed-order graph with optional stages:

```text
capture -> preprocess -> infer -> postprocess -> overlay -> present
```

Required stage behavior:

- Each stage receives a runtime frame handle, not raw Linux buffers.
- Each stage returns a runtime status code.
- Each stage may attach metadata to the frame.
- Each stage must not retain a frame after returning unless it explicitly
  acquires ownership.
- Stage order must remain stable for deterministic behavior.

Application extension points:

- Frame callback after capture.
- Preprocess callback before inference.
- Inference result callback after AI execution.
- Overlay callback before display.
- Input event callback.
- Error callback.

### Buffer Lifecycle

The runtime owns buffer pools. The application owns no physical buffer detail.

Lifecycle:

```text
allocate pool
  -> acquire empty buffer
  -> fill from camera
  -> pass through runtime stages
  -> present or consume
  -> release to pool
```

Buffer states:

- FREE: available for capture or processing.
- CAPTURING: owned by camera BSP.
- READY: frame data is valid.
- PROCESSING: owned by a runtime stage.
- INFERENCING: owned by the AI wrapper.
- PRESENTING: owned by display BSP.
- RELEASED: returned to the pool.
- ERROR: cannot be reused until reset.

Zero-copy preference:

- If camera capture and display presentation can share a DMA-capable buffer,
  the runtime should pass handles instead of copying pixels.
- If AI inference requires a different layout, preprocess may create a derived
  tensor buffer while preserving the source frame handle.
- If zero-copy is not possible, the copy is still a runtime implementation
  detail and must not leak to applications.

### Threading and Event Model

The default model is a single public event loop:

```c
vision_runtime_run(runtime);
```

Internal workers are allowed only when they preserve the public lifecycle.

Allowed internal workers:

- Camera capture worker.
- AI inference worker.
- Display pacing worker.
- Input polling worker.

Rules:

- Runtime APIs must document whether they are thread-safe.
- Application callbacks are serialized by default.
- A callback must not block indefinitely.
- The runtime must provide a stop request API.
- Buffer release must be safe even when internal workers are enabled.

## 8. Boot Sequence

The standard boot sequence is:

```text
BootROM
  -> U-Boot
  -> Linux kernel
  -> BusyBox init
  -> platform init script
  -> vision runtime service or demo launcher
  -> SDK demo app or user app
```

The init system must be simple and deterministic. If the platform starts a
default demo, that demo must still use public SDK, runtime, or BSP APIs.

## 9. Repository Ownership

Expected repository structure:

```text
t-display-k230-vision-platform/
|-- buildroot/
|   |-- configs/
|   |-- board/
|   |-- rootfs_overlay/
|   `-- kernel_config/
|-- bsp/
|   |-- camera/
|   |-- display/
|   |-- input/
|   `-- lora/
|-- runtime/
|   |-- vision_pipeline/
|   |-- buffer/
|   |-- ai/
|   `-- event_loop/
|-- sdk/
|   |-- examples/
|   |-- templates/
|   `-- tools/
|-- apps/
|   |-- demo_camera/
|   `-- demo_ai/
`-- docs/
    |-- architecture.md
    |-- api.md
    `-- getting_started.md
```

Ownership rules:

- `buildroot/` owns system image generation.
- `bsp/` owns hardware-specific implementation and public hardware APIs.
- `runtime/` owns pipeline, buffers, AI wrapper, and event loop.
- `sdk/` owns developer workflow and templates.
- `apps/` owns examples only.
- `docs/` owns the platform contract.

## 10. Stability Rules

The platform is successful only when developers can do this:

```text
flash image
open camera through BSP/runtime API
receive frames
run AI inference
render to display
never touch Linux internals
```

Any change that forces application developers to know device nodes, ioctls,
DRM planes, V4L2 formats, kernel module names, or Buildroot package choices is
an architecture regression.
