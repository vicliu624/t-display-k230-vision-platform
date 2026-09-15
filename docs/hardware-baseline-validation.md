# Hardware Baseline Validation

Profile: `k230_canmv_t_display_rm69a10_labwc_desktop_defconfig`.
Each record must identify the image manifest, source revision, boot ID, attached
hardware and files replaced during testing.

## Host checks

```sh
SDK_WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$SDK_WORKTREE"
bash buildroot/tools/assert-public-release.sh "$SDK_WORKTREE"
```

The image verifier checks raw boot layout, paired CPU1 firmware, rootfs, device
tree, login/lock permissions, VGLite policy and hardware interface files.
The release check also accesses the online feed to verify signatures and index
metadata. See the [release contract](release-contract.md) for coverage boundaries.

## Read-only checks on the new card

These commands neither switch sessions nor submit AI jobs:

```sh
uname -a
cat /proc/sys/kernel/random/boot_id
systemctl --no-pager status greetd sshd NetworkManager seatd vicliu-pocket-linux-hardware
nmcli device status
nmcli connection show --active
ls -l /dev/dri /dev/input /dev/tdvp-vision /dev/tdvp-ai
cat /sys/class/drm/card0-DSI-1/status
cat /proc/bus/input/devices
cat /sys/class/misc/tdvp-vision/status
cat /sys/class/misc/tdvp-ai/status
vpl-hwctl status
tdvp-renderer-profile status
arecord -l
aplay -l
ls /sys/bus/i2c/devices
```

Linux is expected to report only CPU0. Camera and AI use cross-core interfaces.
The old V4L2/vendor ISP services and Linux KPU acceptance utility are outside the
current profile. Driver binding and readable status establish interface presence;
functional tests are still required.

## Functional acceptance

- **CPU1:** Follow the [vision and AI jobs guide (Chinese)](cpu1-ai-jobs.zh-CN.md)
  for real frames and supported jobs' numerical results. Tests occupy CPU1;
  first confirm no other application owns the job interface.
- **VGLite:** Check greeter/desktop configuration, processes, VGLite device
  handles and error logs, then run the Wayland session tests in
  [display validation](display-validation.md). Do not run the raw
  `tdvp-display-smoke` DRM-master/modeset test during a desktop session.
  Use the dedicated KMS maintenance workflow after saving desktop work.
- **Input and session:** Test Menu, Fn, touch, workspaces, login, rejection of
  incorrect passwords, idle lock, screen-off, wake and successful unlock.
- **Board functions:** Physically test all three keyboard-backlight levels,
  Wi-Fi and audio capture/playback. Use the actual enumerated USB Ethernet name.
  Inspect I2C sysfs bindings first; establish bus and resource ownership before
  active probing.
- **Open items:** Record nRF52840 Bluetooth integration and LoRa RF tests
  separately. Device-node presence does not establish transmit/receive acceptance.
  Package installation and upgrades are paused; see [feed status](package-feed-status.md).

Summarize results with the [V1.3 checklist](hardware-v1.3-acceptance.md).
Mark skipped or unattached items as untested and give the reason.
