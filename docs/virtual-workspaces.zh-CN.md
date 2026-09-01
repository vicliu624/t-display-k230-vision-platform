# T-Display K230 Wayland 虚拟工作区

## 范围与所有权

本镜像的虚拟工作区由 Labwc 合成器管理。它不是独立 daemon、X11
EWMH 兼容层，也不会通过伪造键盘或鼠标事件来切换窗口。

```text
Goodix GT9895 touch
        ↓ libinput
Labwc / wlroots compositor
        ├─ edge-swipe recognition
        ├─ workspace membership and focus
        ├─ xdg-toplevel visibility
        └─ workspace OSD / ext-workspace-v1 state
        ↓
ordinary Wayland clients
```

Labwc 已经是工作区的唯一权威：每个普通顶层窗口属于一个 workspace，
合成器只绘制当前 workspace 的 scene tree，并在切换时更新焦点。应用不
感知“当前桌面”，也没有获得直接改写该状态的权限。

## 产品行为

`/etc/xdg/labwc/rc.xml` 固定定义四个工作区：

```text
Workspace 1 ←→ Workspace 2 ←→ Workspace 3 ←→ Workspace 4
```

- 新打开的窗口属于当前工作区。
- 新窗口、对话框、popup 和焦点关系继续由 Labwc 管理。
- 工作区不会从第一个循环到最后一个，反之亦然。
- Labwc 的 550 ms workspace OSD 显示当前工作区；该 OSD 由 compositor
  生成而非独立的 Wayland client。
- PCManFM 桌面背景、wf-panel-pi、TDVP Quick Settings、屏幕键盘和其他
  layer-shell 系统界面保持全局可见。它们不是可被移动到工作区的普通
  xdg_toplevel 窗口。

物理键盘提供以下等价操作：

| 操作 | 按键 |
| --- | --- |
| 上一个 / 下一个工作区 | Super+Left / Super+Right |
| 移动当前窗口但留在当前工作区 | Super+Shift+Left / Super+Shift+Right |
| 直接跳到 1–4 | Super+1 … Super+4 |

## 触摸边缘滑动

`0003-tdvp-workspace-edge-swipe.patch` 在 Labwc 的触摸输入层实现工作区
切换。仅由当前 TDVP 配置中 'mouseEmulation=yes' 的触摸设备触发：

```text
左侧 72 logical px 内起手并向右滑 → 前一个工作区
右侧 72 logical px 内起手并向左滑 → 后一个工作区
```

触发条件使用经过 Libinput 校准和 wlroots 输出变换后的 layout 坐标：

| 参数 | 值 |
| --- | ---: |
| 边缘保留区 | 72 px |
| 完整滑动距离 | 当前输出宽度的 12% |
| 快速甩动最小距离 | 当前输出宽度的 5% |
| 快速甩动速度 | 600 px/s |
| 竖直容差 | 96 px |

一个从边缘开始的候选触摸会在 `touch_down` 时被 compositor 保留，因而
不会产生 `wl_touch` 或模拟的 `wl_pointer` 按下事件。这个顺序很重要：
如果先把按下发送给客户端，再在移动后决定它是系统手势，Wayland 没有
一种正确的方式可以撤回已交付的点击或拖动。

以下情况会消耗但取消候选手势，不切换工作区：

- 第二根手指落下；
- 竖直位移超过容差；
- 滑动方向错误；
- 距离和速度均未达到阈值；
- 已经在首个或末个工作区；
- 会话锁屏期间。

非边缘起手的触摸完全走既有路径，包括 GTK/浏览器滚动、滑块和窗口拖动、
触摸模拟指针，以及 650 ms 的长按右键。

## 合成和性能

当前镜像使用 wlroots Pixman 软件渲染器。首版在手势释放后执行原生
workspace 切换并显示 Labwc OSD；它不承诺 GNOME 风格的每帧跟手桌面
位移动画。

实时动画需要同一帧同时渲染当前与相邻 workspace 的 scene tree。只有在
真机对连续滑动的帧时间、CPU 使用率、输入延迟和 Quick Settings/全屏
应用并存情况完成测量后，才能作为后续的 compositor patch 启用。虚拟
工作区本身与将来的 VGLite renderer 演进解耦。

## 维护与验收

相关文件：

```text
buildroot/k230-sdk-overlay/package/labwc/
  0001-tdvp-touch-long-press-emulates-right-click.patch
  0002-tdvp-advertise-pointer-for-mouse-emulated-touch.patch
  0003-tdvp-workspace-edge-swipe.patch

user-space/tdvp-labwc-desktop/src/rc.xml
buildroot/k230-sdk-overlay/board/tdvp/verify-sdcard-image.sh
```

构建镜像时，Buildroot 依次应用上述 Labwc patch。基线检查同时确认第三个
触摸补丁已经进入 staged overlay；镜像验证器则确认四工作区配置、非循环键盘
动作，以及已编译 Labwc 二进制内的边缘滑动切换路径均已进入 rootfs。

真机验收至少覆盖：

1. Super+Left/Right、Super+1…4 和两侧边缘滑动均能切换工作区。
2. Super+Shift+Left/Right 能将窗口移动到相邻工作区且不跟随过去。
3. 在工作区边界滑动不会循环，也不会产生应用点击。
4. 非边缘区域的拖动、滚动、长按右键和多指触摸不回归。
5. 背景、顶部 panel、Quick Settings 和锁屏行为正确。
6. 连续滑动、多次切换和全屏应用下没有 compositor 崩溃或焦点泄漏。
