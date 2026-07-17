# T-Display K230 Boot Artifact 决策

本文定义当前 boot-artifact 决策规则。

英文版本：

- [boot-artifact-decision.md](boot-artifact-decision.md)

## 决策

Boot artifacts 必须跟随所选 `k230_linux_sdk` Linux boot model，除非 TDVP 明确记录
T-Display delta。

可接受 artifact classes：

- 从 pinned SDK 或 Buildroot inputs source-built；
- 带 checksum 和 provenance 的 pinned binary；
- 由 tracked TDVP sources 生成的 environment/image files。

## 必需 Artifacts

第一版 boot baseline 必须识别：

| Artifact | Required information |
| --- | --- |
| SPL | source 或 binary provenance |
| U-Boot | source、board name、version 或 binary provenance |
| U-Boot environment | source file 或 pinned binary provenance |
| OpenSBI | source/version 或 payload provenance |
| Linux kernel | source repo、commit、defconfig |
| DTB | source DTS 和 selected board delta |
| rootfs | Buildroot defconfig 和 package set |

## 拒绝输入

拒绝任何满足以下条件的 artifact：

- 没有 checksum 或 source revision；
- 只属于 vendor demo application model；
- 要求 applications 理解 bootloader internals；
- 与所选 K230 Linux SDK boot path 冲突；
- 无法 regenerate 或 audit。

## 验收

Boot artifact set 满足以下条件才算接受：

- 每个 external input 已固定；
- image generation 可复现；
- U-Boot/OpenSBI hand off 到 K230 big-core Linux；
- Linux 进入 Buildroot rootfs；
- boot path 已文档化，且不向 applications 暴露 boot details。

