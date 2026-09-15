# Getting Started

## Download or build

The current CPU1 integration branch is `codex/cpu1-rtsmart-integration`.
Choose a successful Actions run and check its manifests and `SHA256SUMS`.
PR builds may use a GitHub-generated test merge commit, so the artifact name
can contain a different revision from the branch HEAD.

For a local build, use Ubuntu 24.04 x86_64 and an ext4 Linux/WSL SDK workspace.
See the [Buildroot guide](../buildroot/README.md) for dependencies and static checks.

```sh
git clone --recurse-submodules --branch codex/cpu1-rtsmart-integration https://github.com/vicliu624/t-display-k230-vision-platform.git
cd t-display-k230-vision-platform
SDK_WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$SDK_WORKTREE"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$SDK_WORKTREE"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$SDK_WORKTREE"
bash buildroot/tools/collect-release-bundle.sh "$SDK_WORKTREE" tdvp-k230-labwc-desktop-local
```

The collector places the compressed image, CPU1 firmware, manifests and checksums in
the repository's `output/tdvp-k230-labwc-desktop-local/` directory. It preserves an
existing destination; choose a new release name for another collection.

## Flash and boot

Run `sha256sum -c SHA256SUMS` in the bundle directory. Use a writer that supports
`.img.gz` to flash the compressed image to the confirmed whole microSD card.
Back up the card and verify its device name and capacity first.
See the [release contract](release-contract.md) for the complete file list.

Serial is `ttyS0`, `115200 8N1`. The development account is `tdvp` / `tdvp`;
the recovery account is `root` / `tdvp`. Change both default passwords before
using the device outside a trusted development network.
The greeter authenticates the selected Linux account and starts its desktop.

On a larger card, first boot can use free space to expand the root partition
and ext4, with one possible automatic reboot. The image has no `/data`
partition; automatic expansion skips cards with later partitions. Check:

```sh
systemctl --no-pager status greetd NetworkManager tdvp-rootfs-expand
cat /var/lib/tdvp/rootfs-expand.status
```

## Desktop, lock screen and network

Both the greeter and the logged-in Labwc desktop use VGLite. PCManFM provides
wallpaper, the desktop and Files; `wf-panel-pi` provides the top panel.
The LilyGO Menu key opens the application menu, blank-desktop long press opens
the context menu, Fn enters yellow symbols, and `Alt+F4` closes the current app.

After 5 idle minutes, gtklock displays a password window. The screen turns off
30 seconds later. Wake it with input and enter the logged-in account's password.
See the [login and lock guide (Chinese)](session-login-and-lock.zh-CN.md).

NetworkManager manages connections through the panel or **Edit Connections**.
For a terminal connection:

```sh
nmcli device status
nmcli device wifi list
nmcli device wifi connect "SSID" password "PASSPHRASE"
```

The base image includes no browser and no Camera menu entry. CPU1 owns camera
capture and AI; Linux applications receive data through asynchronous interfaces.
See the [architecture](architecture.md).

## Software feed: pause installs and upgrades

**As of 2026-09-09, package installation and reboot acceptance has failed.
Validate the new image on its own first.** The previous NetSurf installation
pulled in runtime libraries containing RVV instructions unsupported by CPU0;
the subsequent boot stopped in systemd. The current `stable` feed's signature
checks do not cover this compatibility issue.

Inspecting `/etc/opkg/*.conf` and local package records is safe. Pause installs,
upgrades and bulk updates from this feed until compatibility is validated.
See [software-feed status](package-feed-status.md) for evidence and acceptance criteria.
