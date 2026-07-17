# T-Display K230 Boot Artifacts

本目录是 `post-image.sh` 默认读取本地 boot artifacts 的位置。

平台仓库不提交 bootloader binaries。只有在需要生成硬件启动镜像时，才把本地 artifacts
放到这里。

本地 bootloader binaries 必须使用的 canonical filenames：

| File | Meaning | Reference source name |
| --- | --- | --- |
| `spl.bin` | K230 first-stage loader | `uboot/fn_u-boot-spl.bin` |
| `u-boot.bin` | U-Boot image | `uboot/fn_ug_u-boot.bin` |
| `fw_jump_add_uboot_head.bin` | SDK U-Boot `blinux` flow 使用的 wrapped OpenSBI jump image | `boot/fw_jump_add_uboot_head.bin` |

这些文件的 accepted first source 是本 board profile 选择的 K230 Linux 参考路径。
在当前 bring-up 中，这意味着 bootloader artifacts 必须来自 `kendryte/k230_linux_sdk`
baseline，或来自明确记录过的该源码本地镜像：

```text
<k230_linux_sdk-output>/images/uboot/
```

固定的 SDK 生成 images directory 后，使用 importer 导入：

```sh
buildroot/board/t-display-k230/tools/import-boot-artifacts.sh \
  <k230_linux_sdk>/output/k230_canmv_v3_defconfig/images
```

Importer 会在 binaries 旁边写出 `manifest.local`。这个 manifest 是本地验证产物，
继续保持 git ignore。

完整构建流程、host requirements、accepted source commit 和 observed hashes 见
[../bootloader.zh-CN.md](../bootloader.zh-CN.md)。

U-Boot environment block 不是本地 binary input。它由 Buildroot 根据以下平台源文件生成：

```text
buildroot/board/t-display-k230/uboot-linux.env
```

Buildroot 生成的文件是：

```text
output/t_display_k230_vision/images/uboot-env.bin
```

`post-image.sh` 只会在 Buildroot images directory 内部把这个生成文件 staged 成
`boot-artifacts/u-boot.env.bin`，再交给 `genimage` 使用。

不要把非 Linux reference payload 直接当作 Linux payload artifact 使用。
LilyGO/CanMV RT flow 中的 artifacts 只能作为硬件事实和启动机制证据，不能作为已接受的
Linux 平台组件。

当前平台镜像跟随所选 K230 Linux SDK 的 `blinux` 形态：一个 ext4 boot partition，
其中包含 `fw_jump_add_uboot_head.bin`、`Image` 和 `k.dtb`。

如果未来 bring-up 决定走 hidden-payload path，必须把它作为新的 boot policy，并基于当前
K230 Linux 参考源码重新记录。不要因为某条非 Linux reference flow 中出现过 hidden slot、
image name 或 block offset，就直接把它提升为本平台策略。

也可以设置 `TDVP_BOOT_ARTIFACT_DIR`，让 `post-image.sh` 从另一个具有相同 canonical
bootloader binary filenames 的目录读取 artifacts。
