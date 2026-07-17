# T-Display K230 Boot Artifact Decision

This document defines the current boot-artifact decision rules.

Chinese version:

- [boot-artifact-decision.zh-CN.md](boot-artifact-decision.zh-CN.md)

## Decision

Boot artifacts must follow the selected `k230_linux_sdk` Linux boot model unless
TDVP records an explicit T-Display delta.

Accepted artifact classes:

- source-built from pinned SDK or Buildroot inputs;
- pinned binary with checksum and provenance;
- generated environment/image files from tracked TDVP sources.

## Required Artifacts

The first boot baseline must identify:

| Artifact | Required information |
| --- | --- |
| SPL | source or binary provenance |
| U-Boot | source, board name, version, or binary provenance |
| U-Boot environment | source file or pinned binary provenance |
| OpenSBI | source/version or payload provenance |
| Linux kernel | source repo, commit, defconfig |
| DTB | source DTS and selected board delta |
| rootfs | Buildroot defconfig and package set |

## Rejected Inputs

Reject any artifact that:

- has no checksum or source revision;
- belongs only to a vendor demo application model;
- requires applications to know bootloader internals;
- conflicts with the selected K230 Linux SDK boot path;
- cannot be regenerated or audited.

## Acceptance

A boot artifact set is accepted when:

- every external input is pinned;
- image generation is reproducible;
- U-Boot/OpenSBI hands off to K230 big-core Linux;
- Linux reaches the Buildroot rootfs;
- the boot path is documented without exposing boot details to applications.

