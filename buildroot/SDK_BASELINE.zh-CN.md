# K230 SDK 基线

镜像使用的 K230 Linux SDK revision 与外部 RISC-V 工具链记录在
[sdk-sources.lock](sdk-sources.lock)。`prepare-k230-sdk-worktree.sh` 会在新的
ext4 工作目录中准备 overlay、应用受跟踪的补丁队列、注册本地 package，并写入
`.tdvp/sdk-baseline-manifest`。

当前配置为：

```text
k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

profile 提供 systemd rootfs、K230 vendor 内核和 firmware runtime、RM69A10
板级设备树，以及由 seatd、Labwc、Swaybg、SFWBar 与 Foot 组成的 Wayland
桌面。所有构建产物统一位于一个固定的工作目录输出路径：

```text
$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/
```

stage manifest 会随着 release bundle 一起输出，记录该镜像实际使用的输入。
