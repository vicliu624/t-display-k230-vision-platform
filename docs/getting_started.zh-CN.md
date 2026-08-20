# 快速开始

## 前置条件

- Linux 或使用 ext4 工作目录的 WSL。
- 已安装 `buildroot/tools/ci-prepare-host.sh` 所需的主机构建依赖。
- 用于写入镜像的 microSD 卡。

## 构建

```sh
git clone --recurse-submodules <repository-url>
cd t-display-k230-vision-platform

WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$WORKTREE"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$WORKTREE"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

未压缩镜像位于：

```text
$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/sysimage-sdcard.img
```

## 写入 SD 卡

先确认主机上的完整 SD 卡块设备，并卸载已经挂载的分区，再写入镜像：

```sh
sudo dd if="$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/sysimage-sdcard.img" \
  of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

请把 `/dev/sdX` 替换为完整 SD 卡设备。macOS 上请先使用 `diskutil list`
确认设备，再使用对应的完整 `/dev/rdiskN` 设备。

## 首次启动

串口为 `ttyS0`，参数 `115200 8N1`。开发环境的 root 密码为 `tdvp`。基础镜像会
启动 `systemd-networkd`、OpenSSH、seatd 和 Labwc 桌面会话。

```sh
systemctl status sshd systemd-networkd seatd tdvp-labwc-desktop
```

桌面包含 SFWBar 应用菜单和 Foot 终端。SFWBar 按照 XDG 标准发现 desktop entry，
因此安装一个带有效 `.desktop` 文件的应用后，它会出现在应用菜单中。

## Wi-Fi

创建标准的接口 WPA 配置，再启用对应实例：

```sh
install -d -m 700 /etc/wpa_supplicant
wpa_passphrase "SSID" "PASSPHRASE" > /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
chmod 600 /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
systemctl enable --now wpa_supplicant@wlan0.service
```

关联完成后，systemd-networkd 的 `wlan0` 配置会获取 IP 地址。
