# Vicliu Pocket Linux for T-Display K230

Vicliu Pocket Linux is a maintained Buildroot distribution for the LILYGO
T-Display K230 V1.3 keyboard handheld. This repository produces the current
maintained desktop release: a small native-Wayland system for a constrained K230
device, not a cut-down PC distribution and not a custom launcher shell.

The release uses conventional, maintained components:

```text
Linux DRM/KMS + libinput + ALSA
        |
systemd + NetworkManager + seatd + D-Bus
        |
greetd / gtkgreet (authenticates the selected Linux user)
        |
Labwc + PCManFM desktop + wf-panel-pi
        |
Foot, PCManFM, nm-connection-editor, signed opkg feed
```

The desktop is deliberately modest because the K230 is a low-performance,
keyboard-first handheld. It favors reliable, touchable standard programs over
a large GNOME or Chromium stack. No GNOME Shell is included.

## What the image provides

- A `greetd` graphical login. Each authenticated session uses that account's
  actual home directory; it is never forced to `/home/tdvp`.
- Labwc, PCManFM and Raspberry Pi's upstream `wf-panel-pi` plugins. The panel
  has the application menu, NetworkManager status, one output-volume control,
  battery state and clock.
- The physical LilyGO/Menu key opens the panel application menu. A long touch
  on empty desktop space is a right click; short touch behaves as normal
  pointer input. `Alt+F4` closes a normal or full-screen application.
- A real XKB Fn layer for the printed yellow keyboard symbols. Both physical
  Fn keys are `ISO_Level3_Shift` (Mod5); plain Tab remains Tab.
- Foot terminal, PCManFM Files, and the standard NetworkManager connection
  editor. The base image intentionally has no general browser and no dead
  browser menu entry. Browser delivery is an application-feed concern, not a
  board-firmware boot dependency.
- An external MAX98357A-compatible I²S speaker route on the existing
  `K230_I2S_INNO` ALSA card. It is not a second sound card and does not rely
  on a panel plugin or a player owning GPIO34.

## Current release state

The firmware currently configures the immutable `tdvp-k230-…-r1` application
feed. It contains only packages that have already been signed and published
for this firmware ABI. In particular, the base image does **not** currently
offer a browser package. A browser candidate must first be published under a
new immutable feed revision; an existing signed feed index is never rewritten
to add a package.

## External speaker route and acceptance

The kernel pins IO32 (BCLK), IO33 (LRCK), and IO35 (data-out) for the external
I²S path. IO34/GPIO34 is the amplifier shutdown line. The K230 ASoC machine
driver owns that GPIO: it holds the amplifier off while idle or changing
routes, and raises it only for an active external-route playback stream.

The device-tree default selects the external route before userspace starts.
There is deliberately no `tdvp-external-audio.service` in the boot path: a
late policy service can race sound-card registration and turn a non-critical
route preference into a failed graphical boot. `tdvp-audio-route` is the sole
root policy tool for an explicit `external` or `internal` switch; it
deliberately never calls `gpioset`. PulseAudio, players and the panel remain
ordinary ALSA clients rather than competing GPIO owners.

After flashing, verify actual hardware audibility separately from driver
enumeration:

```sh
sudo tdvp-speaker-acceptance status
sudo tdvp-speaker-acceptance test
# Listen for both channels, stop the sine test with Ctrl-C, then:
sudo tdvp-speaker-acceptance confirm-audible
```

The hardware state publisher reports the speaker as **unverified** until that
last explicit audible confirmation. An ALSA card by itself is never treated as
proof that the external amplifier and loudspeaker work.

## SD-card storage behavior

The image contains only GPT boot partition 1 and root partition 2. It never
creates `/data` or consumes the remaining capacity with a third partition.

On the first boot from a larger SD card, `tdvp-rootfs-expand.service` follows
the Raspberry Pi expansion model: it relocates the GPT backup header, expands
partition 2, preserves its exact GPT `PARTUUID`, and reboots once. On the next
boot it expands the mounted ext4 root filesystem. Preserving the partition UUID
is mandatory because U-Boot boots with `root=PARTUUID=…`, not a fragile
`/dev/mmcblkN` index. Existing cards that already have a later user partition
are left unchanged.

## Network and browser delivery

NetworkManager is the single owner of wired and Wi-Fi *connection policy*.
`systemd-networkd` and the old per-interface `wpa_supplicant@wlan0` setup are
not enabled in the image. NetworkManager uses the normal D-Bus
`wpa_supplicant` backend for Wi-Fi; starting that backend is not a second
network manager. Use the Wi-Fi item in the panel to select an access point or
**Edit Connections**; it opens upstream `nm-connection-editor` rather than an
in-house dialog.

The base image intentionally contains no general-purpose browser. This keeps a
heavy WebKit/Chromium stack out of the boot-critical firmware. A future browser
package must be ABI-matched, signed, and published in a new feed revision
before it receives a desktop entry. It must provide a normal address bar and
navigation rather than a kiosk-only shell. Heavy modern web applications may
still exceed the K230's CPU and RAM budget.

## Signed software-distribution feed and Software Manager

The built-in Software Manager opens Foot with the real `opkg` client. The
firmware intentionally keeps only a small hardware/desktop seed; its
ABI-fixed, immutable feed revision is the expandable userland catalogue for
common libraries, command-line tools, desktop programs, and device apps:

```text
https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/r2/riscv64
```

Immediately before each Software Manager or command-line operation, the
privileged `tdvp-opkg` wrapper imports only the embedded release public key
into `/etc/opkg/gpg` and checks this fingerprint:

```text
2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500
```

This is intentionally a lazy operation, not a boot service: an Internet feed
must never block storage, login, or the desktop. Index signatures are
mandatory. From the Software Manager terminal, run:

```sh
sudo tdvp-opkg update
sudo tdvp-opkg list
sudo tdvp-opkg install <package>
```

Do not disable signature checking when an update fails. The feed publisher must
first publish ABI-matched packages, `Packages`/`Packages.gz`, and the detached
signature made by the corresponding private release key. This firmware and
repository deliberately contain only the public key; no signing secret belongs
on a device or in this source tree. Adding software means publishing a new
feed revision and then updating a future image's feed URL; it never means
overwriting an already signed index. Generic libraries retain their upstream
package names (for example `sdl2`, `sdl2-ttf`, and `libmgba`); ABI safety comes
from the exact platform dependency and `riscv64` architecture, not from an
artificial library-name prefix.

## Build and release

Use an ext4 workspace under WSL/Linux for the SDK build, then collect the
result into this Windows repository's `output` directory before delivery. Do
not use a WSL-only artifact as the handoff image.

```sh
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/collect-release-bundle.sh "$HOME/work/tdvp-k230-labwc" <release-name>
```

The build uses a pinned SDK, Linux commit, deterministic ext4/GPT identities,
patch-only source assertions, and a post-image SD-card contract. The final
guard verifies U-Boot's PARTUUID root argument, desktop/runtime executables,
NetworkManager/editor, GIO TLS module, signed-feed trust material, Fn layout,
panel plugins, event-sound assets, external-I²S route contract and acceptance
tools, root-expansion service, and the absence of the retired `/data`
provisioner.

The **release collector** performs one additional online gate before it is
allowed to write a bundle under `output/`: it downloads the configured
`Packages.gz`, `Packages.gz.asc`, and `release.json` over HTTPS, verifies the
signature with the exact public key embedded in the image, and rejects any
published package that does not depend on this exact firmware ABI. A local
image build is therefore not presented as a release while the feed is absent,
unsigned, or published for a different ABI.

After a build, verify the image manifest and `SHA256SUMS` before flashing. The
collector deliberately emits only `<release-name>.img.gz` under Windows
`output/`; it does not retain a multi-gigabyte raw `.img` beside it. Use an
image writer that accepts gzip input, or stream-decompress directly to the
whole SD-card device. Do not first materialize and retain a raw image in the
WSL build workspace or in `output/`.

## Recovery and field checks

Serial recovery is `ttyS0`, `115200 8N1`. Development images have the
documented root recovery account `root` / `tdvp`; change credentials before
exposing a device outside a trusted development network.

Useful checks after login:

```sh
systemctl --no-pager status NetworkManager tdvp-rootfs-expand greetd
nmcli device status
sudo tdvp-opkg update
amixer cget name='External I2S Output Switch'
sudo tdvp-speaker-acceptance status
```

The root expansion log is `/var/lib/tdvp/rootfs-expand.status`. A card with an
existing third partition is intentionally not repartitioned by the new image.

See `docs/` for the release contract and validation procedures.
