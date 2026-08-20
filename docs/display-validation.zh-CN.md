# 显示验证

镜像通过 K230 DRM/KMS 路径验证内置 RM69A10 面板。已验收的 Wayland 桌面使用
`DSI-1` 连接器、输出旋转 `90`、逻辑尺寸 `1232x568` 和 pixman 渲染器。

## 镜像检查

镜像断言会检查下列已安装内容：

- 通过 `tdvp-labwc-desktop.service` 建立的 `/dev/dri/card0` 会话契约。
- `seatd`、`labwc`、`swaybg`、`sfwbar`、`foot` 和 `wlr-randr`。
- 包含 K230 输出参数的 `/etc/tdvp/labwc/environment`。
- 启动 Swaybg 与 SFWBar 的 `/etc/xdg/labwc/autostart`。
- GT9895 的 libinput 校准规则。

构建完成后在主机执行：

```sh
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

## 设备检查

在设备上执行：

```sh
systemctl status seatd tdvp-labwc-desktop
cat /sys/class/drm/card0-DSI-1/status
tdvp-display-smoke --device /dev/dri/card0 --seconds 5
```

Labwc 把桌面送到面板上。SFWBar 提供应​​用菜单与任务栏，Foot 提供终端恢复路径。
