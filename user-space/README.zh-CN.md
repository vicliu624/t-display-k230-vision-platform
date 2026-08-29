# 镜像拥有的用户态代码

这个目录包含由镜像构建直接拥有的源代码：

| 源码 | 职责 |
| --- | --- |
| `tdvp-labwc-desktop` | Labwc 桌面会话的 systemd 服务与 XDG 配置。Labwc 获取 DRM backend 后启动 PCManFM desktop mode 与 TDVP 主题的 wf-panel-pi 单顶栏；已启用鼠标模拟的触摸设备保留短按/拖动左键，并将静止长按转换为右键。 |
| `tdvp-kpu-acceptance` | KPU runtime 检查与验收工具。 |
| `vicliu-pocket-linux-hardware` | 板级集成服务与硬件状态发布工具。 |

应用程序采用普通的 Wayland 与 XDG 约定。桌面通过标准 `.desktop` entry 发现它们；
应用代码无需链接本目录中的会话实现。

对应的 Buildroot package recipe 位于
`buildroot/k230-sdk-overlay/package/`，并在 SDK 工作目录准备阶段将这些源码复制进去。
