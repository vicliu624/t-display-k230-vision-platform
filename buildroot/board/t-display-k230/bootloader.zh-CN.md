# Bootloader 集成

本文定义 TDVP 如何接受 T-Display K230 Linux 平台所需的 bootloader artifacts。

英文版本：

- [bootloader.md](bootloader.md)

## 策略

首选 bootloader 参考是所选 `kendryte/k230_linux_sdk` baseline。

TDVP 可以：

1. 从 pinned SDK/Buildroot sources 构建 SPL/U-Boot/OpenSBI；或
2. 导入带 checksum 和 provenance 的 pinned binary artifacts。

两条路径都必须显式。Opaque local binaries 不被接受。

## 边界

Bootloader artifacts 可以定义：

- low-level K230 boot requirements；
- U-Boot board name and version；
- OpenSBI handoff shape；
- required image offsets or partitions。

它们不得定义：

- TDVP rootfs content；
- application model；
- BSP/runtime public API；
- SDK examples；
- target-side package workflow。

## 必需记录

每个 bootloader artifact 都要记录：

| Field | Required |
| --- | --- |
| artifact name | yes |
| source repository or package | yes |
| source commit or checksum | yes |
| build command or import command | yes |
| target image location | yes |
| acceptance status | yes |

## SDK Artifact Flow

第一条被接受的 boot artifact flow 必须在 WSL ext4 或其他原生 Linux 文件系统中执行。
不要从 Windows 挂载目录运行 SDK 或 Buildroot。

```sh
cd <k230_linux_sdk>
git checkout 5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7
make CONF=k230_canmv_v3_defconfig
```

SDK post-image flow 会生成：

```text
output/k230_canmv_v3_defconfig/images/uboot/fn_u-boot-spl.bin
output/k230_canmv_v3_defconfig/images/uboot/fn_ug_u-boot.bin
output/k230_canmv_v3_defconfig/images/boot/fw_jump_add_uboot_head.bin
```

导入成 TDVP canonical names：

```sh
cd <tdvp-repo>
buildroot/board/t-display-k230/tools/import-boot-artifacts.sh \
  <k230_linux_sdk>/output/k230_canmv_v3_defconfig/images
```

正常 TDVP bring-up 建议使用 wrapper helper。它会执行最小 SDK `opensbi`/`uboot`
build，调用 SDK firmware-header 步骤，然后再调用 importer：

```sh
cd <tdvp-repo>
buildroot/board/t-display-k230/tools/build-sdk-boot-artifacts.sh \
  <k230_linux_sdk>
```

Importer 会写出：

```text
buildroot/board/t-display-k230/boot-artifacts/spl.bin
buildroot/board/t-display-k230/boot-artifacts/u-boot.bin
buildroot/board/t-display-k230/boot-artifacts/fw_jump_add_uboot_head.bin
buildroot/board/t-display-k230/boot-artifacts/manifest.local
```

`manifest.local` 故意保持 ignored。只有导入的 image 通过板上启动验证后，才把其中的
checksums 抄入 artifact inventory。

## 验收

Bootloader integration 满足以下条件才算接受：

- provenance 已固定；
- artifact 可以可复现 rebuild 或 re-import；
- image 启动到 K230 big-core Linux；
- boot details 保持在 platform API boundary 之下。
