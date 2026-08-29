# K230 SDK Baseline

The image uses the K230 Linux SDK revision and external RISC-V toolchain
recorded in [sdk-sources.lock](sdk-sources.lock). `prepare-k230-sdk-worktree.sh`
copies the active overlay into a fresh ext4 worktree, applies the tracked patch
queues, registers local packages, and writes `.tdvp/sdk-baseline-manifest`.

The active configuration is:

```text
k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

The profile supplies a systemd root filesystem, the vendor K230 kernel and
firmware runtime, the RM69A10 board device tree, and a Wayland desktop composed
from seatd, greetd, Labwc, PCManFM, Raspberry Pi wf-panel-pi, Foot, Cog/WPE
WebKit and NetworkManager. All build outputs belong below one
stable worktree path:

```text
$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/
```

The stage manifest is part of each release bundle and records the inputs used
for that image.
