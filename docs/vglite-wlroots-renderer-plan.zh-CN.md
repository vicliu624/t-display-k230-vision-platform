# K230 VGLite / wlroots 合成实现契约

## 目标和边界

本项目的目标不是把桌面程序改为直接使用 DRM，也不是把 Quick
Settings 变成一个直接控制显示器的特殊程序。目标合成链为：

```text
Wayland clients (Foot / PCManFM / Quick Settings / …)
             |
             v
        Labwc + wlroots-vglite
             |
             | VGLite render pass, synchronous finish
             v
 DRM-GEM / PRIME DMA-BUF scan-out buffer (XR24 / AR24, modifier 0)
             |
             v
       K230 DRM atomic commit -> vblank -> DSI panel
```

Labwc 必须继续由 greetd 登录的桌面用户启动，不能改为 root。Quick
Settings 继续是普通 `zwlr_layer_shell_v1` 客户端，既不打开
`/dev/vg_lite`，也不打开 `/dev/dri/card0`。唯一持有 VGLite context 的
是 compositor 进程。

当前 K230 内核已经启用了 `CONFIG_GPU_VGLITE`，但镜像没有 VGLite 用户态
库，`/dev/vg_lite` 也仍为 `root:root 0600`。Mesa 只提供 `swrast` DRI，
所以现有 `WLR_RENDERER=gles2` 并不代表硬件加速；产品当前明确使用
Pixman。不能通过改环境变量假装启用了 GPU。

## Gate 0：独立运行时证明

Gate 0 是 renderer 工作开始前的硬条件，且不会改变当前显示结果。

1. 通过现有 SDK `vg_lite` Buildroot recipe 安装 `/usr/lib/libvg_lite.so`；
   不引入另一个内核模块，当前 VGLite driver 已内建在目标内核中。
2. 安装精确 udev 规则：

   ```udev
   SUBSYSTEM=="vg_lite_class", KERNEL=="vg_lite", GROUP="render", MODE="0660"
   ```

   这只匹配 K230 实际导出的 VGLite class node，不会放宽其他字符设备。
   `tdvp` 已由 Buildroot 用户定义加入 `render` 组。
3. 以 `tdvp` 运行 `/usr/bin/tdvp-vglite-probe`。它必须依次完成：

   - 直接打开 `/dev/vg_lite`；
   - `vg_lite_init()`、版本/芯片信息读取；
   - 在 **未绑定 framebuffer** 的 DRM dumb buffer 上执行
     `DRM_IOCTL_PRIME_HANDLE_TO_FD`；
   - `vg_lite_map(..., VG_LITE_MAP_DMABUF, ...)`；
   - GPU clear、blit、path draw、`vg_lite_finish()`；
   - CPU 读取 DMA-BUF，报告前后校验和、不同像素值、耗时和进程内存。

该 probe 的代码中禁止 `drmSetMaster`、`drmModeAddFB*`、modeset、atomic
commit 和 page flip。它只验证可以被 wlroots 使用的 **off-screen**
DMA-BUF 路径，绝不干扰正在运行的 Labwc。

Gate 0 的通过条件是：普通 `tdvp` 用户返回 `PASS`，桌面未退出，且
probe 输出包含成功的 DMA-BUF 映射、`finish`、非空且包含至少三种绘制
结果的校验信息。若不通过，保持 Pixman，不修改 Labwc 的 renderer。

## Gate 1：受控 wlroots fork

Gate 0 通过后，以当前锁定的 `wlroots 0.18.2` 为基线建立一个单独的
`wlroots-vglite` 源码仓库和 Buildroot package。不是修改上游源码后
不可追溯地替换，也不是让应用各自链接 VGLite。

fork 的初始改动分为四个明确边界：

| 层 | 新增职责 | 初版限制 |
|---|---|---|
| `render/vglite` | `wlr_renderer` 和 `wlr_render_pass` 的 clear / texture / rect / transform 实现 | 同步 render pass；无隐式 Pixman fallback |
| `render/vglite/texture` | 从 Wayland SHM 上传，或从 DMA-BUF 以 `VG_LITE_MAP_DMABUF` 导入纹理 | 只接受 XR24、AR24、modifier 0 |
| `render/vglite/allocator` | 创建 K230 DRM-GEM/PRIME 支持的 scan-out target | 初期不使用压缩/非线性 modifier |
| DRM backend output | 以物理 panel 尺寸提交已完成的 target | renderer 调用 `vg_lite_finish()` 后才允许 output commit |

Labwc 的逻辑坐标仍是 `1232 x 568`；K230 panel scan-out buffer 是物理的
`568 x 1232`。VGLite renderer 将使用 wlroots 的 output transform，将
`0/90/180/270` 作为 render matrix 的一部分处理，不改应用、输入或 DTS
的坐标语义。当前产品的 `90` 度输出变换必须逐项测试。

初版不做异步 fence 猜测。每帧的顺序必须是：

```text
collect scene -> record VGLite -> vg_lite_finish()
              -> wlr_output_commit_state() -> K230 DRM atomic page flip
              -> vblank callback -> release old buffer
```

这会优先保证正确性和帧顺序；只有测得 `finish()` 使 41.75 Hz 输出不能
稳定时，才在有 K230 driver fence 证据的前提下增加显式同步。

## 内存和帧预算

不预设“GPU 一定只用多少内存”。VGLite 内核驱动的 32 MiB contiguous
`mmap` 上限不等同于 32 MiB 实际物理占用，必须由 Gate 0 的
`VmSize/VmRSS/VmHWM` 输出测量。

已知的 scan-out 下界可计算：一个 `568 x 1232 x 4` 的 XR24/AR24 target
约为 2.67 MiB；三缓冲约为 8.01 MiB。初版只保留这个有界 target pool 和
当前帧可见 surface 的纹理，不创建应用私有 GPU context，也不为 Quick
Settings 另建 framebuffer。每次手势更新仍由 Wayland frame callback
合并为最多一次每 vblank 的 surface commit。

因此 VGLite renderer 能减少的是场景合成/缩放/alpha 操作的 CPU 负担；
它不能掩盖一个客户端在每个输入事件中无限制重绘的交互缺陷。Quick
Settings 的手势节流仍独立保留并和 vblank 对齐。

## 失败策略和验收

生产镜像不允许 `wlroots-vglite` 在初始化失败后悄悄切回 Pixman。这会让
用户误以为已得到 GPU 加速。renderer 初始化失败应明确记录失败原因并
停止该会话；恢复必须是运维人员显式选择 Pixman recovery 配置后重启
session。

启用 VGLite 的最终板端验收包括：

1. `tdvp` 的 Gate 0 probe 连续运行通过；
2. Labwc、Foot、PCManFM、wf-panel-pi、Quick Settings 均仍为 `tdvp`；
3. 物理 90 度显示与触摸坐标保持正确；
4. Top-down / bottom-up Quick Settings 手势期间无全屏撕裂、无越帧堆积；
5. 记录 CPU 使用、RSS、每帧 `finish` 时间、DRM page flip/vblank 时序；
6. 重启后仍为 VGLite renderer，且不依赖 root、direct KMS client 或
   Qt/GTK/Quick Settings 私有 workaround。

发布时 `vg-lite`（运行时）和 `wlroots-vglite`（compositor runtime）各有
唯一 ABI 所有者和版本化依赖；它们不能被静态塞进 Labwc 或应用程序。对应
IPK 进入匹配 K230 ABI 的 TDVP feed，固件只引用同一 release 的版本。
