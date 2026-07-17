# Buildroot - T-Display K230 Vision Platform

This directory defines Layer 0 of the T-Display K230 Vision Platform.

Buildroot is the system foundation layer. Its job is to generate a
minimal, deterministic embedded firmware image for the platform.

Chinese version:

- [README.zh-CN.md](README.zh-CN.md)

## Purpose

Buildroot is used to generate a reproducible Linux system for:

- T-Display K230 hardware.
- BSP hardware abstraction.
- Vision runtime execution.
- Embedded camera, display, input, and AI pipelines.
- SDK examples that validate the platform contract.

## Non-Goals

This Buildroot tree must not become:

- A desktop Linux system.
- A Debian or Ubuntu replacement.
- A package-managed distribution.
- A general-purpose development OS.
- A collection of per-application system tweaks.
- A place where demos define platform behavior.

## System Output

The build produces one bootable system image:

```text
sysimage-sdcard.img
```

The image contains:

- Bootloader artifacts required by the target board.
- Linux kernel.
- Device tree blobs.
- Minimal BusyBox root filesystem.
- BSP libraries.
- Vision runtime binaries and libraries.
- SDK examples and required platform assets.

The image must not require runtime package installation after flashing.

## Directory Structure

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

The official Buildroot source tree is tracked as a Git submodule:

```text
buildroot/buildroot/
```

Submodule URL:

```text
https://gitlab.com/buildroot.org/buildroot.git
```

Pinned baseline:

```text
Buildroot 2025.02.14 LTS
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

Rules:

- The submodule must be pinned to an official Buildroot release tag.
- Do not track `master` for platform builds.
- Do not pin to arbitrary development commits.
- Do not place platform code inside `buildroot/buildroot/`.
- Do not edit official Buildroot files for platform customization.
- Keep platform defconfigs, packages, board files, overlays, and kernel config
  fragments in the outer `buildroot/` br2-external tree.
- Upgrade upstream by updating the submodule commit, then validating the
  platform defconfig and runtime image.

Initial setup for the platform repository:

```sh
git submodule add --depth 1 https://gitlab.com/buildroot.org/buildroot.git buildroot/buildroot
git -C buildroot/buildroot fetch --depth 1 origin tag 2025.02.14
git -C buildroot/buildroot checkout 2025.02.14
git config -f .gitmodules submodule.buildroot/buildroot.shallow true
```

Initialize or update the submodule:

```sh
git submodule update --init --depth 1 buildroot/buildroot
```

## Build Host and Line Endings

Buildroot must be configured and built from a Linux-compatible environment.
On Windows, use WSL or a Linux build machine for Buildroot commands.

Do not use Windows-native `mingw32-make` as the platform Buildroot build
entrypoint.

Preferred Windows workflow: keep the full platform repository under the WSL
ext4 filesystem, for example `~/src/t-display-k230-vision-platform`, and edit
it through the Windows editor's WSL integration. Building from `/mnt/c/...`
works poorly for this project: it is much slower, Windows Git may report valid
WSL symlinks as type changes, and Buildroot's source tree relies on real
symlinks.

The official Buildroot submodule must keep LF line endings. If the repository
is checked out on Windows with `core.autocrlf=true`, shell scripts inside
`buildroot/buildroot/` may become CRLF and fail under WSL with errors such as
`/bin/sh^M`.

Before running Buildroot commands from WSL/Linux, force the submodule checkout
policy to LF:

```sh
git -C buildroot/buildroot config core.autocrlf false
git -C buildroot/buildroot config core.eol lf
git -C buildroot/buildroot config core.symlinks true
```

If the submodule was already checked out with CRLF line endings, refresh the
submodule working tree from WSL/Linux. Only do this when
`git -C buildroot/buildroot status --short` is clean:

```sh
git -C buildroot/buildroot checkout-index -f -a
git -C buildroot/buildroot ls-files --eol support/scripts/setlocalversion
```

The expected line-ending status for Buildroot shell scripts is:

```text
i/lf    w/lf
```

The official Buildroot tree also uses real symbolic links. A Windows checkout
with `core.symlinks=false` may turn those links into small text files. One
visible failure mode is:

```text
ERROR: No hash found for gcc-13.4.0.tar.xz
```

with `package/gcc/gcc-initial/gcc-initial.hash` containing only `../gcc.hash`.
Repair symlinks from WSL/Linux before building:

```sh
cd buildroot/buildroot
git config core.symlinks true
git ls-files -s | grep "^120000 " | cut -f2 > /tmp/br-symlinks.txt
while IFS= read -r p; do rm -f "$p"; done < /tmp/br-symlinks.txt
git checkout-index -f --stdin < /tmp/br-symlinks.txt
test -L package/gcc/gcc-initial/gcc-initial.hash
```

Buildroot also rejects host `PATH` values that contain spaces, tabs, or
newlines. When invoking Buildroot from WSL launched by Windows tooling, use a
clean Linux-only `PATH`:

```sh
export PATH="$HOME/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
```

Required host tools should be installed through the Linux package manager. On
Ubuntu/WSL, the minimum practical package set is:

```sh
sudo apt-get update
sudo apt-get install -y build-essential bc bison cpio file flex git perl \
  python3 rsync unzip wget
```

`unzip` is a real Buildroot host dependency. Do not treat a temporary local
shim as part of the platform build contract.

Verify the pinned upstream version:

```sh
git -C buildroot/buildroot describe --tags --exact-match
git -C buildroot/buildroot rev-parse HEAD
```

Expected:

```text
2025.02.14
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

For full upstream history later:

```sh
git -C buildroot/buildroot fetch --unshallow
```

See [UPSTREAM.md](UPSTREAM.md) for the version policy and upgrade procedure.

## BR2_EXTERNAL Model

The outer `buildroot/` directory is a Buildroot external tree.

Buildroot recognizes this directory through:

- `external.desc`
- `external.mk`
- `Config.in`
- `configs/`
- `package/`
- `board/`

This keeps upstream Buildroot and platform customization separate while still
allowing Buildroot to discover platform defconfigs and packages.

## Design Rules

### 1. Deterministic Build

Every build from the same source inputs must produce identical runtime
behavior.

Forbidden:

- Runtime package installation.
- Runtime system mutation required for normal use.
- User-specific filesystem state.
- App-specific image modifications.
- Hidden dependency on host machine state.

### 2. Minimal RootFS

The root filesystem includes only what the platform needs:

- BusyBox.
- libc and runtime linker.
- Init scripts.
- Platform configuration.
- BSP and runtime libraries.
- Runtime and example binaries.
- Required firmware, models, or assets when versioned by the platform.

### 3. No Distribution Features

Forbidden unless explicitly accepted as a platform-level decision:

- systemd.
- apt, opkg, rpm, or other package managers.
- Desktop environments.
- GUI shells.
- Background services unrelated to platform boot or vision runtime.
- Per-user login workflows as the default operating mode.

### 4. Firmware-First Operation

The target behaves like embedded firmware:

```text
boot -> init -> vision runtime or configured platform demo
```

No desktop session is required. No package installation step is required on the
device.

### 5. Hardware Abstraction Is Mandatory

Buildroot may include Linux drivers and device configuration, but applications
must access hardware only through public platform APIs.

Linux details such as V4L2, DRM/KMS, evdev, ioctl, sysfs, procfs, and device
node paths must remain below the BSP boundary.

## Platform Integration Points

Buildroot integrates three platform layers:

### BSP Package

Provides public hardware abstraction for:

- Camera.
- Display.
- Input.
- LoRa when present in the target profile.
- Audio, storage, power, radio, or sensor capabilities when present in the
  target profile.

### Vision Runtime Package

Provides:

- Camera to AI to display pipeline.
- Buffer manager.
- AI inference wrapper.
- Event loop.

### SDK Package

Provides:

- Examples.
- Templates.
- CLI tools.
- Developer utilities.

## Build Flow

If you are looking for how to customize the Linux system before building it,
read [CUSTOMIZATION.md](CUSTOMIZATION.md) first.

Canonical flow:

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

Expected output:

```text
output/t_display_k230_vision/images/sysimage-sdcard.img
```

During early bring-up, this final SD-card image is generated only after the
accepted K230 SPL and U-Boot binaries are supplied. The kernel, DTB, rootfs,
and generated U-Boot environment can still build before those bootloader
binaries are present.

For the current first-boot path, those bootloader binaries must come from the
accepted K230 Linux reference path and be staged as ignored local files. See
[board/t-display-k230/bootloader.md](board/t-display-k230/bootloader.md).

The SDK may wrap this flow with `visionctl`, but the Buildroot contract remains
the same.

For first-time interactive configuration:

```sh
cd buildroot/buildroot
make BR2_EXTERNAL=.. O=../../output/t_display_k230_vision menuconfig
```

After changing configuration, save the platform defconfig outside the upstream
source tree:

```sh
make O=../../output/t_display_k230_vision savedefconfig \
  BR2_DEFCONFIG=../configs/t_display_k230_vision_defconfig
```

## Change Control

Any Buildroot change must be reviewed as platform infrastructure.

Before accepting a change, answer yes to all items:

- Does it serve the shared vision platform rather than one app?
- Does it preserve deterministic boot and runtime behavior?
- Does it keep the root filesystem minimal?
- Does it avoid desktop and distribution assumptions?
- Does it keep hardware access behind BSP/runtime APIs?
- Is the new dependency documented in the right Buildroot subdirectory?

## Summary

This Buildroot tree generates embedded vision platform firmware.

It exists to guarantee:

- Stability.
- Reproducibility.
- Hardware consistency.
- Minimal runtime footprint.
- A clean boundary between Linux internals and platform APIs.
