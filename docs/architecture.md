# Architecture

## Processor and hardware ownership

The image uses an AMP layout: Linux on CPU0 and RT-Smart on CPU1.

```text
CPU0 / Linux                         CPU1 / RT-Smart
  Login, apps, network, audio          GC2093 → VICAP / ISP → vision buffers
  greetd / gtkgreet                    AI2D, FFT, nncase / KPU, job processing
  Labwc / VGLite → DRM/KMS display     AI memory and accelerator resources
        |                                      |
        +---- /dev/tdvp-vision: frame reads -----+
        +---- /dev/tdvp-ai: write/poll/read -----+
```

CPU1 manages camera/AI registers, interrupts, memory and resource lifetimes.
CPU0 retains display, VGLite, input, network and audio drivers. Linux applications
use controlled asynchronous bridges; the Linux scheduler sees one CPU.
Linux userspace packages must use CPU0-compatible scalar instructions.
System SDMA remains owned by Linux; the current CPU1 FFT path uses PIO.
The paired device tree and ownership declaration define AI resource boundaries.
They do not authorize arbitrary CPU1 access to shared clocks, power or DMA controllers.

`/dev/tdvp-vision` delivers CPU1-captured frames. `/dev/tdvp-ai` accepts bounded
AI jobs: selected AI2D operations, FFT/IFFT and a fixed KWS reference model.
Complete vision applications, arbitrary model APIs and speech-to-text remain
development work. The video group grants ordinary users access to both nodes.
The current profile retires the former Linux VVCAM/ISP/GNNE/AI2D production paths.

Read-only status lives at `/sys/class/misc/tdvp-vision/status` and
`/sys/class/misc/tdvp-ai/status`. vpl-hwctl, the hardware daemon and Quick Settings
share the publisher. Availability, job counters and acceptance results are
separate fields. See the [AI interface](cpu1-ai-jobs.zh-CN.md) and
[status contract](cpu1-ai-status-remote-validation-20260909.zh-CN.md).

## Login, desktop and locking

greetd starts gtkgreet as the dedicated greeter user, then starts Labwc for the
authenticated Linux account. Session directories derive from that account.
Both the greeter and user desktop specify `WLR_RENDERER=vglite`.
The user desktop checks renderer policy and the failure marker before startup;
GPU faults require diagnosis and explicit recovery.

Labwc owns windows, workspaces and composition. PCManFM supplies wallpaper,
icons and file management; wf-panel-pi supplies the panel. Foot is the terminal
and nm-connection-editor edits network connections. The logical desktop is
1232×568. The Menu key opens the application menu and Fn uses XKB Mod5.
A long press on blank desktop space opens the context menu.

After 300 seconds of inactivity, swayidle calls `tdvp-session-lock` and gtklock
displays its password window. At 330 seconds, wlopm turns off the output.
Wake and unlock the existing session. The PAM unix_chkpwd helper is installed
root:root 4755; graphical programs retain ordinary user privileges.
See [Login and locking](session-login-and-lock.zh-CN.md).

## Network, audio and board radios

NetworkManager owns Wi-Fi and wired connection policy and uses wpa_supplicant
through D-Bus. PulseAudio, ALSA and the volume plugin handle audio; the ASoC
driver controls the external amplifier GPIO.

nRF52840 is an independent programmable board coprocessor. The Linux client
implements the official UART AT protocol. Desktop Bluetooth uses BlueZ/HCI;
integration between these interfaces remains unfinished. Physical UART identity,
BLE behavior, board power preservation and firmware updates require their own
validation. See [nRF integration](nrf52840-at-host.zh-CN.md).
LoRa control/status is exposed by the hardware service; RF acceptance is pending.

## Storage and packages

The GPT filesystem partitions are boot 1 and rootfs 2. Boot payloads and CPU1
firmware occupy fixed raw regions. U-Boot selects root by PARTUUID. First boot
can expand rootfs on a larger card while preserving later user partitions.

On invocation, tdvp-opkg imports and checks the embedded public key before
running opkg. Boot does not access the feed. Devices configure the mutable
`stable` channel. The r6 installation test on 2026-09-09 exposed instruction-set
and base-library replacement problems. Pause installation and upgrades.
See [Package-feed status](package-feed-status.md).
