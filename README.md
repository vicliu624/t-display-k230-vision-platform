# Vicliu Pocket Linux for T-Display K230

Vicliu Pocket Linux produces a bootable Buildroot image for the LILYGO
T-Display K230 V1.3. The image starts a complete, conventional Wayland
desktop on the K230 panel.

## Desktop

```text
Linux DRM/KMS + libinput
        |
      seatd
        |
     Labwc
     |   |
     |   +-- Swaybg: desktop background
     +------ SFWBar: application menu, taskbar, clock
        |
      Foot: terminal supplied with the image
```

Labwc owns the Wayland compositor session and window stacking. SFWBar obtains
applications from standard XDG `.desktop` entries. Packages and locally
installed programs become visible by placing entries in the normal
`/usr/share/applications`, `/usr/local/share/applications`, or per-user XDG
application directories.

## Board Baseline

The profile includes the K230 Linux SDK kernel and board integration for the
RM69A10 DSI panel, GT9895 touch controller, keyboard extension, RTL8189FS
Wi-Fi, RTL8152 USB Ethernet, camera/ISP runtime, audio utilities, GPIO and
I2C tools, KPU runtime components, and the board hardware publisher service.
The session uses the board-validated DRM output `DSI-1`, transform `90`, and
logical desktop size `1232x568`.

## Build

Use an ext4 workspace in WSL or Linux:

```sh
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$HOME/work/tdvp-k230-labwc"
```

The image is written to:

```text
$HOME/work/tdvp-k230-labwc/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/sysimage-sdcard.img
```

Run the release collector after a successful build:

```sh
bash buildroot/tools/collect-release-bundle.sh \
  "$HOME/work/tdvp-k230-labwc" \
  vicliu-pocket-linux-k230-candidate
```

See [Getting Started](docs/getting_started.md),
[Architecture](docs/architecture.md), and
[Release Contract](docs/release-contract.md).
