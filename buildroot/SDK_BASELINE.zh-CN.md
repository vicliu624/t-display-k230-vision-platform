# K230 SDK 基线

镜像使用的 K230 Linux SDK revision 与外部 RISC-V 工具链记录在
[sdk-sources.lock](sdk-sources.lock)。`prepare-k230-sdk-worktree.sh` 会在新的
ext4 工作目录中准备 overlay、应用受跟踪的补丁队列、注册本地 package，并写入
`.tdvp/sdk-baseline-manifest`。

当前配置为：

```text
k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

profile 提供 CPU0 Linux/systemd、配对的 CPU1 RT-Smart/OpenSBI、RM69A10 设备树，
以及 seatd、greetd/gtkgreet、Labwc/VGLite、PCManFM、wf-panel-pi、Foot、
NetworkManager 和 gtklock。登录页与桌面均使用 VGLite；背景由 PCManFM 提供。
CPU1 托管摄像头与 AI，Linux 通过跨核设备访问；旧 Linux camera/KPU package、
Swaybg 和浏览器均未纳入当前基础镜像。所有构建产物位于：

```text
$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/
```

stage manifest 会随着 release bundle 一起输出，记录该镜像实际使用的输入。
