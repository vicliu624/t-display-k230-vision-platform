# T-Display K230 U-Boot Environment 策略

本文定义 TDVP 如何处理 U-Boot environment configuration。

英文版本：

- [uboot-env.md](uboot-env.md)

## 策略

第一版 U-Boot environment baseline 必须来自官方 K230 Linux SDK boot flow，然后适配到
T-Display K230。

主要参考：

```text
kendryte/k230_linux_sdk
buildroot-overlay/board/canaan/k230-soc/default.env
```

TDVP 可以携带平台自有 environment source，但它必须表达所选 Linux SDK path 和已记录的
T-Display delta。

## 当前参考形态

官方 Linux SDK environment 使用 Linux boot path：从 boot partition 加载 OpenSBI、kernel
和 DTB，然后 hand off 到 Linux。

TDVP 必须提取并固定：

- boot device；
- boot partition；
- kernel filename；
- DTB filename；
- OpenSBI payload filename 或 build mode；
- load addresses；
- console arguments；
- rootfs arguments。

当前 TDVP environment 状态：

```text
bootcmd=run blinux;
mmc_boot_dev_num=1
blinux=ext4load mmc ${mmc_boot_dev_num}:1 0x3000000 /fw_jump_add_uboot_head.bin && ext4load mmc ${mmc_boot_dev_num}:1 0x200000 /${k} && ext4load mmc ${mmc_boot_dev_num}:1 0x2200000 /k.dtb && bootm 0x3000000 - 0x2200000;
```

这已经跟随 pinned SDK baseline。Console mapping 仍然是 T-Display 硬件验证项。

## Source File

如果 TDVP 拥有 environment source，它位于：

```text
buildroot/board/t-display-k230/uboot-linux.env
```

不要通过修改 `output/` 下的生成文件改变 boot behavior。

## 规则

- 不向 applications 暴露 U-Boot commands。
- 正常启动不依赖 interactive boot commands。
- 不在未理解 rootfs、DTB 和 board differences 前照抄 vendor environment。
- SDK baseline 改变后，不保留 stale boot commands。

## 验收

U-Boot environment 满足以下条件才算接受：

- 匹配 pinned SDK boot model 或 documented TDVP delta；
- U-Boot 加载所选 OpenSBI/kernel/DTB inputs；
- Linux 收到预期 command line；
- Linux 挂载 Buildroot rootfs；
- environment source 已跟踪，或 binary provenance 已固定。
