# T-Display K230 Vision Platform 架构

本文定义 T-Display K230 Vision Platform SDK 的系统架构。

英文版本：

- [architecture.md](architecture.md)

本项目不是 application、demo collection 或 general-purpose Linux distribution。
它是一个 embedded vision platform：提供确定性的系统镜像、严格的硬件抽象层、
可复用的 vision runtime，以及用于开发 camera/AI 工作负载的小型 SDK 表面。

## 0. 当前适配 Baseline

第一版 Linux baseline 来自官方 K230 Linux 路径：

```text
kendryte/k230_linux_sdk
  -> 最接近的 K230/CanMV Linux profile
  -> T-Display K230 硬件差异
  -> TDVP Buildroot board profile
```

参考源角色必须分开：

- `kendryte/k230_linux_sdk` 定义第一版 Linux boot、kernel、DTS、OpenSBI、U-Boot、
  Buildroot-overlay 和 image-layout 参考。
- `Xinyuan-LilyGO/T-Display-K230_canmv_rt` 只提供 T-Display K230 硬件事实。
- Upstream Linux 是后续 comparison 和 forward-port target，不是第一轮 bring-up baseline。

这条规则用于阻止早期探索性的 boot experiments 变成平台架构。历史 probe 记录可以作为证据保留，
但不定义当前平台 baseline。

## 1. 架构契约

平台只有一条固定规则：

```text
Applications must depend on platform public APIs only: SDK, runtime, and BSP.
Applications must never depend on Linux device internals.
```

也就是说，application code 可以使用 public `tdvp_*` APIs，包括面向硬件示例的直接 BSP APIs；
但不得直接调用 V4L2、DRM/KMS、evdev、ioctl、sysfs、procfs 或 kernel driver interfaces。
这些细节必须留在 BSP boundary 之下。

必需 stack：

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

## 2. 非目标

平台不得漂移成以下形态：

- Camera application。
- AI application。
- Desktop Linux image。
- Debian、Ubuntu 或 package-manager based runtime。
- 直接暴露给用户的 generic Linux board support package。
- 无关 demo 集合。
- 每个 app 各自拥有 driver 和 pipeline 的分裂生态。

Applications 可以作为 examples 存在，但它们不是平台中心。平台中心是 system、BSP、runtime、
SDK 和 API contract。

## 3. 分层职责

### Layer 0: Buildroot Linux

Buildroot 提供确定性的操作系统基础。

职责：

- 构建 kernel、boot assets、root filesystem 和 system image。
- 提供启动 runtime 和 SDK examples 所需的最小 userspace。
- 提供 BusyBox-based init 和 shell utilities。
- 把 platform libraries 和 demo binaries 打包进 root filesystem。
- 保持 image generation 可复现。

允许内容：

- Linux kernel。
- Device tree。
- 必要时的 U-Boot integration artifacts。
- BusyBox。
- Platform binaries 所需的 libc 和 runtime linker。
- Platform libraries、runtime service、examples 和 scripts。
- 被平台明确版本化的 firmware blobs 或 model files。

禁止内容：

- Desktop environments。
- 与平台 display path 无关的 window systems。
- Runtime dependency 形式的 distribution package managers。
- apt、systemd user services、application-owned udev rules 或 device use 时动态安装 package 等 distro assumptions。
- App-specific system customization。

### Layer 1: BSP Hardware Abstraction Layer

BSP 是平台最重要的 portability asset。它拥有所有硬件访问，并向上隐藏 kernel/device details。

职责：

- Open、configure、start、stop、read camera devices。
- Initialize 并 present frames 到 display devices。
- 以 normalized format 读取 input events。
- 通过平台自有 API 提供 LoRa、audio、storage、GPIO、SPI、I2C、power、radios、
  environmental sensors 等已装配能力。
- 将 Linux driver behavior 转换为稳定平台语义。
- 归一化 error codes 和 device capabilities。

BSP 内部可以使用 V4L2、DRM/KMS、evdev、ioctl、mmap、DMA buffers、sysfs 或 device-specific
kernel APIs。这些 API 不得跨越 BSP boundary。

### Layer 2: Vision Runtime

Runtime 是 vision applications 的执行引擎。

职责：

- 拥有标准 frame pipeline：

```text
camera -> preprocess -> AI -> overlay -> display
```

- 管理 frame buffers 及其 lifecycle。
- 在 BSP 和硬件支持时优先 zero-copy。
- 仅在 runtime APIs 后方 fallback 到 explicit copies。
- 提供统一 AI inference wrapper。
- 为 camera、input、AI、display 和 app callbacks 提供统一 event loop。
- 将 application code 与 device timing、buffer ownership 和 hardware acceleration details 隔离。

Runtime 不是 UI framework，也不是 app framework。它是紧凑的 vision execution model。

### Layer 3: Developer SDK

SDK 是开发者面对的平台层。

职责：

- 提供能基于 stable APIs 编译运行的 examples。
- 提供 application templates。
- 提供 build、flash、package、run workflows 的 CLI tools。
- 提供 public headers 和 generated API documentation。
- 必要时提供 model conversion 和 deployment helpers。

SDK 必须让新开发者走通：

```text
clone -> build -> flash -> run demo -> create app
```

### Layer 4: Applications

Applications 是平台的可选消费者，不是平台设计本身。

允许：

- Demo camera app。
- Demo AI app。
- 使用 SDK、runtime 或 BSP public APIs 的 user applications。

禁止：

- 直接使用 V4L2、DRM/KMS、evdev、ioctl、sysfs 或 procfs。
- Per-app hardware initialization。
- Per-app kernel module selection。
- Per-app rootfs customization。
- 绕过 runtime 的 per-app pipeline ownership。

## 4. Data Flow Model

标准 runtime data flow：

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

Buffer manager 是所有 frame movement 的共享 ownership 机制。每个 frame 同一时间只有一个 owner。
Stage 可以 borrow frame，但 ownership 必须通过 runtime APIs 显式 return 或 transfer。

必需 frame stages：

- Capture：从 camera BSP 获取 frame。
- Normalize：用 runtime-owned terms 转换或描述 frame format。
- Preprocess：为 AI input 做 resize、crop、color convert 或 normalize。
- Infer：通过 runtime AI wrapper 执行 AI model。
- Compose：生成 display overlays 或 final display frames。
- Present：通过 display BSP 提交 final frame。
- Release：把 buffers 归还到正确的 pool。

## 5. Control Flow Model

默认 control model 是 runtime-owned single event loop。

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

Runtime 内部可以为 capture、inference 或 display pacing 使用 worker threads，但 applications
只能看到 deterministic event/callback contract。

Applications 不应创建竞争性的 camera、display 或 inference loops。如果需要自定义行为，
应向 runtime 注册 pipeline stages 或 callbacks。

## 6. Buildroot Design

Buildroot 层必须保持小而确定。

### 6.1 Minimal Configuration Strategy

Buildroot configuration 必须作为 board-specific defconfigs 维护在：

```text
buildroot/configs/
```

每个 defconfig 为一个 board target 产生确定性 image。Configuration 应作为 platform
infrastructure 审查，而不是 app code。

策略：

- 保留一个 canonical K230 target defconfig。
- Kernel config 固定并版本化。
- Device tree 固定并版本化。
- Package selection 保持最小。
- 每个新增 package 都是 platform-level decision。
- 优先 static configuration，而不是 runtime discovery。

禁止：

- Flash 后在设备上安装 packages。
- 依赖 distribution package manager。
- 因为某个 demo app 需要就加入 package。
- 让 application code 修改 rootfs layout。

### 6.2 Root Filesystem Design

Root filesystem 只包含平台 runtime environment 所需内容。

期望 layout：

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

`buildroot/rootfs_overlay/` 只能放 platform-owned 小文件，例如 init scripts、platform config
和 documented demo assets，不得成为 app-specific dumping ground。

### 6.3 Kernel Module Selection Rules

Kernel configuration 只能由平台需求驱动。

必需 kernel subsystems：

- DRM/KMS for display。
- V4L2 media stack for camera。
- Input subsystem for buttons or touch。
- SPI and I2C for board peripherals。
- Camera/display/AI paths 需要的 DMA 和 contiguous memory support。
- 当 target profile 包含 audio/speaker 时启用 ALSA。
- 只有 board 或 developer workflow 需要时才启用 USB。

规则：

- 启用支持目标硬件的最小 driver set。
- 对 boot-critical hardware 优先 built-in drivers。
- 只有 load order 和 dependency behavior 确定时才用 modules。
- 不把 module management 交给 app。
- 没有 platform use case 时不启用 broad subsystems。

## 7. Runtime Design

Runtime pipeline 是固定顺序、stage 可选的 graph：

```text
capture -> preprocess -> infer -> postprocess -> overlay -> present
```

Stage rules：

- 每个 stage 接收 runtime frame handle，而不是 raw Linux buffers。
- 每个 stage 返回 runtime status code。
- 每个 stage 可以向 frame 附加 metadata。
- Stage 返回后不得保留 frame，除非显式取得 ownership。
- Stage order 必须保持稳定。

Buffer lifecycle：

```text
allocate pool
  -> acquire empty buffer
  -> fill from camera
  -> pass through runtime stages
  -> present or consume
  -> release to pool
```

Buffer states 包括 `FREE`、`CAPTURING`、`READY`、`PROCESSING`、`INFERENCING`、
`PRESENTING`、`RELEASED`、`ERROR`。

默认 threading model 是 single public event loop：

```c
vision_runtime_run(runtime);
```

内部 workers 允许存在，但必须保持 public lifecycle 稳定。Application callbacks 默认串行化。

## 8. Boot Sequence

标准 boot sequence：

```text
BootROM
  -> U-Boot
  -> Linux kernel
  -> BusyBox init
  -> platform init script
  -> vision runtime service or demo launcher
  -> SDK demo app or user app
```

Init system 必须简单且确定。如果平台启动默认 demo，该 demo 仍必须使用 public SDK、runtime
或 BSP APIs。

## 9. Repository Ownership

目录 ownership：

- `buildroot/`：system image generation。
- `bsp/`：hardware-specific implementation 和 public hardware APIs。
- `runtime/`：pipeline、buffers、AI wrapper 和 event loop。
- `sdk/`：developer workflow 和 templates。
- `apps/`：examples only。
- `docs/`：platform contract。

## 10. Stability Rules

平台成功的定义：

```text
flash image
open camera through BSP/runtime API
receive frames
run AI inference
render to display
never touch Linux internals
```

任何迫使 application developers 理解 device nodes、ioctls、DRM planes、V4L2 formats、
kernel module names 或 Buildroot package choices 的变化，都是 architecture regression。
