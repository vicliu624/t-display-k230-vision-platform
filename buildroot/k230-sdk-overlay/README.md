# K230 SDK Overlay

This overlay is copied into the pinned K230 Linux SDK worktree before
configuration. Staging also synchronizes local sources from `user-space/*/src`
and verifies their content against a manifest.

## Active board profile

```text
configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

## Main selected packages

| Package | Responsibility |
| --- | --- |
| `gtk-layer-shell`, `wf-panel-pi`, `wfplug-*` | Panel, app menu, network, volume, battery and clock |
| `tdvp-greetd`, `tdvp-gtkgreet`, `tdvp-greeter` | Selected-account login and VGLite greeter |
| `tdvp-labwc-desktop` | VGLite desktop, input, PCManFM, panel and session lifecycle |
| `gtklock`, `gtk-session-lock`, `swayidle`, `wlopm` | Password window, session lock, idle timing and screen-off |
| `tdvp-quick-settings` | Independent touch control center |
| `tdvp-cpu1-vision` | Linux vision/AI kernel bridge and public ABI headers |
| `tdvp-display-smoke` | Maintenance-mode DRM/KMS acceptance |
| `tdvp-vglite-acceptance`, `tdvp-wayland-acceptance` | VGLite and Wayland acceptance tools |
| `tdvp-keyboard-layout`, `vicliu-pocket-linux-hardware` | Keyboard configuration, board control/status and nRF AT host utility |
| `nm-connection-editor` | NetworkManager connection editing |
| `tdvp-opkg-trust` | Public signing key and on-demand trust initialization |

The board build produces paired CPU1 RT-Smart firmware and writes it into the
whole-card raw area. Legacy `tdvp-camera-isp`, `tdvp-camera-isp-runtime` and
`tdvp-kpu-acceptance` recipes remain in source but are not selected by the
current profile. The base desktop excludes the Camera demo, Swaybg and browsers.

`board/tdvp/` holds rootfs hooks, Linux fragments, image layout and verification.
`linux/` holds the controlled kernel patch queue.
`buildroot/tools/register-k230-sdk-tdvp-packages.sh` registers packages during staging.
See [feed status](../../docs/package-feed-status.md) for its acceptance boundaries.
