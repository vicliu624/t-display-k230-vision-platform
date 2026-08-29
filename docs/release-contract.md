# Release Contract

Each release produces one flashable T-Display K230 SD-card image and a Windows-
visible release bundle. It is the maintained baseline for low-performance,
keyboard-first Linux handhelds; it is not an experimental launcher image.

## Delivered files

```text
vicliu-pocket-linux-k230-<revision>.img
vicliu-pocket-linux-k230-<revision>.img.gz
tdvp-image-manifest
tdvp-sdk-baseline-manifest
README.txt
SHA256SUMS
```

The final bundle must be copied to this repository's Windows `output/`
directory. A WSL-only build artifact is not a release deliverable.

The image contains the K230 boot payload, kernel and RM69A10 DTB, systemd,
OpenSSH recovery, NetworkManager, seatd, greetd/gtkgreet, Labwc, PCManFM,
Raspberry Pi `wf-panel-pi` plugins, Foot, Cog/WPE WebKit,
`nm-connection-editor`, `opkg`, the signed-feed bootstrap and board packages.

## Required image invariants

- U-Boot uses the deterministic root `PARTUUID`, never a hard-coded
  `/dev/mmcblkN` number.
- GPT has only boot partition 1 and root partition 2. First boot can expand the
  root filesystem safely; no `/data` provisioner or third data partition exists.
- A greeter-selected account receives its own home and runtime directory.
- LilyGO/Menu opens the upstream application menu; Fn produces printed yellow
  symbols; blank-desktop long press produces the ordinary right-click menu.
- PCManFM provides the wallpaper/Desktop/Files behavior. The only panel is
  upstream `wf-panel-pi`, with one NetworkManager item and one output-volume
  item.
- Cog has GIO TLS support and touchable Labwc title-bar controls.
- `libcanberra` and the Freedesktop theme provide output-volume event feedback.
- NetworkManager is the exclusive network owner and launches upstream
  `nm-connection-editor`; no direct legacy wpa_supplicant configuration UI is
  shipped.
- The configured application feed is ABI-pinned and its release public key is
  checked before signature-verified `opkg` use.

## Release gates

1. Stage the pinned SDK and replay every SDK, Linux, Buildroot and package
   patch with the patch-only assertion.
2. Build the complete bootable image from the release defconfig.
3. Run the post-image SD-card verifier. It inspects partition identity,
   rootfs files, systemd links, desktop configuration, TLS, event sounds and
   signed-feed material rather than trusting a build exit status alone.
4. Run hardware-build preflight and retain the generated evidence report.
5. Boot a device and verify the greeter/session, menu key, keyboard, touch,
   Files, panel, Wi-Fi editor, HTTPS browser and volume event feedback.
6. Download the configured public feed's `Packages.gz`, `Packages.gz.asc`, and
   `release.json` over HTTPS; verify the detached signature with the embedded
   public key and require every published package to depend on the exact image
   ABI.
7. Collect the image, compressed image, manifests and checksums in `output/`.

`tdvp-image-manifest` records source revisions, build inputs, filesystem/GPT
identities and image hashes. `tdvp-sdk-baseline-manifest` records the staged SDK
inputs. `SHA256SUMS` covers every delivered file.

## Signed distribution-feed policy

The standard image configures only:

```text
https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/r2/riscv64
```

The key fingerprint is `2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500`.
The image includes its public key only. The image itself is a small
hardware/desktop seed; this ABI-matched feed is the expandable distribution
catalogue for userland libraries, tools, desktop programs, and device
applications. Publishing must produce ABI-matched packages,
`Packages`/`Packages.gz`, and their detached signature using the offline
private key. Neither devices nor this repository contain the private signing
material, and release instructions must never disable signature checks.

The bundle collector enforces this policy against the live Pages endpoint; it
does not treat an image containing a URL and public key as proof that updates
will work in the field.
