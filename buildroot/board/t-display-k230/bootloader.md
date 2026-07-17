# Bootloader Integration

This document defines how TDVP accepts bootloader artifacts for the
T-Display K230 Linux platform.

Chinese version:

- [bootloader.zh-CN.md](bootloader.zh-CN.md)

## Policy

The preferred bootloader reference is the selected `kendryte/k230_linux_sdk`
baseline.

TDVP may either:

1. build SPL/U-Boot/OpenSBI from pinned SDK/Buildroot sources; or
2. import pinned binary artifacts with checksum and provenance.

Both paths must be explicit. Opaque local binaries are not accepted.

## Boundary

Bootloader artifacts may define:

- low-level K230 boot requirements;
- U-Boot board name and version;
- OpenSBI handoff shape;
- required image offsets or partitions.

They must not define:

- TDVP rootfs content;
- application model;
- BSP/runtime public API;
- SDK examples;
- target-side package workflow.

## Required Record

For each bootloader artifact, record:

| Field | Required |
| --- | --- |
| artifact name | yes |
| source repository or package | yes |
| source commit or checksum | yes |
| build command or import command | yes |
| target image location | yes |
| acceptance status | yes |

## SDK Artifact Flow

Run the first accepted boot artifact flow inside WSL ext4 or another native
Linux filesystem. Do not run the SDK or Buildroot from the Windows-mounted
workspace.

```sh
cd <k230_linux_sdk>
git checkout 5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7
make CONF=k230_canmv_v3_defconfig
```

The SDK post-image flow generates:

```text
output/k230_canmv_v3_defconfig/images/uboot/fn_u-boot-spl.bin
output/k230_canmv_v3_defconfig/images/uboot/fn_ug_u-boot.bin
output/k230_canmv_v3_defconfig/images/boot/fw_jump_add_uboot_head.bin
```

Import them into TDVP canonical names:

```sh
cd <tdvp-repo>
buildroot/board/t-display-k230/tools/import-boot-artifacts.sh \
  <k230_linux_sdk>/output/k230_canmv_v3_defconfig/images
```

For the normal TDVP bring-up path, use the wrapper helper instead. It performs
the minimal SDK `opensbi`/`uboot` build, runs the SDK firmware-header step, and
then calls the importer:

```sh
cd <tdvp-repo>
buildroot/board/t-display-k230/tools/build-sdk-boot-artifacts.sh \
  <k230_linux_sdk>
```

The importer writes:

```text
buildroot/board/t-display-k230/boot-artifacts/spl.bin
buildroot/board/t-display-k230/boot-artifacts/u-boot.bin
buildroot/board/t-display-k230/boot-artifacts/fw_jump_add_uboot_head.bin
buildroot/board/t-display-k230/boot-artifacts/manifest.local
```

`manifest.local` is intentionally ignored. Copy its checksums into the artifact
inventory only after the imported image has passed board boot validation.

## Acceptance

Bootloader integration is accepted when:

- provenance is pinned;
- the artifact can be rebuilt or re-imported reproducibly;
- the image boots into K230 big-core Linux;
- boot details remain below the platform API boundary.
