# Getting Started

## Build and collect

Build in an ext4 Linux/WSL workspace; use the Windows repository `output/`
directory only for release handoff.

```sh
git clone --recurse-submodules <repository-url>
cd t-display-k230-vision-platform
WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$WORKTREE"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$WORKTREE"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

The raw image is
`$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/sysimage-sdcard.img`.
Run the collector after its image verifier succeeds and use the resulting
Windows-visible `output/` bundle plus `SHA256SUMS` for flashing.

Write the image to the whole confirmed SD-card device, never to one partition:

```sh
sudo dd if="$IMAGE" of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

## First boot and desktop

Serial is `ttyS0`, `115200 8N1`. The development recovery account is
`root` / `tdvp`; change this credential before exposing a device outside a
trusted development network.

The greeter authenticates a Linux account and starts that account's session.
The first boot on an unpartitioned larger card may reboot once while root
partition 2 and ext4 expand. It never creates `/data`. Check:

```sh
systemctl --no-pager status greetd NetworkManager tdvp-rootfs-expand
cat /var/lib/tdvp/rootfs-expand.status
```

The desktop uses PCManFM for wallpaper, desktop and Files; Raspberry Pi
`wf-panel-pi` for the top panel; and Labwc for window management. The LilyGO
Menu key opens the application menu. Blank desktop long press is right click.
Fn is a real keyboard modifier for yellow characters; use `Alt+F4` to close a
full-screen app.

## Network, browser and packages

NetworkManager is the one network owner. Choose Wi-Fi through the panel or
open **Edit Connections**, which runs upstream `nm-connection-editor`:

```sh
nmcli device status
nmcli device wifi list
nmcli device wifi connect "SSID" password "PASSPHRASE"
```

Cog provides a lightweight HTTPS browser. Its window controls are deliberately
visible under the panel. The standard event sound plays when the output-volume
panel control changes.

The release feed is signature-verified. Do not create a local direct
`wpa_supplicant` profile or disable signatures:

```sh
sudo opkg update
sudo opkg list
sudo opkg install <package>
```
