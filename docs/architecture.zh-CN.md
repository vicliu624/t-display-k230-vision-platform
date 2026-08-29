# 系统架构

## 运行层级

```text
T-Display K230 V1.3 硬件
  |
K230 Linux 内核、板级 DTB 与 vendor firmware runtime
  |
DRM/KMS、DSI、libinput、I2C、SDIO、USB、ALSA、camera/ISP、KPU runtime
  |
systemd、udev、NetworkManager、OpenSSH、seatd、D-Bus
  |
greetd / gtkgreet 认证用户实际选择的 Linux 账户
  |
该账户的 Labwc Wayland 会话
  |-- PCManFM desktop：壁纸、图标与桌面空白处右键菜单
  |-- Raspberry Pi wf-panel-pi：菜单、网络、音量、电池、时钟
  `-- Foot、Cog/WPE WebKit、PCManFM、nm-connection-editor
```

这是面向低性能全键盘掌机的小型标准 Wayland 桌面；不包含 GNOME Shell、自造
Launcher、自造 Wi-Fi 对话框或私有声音播放器。

## 登录会话契约

`greetd` 为 Greeter 中实际认证的账户启动 `tdvp-labwc`。启动包装器从该账户派生
`HOME`、`USER`、`XDG_CONFIG_HOME`、`XDG_CACHE_HOME`、`XDG_DATA_HOME` 和
`XDG_RUNTIME_DIR`，不会硬编码用户或 `/home/tdvp`。

`/etc/tdvp/labwc/environment` 固定 K230 DRM、pixman、seatd 与 1232×568 逻辑桌面。
Labwc 随后启动 PCManFM、该用户的 PulseAudio、上游面板与 LilyGO 键桥。键桥把实体
Menu 键交给 `wfpanelctl smenu menu`；Fn 是 XKB Mod5 层，并不是用户态按键模拟。

触摸规则保留正常短按/拖动左键。桌面 PCManFM 空白处的静止长按会被交付为右键，从而使用
它本来的上下文菜单；GTK 客户端使用该触摸栈所需的 committed-text 兼容修复。

## 网络、浏览器与声音

NetworkManager 独占有线和 Wi-Fi；它需要时才通过 D-Bus 启动内置 `wpa_supplicant`，不会
启用独立 `wpa_supplicant@wlan0` 或 `systemd-networkd`。`wfplug-netman` 与上游
`nm-connection-editor` 都只调用其公开 D-Bus API。

Cog 是原生 Wayland 浏览器，以顶部栏下方非最大化窗口启动。Labwc 对 Cog 的规则强制
server decoration，使最小化、最大化、关闭控制能在触摸屏上使用。HTTPS 由
`glib-networking` 与 GIO OpenSSL 模块提供。

面板只有一个输出音量插件。它是 Raspberry Pi 上游实现，使用 `libcanberra` 与
Freedesktop `audio-volume-change` 事件，不使用 TDVP 私有守护进程或声音资源格式。

## 存储与现场更新

镜像只有 GPT boot 分区 1 与 ext4 root 分区 2。大容量卡首次启动时，一次性服务移动 GPT
备用头、扩展分区 2 并严格保留 PARTUUID，自动重启后扩展 ext4；已有后续用户分区的卡会被
原样保留。U-Boot 按 PARTUUID 挂载根分区，不依赖会随板子变化的 `/dev/mmcblkN`；镜像绝不
创建 `/data`。

`opkg` 使用 ABI 固定的软件源。启动服务只导入并校验内置发行公钥指纹，签名校验始终开启；
这样可以在不把私钥写入设备的前提下，为低性能设备提供安全的现场更新路径。
