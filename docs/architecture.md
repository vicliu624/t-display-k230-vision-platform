# Architecture

## Runtime Layers

```text
T-Display K230 V1.3 hardware
  |
K230 Linux kernel, device tree, firmware runtime
  |
DRM/KMS, DSI, input, I2C, SDIO, USB, ALSA, camera/ISP, KPU runtime
  |
systemd, udev, systemd-networkd, OpenSSH, seatd, D-Bus
  |
Labwc Wayland compositor
  |-- Swaybg desktop background
  |-- SFWBar application menu, taskbar, clock
  `-- standard Wayland applications such as Foot
```

## Session

`tdvp-labwc-desktop.service` runs as the `tdvp` desktop user. It creates a
private runtime directory, receives access to `seat`, `video`, `input`, and
`render`, loads the K230 environment contract, and starts Labwc through a
D-Bus session.

The session contract is stored in `/etc/tdvp/labwc/environment`:

| Setting | Value | Purpose |
| --- | --- | --- |
| `WLR_DRM_DEVICES` | `/dev/dri/card0` | K230 DRM device. |
| `WLR_RENDERER` | `pixman` | Validated software renderer for the K230 path. |
| `LIBSEAT_BACKEND` | `seatd` | DRM/input device access. |
| `TDVP_K230_OUTPUT` | `DSI-1` | Internal DSI connector. |
| `TDVP_K230_OUTPUT_TRANSFORM` | `90` | Physical panel transform. |
| logical size | `1232x568` | Wayland desktop coordinate space. |

Labwc processes the XDG autostart file after it owns the DRM backend. The
autostart file applies the output transform and launches Swaybg and SFWBar.

## Application Model

Applications use normal Wayland protocols and the XDG desktop-entry format.
SFWBar scans XDG data directories for `.desktop` files and starts each
application through its declared `Exec` command. The taskbar tracks windows
reported by the compositor. Foot is the image-provided terminal entry.

## Board Integration

The `vicliu-pocket-linux-hardware` service publishes board hardware state and
performs the integration steps used by the profile. The vendor ISP startup
script remains enabled for the camera runtime. Keyboard layout, touch
calibration, display acceptance, and KPU acceptance are installed as focused
system utilities and services.
