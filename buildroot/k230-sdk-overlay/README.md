# K230 SDK Overlay

This overlay is copied into the pinned K230 Linux SDK worktree before
configuration. It supplies the board configuration and the local package
recipes required by the image.

## Active Board Profile

```text
configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

## Local Packages

| Package | Responsibility |
| --- | --- |
| `gtk-layer-shell` | Layer-shell protocol support for the panel. |
| `sfwbar` | XDG application menu, taskbar, and clock. |
| `tdvp-labwc-desktop` | Labwc session, output transform, touch calibration, background and panel startup. |
| `tdvp-display-smoke` | DRM/KMS display acceptance utility. |
| `tdvp-keyboard-layout` | T-Display K230 keyboard layout service. |
| `tdvp-wayland-acceptance` | Wayland session acceptance utility. |
| `tdvp-kpu-acceptance` | KPU runtime acceptance utility. |
| `vicliu-pocket-linux-hardware` | Board hardware state publisher and integration service. |

`board/tdvp/` contains the rootfs hooks, Linux fragments, deterministic image
configuration, and image verification script. `linux/` contains the tracked
K230 kernel patch sequence.

Package registration is performed by
`buildroot/tools/register-k230-sdk-tdvp-packages.sh` during worktree staging.
