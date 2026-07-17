# T-Display K230 启动策略

本文定义 T-Display K230 Linux 平台的启动策略。它把当前 Linux baseline 事实、
硬件参考事实和历史启动实验分开。

英文版本：

- [boot.md](boot.md)

## 来源顺序

启动决策必须按以下顺序推导：

1. `kendryte/k230_linux_sdk`：主要 Linux boot、OpenSBI、U-Boot、environment
   和 image-layout 参考。
2. 来自 LilyGO 参考树和原理图的 T-Display K230 硬件事实。
3. TDVP 为保持最小系统和稳定 API 所做的平台策略。
4. 历史 probe 归档，且只有在基于当前 Linux baseline 重新推导后才可使用。

LilyGO/CanMV RT flow 可以提供硬件线索，但不定义 Linux 平台启动契约。

## 接受的 Baseline 输入

第一版 Linux boot profile 必须记录：

| 项 | 必需记录 |
| --- | --- |
| K230 Linux SDK revision | exact repository commit |
| Buildroot profile | 所选 SDK defconfig 或 board profile |
| kernel source | repository URL 和 revision |
| kernel defconfig | exact defconfig name |
| device tree | source DTS 和 TDVP board delta |
| OpenSBI source | source、version 和 packaging mode |
| U-Boot source | source、version、board name 和 environment policy |
| image layout | partition map、offset、filesystem type |

没有固定 provenance 的 boot artifact 不被接受。

## 拒绝作为平台策略

没有新的显式决策时，不得把以下内容提升为 TDVP boot policy：

- 把非 Linux reference payload 当成 Linux payload；
- 继承非 Linux flow 的 hidden payload slot；
- 把 CanMV application partition model 当成 TDVP application model；
- 把 U-Boot DTS files 当成 Linux kernel DTS files；
- 把本地手写拼装的 OpenSBI/Linux chain 当成第一版平台 baseline；
- 使用绕过所选 K230 Linux boot model 的 direct boot command 作为平台契约。

这些内容可以作为调试证据，但不是架构。

## 目标启动链

目标 Linux boot chain 是：

```text
BootROM
  -> first-stage loader / SPL
  -> U-Boot
  -> OpenSBI if required by the selected K230 Linux boot flow
  -> Linux kernel
  -> BusyBox init
  -> TDVP platform init
  -> vision-runtime service or selected SDK demo
```

SPL、U-Boot、OpenSBI、kernel、DTB 和 rootfs 的具体打包方式，由所选 K230 Linux
baseline 加上已记录的 T-Display delta 定义。

## U-Boot Environment 策略

只有在理解所选 K230 Linux boot model 之后，TDVP 才可以拥有生成式 U-Boot
environment。

规则：

- Environment source 必须在本仓库中被跟踪。
- Binary environment 必须由 Buildroot 或已记录的构建步骤生成。
- Kernel 和 DTB load address 必须继承自所选 K230 Linux SDK profile，或相对它给出理由。
- 手动串口命令只是调试工具，不是平台启动契约。
- 只有所选 U-Boot path 支持时，`extlinux.conf` 才可以作为说明或 fallback 生成。

## Buildroot 职责

Buildroot 负责从 tracked inputs 生成确定性的可启动镜像：

- board profile 选择的 bootloader artifacts；
- kernel image；
- device tree blobs；
- generated U-Boot environment 或 boot scripts；
- minimal root filesystem；
- platform init scripts；
- BSP/runtime packages。

Buildroot 不得变成：

- target-side package installation workflow；
- desktop startup system；
- per-application image generator；
- 把 raw Linux hardware node 变成 public application API 的地方。

## 早期启动验收标准

早期 Linux image 只有全部满足以下条件才算接受：

1. 板子能从生成的 SD image 启动。
2. UART 能看到 Linux kernel logs。
3. UART 能进入 BusyBox userspace。
4. `/proc/cpuinfo` 和 kernel logs 能识别预期 K230/RISC-V target。
5. Rootfs 从生成镜像挂载，不需要手动 target-side 修改。
6. Reboot 后行为一致。
7. Boot details 保持在 BSP/runtime/SDK API boundary 之下。

## 硬件验证项

这些项目必须在真实 T-Display K230 硬件上证明：

- U-Boot 和 Linux 使用的 SD-card controller 编号。
- UART console 映射。
- Kernel 和 DTB load address。
- OpenSBI packaging mode。
- Linux 进入 userspace 后，display、camera、input、storage、radio、power 和 sensor
  的可用性。
