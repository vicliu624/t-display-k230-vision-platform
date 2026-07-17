# T-Display K230 Artifact Inventory

This document defines the current artifact inventory policy.

Chinese version:

- [artifacts.zh-CN.md](artifacts.zh-CN.md)

## Purpose

TDVP must know exactly which external artifacts enter the firmware image.

Every non-generated artifact must have:

- source repository or package;
- exact revision or checksum;
- owner;
- role;
- acceptance status.

## Primary Artifact Source

The primary Linux artifact source is:

```text
kendryte/k230_linux_sdk
```

The first inventory task is to pin and record:

- SDK commit;
- external kernel commit;
- OpenSBI version/source;
- U-Boot version/source;
- selected DTS names;
- genimage layout;
- boot environment source;
- required firmware/model assets, if any.

## Secondary Hardware Reference

The LilyGO T-Display K230 reference may provide board-specific hardware facts
and bootloader clues. It must not define the Linux application model or the
TDVP public API.

## Artifact Classes

| Class | Meaning |
| --- | --- |
| source-built | built from pinned source during the platform build |
| pinned-binary | imported as a binary with checksum and provenance |
| generated | produced by Buildroot from tracked inputs |
| validation-only | used during bring-up, not required by base image |
| rejected | not allowed as platform input |

## Required Inventory Table

Each accepted artifact must be recorded in this shape:

| Artifact | Class | Source | Revision/checksum | Role |
| --- | --- | --- | --- | --- |
| Buildroot | source-built | official Buildroot submodule | `2025.02.14`, `898251ee2b83a9cd5ae0ae5db57828035a5a6f85` | build engine |
| K230 Linux SDK | source reference | `kendryte/k230_linux_sdk` | `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7` | Linux baseline |
| Kernel | source-built | `https://github.com/ruyisdk/linux-xuantie-kernel.git` | `7d4e1f444f461dbe3833bd99a4640e7b6c2cd529` | K230 Linux 6.6.36 |
| OpenSBI | SDK-derived artifact | SDK OpenSBI 1.4 flow | source/provenance pending for generated artifact | supervisor firmware |
| U-Boot | SDK-derived artifact | SDK U-Boot 2022.10 flow | source/provenance pending for generated artifact | bootloader |
| DTB | generated | SDK `canaan/k230-canmv-v3-lcd`, later TDVP delta | Git-tracked selection | board description |
| Rootfs | generated | TDVP Buildroot profile | Git-tracked | BusyBox userspace |

The authoritative external input lock is [sources.lock](sources.lock).

Local bootloader binaries are imported with
[tools/import-boot-artifacts.sh](tools/import-boot-artifacts.sh). The generated
`boot-artifacts/manifest.local` is ignored until a board-validated image is
accepted into this inventory.

## Rules

- Do not accept opaque binaries without checksum and provenance.
- Do not import full vendor demo partitions as platform artifacts.
- Do not treat validation-only assets as base-image requirements.
- Do not let artifact names leak into application APIs.
