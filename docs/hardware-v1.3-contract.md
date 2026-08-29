# T-Display K230 V1.3 Hardware Contract

The Labwc desktop profile uses the T-Display K230 V1.3 board configuration in
the pinned K230 Linux SDK. Hardware integration is exposed through normal
Linux kernel subsystems and user-space interfaces.

| Board function | Linux interface | Image component |
| --- | --- | --- |
| RM69A10 internal panel | DRM/KMS connector `DSI-1`, `/dev/dri/card0` | Canaan DRM, Labwc session |
| GT9895 touch | Linux input event device, libinput | kernel touch fragment and `70-tdvp-touch.rules` |
| Keyboard extension | Linux input event device | `tdvp-keyboard-layout.service` |
| Keyboard expansion I2C bus | `/dev/i2c-*` | K230 keyboard/hardware fragments |
| RTL8189FS Wi-Fi | `wlan0`, NetworkManager / `nmcli` | RTL8189FS package and NetworkManager |
| RTL8152 USB Ethernet | `enu1`, NetworkManager / `nmcli` | kernel r8152 driver and NetworkManager |
| Camera and ISP | V4L2/media nodes and vendor ISP service | `vvcam` package and vendor service |
| Audio capture/playback | ALSA devices | ALSA utilities |
| GPIO and I2C diagnostics | gpio character devices and `/dev/i2c-*` | libgpiod tools and i2c-tools |
| KPU and AI2D | K230 runtime devices and nncase assets | `libnncase`, `ai2d-kpu`, acceptance utility |

The removable keyboard extension shares one physical module for keyboard,
backlight, BQ25896 charge controller, BQ27220 fuel gauge, and optional nRF9151
LTE-M/GNSS hardware. Its I2C bus is connected through IO32 SCL and IO33 SDA.
The V1.3 profile retains the board wiring required by both K256-04 and
K256-04-A variants. A K256-04-A unit simply has no nRF9151 device to expose.

The image sets the keyboard backlight from the board integration service and
publishes hardware state through standard device files and service status.
