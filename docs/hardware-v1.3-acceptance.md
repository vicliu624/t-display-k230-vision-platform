# T-Display K230 V1.3 Acceptance Checklist

Start with the read-only [baseline checks](hardware-baseline-validation.md),
then schedule functional tests. Record the image, boot ID, hardware variant and
individual results. This checklist does not itself certify acceptance.

- [ ] Whole-card boot and reboot succeed; SSH works and CPU0 Linux is paired with CPU1 firmware.
- [ ] CPU1 vision/AI status is valid and real GC2093 frames reach Linux asynchronously.
- [ ] Supported AI2D, FFT/IFFT and fixed KWS jobs pass numerical checks without resource conflicts.
- [ ] Greeter and desktop both use VGLite, with no Pixman session; retain logs and device-handle evidence.
- [ ] Selected-account login, wrong-password rejection, 300-second idle lock,
      330-second screen-off, password window on wake and unlock work.
- [ ] PCManFM, wf-panel-pi and Quick Settings work; the menu launches Foot.
- [ ] Menu, Fn, touch, long-press context menu and workspace switching work.
- [ ] Quick Settings off/low/high keyboard-backlight controls match physical output.
- [ ] Wi-Fi connects and reconnects; test attached USB Ethernet by its actual interface name.
- [ ] Audio playback, capture, volume control and event sounds work.
- [ ] Battery, charging and fitted sensor status match the hardware.
- [ ] nRF52840 UART identity and BLE transmit/receive pass; this remains open.
- [ ] LoRa RF transmit/receive pass; enumeration and power status alone leave this item open.
- [ ] Package installation, app execution and reboot pass; currently paused, see [feed status](package-feed-status.md).

When the relevant module is fitted, inspect sysfs bindings for AHT20 `0x38`,
BQ27220 `0x55` and BQ25896 `0x6b`. These addresses help identify devices;
establish the actual bus before active probing.
Raw DRM/KMS tests interrupt the desktop; see [display validation](display-validation.md).
