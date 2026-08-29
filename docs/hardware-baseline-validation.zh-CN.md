# 硬件基线验证

本文定义 `k230_canmv_t_display_rm69a10_labwc_desktop_defconfig` 的发布门禁。

## 主机门禁

```sh
WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
bash buildroot/tools/assert-public-release.sh "$WORKTREE"
```

镜像 guard 会验证启动 payload 布局、rootfs、设备树 payload、K230 桌面会话文件、
网络恢复工具、键盘布局服务、触摸规则、KPU 验收工具和硬件集成服务。

## 设备门禁

在已写入镜像的设备上执行：

```sh
uname -a
systemctl status sshd NetworkManager seatd tdvp-labwc-desktop
ip address
ls -l /dev/dri /dev/input
cat /sys/class/drm/card0-DSI-1/status
tdvp-display-smoke --device /dev/dri/card0 --seconds 5
```

对于已连接的板级功能，检查对应的标准 Linux 接口：

| 功能 | 验证接口 |
| --- | --- |
| Wi-Fi | `nmcli device`、`nmcli connection show --active`、`ip address show wlan0` |
| USB 网卡 | `nmcli device`、`ethtool enu1`、`ip address show enu1` |
| 键盘 | `cat /proc/bus/input/devices`、`evtest` |
| 触摸 | `cat /proc/bus/input/devices`、`evtest` |
| I2C 外设 | `i2cdetect -y <bus>` |
| GPIO | `gpioinfo`、`gpiomon` |
| 音频 | `arecord -l`、`aplay -l` |
| 摄像头/ISP | vendor ISP 服务状态与 V4L2 节点 |
| KPU | `tdvp-kpu-acceptance` |

当 `tdvp-labwc-desktop.service` 启动 Labwc、Swaybg、PCManFM、wf-panel-pi 与 Foot，
面板和桌面菜单能够响应键盘与触摸输入，并且 NetworkManager 管理活动网络连接时，
Wayland 桌面通过验收。
