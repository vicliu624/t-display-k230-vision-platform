# Buildroot - T-Display K230 Vision Platform

本目录定义 T-Display K230 Vision Platform 的 Layer 0。

Buildroot 是系统基础层。它的职责是为本平台生成最小、确定、可复现的 embedded firmware image。

英文版本：

- [README.md](README.md)

## 目的

Buildroot 用于生成可复现的 Linux system，服务于：

- T-Display K230 hardware。
- BSP hardware abstraction。
- Vision runtime execution。
- Embedded camera、display、input 和 AI pipelines。
- 用于验证平台契约的 SDK examples。

## 非目标

这个 Buildroot tree 绝不能变成：

- Desktop Linux system。
- Debian 或 Ubuntu replacement。
- Package-managed distribution。
- General-purpose development OS。
- Per-application system tweaks 集合。
- 由 demo 定义平台行为的地方。

## 系统输出

构建产物是一个可启动系统镜像：

```text
sysimage-sdcard.img
```

镜像包含：

- 目标板所需的 bootloader artifacts。
- Linux kernel。
- Device tree blobs。
- 最小 BusyBox root filesystem。
- BSP libraries。
- Vision runtime binaries 和 libraries。
- SDK examples 和必要 platform assets。

镜像在烧录后不得要求 runtime package installation。

## 目录结构

```text
buildroot/
|-- buildroot/         # official Buildroot source submodule
|-- configs/           # platform defconfigs exposed through BR2_EXTERNAL
|-- board/             # board-specific boot and device integration
|-- package/           # platform packages: BSP, runtime, SDK examples
|-- rootfs_overlay/    # final filesystem overlay
|-- kernel_config/     # kernel config fragments and rules
|-- UPSTREAM.md        # pinned Buildroot upstream version policy
|-- UPSTREAM.zh-CN.md  # Chinese translation of upstream version policy
|-- CUSTOMIZATION.md   # how to customize the Linux system
|-- CUSTOMIZATION.zh-CN.md
|-- external.desc      # br2-external identity
|-- external.mk        # br2-external make integration
|-- Config.in          # br2-external Kconfig entry point
|-- README.md          # system foundation contract
`-- README.zh-CN.md
```

## Upstream Source Model

官方 Buildroot source tree 以 Git submodule 方式跟踪：

```text
buildroot/buildroot/
```

Submodule URL：

```text
https://gitlab.com/buildroot.org/buildroot.git
```

固定 baseline：

```text
Buildroot 2025.02.14 LTS
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

规则：

- Submodule 必须固定到 official Buildroot release tag。
- 平台构建不得跟踪 `master`。
- 不得固定到任意 development commit。
- 不得把 platform code 放进 `buildroot/buildroot/`。
- 不得为了平台定制而编辑 official Buildroot files。
- Platform defconfigs、packages、board files、overlays 和 kernel config fragments
  必须保留在外层 `buildroot/` br2-external tree。
- 升级 upstream 时，先更新 submodule commit，再验证 platform defconfig 和 runtime image。

平台仓库的初始化设置：

```sh
git submodule add --depth 1 https://gitlab.com/buildroot.org/buildroot.git buildroot/buildroot
git -C buildroot/buildroot fetch --depth 1 origin tag 2025.02.14
git -C buildroot/buildroot checkout 2025.02.14
git config -f .gitmodules submodule.buildroot/buildroot.shallow true
```

初始化或更新 submodule：

```sh
git submodule update --init --depth 1 buildroot/buildroot
```

## 构建主机与换行符

Buildroot 必须在 Linux-compatible environment 中配置和构建。
在 Windows 上，Buildroot 命令应从 WSL 或 Linux build machine 中执行。

不要把 Windows-native `mingw32-make` 作为本平台 Buildroot 构建入口。

推荐的 Windows 工作方式：把完整平台仓库放在 WSL ext4 文件系统中，例如
`~/src/t-display-k230-vision-platform`，再通过 Windows editor 的 WSL integration 编辑。
在 `/mnt/c/...` 下构建本项目会很别扭：速度明显更慢，Windows Git 可能把合法的 WSL
symlink 报成 type changes，而 Buildroot source tree 本身依赖真实 symlinks。

Official Buildroot submodule 必须保持 LF line endings。如果仓库在 Windows 上以
`core.autocrlf=true` checkout，`buildroot/buildroot/` 内的 shell scripts 可能变成
CRLF，并在 WSL 下以 `/bin/sh^M` 这类错误失败。

从 WSL/Linux 运行 Buildroot 命令之前，先把 submodule checkout policy 固定为 LF：

```sh
git -C buildroot/buildroot config core.autocrlf false
git -C buildroot/buildroot config core.eol lf
git -C buildroot/buildroot config core.symlinks true
```

如果 submodule 已经以 CRLF line endings checkout，需要从 WSL/Linux 重新刷新 submodule
working tree。只在 `git -C buildroot/buildroot status --short` 干净时执行：

```sh
git -C buildroot/buildroot checkout-index -f -a
git -C buildroot/buildroot ls-files --eol support/scripts/setlocalversion
```

Buildroot shell scripts 的期望 line-ending status 是：

```text
i/lf    w/lf
```

Official Buildroot tree 还使用真实 symbolic links。Windows checkout 如果使用
`core.symlinks=false`，这些链接可能会退化成很小的文本文件。一个可见失败现象是：

```text
ERROR: No hash found for gcc-13.4.0.tar.xz
```

同时 `package/gcc/gcc-initial/gcc-initial.hash` 内容只有 `../gcc.hash`。
构建前应从 WSL/Linux 修复 symlinks：

```sh
cd buildroot/buildroot
git config core.symlinks true
git ls-files -s | grep "^120000 " | cut -f2 > /tmp/br-symlinks.txt
while IFS= read -r p; do rm -f "$p"; done < /tmp/br-symlinks.txt
git checkout-index -f --stdin < /tmp/br-symlinks.txt
test -L package/gcc/gcc-initial/gcc-initial.hash
```

Buildroot 也会拒绝包含空格、tab 或换行符的 host `PATH`。如果从 Windows tooling 启动
WSL 再调用 Buildroot，应使用干净的 Linux-only `PATH`：

```sh
export PATH="$HOME/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
```

Required host tools 应通过 Linux package manager 安装。Ubuntu/WSL 下最小实用 package set
是：

```sh
sudo apt-get update
sudo apt-get install -y build-essential bc bison cpio file flex git perl \
  python3 rsync unzip wget
```

`unzip` 是真实的 Buildroot host dependency。不要把临时 local shim 当成平台构建契约的一部分。

验证固定的 upstream version：

```sh
git -C buildroot/buildroot describe --tags --exact-match
git -C buildroot/buildroot rev-parse HEAD
```

期望输出：

```text
2025.02.14
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

之后如果需要完整 upstream history：

```sh
git -C buildroot/buildroot fetch --unshallow
```

版本策略和升级流程见 [UPSTREAM.zh-CN.md](UPSTREAM.zh-CN.md)。

## BR2_EXTERNAL Model

外层 `buildroot/` 目录是一个 Buildroot external tree。

Buildroot 通过以下文件和目录识别它：

- `external.desc`
- `external.mk`
- `Config.in`
- `configs/`
- `package/`
- `board/`

这样可以把 upstream Buildroot 和 platform customization 分离，同时仍然允许 Buildroot 发现
platform defconfigs 和 packages。

## 设计规则

### 1. Deterministic Build

每次从相同 source inputs 构建，都必须产生相同 runtime behavior。

禁止：

- Runtime package installation。
- 正常使用所需的 runtime system mutation。
- User-specific filesystem state。
- App-specific image modifications。
- 对 host machine state 的隐藏依赖。

### 2. Minimal RootFS

Root filesystem 只包含平台需要的内容：

- BusyBox。
- libc 和 runtime linker。
- Init scripts。
- Platform configuration。
- BSP 和 runtime libraries。
- Runtime 和 example binaries。
- 当 firmware image 需要版本化管理时包含 required firmware、models 或 assets。

### 3. No Distribution Features

除非被明确接受为 platform-level decision，否则禁止：

- systemd。
- apt、opkg、rpm 或其他 package managers。
- Desktop environments。
- GUI shells。
- 与 platform boot 或 vision runtime 无关的 background services。
- 以 per-user login workflows 作为默认运行模式。

### 4. Firmware-First Operation

目标设备行为类似 embedded firmware：

```text
boot -> init -> vision runtime or configured platform demo
```

设备上不需要 desktop session，也不需要 package installation step。

### 5. Hardware Abstraction Is Mandatory

Buildroot 可以包含 Linux drivers 和 device configuration，但 applications 必须只通过 public
platform APIs 访问硬件。

V4L2、DRM/KMS、evdev、ioctl、sysfs、procfs、device node paths 等 Linux details 必须保持在
BSP boundary 之下。

## Platform Integration Points

Buildroot 集成三个平台层：

### BSP Package

提供以下 public hardware abstraction：

- Camera。
- Display。
- Input。
- 目标 profile 中实际存在时的 LoRa。
- 目标 profile 中实际存在时的 audio、storage、power、radio 或 sensor capabilities。

### Vision Runtime Package

提供：

- Camera to AI to display pipeline。
- Buffer manager。
- AI inference wrapper。
- Event loop。

### SDK Package

提供：

- Examples。
- Templates。
- CLI tools。
- Developer utilities。

## Build Flow

如果你想知道构建前如何定制 Linux system，请先阅读
[CUSTOMIZATION.zh-CN.md](CUSTOMIZATION.zh-CN.md)。

标准流程：

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

期望输出：

```text
output/t_display_k230_vision/images/sysimage-sdcard.img
```

Early bring-up 阶段，只有提供已接受的 K230 SPL 和 U-Boot binaries 后，才会生成最终
SD-card image。在这些 bootloader binaries 缺失时，kernel、DTB、rootfs 和 generated
U-Boot environment 仍然可以先构建出来。

当前 first-boot path 中，这些 bootloader binaries 必须来自已接受的 K230 Linux 参考路径，
并作为 ignored local files staged。见
[board/t-display-k230/bootloader.zh-CN.md](board/t-display-k230/bootloader.zh-CN.md)。

SDK 可以用 `visionctl` 包装这个流程，但 Buildroot contract 保持不变。

第一次交互式配置：

```sh
cd buildroot/buildroot
make BR2_EXTERNAL=.. O=../../output/t_display_k230_vision menuconfig
```

修改配置后，把 platform defconfig 保存到 upstream source tree 外部：

```sh
make O=../../output/t_display_k230_vision savedefconfig \
  BR2_DEFCONFIG=../configs/t_display_k230_vision_defconfig
```

## Change Control

任何 Buildroot change 都必须作为 platform infrastructure 审查。

接受变更之前，必须能对以下问题全部回答“是”：

- 它服务的是 shared vision platform，而不是某一个 app 吗？
- 它是否保持 deterministic boot 和 runtime behavior？
- 它是否保持 root filesystem 最小化？
- 它是否避免 desktop 和 distribution assumptions？
- 它是否把 hardware access 保持在 BSP/runtime APIs 之后？
- 新 dependency 是否记录在正确的 Buildroot subdirectory 中？

## Summary

这个 Buildroot tree 生成 embedded vision platform firmware。

它存在的目的，是保证：

- Stability。
- Reproducibility。
- Hardware consistency。
- Minimal runtime footprint。
- Linux internals 和 platform APIs 之间的清晰边界。
