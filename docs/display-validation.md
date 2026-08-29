# Display Validation

The image validates the internal RM69A10 panel through the K230 DRM/KMS path.
The accepted Wayland desktop uses connector `DSI-1`, output transform `90`,
logical size `1232x568`, and the pixman renderer.

## Image Checks

The image assertion verifies these installed artifacts:

- `/dev/dri/card0` session contract through `tdvp-labwc-desktop.service`.
- `seatd`, `labwc`, `swaybg`, `wf-panel-pi`, its upstream `wfplug-*`
  modules, `pcmanfm`, `foot`, and `wlr-randr`.
- `/etc/tdvp/labwc/environment` with the K230 output values.
- `/etc/xdg/labwc/autostart` launching Swaybg, PCManFM desktop handling, and
  wf-panel-pi.
- GT9895 libinput calibration rule.

Run the host-side check after a build:

```sh
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

## Device Checks

On the device:

```sh
systemctl status seatd tdvp-labwc-desktop
cat /sys/class/drm/card0-DSI-1/status
tdvp-display-smoke --device /dev/dri/card0 --seconds 5
```

The desktop reaches the panel through Labwc. The Raspberry Pi-maintained
wf-panel-pi and its upstream plugins provide the application menu, status
modules, and window list. Foot provides the terminal recovery path.
