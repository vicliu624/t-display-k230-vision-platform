# T-Display K230 V1.3 Hardware Contract

The current profile uses CPU0 Linux + CPU1 RT-Smart AMP.
Record interface presence, driver binding and functional acceptance separately.

| Function | Owner and interface | Acceptance scope |
| --- | --- | --- |
| RM69A10 display, VGLite | CPU0, DRM/KMS, VGLite, Labwc | VGLite in both greeter and desktop |
| GT9895 touch, keyboard | CPU0, Linux input/libinput | Keys, Fn, Menu, touch and wake |
| Keyboard backlight | CPU0, IO52 / PWM4, board service and Quick Settings | Physical off/low/high changes |
| RTL8189FS Wi-Fi | CPU0, NetworkManager | Scan, connect and reconnect |
| RTL8152 USB Ethernet | CPU0, r8152, NetworkManager | Inspect the actual interface after attachment |
| GC2093, VICAP/ISP, vision buffers | CPU1; Linux `/dev/tdvp-vision` | Real frames, sequence, dimensions, timeouts and lifecycle |
| KPU, AI2D, FFT, AI memory | CPU1; Linux `/dev/tdvp-ai` | Numerical results and error recovery for supported jobs |
| Audio capture/playback | CPU0, ALSA/ASoC | Devices, input/output and speaker |
| Power, charging, battery and sensors | Bound Linux drivers, sysfs, board status service | Check against the fitted hardware |
| nRF52840 | Independent firmware, K230 UART AT host utility | Physical UART identity and BLE integration still pending |
| LoRa | Linux transport and board control | Separate enumeration/power checks from RF transmit/receive |

The Linux profile disables the old direct camera/ISP, GNNE/KPU and AI2D paths.
`tdvp-vision` and `tdvp-ai` are cross-core interfaces. Linux is expected to report one CPU.

Speaker I2S uses IO32 BCLK, IO33 LRCK and IO35 DATA, with GPIO34 managed by ASoC.
Keyboard backlight uses IO52 PWM with default levels of 0%, 33% and 100%.
Check the current DTS, patches and hardware revision together; the old keyboard
expansion-bus pin description conflicts with the current audio routing.

Expansion sensors and the optional nRF9151 depend on the fitted module.
nRF9151 and nRF52840 are managed separately. The official nRF52840 AT application
has not been mapped into a BlueZ HCI controller; the missing panel Bluetooth
entry remains an integration gap.

See [baseline validation](hardware-baseline-validation.md) and the
[V1.3 checklist](hardware-v1.3-acceptance.md) for procedures and evidence requirements.
