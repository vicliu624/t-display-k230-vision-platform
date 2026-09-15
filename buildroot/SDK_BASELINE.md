# K230 SDK Baseline

The image uses the K230 Linux SDK revision and external RISC-V toolchain
recorded in [sdk-sources.lock](sdk-sources.lock). `prepare-k230-sdk-worktree.sh`
copies the active overlay into a fresh ext4 worktree, applies the tracked patch
queues, registers local packages, and writes `.tdvp/sdk-baseline-manifest`.

The active configuration is:

```text
k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

The profile supplies CPU0 Linux/systemd, paired CPU1 RT-Smart/OpenSBI, the
RM69A10 device tree, seatd, greetd/gtkgreet, Labwc/VGLite, PCManFM, wf-panel-pi,
Foot, NetworkManager and gtklock. Both greeter and desktop use VGLite;
PCManFM supplies the background. CPU1 owns camera and AI resources, accessed
through Linux cross-core devices. The old Linux camera/KPU packages, Swaybg
and browsers are excluded from the current base image. Build outputs live under:

```text
$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/
```

The stage manifest is part of each release bundle and records the inputs used
for that image.
