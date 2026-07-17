# Historical Boot-Chain Debug Archive

This file is a historical archive of the early boot experiments performed
before the project baseline was corrected to `kendryte/k230_linux_sdk`.

It is not part of the active platform specification.

## Current Decision

The active Linux bring-up path is:

```text
kendryte/k230_linux_sdk
  -> closest K230/CanMV Linux profile
  -> T-Display K230 hardware delta
  -> TDVP Buildroot board profile
```

This archive must not be used to choose:

- target kernel version,
- OpenSBI baseline,
- U-Boot policy,
- hidden image slots,
- SD-card image layout,
- Linux payload format,
- application/runtime architecture.

## What The Historical Probes Proved

The early probes were still useful as evidence. They showed that:

- the board reaches U-Boot,
- U-Boot can read the visible FAT boot partition,
- U-Boot can read the ext4 rootfs partition,
- UART0 output works through the K230 boot firmware path,
- a minimal S-mode payload can be reached through the tested handoff shape.

Those facts remain useful while debugging board bring-up.

## What They Did Not Prove

The probes did not establish a clean Linux platform baseline.

They did not prove that a locally assembled OpenSBI/Linux payload chain should
become the project architecture, and they did not prove that non-Linux
reference payloads should be accepted as platform components.

## Rule For Future Use

Use this file only as a debugging evidence archive. If a fact from this archive
is needed again, re-derive it against the current `k230_linux_sdk` baseline and
record the result in the active board documents:

- `adaptation-baseline.md`
- `boot.md`
- `image.md`
- `uboot-env.md`
- `kernel-selection.md`
- `bringup-plan.md`
- `validation.md`
