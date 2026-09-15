# K230 SDK Overlay

此 overlay 在配置前复制到固定版本的 K230 Linux SDK 工作目录。
准备脚本还会从 `user-space/*/src` 同步本地源码，并用 manifest 校验两者一致。

## 当前板级 Profile

```text
configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

## 当前主要 Package

| Package | 职责 |
| --- | --- |
| `gtk-layer-shell`、`wf-panel-pi`、`wfplug-*` | 面板及应用菜单、网络、音量、电量和时钟 |
| `tdvp-greetd`、`tdvp-gtkgreet`、`tdvp-greeter` | 所选账户登录、VGLite 登录页 |
| `tdvp-labwc-desktop` | VGLite 桌面、输入、PCManFM、面板与会话生命周期 |
| `gtklock`、`gtk-session-lock`、`swayidle`、`wlopm` | 密码窗口、会话锁定、空闲计时与息屏 |
| `tdvp-quick-settings` | 独立触摸控制中心 |
| `tdvp-cpu1-vision` | Linux 视觉/AI 内核桥接与公共 ABI 头文件 |
| `tdvp-display-smoke` | 维护模式 DRM/KMS 验收 |
| `tdvp-vglite-acceptance`、`tdvp-wayland-acceptance` | VGLite 与 Wayland 验收工具 |
| `tdvp-keyboard-layout`、`vicliu-pocket-linux-hardware` | 键盘配置、板级控制、状态发布和 nRF AT 主机工具 |
| `nm-connection-editor` | NetworkManager 连接编辑 |
| `tdvp-opkg-trust` | 签名公钥与按需信任初始化 |

CPU1 RT-Smart 固件由板级构建流程配对生成，并写入整卡 raw 区域。
旧 `tdvp-camera-isp`、`tdvp-camera-isp-runtime` 和 `tdvp-kpu-acceptance`
recipe 保留在源码中，当前 profile 不选择它们。基础桌面无 Camera demo、Swaybg 或浏览器。

`board/tdvp/` 包含 rootfs hook、Linux fragment、镜像布局与校验脚本；
`linux/` 包含受控内核补丁队列。package 注册由
`buildroot/tools/register-k230-sdk-tdvp-packages.sh` 在 staging 时执行。
软件源的实际验收边界见 [软件源状态](../../docs/package-feed-status.zh-CN.md)。
