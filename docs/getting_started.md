# T-Display K230 Vision Platform Getting Started

This guide defines the standard developer workflow for the T-Display K230
Vision Platform SDK.

Chinese version:

- [getting_started.zh-CN.md](getting_started.zh-CN.md)

The target experience is:

```text
clone -> build -> flash -> boot -> run demo -> create app
```

The platform is an embedded vision SDK. It is not a desktop Linux distribution
and it does not require developers to install packages on the device after
flashing.

The current board bring-up baseline follows `kendryte/k230_linux_sdk` first.
T-Display specific differences are applied as a board delta after the closest
official K230/CanMV Linux profile is understood. Do not start first bring-up
from unrelated upstream kernel assumptions or from the RT-Smart application
stack.

## 1. What You Build

The build produces one deterministic system image:

```text
sysimage-sdcard.img
```

The image contains:

- Minimal Buildroot Linux.
- Linux kernel and device tree for the target board.
- BusyBox root filesystem.
- BSP libraries.
- Vision runtime libraries.
- SDK example binaries.
- Optional demo model files.
- Platform init scripts.

The image does not contain:

- Desktop environment.
- Debian/Ubuntu user space.
- Runtime package manager dependency.
- Per-app driver configuration.

## 2. Repository Layout

Expected layout:

```text
t-display-k230-vision-platform/
|-- buildroot/
|   |-- configs/
|   |-- board/
|   |-- rootfs_overlay/
|   `-- kernel_config/
|-- bsp/
|-- runtime/
|-- sdk/
|-- apps/
`-- docs/
```

Key ownership:

- `buildroot/` builds the system image.
- `bsp/` owns hardware access.
- `runtime/` owns camera-to-AI-to-display execution.
- `sdk/` owns developer tools, examples, and templates.
- `apps/` contains demos that consume the SDK.

## 3. Host Requirements

Use a Linux build host or a Linux container/VM. Buildroot workflows are not
defined against the target device itself.

Required host tools:

- `git`
- `make`
- `gcc` and `g++`
- `patch`
- `rsync`
- `cpio`
- `unzip`
- `bc`
- `file`
- `python3`
- `dd` or another image flashing tool

Recommended host resources:

- 8 GB RAM minimum.
- 20 GB free disk space minimum.
- Stable network access for first-time Buildroot source downloads.

The target device must boot from the generated SD card image.

## 4. Clone

```sh
git clone <repo-url> t-display-k230-vision-platform
cd t-display-k230-vision-platform
```

After cloning, do not add device-specific hacks to application code. Board
support belongs under `buildroot/` and `bsp/`.

## 5. Configure

The official Buildroot source is tracked as a submodule:

```text
buildroot/buildroot/
```

The platform pins this submodule to an official Buildroot release tag:

```text
Buildroot 2025.02.14 LTS
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

Initialize it after cloning the platform repository:

```sh
git submodule update --init --depth 1 buildroot/buildroot
```

Verify the expected version:

```sh
git -C buildroot/buildroot describe --tags --exact-match
git -C buildroot/buildroot rev-parse HEAD
```

Expected:

```text
2025.02.14
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

The outer `buildroot/` directory is the platform `BR2_EXTERNAL` tree. Platform
defconfigs, packages, board files, rootfs overlays, and kernel config fragments
must live outside the upstream source tree.

The platform must provide a canonical Buildroot defconfig for the T-Display
K230 target:

```text
buildroot/configs/t_display_k230_vision_defconfig
```

The canonical configuration must select:

- Minimal BusyBox user space.
- Target Linux kernel from the accepted K230 Linux baseline.
- Target device tree derived from the accepted K230 Linux baseline plus the
  T-Display board delta.
- Required camera/display/input drivers.
- BSP and runtime libraries.
- SDK demo binaries.

Configuration should be treated as platform infrastructure. Developers should
not change Buildroot config to build ordinary applications.

## 6. Build System Image

The canonical SDK workflow should be:

```sh
./sdk/tools/visionctl build system
```

The tool is responsible for invoking Buildroot and placing the final image at:

```text
output/images/sysimage-sdcard.img
```

Until the CLI tool is implemented, the equivalent low-level Buildroot workflow
is:

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

For first-time interactive configuration:

```sh
cd buildroot/buildroot
make BR2_EXTERNAL=.. O=../../output/t_display_k230_vision menuconfig
```

After changing configuration, save the minimal platform defconfig back to the
outer platform tree:

```sh
make O=../../output/t_display_k230_vision savedefconfig \
  BR2_DEFCONFIG=../configs/t_display_k230_vision_defconfig
```

Build outputs:

```text
output/images/
|-- sysimage-sdcard.img
|-- Image or vmlinux
|-- *.dtb
`-- rootfs.*
```

Build rules:

- The image must be reproducible from versioned repository inputs.
- Rootfs changes must come from Buildroot packages or `rootfs_overlay`.
- App code must not require manual edits inside `output/`.
- Device behavior must not depend on packages installed after flashing.

## 7. Flash

Insert the SD card and identify the host block device.

On Linux:

```sh
lsblk
```

Flash the image:

```sh
sudo dd if=output/images/sysimage-sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

Replace `/dev/sdX` with the actual SD card device.

Do not use a partition path such as `/dev/sdX1`; flash the whole card device.

The canonical SDK workflow should eventually wrap this as:

```sh
./sdk/tools/visionctl flash --image output/images/sysimage-sdcard.img --device /dev/sdX
```

## 8. Boot

Insert the SD card into the T-Display K230 device and power it on.

Expected boot sequence:

```text
BootROM
  -> U-Boot
  -> Linux kernel
  -> BusyBox init
  -> platform init script
  -> vision runtime service or demo launcher
```

A successful boot should provide:

- Console output.
- Mounted root filesystem.
- BSP initialization logs.
- Runtime or demo startup logs.

The default boot must not require a desktop session.

## 9. Run First Demo

The first demo validates the complete platform path:

```text
camera -> runtime buffer -> display
```

Expected command on device:

```sh
/opt/vision/examples/demo_camera
```

Expected behavior:

- Camera opens through the BSP API.
- Frames are acquired through platform frame handles.
- Runtime presents frames to the display.
- Application code does not touch V4L2 or DRM.

The SDK wrapper should expose:

```sh
./sdk/tools/visionctl run demo_camera
```

If the demo fails, inspect platform logs first:

```sh
dmesg
cat /var/log/vision-platform.log
```

Failure diagnosis must remain platform-owned. Do not fix demos by adding
device-node access to application code.

## 10. Run AI Demo

The AI demo validates:

```text
camera -> preprocess -> AI -> overlay -> display
```

Expected command on device:

```sh
/opt/vision/examples/demo_ai --model /opt/vision/models/demo.kmodel
```

Expected behavior:

- Camera frames are captured through the BSP.
- Runtime preprocesses frames for the model.
- AI wrapper selects the configured backend.
- Results are returned as normalized tensors or metadata.
- Overlay/presentation stays in runtime/display APIs.

Application code must not depend on KPU or nncase implementation handles.

## 11. Create a New Application

The standard app creation workflow should be:

```sh
./sdk/tools/visionctl new app my_camera_app
```

Expected generated layout:

```text
my_camera_app/
|-- CMakeLists.txt
|-- src/
|   `-- main.c
`-- README.md
```

The template should include only platform headers:

```c
#include "tdvp/runtime.h"
```

Applications should use runtime configuration, callbacks, and pipeline stages.
They should not open Linux devices directly.

## 12. Minimal Camera Runtime App

```c
#include "tdvp/runtime.h"

static void on_event(const tdvp_runtime_event_t *event)
{
    if (event->type == TDVP_RUNTIME_EVENT_FRAME_READY) {
        /* Frame is available through platform metadata and runtime ownership. */
    }
}

int main(void)
{
    tdvp_runtime_config_t config = {0};
    config.size = sizeof(config);
    config.pipeline.camera.size = sizeof(config.pipeline.camera);
    config.pipeline.camera.camera_id = TDVP_CAMERA_DEFAULT;
    config.pipeline.camera.frame_size.width = 640;
    config.pipeline.camera.frame_size.height = 480;
    config.pipeline.camera.pixel_format = TDVP_PIXFMT_RGB565;
    config.pipeline.camera.fps = 30;
    config.pipeline.camera.buffer_count = 4;
    config.pipeline.display.size = sizeof(config.pipeline.display);
    config.pipeline.display.display_id = TDVP_DISPLAY_DEFAULT;
    config.pipeline.display.output_size.width = 320;
    config.pipeline.display.output_size.height = 240;
    config.pipeline.display.pixel_format = TDVP_PIXFMT_RGB565;
    config.pipeline.display.present_mode = TDVP_PRESENT_VSYNC;
    config.pipeline.enable_display = true;
    config.pipeline.enable_ai = false;
    config.event_callback = on_event;

    tdvp_runtime_t *runtime = NULL;
    if (tdvp_runtime_create(&config, &runtime) != TDVP_OK) {
        return 1;
    }

    tdvp_status_t status = tdvp_runtime_run(runtime);
    tdvp_runtime_destroy(runtime);

    return status == TDVP_OK ? 0 : 1;
}
```

## 13. Developer Rules

Follow these rules when adding examples or applications:

- Use `tdvp_runtime_*` for the main loop.
- Use `tdvp_pipeline_*` for camera/preprocess/AI/display flow.
- Use `tdvp_camera_*`, `tdvp_display_*`, and `tdvp_input_*` only through BSP
  public APIs.
- Do not include Linux driver headers in app code.
- Do not hardcode `/dev/*` paths in app code.
- Do not require users to change Buildroot for an app.
- Do not create a new pipeline per demo unless it is implemented as a runtime
  stage or callback.

## 14. Troubleshooting

### Buildroot cannot download sources

Check host network access and retry the Buildroot build. Source download
caching should be handled by the build environment, not the target device.

### Image boots but camera demo fails

Check:

- Kernel camera driver enabled in platform config.
- Device tree matches target board.
- BSP camera implementation maps the device correctly.
- Runtime buffer format matches camera output.

Do not fix this by opening `/dev/video*` from the demo app.

### Display is blank

Check:

- DRM/KMS driver enabled in platform kernel config.
- Device tree display node.
- BSP display mode selection.
- Runtime output pixel format.
- Display backlight/power sequencing in BSP.

Do not fix this by calling DRM APIs from the app.

### AI demo fails to load model

Check:

- Model path under `/opt/vision/models/`.
- Model format supported by selected backend.
- AI backend package selected by platform Buildroot config.
- Runtime preprocess output shape.

Do not fix this by using backend-private handles in application code.

## 15. Success Checklist

The platform is ready for first developer use when all items pass:

- `sysimage-sdcard.img` builds from a clean checkout.
- The generated image boots without desktop or distro runtime dependencies.
- Camera demo runs through platform APIs.
- Display presentation runs through platform APIs.
- AI demo runs through platform APIs.
- Application code does not include Linux device headers.
- No application hardcodes `/dev/video*`, `/dev/dri/*`, or `/dev/input/*`.
- A developer can understand the platform from `docs/architecture.md`,
  `docs/api.md`, and this guide.
