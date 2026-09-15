# Release Contract

This document defines T-Display K230 deliverables and acceptance scope.
Record code integration, CI builds and hardware acceptance separately.
A successful build establishes only the properties checked in that run.

## Delivered files

`collect-release-bundle.sh` produces these files under a caller-selected release name:

```text
<release-name>.img.gz
tdvp-image-manifest
tdvp-cpu1-rtsmart.bin
tdvp-cpu1-rtsmart.manifest
tdvp-sdk-baseline-manifest
tdvp-image-base.json
tdvp-opkg-status
tdvp-opkg-info.tar.gz
tdvp-buildroot-packages.json
<release-name>-cpu0-sdk.tar.gz
tdvp-sdk-manifest.json
README.txt
SHA256SUMS
```

The bundle lives in the repository's `output/<release-name>/`. Local WSL builds
must collect into the user-visible repository directory; CI uploads that directory.
The bundle delivers only the compressed image. CPU1 firmware is also embedded
in the whole-card image; its separate copy supports pairing and checksum inspection.
CI also adds `tdvp-sdk-validation.log` and its SHA-256 after the isolated SDK test.
The application SDK contains the paired compiler, development sysroot and image
package records; see [CPU0 application SDK](cpu0-application-sdk.md).

The image includes CPU0 Linux, CPU1 RT-Smart/OpenSBI, systemd, OpenSSH,
NetworkManager, seatd, greetd/gtkgreet, Labwc/VGLite, PCManFM, wf-panel-pi,
Foot, nm-connection-editor, gtklock, board services and opkg trust material.
Browsers and the Linux Camera demo have been removed from the base desktop.

## Image invariants

- U-Boot locates the root filesystem through a fixed root `PARTUUID`.
- GPT contains boot partition 1 and root partition 2. Boot firmware also occupies
  raw areas, including the CPU1 slot at 10–30 MiB. First boot can expand root,
  preserves later partitions, and creates no `/data`.
- Linux runs on CPU0 using its supported scalar ISA. CPU1 exclusively owns
  GC2093, capture/ISP, KPU, AI2D, FFT and related AI memory. Linux uses
  `/dev/tdvp-vision` and `/dev/tdvp-ai` asynchronously.
- Both greeter and desktop use VGLite. Renderer failures end the session and
  leave diagnostic state; switching to Pixman is prohibited.
- The greeter authenticates the selected account and uses its home/runtime.
  gtklock authenticates the current session account. Defaults are 300 idle
  seconds to lock and 330 seconds to screen-off, with a password window on wake.
- PCManFM supplies wallpaper, desktop and Files; wf-panel-pi supplies the panel.
  Test Menu, Fn, touch and keyboard backlight on physical hardware.
- NetworkManager manages connections; nm-connection-editor edits them.
- Feed signature verification stays enabled. Valid signatures and ABI metadata
  also require ISA, file ownership, dependency closure and cold-boot validation
  before package installation can be accepted as safe.

## Checks and acceptance

| Stage | Checks | Scope of the conclusion |
| --- | --- | --- |
| Pre-build | Pinned SDK, patch structure/replay, source contracts, hardware preflight | Checked inputs and configuration |
| Full build | Release defconfig, paired CPU1 firmware, post-image verifier | Asserted image layout, rootfs files and configuration |
| SDK handoff | File hashes, image binding, two isolated paths, C/C++/GTK/CMake and ELF attributes | Application compiler and checked dependencies work without the original build tree |
| Static feed gate | HTTPS, index signatures, release metadata, package ABI dependencies, required names | Published index and trust material |
| Fresh-card hardware | Boot/reboot, CPU1 data and numerical results, VGLite, login/lock, input, network, audio | That image on that hardware |
| On-device packages | Dependency installation, app startup, removal/upgrade boundaries, reboot | End-to-end package-manager and feed usability |

See [hardware baseline validation](hardware-baseline-validation.md).
Historical hot-deployment results must identify the base image and replaced
files. Each candidate whole-card image still requires its own acceptance.

`tdvp-image-manifest` records source/build inputs, partition identities and image
hashes. The CPU1 manifest describes paired firmware; `tdvp-sdk-baseline-manifest`
records staged inputs. `tdvp-sdk-manifest.json` binds the application SDK to the
image and package inventory. `SHA256SUMS` covers the other delivered files.

## Feed status and remaining requirements

The image configures the mutable `stable` channel. See [feed status](package-feed-status.md)
for its full URL and public key. The trailing `r1` in the platform identifier is
part of the ABI label; the observed feed revision on 2026-09-09 was `r6`.

**End-to-end package acceptance has failed; installs and upgrades are paused.**
Runtime libraries installed with NetSurf contained RVV instructions unsupported
by CPU0, and the device subsequently failed to boot. Image publication now
checks final ext4 ownership and the preinstalled package database and exports
those records. The online feed signature/metadata gate is a separate operation.
Per-IPK compatibility and end-to-end acceptance remain required. Matching
SDK/sysroot publication is still needed before establishing a public feed's
build baseline.

Before resuming package delivery, require:

- CPU0-compatible executables and runtime libraries throughout the Linux feed.
- Explicit, verifiable versions and package ownership for libraries shared by
  the base image and feed.
- One provider for each runtime library, plugin and helper, with a complete dependency closure.
- Continued signature verification, with private keys kept at the publisher
  and only public keys present on the device.
- NetSurf installation, HTTPS browsing and reboot acceptance on the paired candidate image.
