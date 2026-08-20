# Release Contract

Each CI run produces one bootable K230 SD-card image from the active Labwc
desktop profile. A tagged build publishes the same artifact set as GitHub
Release assets.

## Files

```text
vicliu-pocket-linux-k230-<revision>.img
vicliu-pocket-linux-k230-<revision>.img.gz
tdvp-image-manifest
tdvp-sdk-baseline-manifest
README.txt
SHA256SUMS
```

The image contains the boot payload, K230 kernel and board device tree,
systemd, network and SSH recovery services, seatd, Labwc, Swaybg, SFWBar,
Foot, and the board integration packages.

`tdvp-image-manifest` records the SDK and Linux commits, the generated image
hashes, deterministic filesystem and GPT identities, and the desktop runtime
components. `tdvp-sdk-baseline-manifest` records the staged source inputs.
`SHA256SUMS` covers every release file.

## CI

The GitHub Actions workflow performs these stages:

1. Check out the repository and restore source/toolchain download caches.
2. Prepare a fresh SDK worktree from the pinned source lock.
3. Replay the K230 Linux patch queue and run the patch-only assertion.
4. Build the complete bootable SD image.
5. Assert the generated image and collect the release bundle.
6. Upload the bundle for pull requests, branch builds, and manual runs.
7. Publish the bundle for `v*` tags.

The Buildroot image includes `opkg` as the field package-management tool.
Package source configuration is explicit administrator policy; the image does
not install an unverified default source.
