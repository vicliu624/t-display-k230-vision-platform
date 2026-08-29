# T-Display K230 V1.3 验收

写入 Labwc 桌面镜像后，在设备上执行下列命令。它们记录板级验收所需的标准 Linux
证据。

```sh
systemctl status sshd NetworkManager seatd tdvp-labwc-desktop
ip address
nmcli device
nmcli connection show --active
cat /sys/class/drm/card0-DSI-1/status
cat /proc/bus/input/devices
arecord -l
aplay -l
gpioinfo
i2cdetect -y 2
```

安装键盘扩展模块时，已知 I2C 地址如下：

| 地址 | 设备 |
| --- | --- |
| `0x38` | AHT20 温湿度传感器 |
| `0x55` | BQ27220 电量计 |
| `0x6b` | BQ25896 充电管理芯片 |

显示与会话检查：

```sh
tdvp-display-smoke --device /dev/dri/card0 --seconds 5
systemctl status tdvp-keyboard-layout
systemctl status vicliu-pocket-linux-hardware
tdvp-kpu-acceptance
```

当 `DSI-1` 面板显示 Labwc、Swaybg、PCManFM 和 wf-panel-pi，能够从应用菜单打开
Foot，且键盘与触摸都可与 Wayland 会话交互时，设备通过桌面部分验收。
