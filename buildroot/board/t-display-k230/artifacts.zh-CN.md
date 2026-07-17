# T-Display K230 Artifact Inventory

本文定义当前 artifact inventory 策略。

英文版本：

- [artifacts.md](artifacts.md)

## 目的

TDVP 必须准确知道哪些外部 artifacts 进入 firmware image。

每个非生成 artifact 都必须有：

- source repository 或 package；
- exact revision 或 checksum；
- owner；
- role；
- acceptance status。

## 主要 Artifact 来源

主要 Linux artifact 来源是：

```text
kendryte/k230_linux_sdk
```

第一项 inventory 工作是固定并记录：

- SDK commit；
- external kernel commit；
- OpenSBI version/source；
- U-Boot version/source；
- selected DTS names；
- genimage layout；
- boot environment source；
- required firmware/model assets，如果存在。

## 次要硬件参考

LilyGO T-Display K230 reference 可以提供板级硬件事实和 bootloader 线索。它不能定义
Linux application model 或 TDVP public API。

## Artifact Classes

| Class | Meaning |
| --- | --- |
| source-built | 从 pinned source 在平台构建中生成 |
| pinned-binary | 作为 binary 导入，带 checksum 和 provenance |
| generated | 由 Buildroot 从 tracked inputs 生成 |
| validation-only | bring-up 使用，不是 base image 必需 |
| rejected | 不允许作为平台输入 |

## 必需 Inventory 表

每个 accepted artifact 必须按这个形态记录：

| Artifact | Class | Source | Revision/checksum | Role |
| --- | --- | --- | --- | --- |
| Buildroot | source-built | official Buildroot submodule | `2025.02.14`, `898251ee2b83a9cd5ae0ae5db57828035a5a6f85` | build engine |
| K230 Linux SDK | source reference | `kendryte/k230_linux_sdk` | `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7` | Linux baseline |
| Kernel | source-built | `https://github.com/ruyisdk/linux-xuantie-kernel.git` | `7d4e1f444f461dbe3833bd99a4640e7b6c2cd529` | K230 Linux 6.6.36 |
| OpenSBI | SDK-derived artifact | SDK OpenSBI 1.4 flow | generated artifact 的 source/provenance 待记录 | supervisor firmware |
| U-Boot | SDK-derived artifact | SDK U-Boot 2022.10 flow | generated artifact 的 source/provenance 待记录 | bootloader |
| DTB | generated | SDK `canaan/k230-canmv-v3-lcd`，后续叠加 TDVP delta | Git-tracked selection | board description |
| Rootfs | generated | TDVP Buildroot profile | Git-tracked | BusyBox userspace |

权威外部输入 lock 是 [sources.lock](sources.lock)。

本地 bootloader binaries 通过
[tools/import-boot-artifacts.sh](tools/import-boot-artifacts.sh) 导入。生成的
`boot-artifacts/manifest.local` 会继续 ignored，直到对应 image 通过板上验证后，才把它
接受进本 inventory。

## 规则

- 不接受没有 checksum 和 provenance 的 opaque binaries。
- 不把完整 vendor demo partitions 作为 platform artifacts 导入。
- 不把 validation-only assets 当作 base-image requirements。
- 不让 artifact names 泄漏到 application APIs。
