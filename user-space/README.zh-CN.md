# 镜像拥有的用户态代码

这个目录包含由镜像构建直接拥有的源代码：

| 源码 | 职责 |
| --- | --- |
| `tdvp-greeter` | greetd 登录配置、VGLite 登录页与所选账户认证。 |
| `tdvp-labwc-desktop` | 认证后的 VGLite/Labwc 会话、XDG 配置、PCManFM、wf-panel-pi 与输入处理。会话内 swayidle 调用 PAM 认证的 gtklock，随后由 wlopm 息屏。 |
| `vicliu-pocket-linux-hardware` | 板级服务、硬件状态与控制、CPU1 客户端和 nRF52840 AT 主机工具。 |

旧 `tdvp-camera-isp` 与 `tdvp-kpu-acceptance` 源码保留供参考，当前 profile
停用其 Linux 直连摄像头/KPU 路径。摄像头与 AI 由 CPU1 托管，见
[架构](../docs/architecture.zh-CN.md)和 [AI 作业](../docs/cpu1-ai-jobs.zh-CN.md)。

应用程序采用普通的 Wayland 与 XDG 约定。桌面通过标准 `.desktop` entry 发现它们；
应用代码无需链接本目录中的会话实现。

对应的 Buildroot package recipe 位于
`buildroot/k230-sdk-overlay/package/`，并在 SDK 工作目录准备阶段将这些源码复制进去。
