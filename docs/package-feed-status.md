# Package-feed status and acceptance

## Current conclusion — 2026-09-09

End-to-end feed acceptance failed. Pause package installation and upgrades on
delivery cards, including NetSurf. Complete image-side package safety, CI and
release preparation before providing a new whole-card image for validation.
The feed will then use a published image's libraries and package database.
Public feed configuration and published contents remain unchanged.

## Image configuration

post-build.sh writes the following URL to /etc/opkg/tdvp-feed.conf:

```text
https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/stable/riscv64
```

stable is mutable; release.json declares publication_type=mutable-channel.
The 2026-09-09 installation test resolved it to r6. The r1 suffix in the platform
ID belongs to the platform ABI identity; the feed revision is recorded separately.

The /usr/local/sbin/tdvp-opkg wrapper imports the release public key into
/etc/opkg/gpg on demand, checks its fingerprint and invokes opkg.
Software Manager uses this entry point inside Foot. Fingerprint:

```text
2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500
```

Index signatures, exact platform dependencies and riscv64 architecture checks
remain enabled. Boot does not access the feed.

## Incident evidence

Installing tdvp-netsurf 3.10-1 and 52 dependencies returned success and replaced
base-system libraries. A subsequent boot stopped when systemd's sd-gens hit an
illegal instruction in libmount:

| Item | Result |
| --- | --- |
| Library | libmount.so.1.1.0 |
| Library offset | 0x182f8, entry of mnt_table_parse_stream |
| Fault instruction | 0xcc747057, identical at that offset in the r6 package |
| ELF attributes | Require RVV 1.0 |
| libmount IPK SHA-256 | 20dfd1e31d89e655dd34da19e7190dcd0b0aeb115fa92cef83ddf1cfd78337e4 |

CPU0 Linux cannot execute this instruction. Sampled libblkid, GLib, GTK3,
Wayland client and libcrypto libraries also declared vector requirements.
Compatibility review must cover the installed dependency set.

Before installation, the package database contained only the platform ABI
package; some bundled libraries had no package ownership. Differences between
those libraries and feed payloads were observed before installation proceeded.
This exposed a missing installation gate. Browser startup and post-install
system acceptance were never completed.

## Existing publication checks

assert-tdvp-opkg-feed-release.sh downloads Packages.gz, Packages.asc,
Packages.gz.asc and release.json. It checks both index signatures, channel
metadata, architecture, exact ABI dependencies and required package names.

It does not yet validate every IPK's instruction set, library identity against
the actual image, complete file ownership or behavior after a device reboot.
These checks still need implementation and device evidence.

## Image-side repair progress

`post-build.sh` captures selected Buildroot packages with `show-info`.
`post-fakeroot.sh` seeds the opkg database after accounts, permissions and
service links are final. Preinstalled components use `tdvp-image-<Buildroot name>`
with a source version and final-payload digest. Ambiguous Buildroot path claims
remain owned by `tdvp-image-base` and are recorded in the inventory. Image
packages are essential and held, including merged-/usr alias ownership.

The feed must generate dependencies from the published inventory; r6 runtime
version labels do not identify these components. Applications can depend on
preinstalled providers; base-system upgrades remain image updates. Forced
administrator overrides, maintainer scripts and dependency closure need their
own safety checks.

The image guard reads the final ext4 and verifies recorded file bytes, modes,
symlinks and the opkg database. The collector exports `tdvp-image-base.json`,
`tdvp-opkg-status`, `tdvp-opkg-info.tar.gz` and `tdvp-buildroot-packages.json` in
`SHA256SUMS`. Image publication is independent of the old live feed's
signature/metadata gate. On-device signature verification remains enabled.

The [CI build for d2d8395](https://github.com/vicliu624/t-display-k230-vision-platform/actions/runs/34336774692)
failed at final rootfs verification: `debugfs rdump` clears setuid/setgid during
extraction, leaving a temporary file at `0755` when its actual ext4 inode is
`04755`. The guard now reads actual inode permissions in one read-only batch;
the extracted copy still supplies file bytes and link targets. Permission
failures report paths, expected values and actual values. Missing permission
bits and unexpectedly added privilege bits remain rejected.

Fifteen tests passed as both root and an ordinary user in an Ubuntu 24.04
container, including real opkg overwrite rejection, preinstalled dependency
resolution, the production post-fakeroot hook, special permissions and payload
tamper rejection. A copy of an older SDK's full rootfs was regenerated with
`unix_chkpwd=04755` and passed ext4 checks for 12,245 paths and 165 package
records. The earlier full-copy test had a `0755` helper and missed this case.
The [CI build for 6cd7b34 passed](https://github.com/vicliu624/t-display-k230-vision-platform/actions/runs/34351987911),
closing the inode-permission verification failure. Its image is still a
candidate; no new tagged Release or fresh-card hardware acceptance is recorded.

This revision adds [paired CPU0 SDK/sysroot export and isolated validation](cpu0-application-sdk.md).
Local Ubuntu 24.04 tests use the existing SDK and a disposable rootfs copy to
exercise export, relocation and application builds. These fixtures are not a
published baseline. CI must produce image and SDK from the same complete build
and pass their pairing checks before upload. Published baseline resolution and
end-to-end feed acceptance remain pending.

## Acceptance on the new image

### Published baseline requirement (confirmed 2026-09-09)

A public feed must target an already published, downloadable image release.
Finish image-side package safety fixes and baseline acceptance first; publish
the image, SHA-256, matching SDK/sysroot and preinstalled package/file inventory
in a tagged GitHub Release. Then lock the feed to those release assets, test
candidate packages and reboot behavior, and publish the signed feed/channel.

Local candidates remain valid development inputs. Temporary Actions artifacts,
local SDK directories and a Git commit alone do not satisfy the public-feed
baseline requirement. Release resolution and mandatory matching still require
implementation; the current public channel has not passed this new contract.

The initial image release must state that feed installation acceptance is
pending, retain signature checks and prevent packages from replacing recorded
image files. Add the accepted feed revision after package/device validation.

1. Record the image commit, manifests, hashes, library inventory and package database.
2. Check CPU0 ELF requirements and the identity/ownership of overlapping files.
3. Prepare a recoverable test card or complete backup and test a defined package set.
4. Verify application startup, desktop/VGLite, CPU1 AI, networking and login; reboot and repeat.
5. Record the accepted image/feed-revision combination before restoring installation guidance.

Keep signature verification enabled. Stop and investigate installation conflicts.

## Source references

- [Release defconfig](../buildroot/k230-sdk-overlay/configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig): CPU0 ISA configuration.
- [post-build](../buildroot/k230-sdk-overlay/board/tdvp/post-build.sh): feed URL written into the image.
- [Publication check](../buildroot/tools/assert-tdvp-opkg-feed-release.sh): implemented signature and metadata checks.
- [opkg wrapper](../buildroot/k230-sdk-overlay/package/tdvp-opkg-trust/src/tdvp-opkg): on-demand trust initialization and command forwarding.
