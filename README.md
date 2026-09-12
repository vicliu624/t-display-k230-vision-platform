# Vicliu Pocket Linux for T-Display K230

[简体中文](README.zh-CN.md)

Vicliu Pocket Linux is a Buildroot system for the LILYGO T-Display K230 V1.3
keyboard handheld. The current integration branch is
`codex/cpu1-rtsmart-integration`; PR #1 builds candidate images.

## Processor responsibilities

| Processor | Resources and work |
| --- | --- |
| CPU0 / Linux | Login and applications, Wayland desktop, VGLite composition, display, input, networking, audio capture and playback |
| CPU1 / RT-Smart | GC2093, camera capture, vision buffers, KPU, AI2D, FFT, AI memory and job processing |
| Linux application interfaces | Frames through `/dev/tdvp-vision`; asynchronous AI requests and results through `/dev/tdvp-ai` |

Linux schedules work on CPU0. Seeing one Linux CPU is expected with this design.
CPU1 receives work through the cross-core interfaces. Linux packages must target
CPU0's scalar instruction set.

CPU1 implements camera capture, bounded AI2D operations, FFT/IFFT and a fixed
KWS reference model. Device records cover these paths. Arbitrary model loading,
complete vision applications and speech-to-text remain development work.
See [Architecture](docs/architecture.md), the
[AI job interface](docs/cpu1-ai-jobs.zh-CN.md) and the
[2026-09-09 device record](docs/cpu1-ai-status-remote-validation-20260909.zh-CN.md).

## Desktop and login

- greetd/gtkgreet provides graphical login. The session uses the authenticated
  account's home and runtime directories.
- Both the greeter and user desktop use VGLite. The user session requires valid
  image policy and a clear GPU failure marker; failed checks block startup for diagnosis.
- Labwc manages windows, PCManFM supplies the desktop and files, and wf-panel-pi
  displays the application menu, network, volume, battery and clock.
- After 300 seconds of inactivity, gtklock displays a password window.
  Screen output turns off 30 seconds later. Wake with a key and unlock with the
  current account's password; applications remain in the same session.
- LilyGO/Menu opens the application menu, Fn enters the yellow keycap symbols,
  and `Alt+F4` closes the current application.
- Foot, PCManFM and nm-connection-editor are included. General browsers and a
  Camera demo menu entry are absent from the base image.

See [Login, lock and screen blanking](docs/session-login-and-lock.zh-CN.md).

## Delivery status

As of 2026-09-09, CPU1 AI/camera, VGLite and keyboard-backlight validation records
exist for specific images or hot deployments. Each record's commit, image name
and boot ID define its scope. New candidates still require whole-card boot and
hardware checks.

The nRF52840 Linux AT client has passed protocol replay tests. Physical UART
identity, BLE traffic and desktop Bluetooth integration remain incomplete.
No nRF firmware has been flashed. LoRa has control/status interfaces; RF
transmit/receive acceptance remains outstanding.
See [nRF52840 status](docs/nrf52840-at-host.zh-CN.md).

**Package-feed acceptance failed. Pause package installation and upgrades on
delivery cards.** On 2026-09-09, installing NetSurf dependencies replaced CPU0
system libraries with RVV libraries from r6. A later boot stopped on an illegal
instruction in libmount. The next feed test will use a newly flashed image as
its baseline. See [Package-feed status](docs/package-feed-status.md).

## Network and external speaker

NetworkManager manages Wi-Fi and wired connections. The panel's **Edit
Connections** action opens nm-connection-editor; Wi-Fi uses the D-Bus
wpa_supplicant backend.

The external MAX98357A-compatible amplifier uses the existing
`K230_I2S_INNO` ALSA card. IO32, IO33 and IO35 carry BCLK, LRCK and data-out;
the ASoC driver controls GPIO34 shutdown. Explicit route selection uses
`tdvp-audio-route external|internal`. Speaker acceptance includes listening:

```sh
sudo tdvp-speaker-acceptance status
sudo tdvp-speaker-acceptance test
# After hearing both channels, stop with Ctrl-C and record confirmation:
sudo tdvp-speaker-acceptance confirm-audible
```

## Build, download and flash

Use native Ubuntu 24.04 x86_64 or a matching container for CI parity and keep
SDK output on Linux ext4. See [Getting Started](docs/getting_started.md) and
[Buildroot workflow](buildroot/README.md).

PR Actions build GitHub's test merge commit. The hash in an artifact name can
therefore differ from the branch HEAD. Match the run's head commit, artifact
manifest and `SHA256SUMS` before flashing.
The collector writes `<release-name>.img.gz` and companion files to
`output/<release-name>/` in the project checkout; Actions uploads that directory.

The GPT filesystem partitions are boot 1 and rootfs 2. Boot payloads and CPU1
firmware also occupy fixed raw offsets. First boot on a larger card may reboot
once while expanding root; its PARTUUID is preserved. Cards with later user
partitions skip automatic partition expansion.

## Checks after login

Serial is `ttyS0`, 115200 8N1. Development accounts `tdvp` and recovery
`root` initially use password `tdvp`; change these before connecting to
untrusted networks.

```sh
systemctl --no-pager status greetd NetworkManager sshd vicliu-pocket-linux-hardware
nmcli device status
tdvp-renderer-profile status
vpl-hwctl status
cat /sys/class/misc/tdvp-vision/status
cat /sys/class/misc/tdvp-ai/status
```

Record read-only status checks and functional acceptance separately.
See the [Release Contract](docs/release-contract.md) and
[Hardware Validation](docs/hardware-baseline-validation.md).
