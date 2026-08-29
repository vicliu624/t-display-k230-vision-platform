# Architecture

## Runtime layers

```text
T-Display K230 V1.3 hardware
  |
K230 Linux kernel, board DTB and vendor firmware runtime
  |
DRM/KMS, DSI, libinput, I2C, SDIO, USB, ALSA, camera/ISP and KPU runtime
  |
systemd, udev, NetworkManager, OpenSSH, seatd and D-Bus
  |
greetd / gtkgreet authenticates a selected Linux account
  |
Labwc Wayland compositor in that account's session
  |-- PCManFM desktop: wallpaper, icons and blank-desktop context menu
  |-- Raspberry Pi wf-panel-pi: menu, network, volume, battery and clock
  `-- Foot, Cog/WPE WebKit, PCManFM and nm-connection-editor
```

The system is intentionally a small standard Wayland desktop for a constrained
keyboard handheld. It uses no GNOME Shell, custom launcher, custom Wi-Fi UI or
custom audio player.

## Authenticated session contract

`greetd` starts the `tdvp-labwc` Wayland session for the account selected at
the greeter. The launch wrapper derives `HOME`, `USER`, `XDG_CONFIG_HOME`,
`XDG_CACHE_HOME`, `XDG_DATA_HOME` and `XDG_RUNTIME_DIR` from that account; it
does not hard-code a particular user or `/home/tdvp`.

The session environment in `/etc/tdvp/labwc/environment` selects the K230 DRM
device, pixman renderer, seatd backend and the 1232x568 logical desktop. Labwc
then starts PCManFM, per-user PulseAudio, the upstream panel and the LilyGO key
bridge. The bridge maps the board Menu key to `wfpanelctl smenu menu`; Fn is an
XKB Mod5 layer, not a user-space key remapper.

The touch rules keep normal taps and drags as left-pointer input. A stationary
long press on an empty PCManFM desktop is delivered as a right click so its
normal context menu opens. GTK clients receive the standard committed-text
compatibility fix required by this touch stack.

## Network, browser and sound

NetworkManager exclusively owns Ethernet and Wi-Fi. It starts the bundled
`wpa_supplicant` only through D-Bus when needed; neither a standalone
`wpa_supplicant@wlan0` service nor `systemd-networkd` is enabled. `wfplug-netman`
and upstream `nm-connection-editor` use NetworkManager's public D-Bus API.

Cog is the native Wayland browser. It launches as a non-maximized window below
the panel; the Labwc Cog rule forces server decorations, which leaves minimize,
maximize and close controls usable on touch hardware. HTTPS is supplied by
`glib-networking` and its GIO OpenSSL module.

The panel has exactly one output-volume plugin. That upstream Raspberry Pi
plugin uses `libcanberra` and the Freedesktop `audio-volume-change` event, so
volume adjustment has standard sound feedback without a private TDVP daemon or
asset format.

## Storage and field updates

The image carries GPT boot partition 1 and ext4 root partition 2 only. On a
larger card the one-shot root expansion service relocates the GPT backup header,
extends partition 2 while preserving its exact PARTUUID, reboots, then expands
ext4. It deliberately leaves a card with a later user partition untouched.

U-Boot mounts root by PARTUUID rather than `/dev/mmcblkN`, avoiding controller
enumeration differences between boards. The image never creates `/data`.

`opkg` is configured with the release's ABI-fixed application feed. A boot
service imports and fingerprint-checks only the embedded public release key;
signature checking remains mandatory. This gives a constrained device a safe
field-update path without embedding a signing key.
