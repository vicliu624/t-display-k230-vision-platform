# T-Display K230 Vision Platform 入门

本文定义 T-Display K230 Vision Platform SDK 的标准开发者 workflow。

英文版本：

- [getting_started.md](getting_started.md)

目标体验：

```text
clone -> build -> flash -> boot -> run demo -> create app
```

本平台是 embedded vision SDK，不是 desktop Linux distribution，也不要求开发者在 flash 后到设备上安装 packages。

当前 board bring-up baseline 优先跟随 `kendryte/k230_linux_sdk`。先理解最接近的官方
K230/CanMV Linux profile，再把 T-Display 差异作为 board delta 叠加。第一轮 bring-up
不得从无关 upstream kernel 假设或 RT application stack 开始。

## 1. 构建什么

构建产物是一个确定性的系统镜像：

```text
sysimage-sdcard.img
```

镜像包含：

- Minimal Buildroot Linux。
- 目标板 Linux kernel 和 device tree。
- BusyBox root filesystem。
- BSP libraries。
- Vision runtime libraries。
- SDK example binaries。
- Optional demo model files。
- Platform init scripts。

镜像不包含：

- Desktop environment。
- Debian/Ubuntu user space。
- Runtime package manager dependency。
- Per-app driver configuration。

## 2. 仓库结构

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

Ownership：

- `buildroot/` 构建 system image。
- `bsp/` 拥有 hardware access。
- `runtime/` 拥有 camera-to-AI-to-display execution。
- `sdk/` 拥有 developer tools、examples 和 templates。
- `apps/` 包含使用 SDK 的 demos。

## 3. Host Requirements

使用 Linux build host、Linux container/VM，或 WSL ext4 文件系统中的 Linux 环境。
Buildroot workflow 不定义为在目标设备上运行。

必需 host tools：

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
- `dd` 或其他 image flashing tool

推荐资源：

- 至少 8 GB RAM。
- 至少 20 GB 空闲磁盘。
- 第一次 Buildroot source downloads 需要稳定网络。

目标设备必须能从生成的 SD card image 启动。

## 4. Clone

```sh
git clone <repo-url> t-display-k230-vision-platform
cd t-display-k230-vision-platform
```

Clone 后，不要把 device-specific hacks 放入 application code。Board support 属于
`buildroot/` 和 `bsp/`。

## 5. Configure

Official Buildroot source 作为 submodule 跟踪：

```text
buildroot/buildroot/
```

平台固定该 submodule 到 official Buildroot release tag：

```text
Buildroot 2025.02.14 LTS
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

初始化：

```sh
git submodule update --init --depth 1 buildroot/buildroot
```

验证版本：

```sh
git -C buildroot/buildroot describe --tags --exact-match
git -C buildroot/buildroot rev-parse HEAD
```

期望：

```text
2025.02.14
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

外层 `buildroot/` 是 platform `BR2_EXTERNAL` tree。Platform defconfigs、packages、
board files、rootfs overlays 和 kernel config fragments 必须位于 upstream source tree 外部。

Canonical Buildroot defconfig：

```text
buildroot/configs/t_display_k230_vision_defconfig
```

该配置必须选择：

- Minimal BusyBox user space。
- 来自 accepted K230 Linux baseline 的 target Linux kernel。
- 来自 accepted K230 Linux baseline 并叠加 T-Display board delta 的 target device tree。
- Required camera/display/input drivers。
- BSP 和 runtime libraries。
- SDK demo binaries。

Configuration 是 platform infrastructure。普通 application 不应通过修改 Buildroot config 构建。

## 6. Build System Image

最终 SDK workflow 应该是：

```sh
./sdk/tools/visionctl build system
```

CLI tool 负责调用 Buildroot，并把 final image 放到：

```text
output/images/sysimage-sdcard.img
```

在 CLI tool 实现之前，低层 Buildroot workflow 是：

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

首次交互式配置：

```sh
cd buildroot/buildroot
make BR2_EXTERNAL=.. O=../../output/t_display_k230_vision menuconfig
```

修改配置后，把 minimal platform defconfig 保存回外层平台树：

```sh
make O=../../output/t_display_k230_vision savedefconfig \
  BR2_DEFCONFIG=../configs/t_display_k230_vision_defconfig
```

Build rules：

- Image 必须能从 versioned repository inputs 可复现。
- Rootfs changes 必须来自 Buildroot packages 或 `rootfs_overlay`。
- App code 不得要求手动修改 `output/`。
- Device behavior 不得依赖 flash 后安装 packages。

## 7. Flash

插入 SD card 并确认 host block device。

Linux 下：

```sh
lsblk
```

烧录镜像：

```sh
sudo dd if=output/images/sysimage-sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

把 `/dev/sdX` 替换成真实 SD card device。不要使用 `/dev/sdX1` 这种 partition path；
必须烧录整个 card device。

最终 SDK workflow 应包装为：

```sh
./sdk/tools/visionctl flash --image output/images/sysimage-sdcard.img --device /dev/sdX
```

## 8. Boot

把 SD card 插入 T-Display K230 并上电。

期望 boot sequence：

```text
BootROM
  -> U-Boot
  -> Linux kernel
  -> BusyBox init
  -> platform init script
  -> vision runtime service or demo launcher
```

成功启动应提供：

- Console output。
- Mounted root filesystem。
- BSP initialization logs。
- Runtime 或 demo startup logs。

默认 boot 不得要求 desktop session。

## 9. Run First Demo

第一版 demo 验证完整平台路径：

```text
camera -> runtime buffer -> display
```

目标设备上的期望命令：

```sh
/opt/vision/examples/demo_camera
```

期望行为：

- Camera 通过 BSP API 打开。
- Frames 通过 platform frame handles 获取。
- Runtime 将 frames present 到 display。
- Application code 不接触 V4L2 或 DRM。

SDK wrapper 应提供：

```sh
./sdk/tools/visionctl run demo_camera
```

如果 demo 失败，先看 platform logs：

```sh
dmesg
cat /var/log/vision-platform.log
```

Failure diagnosis 必须保持 platform-owned。不要通过在 app code 中加入 device-node access
来修 demo。

## 10. Run AI Demo

AI demo 验证：

```text
camera -> preprocess -> AI -> overlay -> display
```

目标设备上的期望命令：

```sh
/opt/vision/examples/demo_ai --model /opt/vision/models/demo.kmodel
```

期望行为：

- Camera frames 通过 BSP capture。
- Runtime 为 model preprocess frames。
- AI wrapper 选择 configured backend。
- Results 以 normalized tensors 或 metadata 返回。
- Overlay/presentation 保持在 runtime/display APIs 内。

Application code 不得依赖 KPU 或 nncase implementation handles。

## 11. Create a New Application

标准 app creation workflow：

```sh
./sdk/tools/visionctl new app my_camera_app
```

生成 layout：

```text
my_camera_app/
|-- CMakeLists.txt
|-- src/
|   `-- main.c
`-- README.md
```

Template 只应 include platform headers：

```c
#include "tdvp/runtime.h"
```

Applications 应使用 runtime configuration、callbacks 和 pipeline stages，不应直接打开 Linux devices。

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

- 使用 `tdvp_runtime_*` 作为 main loop。
- 使用 `tdvp_pipeline_*` 处理 camera/preprocess/AI/display flow。
- `tdvp_camera_*`、`tdvp_display_*`、`tdvp_input_*` 只能作为 BSP public APIs 使用。
- App code 不 include Linux driver headers。
- App code 不 hardcode `/dev/*` paths。
- 不要求 users 为一个 app 修改 Buildroot。
- 不为每个 demo 新建旁路 pipeline；需要扩展时应实现为 runtime stage 或 callback。

## 14. Troubleshooting

### Buildroot 无法下载 sources

检查 host network access，然后重试 Buildroot build。Source download cache 应由 build
environment 处理，不属于 target device。

### Image 能启动但 camera demo 失败

检查：

- Platform config 是否启用 kernel camera driver。
- Device tree 是否匹配 target board。
- BSP camera implementation 是否正确映射 device。
- Runtime buffer format 是否匹配 camera output。

不要通过 demo app 打开 `/dev/video*` 来修复。

### Display 空白

检查：

- Platform kernel config 是否启用 DRM/KMS driver。
- Device tree display node。
- BSP display mode selection。
- Runtime output pixel format。
- BSP 中的 display backlight/power sequencing。

不要在 app 中直接调用 DRM APIs。

### AI demo 加载 model 失败

检查：

- `/opt/vision/models/` 下的 model path。
- Selected backend 是否支持 model format。
- Platform Buildroot config 是否选择 AI backend package。
- Runtime preprocess output shape。

不要在 application code 中使用 backend-private handles。

## 15. Success Checklist

平台达到 first developer use 的条件：

- `sysimage-sdcard.img` 可以从 clean checkout 构建。
- 生成镜像启动时不依赖 desktop 或 distro runtime。
- Camera demo 通过 platform APIs 运行。
- Display presentation 通过 platform APIs 运行。
- AI demo 通过 platform APIs 运行。
- Application code 不 include Linux device headers。
- 没有 application hardcode `/dev/video*`、`/dev/dri/*` 或 `/dev/input/*`。
- 开发者可以通过 `docs/architecture.md`、`docs/api.md` 和本文理解平台。
