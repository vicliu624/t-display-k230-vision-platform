# Getting Started

## Requirements

- Linux or WSL with an ext4 workspace.
- The host dependencies installed by `buildroot/tools/ci-prepare-host.sh`.
- A microSD card for the generated image.

## Build

```sh
git clone --recurse-submodules <repository-url>
cd t-display-k230-vision-platform

WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$WORKTREE"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$WORKTREE"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

The uncompressed image is:

```text
$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/sysimage-sdcard.img
```

## Flash

Identify the complete SD-card block device on the host, unmount its mounted
partitions, then write the image to the device.

```sh
sudo dd if="$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/sysimage-sdcard.img" \
  of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

Replace `/dev/sdX` with the whole SD-card device. On macOS use the matching
whole `/dev/rdiskN` device after confirming it with `diskutil list`.

## First Boot

The serial console is `ttyS0` at `115200 8N1`. The development root password is
`tdvp`. The base image starts `systemd-networkd`, OpenSSH, seatd, and the Labwc
desktop session.

```sh
systemctl status sshd systemd-networkd seatd tdvp-labwc-desktop
```

The desktop contains the SFWBar application menu and Foot terminal. SFWBar
discovers standard XDG desktop entries, so installing an application with a
valid `.desktop` file is sufficient for it to appear in the menu.

## Wi-Fi

Create the standard per-interface WPA profile, then enable its matching
instance:

```sh
install -d -m 700 /etc/wpa_supplicant
wpa_passphrase "SSID" "PASSPHRASE" > /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
chmod 600 /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
systemctl enable --now wpa_supplicant@wlan0.service
```

The systemd-networkd `wlan0` configuration obtains the IP address after
association.
