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
必须是 compositor 的 Labwc 进程本身；它 `exec()` 的 PCManFM、wf-panel、Quick
Settings、输入桥和其他 autostart 子进程都不得保留 `/dev/vg_lite` 文件描述符。

这是与“第二次 `open()` 返回 `EBUSY`”不同的生命周期合同。K230 driver 的 single-context
检查只能阻止新的 `open()`；如果 SDK 以不带 `O_CLOEXEC` 的方式打开设备，Labwc 派生的
程序会继承同一个 open-file description，看似没有第二个 context，却能在 Labwc 退出后继续
钉住设备状态。已定位的 vendor `vg_lite_open()` 正是这种情况。平台的
`package/vg_lite/0001-tdvp-vglite-close-on-exec.patch` 将其改为
`open("/dev/vg_lite", O_RDWR | O_CLOEXEC)`；候选部署后的运行时验收必须确认仅 Labwc
保有该设备 fd，所有稳定的 exec 后桌面子进程均为零。该补丁尚未改变 client DMA-BUF、format
modifier 或 fence 合同。

这里有一个 Buildroot 的实现细节必须一并锁定：VGLite 包采用 `SITE_METHOD=local`，它走
override-source 的 rsync 路径而刻意跳过通用 `_PATCH` 阶段。因此配方把该补丁放在
`VG_LITE_POST_RSYNC_HOOKS` 中显式应用；验证脚本同时检查补丁文件、哈希与这个 hook，避免出现
“补丁已随源码复制但二进制仍是 vendor 旧实现”的假阳性。

2026-09-05 的实机候选验收已验证这条合同：部署前，Labwc 与 6 个 autostart 子进程共有 7 个
`/dev/vg_lite` fd，flags 均为 `0100002`；部署、重启后，仅 Labwc 保有 1 个 fd，flags 为
`02100002`（含 `O_CLOEXEC`），PCManFM、wf-panel、Quick Settings 和 key bridge 仍正常运行但
均不再持有设备。发布库 SHA-256 为
`f181f7f41468f27ce4d64d636e5dc9f3f2066615cba4280c9688912bfeda4ac5`；板端旧库保存在
`/var/lib/tdvp/renderer-stack-backup/vglite-userspace-20260904T180207Z/`，因此回退不依赖网络或
重新生成镜像。

K230 内核已经启用了 `CONFIG_GPU_VGLITE`，平台镜像也安装了与它匹配的 VGLite
用户态库和仅授予 `render` 组的 `/dev/vg_lite` 规则。Mesa 仍只提供 `swrast` DRI，
所以 `WLR_RENDERER=gles2` 不代表硬件加速。通用发布/回退 profile 仍以 Pixman 为
保守基线；但 2026-09-05 的受控板端候选已经明确选择 `WLR_RENDERER=vglite`，并由
approval marker、watchdog、session recovery 与本节 Gate 1 共同约束。两种 profile
不得靠临时改环境变量互相伪装。

当前登录会话由 `dbus-run-session` 提供私有 session bus，而镜像没有启用
`systemd --user` manager。因此环境文件必须固定
`LABWC_UPDATE_ACTIVATION_ENV=0`：否则 Labwc 的 DRM-backend 默认行为会派生
`systemctl --user import-environment`（退出时还会 unset），D-Bus 会尝试激活一个
不存在的 `org.freedesktop.systemd1` 服务。该报错既不是 VGLite 初始化失败，也不能用
启动 user manager 来掩盖；普通 Wayland client 不依赖这条 service-activation 路径。

Labwc 会把 XDG autostart 置于其自建的 detached session 中，不能假定 Labwc 退出会
自动结束 PCManFM、wf-panel、Quick Settings 或输入/idle 辅助进程。TDVP 登录启动器会
为每次认证生成一个私有 token 目录；autostart 只在该目录中登记自身 PGID，启动器在
Labwc 结束后仅结束已登记的那一组。这是显示稳定性前提：不能让旧 layer-shell surface
在下次 compositor 启动时重连并制造重复提交或闪烁。

## 源码责任域与 fork 准入

当前不再扩展工作仓库。现有 `wlroots-vglite` 与平台仓库承担集成、锁定版本、
Buildroot 打包和硬件验收；以下三个边界是后续出现上游无法及时合入的缺陷时必须
由 TDVP 控制的源码责任域。它们不是让桌面组件绕开合成器或直接操纵 DRM 的授权。

| 责任域 | 受控来源和当前状态 | 允许承担的修复 | 明确不承担的修复 |
| --- | --- | --- | --- |
| Kernel | `vicliu624/linux-xuantie-kernel`，本地工作分支 `tdvp/k230-pageflip-7d4e1f`；当前候选从固定 `7d4e…` 基线重放平台 `0053`–`0061` 补丁 | Canaan DRM/VO、vblank、page-flip event、GEM/format、KMS plane 和同步原语 | 把客户端重绘、Labwc 策略或 VGLite 失败伪装为内核问题 |
| VGLite SDK | `vicliu624/k230_linux_sdk` 的 `dev` fork；平台当前 vendor lock 仍是已验证的上游基线 | `vg_lite` 用户态 ABI、`/dev/vg_lite` 权限、DRM dumb-buffer/PRIME 映射和版本兼容性 | 用 SDK 改写 client DMA-BUF acquire/release 协议，或使应用直接成为 DRM master |
| Labwc | 目前使用固定的上游 package 与少量可审计 patch；当输入/窗口策略不能继续在 package queue 维护时再创建并锁定 TDVP fork | 输入、焦点、layer-shell、窗口和会话策略；每个 patch 需保留可回放的上游基线 | 合成渲染、buffer 生命周期、KMS page flip 或 VGLite command submission |

`wlroots-vglite` 是 VGLite renderer、SHM 上传、allocator 和 DRM backend 调用之间的
第四个既有集成边界。它不取代上述责任域：Kernel 决定何时真正完成 scan-out，VGLite
SDK 决定 GPU ABI，Labwc 决定窗口策略，wlroots 只在三者提供的合同内管理合成和输出。
每次跨域修复都必须在平台 manifest 中同时记录 kernel、VGLite SDK、wlroots 与 Labwc
的不可变 revision，不能只替换其中一个二进制文件。

### 上游漂移检查（2026-09-05）

上游 `kendryte/k230_linux_sdk` 的 `dev` ref 当前为 `0b890a12e3b07fa7524722de3ab53280840e833e`；
该 revision 的 `k230_canmv_v3_defconfig` 仍把
`BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION` 固定为
`7d4e1f444f461dbe3833bd99a4640e7b6c2cd529`，也就是本平台锁定的旧 kernel 基线。因而 SDK
`dev` 的“持续更新”不等于它已经包含 TDVP 的 page-flip lifecycle `0053` 或 VGLite
per-file resource ownership `0054`、secondary-open reset 抑制 `0055`、submit/wait lease
`0056`、其 command-engine edge guard `0057`、in-flight close recovery `0058`、有界
infinite-wait watchdog `0059`、IRQ/ioctl completion flag 原子化 `0060`，以及单 VGLite
context open 强制 `0061`：九者都是必须从本平台 queue 重放、编译并真机验收的受控 patch。

以后上游 SDK `dev` 变更其 kernel ref 时，不能直接跟随。必须先在三个独立方向比较：
`canaan_crtc/canaan_vo` 的 vblank/page-flip 生命周期、VGLite kernel/userspace ABI、以及
Buildroot recipe/overlay；随后对 `0053` 至 `0061` 和 wlroots/Labwc queue 逐个做 dry-run 回放、
重建 stack fingerprint，并重新走 Gate 0.5/1。这样 SDK fork 才是可控责任域，而不是把一个
可能改变 DRM/VGLite 合同的上游快照直接带入桌面镜像。

从本轮开始，该要求由 staging 流程强制执行：`sdk-sources.lock` 锁定
`wlroots_vglite`、Labwc ABI 与 VGLite SDK 的交叉身份，
`verify-tdvp-renderer-stack-lock.sh` 则对最终会被 Buildroot 使用的
`package/vg_lite` 完整树生成 `renderer_stack_vglite_package_sha256`，并记录可回放
`0001-tdvp-vglite-close-on-exec.patch` 的 SHA-256；wlroots 的
`0001-tdvp-vglite-linear-shm-lifecycle.patch` 与 Labwc recovery patch 也分别锁定。
这样 SDK worktree 中经审查的本地 VGLite patch 不会被静默覆盖，wlroots 的 linear-SHM
生命周期修复也不会只存在于编译机目录；但三者都不能在已有 manifest 后继续变更而不重新
staging，build 前的摘要复验会拒绝这种漂移。

### 当前实现真相与 Gate 2 的最小合同

截至 `wlroots-vglite` 的 `94bca3e` 基线，必须区分三个容易混淆的 DMA-BUF 路径：

| 路径 | 当前代码事实 | 结论和后续所有者 |
| --- | --- | --- |
| compositor render target | VGLite renderer 将单平面、offset 为 0、linear/implicit 的 `XR24/AR24` DRM DMA-BUF 以 `vg_lite_map(..., VG_LITE_MAP_DMABUF, fd)` 映射为输出目标；同时仍要求 CPU `data_ptr` | 这是固定面板 dumb-buffer 合成路径，可继续作为 Gate 1 的实验条件；它不证明应用 buffer 可以被作为 texture 导入。VGLite SDK 负责确认这条 ABI 与 kernel driver 配对。 |
| Wayland client texture | `texture_from_buffer()` 和 update 路径仅接受 `WLR_BUFFER_CAP_DATA_PTR`，并明确拒绝没有 CPU 指针的 client DMA-BUF；renderer 也只声明 `WLR_BUFFER_CAP_DATA_PTR`，所以不会创建 `zwp_linux_dmabuf_v1` 全局 | 这是 Qt/Chromium/相机/硬解的直接缺口。VGLite SDK 必须先提供经过实机验证的 fd-only import，或证明受控 CPU mapping 的完整 ABI；之后才可在既有 `wlroots-vglite` 中新增严格的 client import 路径，初期只允许 K230 已验证的单平面 `XR24/AR24 + LINEAR`。不能把任意 FD 或 implicit modifier 当成可合成输入。 |
| KMS presentation / fullscreen | wlroots scene 已有“单一、全屏、无 transform、无软件 cursor、无 color transform”的 generic primary-plane direct scan-out 分支；真机 plane/CRTC 已可观察到 `IN_FENCE_FD`/`OUT_FENCE_PTR` property，但当前 `DRM_CAP_SYNCOBJ=0`、`DRM_CAP_SYNCOBJ_TIMELINE=0`，而 wlroots DRM backend 也尚未登记/提交这些 property | 不要重新实现 scene direct scan-out，也不能因 property 存在就启用 explicit-sync protocol。Kernel 责任域须给出可验证的 sync-file/syncobj fence 语义；`wlroots-vglite` 再将 acquire/release fence 接到 buffer ownership 和 atomic commit；Labwc 只在其窗口/overlay 策略需要改变 direct-scan-out eligibility 时才需要受控 fork。 |

VGLite SDK 的公开头文件确实有 `VG_LITE_MAP_DMABUF` 与 map/unmap/flush/finish，但没有把
DRM format modifier、acquire fence 或 release fence 暴露为该 API 的一部分。因此“设备有 GPU
且 VGLite 能 map DMA-BUF”不足以宣称一般桌面 DMA-BUF 已完成。Gate 2 必须按以下顺序推进，且每
一步失败均拒绝该 buffer；它不能在运行中的 VGLite 会话里偷偷换成 Pixman renderer。Pixman
本身也只声明 `WLR_BUFFER_CAP_DATA_PTR`，并不能充当 GPU-only DMA-BUF 的逐帧 import fallback。
可恢复策略只能分两层：单个 import 失败时保留该 surface 既有的已上传 texture（若有）并记录
失败；连续/致命失败则由受控 session profile 在**下次会话**选用 Pixman/SHM 基线。

更严格地说，当前 vendor SDK 的 `vg_lite_map()` 源码会在调用 kernel 前拒绝同时缺少
`buffer->memory` 与 `buffer->address` 的对象；它对 `VG_LITE_MAP_DMABUF` 也仍如此。当前
输出 target 恰好从 `wlr_buffer_begin_data_ptr_access()` 取得了前者，因而可以工作；这并不等于
任意 client 的 DMA-BUF fd 能被 VGLite 直接导入。Gate 2.0 必须先用真实 producer 验证这一个
SDK ABI 前置条件，而不是先修改 renderer 的 capability：

1. **CPU-mappable linear import probe。** 只对单平面、offset 为 0、`XR24/AR24`、linear 的
   producer DMA-BUF 做受控 `mmap`，把得到的 logical address、stride 与 fd 传给
   `vg_lite_map(..., VG_LITE_MAP_DMABUF, fd)`，再完成一次 sample/blit/finish/unmap。任何 mmap、
   cache 同步或 map 失败都必须记录 producer、format 和 errno，并拒绝该次 client buffer；不能
   把 fd 塞进一个 address 为零的 `vg_lite_buffer_t`，更不能尝试在该帧热切 renderer。
2. **决定 GPU-only DMA-BUF 的责任边界。** 若 Qt、Chromium、相机或硬解 buffer 不能 CPU mmap，
   当前 VGLite SDK API 没有 fd-only import 合同，不能以猜测的 `memory == NULL` 参数绕过检查。
   这时应在受控 VGLite SDK fork 中增加并实机验证 fd-only import ABI，或维持拒绝/应用侧 copy；
   该限制不能由 Labwc 或 wlroots 的 policy 掩盖。
3. 在 `wlroots-vglite` 为已经通过上述 probe 的 client texture 添加严格的 format、plane count、
   offset、stride、modifier、mmap lifetime 与 unmap 校验；仅在 VGLite 映射和渲染均成功后，才对
   该已验证集合 advertise `WLR_BUFFER_CAP_DMABUF`。import 失败时必须在 surface commit 路径中
   有明确日志和可恢复的拒绝行为，绝不访问不受支持的 buffer 内存。
4. 先在正常运行的 Labwc 会话里以非 master、只读方式运行
    `/usr/bin/tdvp-kms-capability-observer`，保存 primary/overlay plane 的 `XR24/AR24`、`IN_FORMATS`
    modifier、`IN_FENCE_FD`、rotation/zpos/alpha、CRTC `OUT_FENCE_PTR`，以及
    `DRM_CAP_SYNCOBJ`/`DRM_CAP_SYNCOBJ_TIMELINE` 输出。该观察器不分配 buffer、不 modeset、不
    page-flip；单纯出现某个 property 也不证明它可用于 K230，反之缺失则明确阻止
    direct scan-out/fence 实现继续宣称就绪。
5. 2026-09-05 的真机观察结果是：活动 primary plane 与两个 overlay plane 对 `XR24/AR24` 声明
    linear modifier `0`，并有 `IN_FENCE_FD`；活动 CRTC 有 `OUT_FENCE_PTR`；但两个 syncobj
    capability 都是 `0`。所以现有内核不能创建 wlroots 的 `linux-drm-syncobj-v1` backing object，
    该 global 不得启用。Kernel 受控 fork 的工作不是机械添加 feature bit，而是先验证
    `DRM_IOCTL_SYNCOBJ_EVENTFD`、sync-file 与 timeline 的 import/export、`IN_FENCE_FD` 的消费/关闭
    语义、以及 release 仅在硬件完成/page-flip 后 signal；任一项失败即保持 capability 关闭。
6. 在 Kernel 与 wlroots DRM backend 之间验证 `IN_FENCE_FD`/`OUT_FENCE_PTR` 的 ownership、关闭
    时机和错误恢复；VGLite 的同步调用不能替代 KMS release fence。即使 kernel future work 通过，当前
    `linux-drm-syncobj-v1` 代码仍只会延迟 acquire point 的 surface commit，尚未把 release point
    接到 renderer/backend，且 state move 中保留“立即 signal release”的 TODO，因此在这一步完成前
    仍不得创建该 protocol global。
7. 对每个被允许的 format/modifier 做真实 producer 测试（至少 Qt、Chromium/视频和相机各一类），
    覆盖导入失败、producer 超时、output commit 失败、surface 销毁和 session restart。
8. 只有上述路径稳定后，再让现有 scene 的 primary-plane direct scan-out 接管满足条件的全屏
    DMA-BUF；有 layer-shell、cursor、缩放/旋转、capture 或 color transform 时必须回退到合成。当前
    VGLite profile 显式设置 `WLR_SCENE_DISABLE_DIRECT_SCANOUT=1`，在 client DMA-BUF 与完整 fence
    链路真实通过前不得移除这个保护。

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
3. 以 `tdvp` 运行 `/usr/bin/tdvp-vglite-probe --frames 120`。它必须依次完成：

   - 直接打开 `/dev/vg_lite`；
   - `vg_lite_init()`、版本/芯片信息读取；
   - 在 **未绑定 framebuffer** 的 DRM dumb buffer 上执行
     `DRM_IOCTL_PRIME_HANDLE_TO_FD`；
   - `vg_lite_map(..., VG_LITE_MAP_DMABUF, ...)`；
   - GPU clear、blit、path draw、`vg_lite_finish()`；
   - 每一帧 CPU 读取 DMA-BUF，确认校验和相对前一帧更新，并报告前后
     校验和、不同像素值、`finish` 最小/平均/最大耗时和进程内存。

该 probe 的代码中禁止 `drmSetMaster`、`drmModeAddFB*`、modeset、atomic
commit 和 page flip。它只验证可以被 wlroots 使用的 **off-screen**
DMA-BUF 路径，绝不干扰正在运行的 Labwc。

“off-screen”并不等于旧 kernel 上无风险：历史 `VG_LITE_INFINITE` wait 在 completion IRQ
丢失时仍可能把测试 client 与其后的 compositor 一同拖入无限等待。因此源码中的 probe 在只打开并关闭
`/dev/vg_lite`（允许模块加载）后，必须先读到
`/sys/module/vglite/parameters/infinite_wait_watchdog_ms` 或
`/sys/module/vg_lite/parameters/infinite_wait_watchdog_ms`，且值必须在 `1..5000 ms`；否则在
分配 dumb-buffer、`vg_lite_init()` 或任一 GPU 命令之前拒绝运行。它是 `0059/0060` kernel 的安全前置
条件，不是“GPU 可用”的成功证据。

Gate 0 的通过条件是：普通 `tdvp` 用户在默认的一帧 smoke 之后，完成
`--frames 120` 的有界重复提交并返回 `PASS`；桌面不得退出，且输出必须
包含成功的 DMA-BUF 映射、每帧 `finish` 后内容更新、非空且至少三种绘制
结果以及 finish 的最小/平均/最大耗时。若不通过，保持 Pixman，不修改
Labwc 的 renderer。

## Gate 0.5：KMS page-flip 生命周期

Gate 0 只证明 GPU 可以对一个离屏 scan-out target 完成渲染；它**不**证明
K230 DRM 在 vblank 前后正确持有/释放 framebuffer。因此 VGLite 不能仅因 Gate 0
通过就成为默认 renderer。必须先以包含下列修复的内核在真机上通过 KMS 生命周期门禁：

| 内核职责 | 必须具备的行为 | 失效时的外观 |
| --- | --- | --- |
| VO IRQ | 只确认已观察到的 IRQ status，只将 VTTH/vblank status 报告为 vblank | 虚假 vblank、随机回调 |
| atomic flush | 在硬件 GO 前取得 vblank 引用；把 pending event 保存到 CRTC | GO 到 IRQ 之间的 event race |
| VTTH IRQ | IRQ 中 `drm_crtc_handle_vblank()` 后发送保存的 event，并释放引用 | page-flip 永不完成或过早复用旧 buffer |
| 时序寄存器 | 以实际 `vth_line`（本板为 10）设置 VTTH，不能把 `log2(vtotal)` 当作行号 | 不稳定或错误时机的 vblank |

门禁只能通过 SSH/串口维护模式执行，不能在正在显示的 Labwc 里直接运行：

```sh
sudo systemctl stop greetd
while pgrep -x labwc >/dev/null; do sleep 0.1; done
sudo systemctl start tdvp-kms-acceptance
sudo cat /run/tdvp/acceptance/kms.status
sudo cat /run/tdvp/acceptance/kms.log
sudo systemctl start greetd
```

在请求真机维护窗口前，`buildroot/tools/test-tdvp-kms-acceptance-guard.sh` 必须在
`bwrap` sandbox 中通过。它以 fake `systemctl`、`pgrep` 和 `tdvp-display-smoke` 覆盖三种
情况：`greetd=active` 时拒绝、Labwc/gtkgreet 仍在时拒绝，以及仅在两者均退出时调用
display-smoke。最后一种情况必须逐字验证
`--format XR24 --pattern counter --seconds 10 --fps 30 --page-flip-timeout-ms 1000`；因此
该测试能防止维护 wrapper 的 guard 或固定参数在构建机上静默退化，但不能替代真实 KMS
event/old-FB 验收。

该服务以 `XR24` 运行 10 秒、30 fps 的 `counter` 模式。它交替提交两个 dumb
buffer，并在重写旧 buffer 前等待相同 CRTC 的 page-flip event。测试结束时，它还会
先原子 detach 测试 plane、等待对应 event，再通过 `DRM_VBLANK_EVENT` 等待同一 CRTC
的一个独立后续 vblank，最后才允许 `RmFB/DESTROY_DUMB`。后续 vblank 超时、CRTC
选择异常或事件处理失败时，测试保持 buffer 到 DRM FD 关闭，不能以提前 free 的方式
“完成”维护事务。每一个翻页都会记录：

- kernel event 给出的 vblank sequence 与 timestamp；
- 用户态 `drmModeAtomicCommit()` 返回前开始计时、到对应 event 被
  `drmHandleEvent()` 派发的 `submit_to_event_ms`；
- 被 release 的旧 FB 与新提交的 FB。验收状态机只会把旧 FB 标为可写，随后下一轮才
  允许复用它。

最终 `PASS dynamic` 行必须给出 `submit_to_event_ms=min/avg/max` 与与帧数相等的
`released_buffers`。这个延迟是用户态可观察的提交到事件时间，不把调度延迟误报为
硬件 vblank 周期；硬件 cadence 仍以 event timestamp 和 sequence 验证。任何 timeout、
错误 CRTC、重复 sequence、倒退 timestamp、无效时钟样本，或尝试在 release 前重写 FB，
都使 Gate 0.5 失败，默认 Pixman 保持不变。

2026-09-04 已在仍运行 Pixman/Labwc 的会话中，以不会成为 DRM master 的
`tdvp-vblank-observer --device /dev/dri/card0 --frames 120 --max-interval-ms 250`
观察到 120/120 个单调递增的 IRQ1 vblank event：event interval 为
`23944/23951.0/23957 us`（min/avg/max），delivery interval 为
`19668/23934.6/28133 us`。这只验证活动 compositor 共存时的 IRQ1
sequence/timestamp cadence；它不验证 page-flip 的 old-FB ownership。因此它不能替代
停止 greetd 后的 counter 验收，也不能作为准许 VGLite 会话的 Gate 0.5 PASS。

### 已确认的 K230 运行时约束

2026-09-04 在运行 Pixman/Labwc 的 RM69A10 实机上，Gate 0 以普通 `tdvp`
用户连续运行通过：VGLite 识别为 `GCNanoUltraV`（chip `0x265`），K230 DRM
dumb buffer 导出的 PRIME DMA-BUF 可映射为 VGLite render target。probe 从不取得
DRM master，也不创建 framebuffer、modeset、atomic commit 或 page flip；Labwc PID
在所有探针前后保持不变。

| target / source / 120 帧 | `vg_lite_finish()` min / avg / max | 端到端 frame avg / max | 结论 |
| --- | --- | --- | --- |
| `256 x 128`，`VG_LITE_BGRA8888`，`64 x 48` source | `0.075 / 0.394 / 9.064 ms` | `48.523 ms` total | 基本的 DRM PRIME + CPU upload + GPU clear/blit/path 循环通过 |
| `568 x 1232`，`VG_LITE_BGRA8888`，`64 x 48` source | `1.094 / 1.692 / 5.635 ms` | `205.303 ms` total | 面板物理尺寸的 ARGB target 循环通过 |
| `568 x 1232`，`VG_LITE_BGRX8888`，`64 x 48` source | `1.091 / 1.776 / 10.260 ms` | `215.494 ms` total | `DRM_FORMAT_XRGB8888` 的无 alpha target 循环通过 |
| `568 x 1232`，`VG_LITE_BGRX8888`，`64 x 64` source | `1.105 / 1.877 / 8.609 ms` | `1.934 / 8.663 ms` | 新增分段计时后的小 texture 基线；满足 41.75 Hz 的离屏余量 |
| `568 x 1232`，`VG_LITE_BGRX8888`，`568 x 1232` source | `3.409 / 7.610 / 17.876 ms` | `22.515 / 35.169 ms` | 每帧全纹理 CPU upload + 合成已经贴近或超过单个 vblank；不得作为 Gate 1 常规路径 |

前面三组 probe 的 `VmPeak` 约为 `38.4 MiB`；新增 `64 x 64` 与满 source 组分别为
`38.4 MiB` 与 `41.2 MiB`。满 source 组中 CPU upload 为 `4.989 / 14.633 / 28.628 ms`，
submit 为 `0.137 / 0.272 / 8.402 ms`，finish 为上表所示；候选二进制 SHA-256 为
`4af569f5d795c66c88e53d6999fcdd79de6030f409a03e76f5732e3d60c205e8`。因此性能结论是：
小 VGLite-owned texture 的离屏路径有余量，但“每帧完整上传大 SHM surface”本身就会耗尽
帧预算，不能寄望 GPU 掩盖它。先前记录的 67--69 ms 不能在当前镜像与同一验收流程中复现，
已由以上有命令、尺寸和帧数的测量替代。上述数字仍不包含 KMS 提交或 old-FB release，
不能当作 Gate 0.5 的结论。

同一块板还确认了一个必须体现在 renderer 中的限制：

```text
VG_LITE_MAP_USER_MEMORY(普通 Wayland SHM 映射) -> VG_LITE_OUT_OF_RESOURCES
```

所以首版不能把任意 `wl_shm` 内存宣称为 VGLite 的零拷贝 texture。它将为
每个可见 SHM surface 保留一个 VGLite 分配的 texture，只把客户端 damage
区域以 CPU `memcpy` 上传到该 texture，再由 GPU 完成缩放、alpha、旋转和
所有场景合成。该 CPU copy 是输入上传，不是 Pixman 合成 fallback。对应
probe 也会验证“CPU 写入 VGLite 分配的 source -> GPU blit -> DRM DMA-BUF
target”的完整路径。

未经单独实机验收前，renderer 不向 Wayland 客户端声明任意 DMA-BUF texture
导入能力；DRM DMA-BUF render target 与 client DMA-BUF import 是两个不同的
合同，不能由前者替代后者。

## Gate 1：现有受控 wlroots 集成仓库

Gate 0.5 通过后，以当前锁定的 `wlroots 0.18.2` 为基线，在既有
`wlroots-vglite` 仓库及其现有 Buildroot package 中推进 renderer。当前阶段不得
为此再创建仓库；该仓库就是 wlroots/VGLite 集成的唯一可追溯修改点，也不能让应用各自
链接 VGLite。

fork 的初始改动分为四个明确边界：

| 层 | 新增职责 | 初版限制 |
|---|---|---|
| `render/vglite` | `wlr_renderer` 和 `wlr_render_pass` 的 clear / texture / rect / transform 实现 | 同步 render pass；无隐式 Pixman fallback |
| `render/vglite/texture` | 将 Wayland SHM damage 上传到 VGLite-owned texture | 首版不声明 client DMA-BUF import；只接受 XR24、AR24、modifier 0 |
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

在这条 fence 合同存在前，获准的 VGLite Labwc profile 必须导出
`WLR_SCENE_DISABLE_DIRECT_SCANOUT=1`。wlroots 默认会为单一、全屏、无 transform
的 scene entry 尝试 generic direct scan-out；该优化会绕过 VGLite render pass，因而也绕过
本阶段已验收的 `finish() -> atomic page flip -> old-FB release` 责任链。这个开关只属于
受控 VGLite profile，不改变保守的 Pixman 回退会话。将来只有 client DMA-BUF import、format/
modifier 协商、`IN_FENCE_FD`/`OUT_FENCE_PTR` 和 K230 真机验收都通过后，才按 Gate 2 的
fullscreen policy 重新开放该分支。

对已经登录、**实际使用 VGLite** 的 Labwc 会话，Gate 1 必须由图形登录用户运行
不取得 DRM master 的会话 gate：

```sh
/usr/bin/tdvp-vglite-session-gate --expect-vglite-compositor \
  --width 1232 --height 568 --format xr24 --damage-size 0 --frames 120 --max-frame-ms 1000 --repeat 3
```

这个 wrapper 首先从同一用户的真实 Labwc PID 读取环境，并且只在下列四个条件同时成立时
才开始负载：`tdvp-renderer-profile status` 精确为获准、未熔断的 VGLite profile，
`WLR_RENDERER=vglite`，`TDVP_LABWC_VGLITE_FAILURE_RECOVERY=1`，以及
`WLR_SCENE_DISABLE_DIRECT_SCANOUT=1`。含内核 0059 的 candidate 还必须读到已加载 module
的 `/sys/module/vglite/parameters/infinite_wait_watchdog_ms=5000`；这是只读运行态检查，
不是故意触发 GPU hang。它在每一轮 SHM 完成后再次检查这些条件和 watchdog 参数；
`--repeat` 将同一负载连续运行指定轮数，因此不能把 Pixman 会话、已经重启恢复的会话，或
绕过 VGLite render pass 的 direct-scanout 会话误记为 VGLite PASS。它本身不启停 Labwc、不选择
renderer、不打开 DRM，也不写入 approval marker 或熔断状态；root 运行会明确被拒绝。watchdog
参数缺失、不可读或不等于 `5000` 都应使 Gate 1 失败。

随后 wrapper 通过已有的会话包装器运行 panel-logical-size 的双线性 `wl_shm` client。默认
`XR24` 仍保留为历史基线；`--format ar24` 则明确创建带非不透明 alpha 的 `AR24` buffer。client
在分配前必须由真实 `wl_shm` global 通告所选格式，因而不会把不支持的 alpha import 误作超时或
PASS。只有所有提交、callback 和 `wl_buffer.release` 都完成时，底层客户端才会输出 `PASS`；输出
同时记录格式以及 commit 到 callback、commit 到 release 的最小/平均/最大延迟，和 release 位于其
对应 callback 前后次数。`0061` 明确把 K230 VGLite driver 约束为单 context：实际 VGLite
Labwc 持有 `/dev/vg_lite` 时，任何第二次 `open()` 必须在提交 GPU 命令前返回 `EBUSY`。因此
session gate 固定 `--churn-iterations 0`，只跑 SHM client，绝不能在 live compositor 上启动
离屏 probe、client churn 或 in-flight-close。单独的 owner 观察可以只做一次无 ioctl 的第二次
`open()` 并记录 `EBUSY`，但它不是 GPU 压力负载，也不属于每轮 Gate 1 工作。

每一个 candidate 至少需要让 `XR24` 和 `AR24` 各在以下两种负载连续三轮，并保存完整
stdout/stderr、当前 `$XDG_RUNTIME_DIR/tdvp-labwc.log`、VGLite diagnostics 与 kernel log：

```sh
# 全帧动态 SHM：暴露整屏 upload/合成和 page-flip 背压。
/usr/bin/tdvp-vglite-session-gate --expect-vglite-compositor \
  --width 1232 --height 568 --format xr24 --damage-size 0 --frames 120 --max-frame-ms 1000 --repeat 3

/usr/bin/tdvp-vglite-session-gate --expect-vglite-compositor \
  --width 1232 --height 568 --format ar24 --damage-size 0 --frames 120 --max-frame-ms 1000 --repeat 3

# 静态大 surface 上的小 damage：隔离 drag/移动时的 damage 与 cache 成本。
/usr/bin/tdvp-vglite-session-gate --expect-vglite-compositor \
  --width 1232 --height 568 --format xr24 --damage-size 64 --frames 120 --max-frame-ms 1000 --repeat 3

/usr/bin/tdvp-vglite-session-gate --expect-vglite-compositor \
  --width 1232 --height 568 --format ar24 --damage-size 64 --frames 120 --max-frame-ms 1000 --repeat 3
```

最终基线必须在干净的 Pixman 和获准的 VGLite 会话中各连续采样至少三轮，连同
`SCHED` 行、renderer diagnostics、page-flip/vblank 日志及屏幕截图保存。`1000 ms`
只用于阻断卡死或 buffer release 停滞，不能作为 VGLite 性能达标线；性能结论应由
同一硬件、同一输入负载、同一面板尺寸下的两组完整分布比较得出。

获准的 Gate 1 诊断轮次将只在该单次 session 导出
`TDVP_VGLITE_DIAGNOSTICS=1`。它会为每次 client texture update 输出
`TDVP_VGLITE_DIAG stage=texture_upload`，包括 `damage_rects`、实际
`copied_bytes` 和 texture 总 `source_bytes`；同一开关还会输出 render pass 的
`stage=vglite_finish`，其中 `state_recovery_attempted`/`state_recovery_ok` 明确记录失败
pass 是否已尝试恢复 VGLite 的全局 scissor/source-alpha 状态以及恢复是否成功。两类日志必须与
Wayland callback/release 和 KMS page-flip sequence 一起保存：`copied_bytes` 很小而
`source_bytes` 很大时，才能准确识别 vendor
全 source cache 维护是否是拖动瓶颈。`texture_upload` 还会记录 `copy_calls`：仅当 damage
覆盖一个无 padding 且输入/输出 stride 相同的完整 texture 时，首轮上传可安全合并为一次
`memcpy`；任何 partial damage 仍按每行、每个 pixman rectangle 的精确范围复制，不得为减少
copy call 扩展 damage 或复制 row padding。renderer 还会拒绝“data access 成功但 data 指针为
NULL”、source memory 丢失或 stride/尺寸变化的 client buffer，以错误返回替代对空指针的
`memcpy` 崩溃。该环境变量默认关闭，不能作为发布会话的常驻日志。

不要手工在 greeter、greetd 或环境文件中持久写入这个变量。root 在下一次**实际解析为
VGLite** 的登录会话前执行：

```sh
/usr/local/bin/tdvp-renderer-profile diagnostics-next-vglite-session
/usr/local/bin/tdvp-renderer-profile diagnostics-status
```

第一个命令只在 `tdvp` 的私有 state 目录放入一个 0600 marker；`tdvp-labwc-session` 仅在
effective renderer 是 `vglite` 时，先删除该 marker、再导出诊断变量。若 profile 被 breaker
解析为 Pixman，marker 不会被消费，下一次获准的 VGLite 登录才会使用它；若 VGLite 随后异常并触发
同一认证会话的 Pixman self-recovery，变量也不会继承。root 可在重启会话前用
`clear-diagnostics-next-vglite-session` 撤销。该开关既不修改 profile/approval/breaker，也不打开
DRM 或 `/dev/vg_lite`。

### 2026-09-04 Pixman 基线：damage 必须作为独立负载

在已登录的 K230 实机、现有 `WLR_RENDERER=pixman` 会话中，`1232x568`、两张
`XR24 wl_shm` buffer、120 帧的全帧动态更新得到：callback `min/avg/max =`
`108403/154305.2/313489 us`，release `min/avg/max =`
`243868/317821.8/495473 us`。同一次实验还曾在提交第 3 帧后因两张 buffer 均忙而
连续 294 ms 没有 Wayland 进展；这是有意义的背压信号，不应通过增大超时掩盖。

使用**同一窗口尺寸、同一两 buffer 协议**，但仅在每一帧更新并声明一个 `64x64`
正方形 damage（两个 buffer 初始内容相同，故不会低报交替 buffer 的可见变化）后，120/120
callback 和 release 都完成，callback 为 `4347/26453.8/146170 us`，release 为
`8111/29360.6/327374 us`，且所有 release 都在对应 callback 之后。该结果不是 VGLite
性能结论；它只证明在此硬件/会话上，整屏 SHM 写入和合成是主导变量。因此 Gate 1 的
Pixman/VGLite 对照必须至少分别覆盖“全帧动态”和“小 damage 覆盖静态大 surface”，并分别
报告 callback、release、CPU 与 `vg_lite_finish()` 的分布，不能以静态棋盘格或仅一种 damage
模式宣称拖动性能达标。

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

还存在一个必须实测、不能由 wlroots 的 damage copy 结论掩盖的 SDK 成本：当前
K230 VGLite 的 `vg_lite_blit()` 在每次 blit 前会对非空 `source->memory` 的整张
buffer 做 D-cache clean/invalidate；`vg_lite_finish()` 也会对当前 render target
做完整的 cache 维护。也就是说，wlroots 虽然只把客户端 damage 区域 memcpy 到
VGLite-owned texture，透明的全屏 layer-shell surface 在多个 damage clip 中被重复
blit 时，仍可能把一笔窄带更新放大为多次完整 source cache 操作。

候选 renderer 为每个内部 VGLite texture 维护 CPU-upload dirty bit：首个携带新写入的
blit 保留 `source->memory`，仍走 SDK 原有的全 buffer clean；成功提交后，同一 texture
在余下 clip 和后续无 CPU 写入的帧内临时隐藏该 CPU 指针，再立刻恢复。锁定 SDK 中该指针
在 `vg_lite_blit()` 的 source 路径仅用于这次 cache clean（GPU 仍使用 address/handle），
所以这不是跳过上传同步，而是把一次 upload 的同步限制为一次。它也**不**使用
`vg_lite_flush_mapped_buffer()`：锁定 K230 内核的 `VG_LITE_CACHE` ioctl 目前为空成功，
不能作为替代一致性合同。每次 SDK 更新都必须重新审计这一 ABI 条件。

2026-09-04 的新 probe 将 `CPU upload`、`command submit`、`finish` 和完整离屏 frame
分别报告，证明全 source CPU upload 已足以把 frame 平均值推到 `22.515 ms`；但它没有、
也不应伪造“只写 64x64 damage 却让普通 `vg_lite_blit()` 读取完整 source”的 cache 行为。
因为 vendor `vg_lite_blit_rect()` 未进行与 `vg_lite_blit()` 相同的 source cache clean，
它不能替代 renderer 的一致性测试。下一轮 Gate 1 必须在 wlroots 的真实 texture 实现上
采集小 damage 覆盖静态大 texture 的 submit/finish 分布。

为避免把上述静态判断误当成优化，probe 现提供 `--blit-api normal|rect`：`rect` 仍传入
完整 source rectangle，因而可以在不改变待采样像素的前提下与普通 API 比较。实机
`568x1232`、`120` 帧、`BGRX` 的结果如下（同一三点签名 `ffc5ac52b3833d93` 均通过）：

| source | API | upload avg | finish avg | frame avg | 结论 |
| --- | --- | ---: | ---: | ---: | --- |
| `64x64` | `vg_lite_blit` | 0.032 ms | 1.837 ms | 1.888 ms | 当前正确路径 |
| `64x64` | `vg_lite_blit_rect`（full rect） | 0.032 ms | 1.761 ms | 1.812 ms | 差异处于硬件波动，不能据此改 renderer |
| `568x1232` | `vg_lite_blit` | 14.633 ms | 7.610 ms | 22.515 ms | 满屏上传超过 41.75 Hz 的 23.951 ms vblank 预算边缘 |
| `568x1232` | `vg_lite_blit_rect`（full rect） | 15.053 ms | 7.754 ms | 22.827 ms | 没有性能收益，仍不可用于满屏 SHM 动态内容 |

因此当前结论是**不能**仅因 `blit_rect` 省去用户态 source clean 就切换 API。当前本地
`linux-xuantie-kernel` 的 K230 VGLite 驱动还显示两个必须先被受控修复/验收的机制缺口：
`vg_lite_hal_operation_cache()` 对 `VG_LITE_CACHE` 直接成功返回而未做实际操作，
`vg_lite_hal_barrier()` 则在每次 submission 使用全局 `flush_cache_all()`。也就是说，
`vg_lite_flush_mapped_buffer()` 目前不是可证明的 dirty-range 同步 API，而 `rect` 仍会
被提交时的全局 flush、target cache 维护和完整 GPU raster 成本主导。这里是源码审计结论，
尚非对运行中内核二进制的替代证明；Gate 0.5 仍要确认加载的驱动与此受控 kernel 基线一致。

同一内核基线还存在一个独立于 cache 的资源所有权风险：旧版 VGLite driver 将连续内存
分配和 DMA-BUF 映射挂在全局 device 链表上，`release()` 却在任意 `/dev/vg_lite` 文件描述符
关闭时遍历并释放该全局链表。这样第二个 VGLite 进程（包括短生命周期测试程序）退出时，可能
释放 Labwc/wlroots 尚在提交或显示的 VGLite resource。补丁
`buildroot/k230-sdk-overlay/linux/0054-tdvp-vglite-per-client-resource-ownership.patch`
将这两张链表移入每个 file 的 `client_data`，并用互斥锁将 vendor ioctl 的活动 client 与其
分配/映射/释放生命周期串行化。这一补丁只解决跨客户端错误释放和 `private_data` 竞争；它不
改变 cache API，也不构成 DMA-BUF fence 支持。

在此之后还识别出两个不同的跨进程机制缺口。`0055` 令 vendor `drv_open()` 只在
`/dev/vg_lite` 从零 open 变为一个 open 时 reset 硬件，避免一个短生命周期 probe 的 secondary
open 把运行中的 Labwc command stream reset。`0056` 把一次 `VG_LITE_SUBMIT` 与它匹配的成功
`VG_LITE_WAIT` 用全局 semaphore 保持为一个 lease；只给每一个 ioctl 加锁并不够，因为硬件命令
寄存器和 interrupt flag 是 device-global 的，第二个 client 可以在第一个 client 的 Submit 与
Wait 之间覆盖 command/completion。超时仍保留 lease，只有匹配成功 Wait、Terminate 或 submit
失败才释放，避免错误地将未完成硬件交给下一个 client。

### 2026-09-05 受控候选状态与剩余 Gate 1

旧的 `0059`/`0060` “尚未部署”记录已被本候选取代：`0053`–`0061` 已从干净解包树完成目标
交叉编译，使用保留 boot 与 renderer-stack 备份的差分部署流程安装并重启。候选 `Image` SHA-256 为
`7a9268269a4df446fd5adbaa19601f714888b190c8dce07b16ba23edf1f39bc6`；板端的 boot 备份目录为
`/var/lib/tdvp/boot-backup/boot-20260904T162202Z`。重启后已核对 profile 为
`configured=vglite effective=vglite vglite_enabled=yes breaker=clear`，实际 Labwc 导出
`WLR_RENDERER=vglite`、`WLR_SCENE_DISABLE_DIRECT_SCANOUT=1` 与 5000 ms watchdog；一次仅做
`open()` 的第二 owner 检查返回 `EBUSY`。120 帧 vblank observer 的 event interval 为
min `23926`、avg `23929.7`、max `23933` microseconds，表明 page-flip/vblank event 没有停滞。

这一证据说明候选已在板端实际使用 VGLite，且单-context 防护已经生效。随后同一 boot、同一
Labwc PID `252` 已完成 `XR24/AR24 × full/64×64 damage × 120 frames × repeat 3` 的完整 Gate 1
SHM 矩阵；原始记录固定保存于
`/var/lib/tdvp/vglite-gate-20260904T164900Z/`。12 个 workload rounds 均为 `submitted=120`、
`callbacks=120`、`released=120`，没有 timeout、PID 替换、RSS 增长、profile breaker 或 watchdog
变化。postflight 120-frame vblank observer 为 min `23947`、avg `23950.2`、max `23954`
microseconds（delivery max `27015` microseconds）；记录中的完整 dmesg 没有 VGLite timeout/error、
DRM error 或 blocked task。

这次数据同时界定了当前 SHM 模型的性能边界，而不是把“全部 PASS”误称为任何场景都高帧率：

| workload | round 1 / 2 / 3 average callback | 解释 |
| --- | --- | --- |
| `XR24` full | 45.74 / 46.03 / 45.75 ms | 全屏不透明动态 SHM 稳定，但低于面板每帧约 23.95 ms 的节奏。 |
| `AR24` full | 66.76 / 68.05 / 68.40 ms | alpha blend/full upload 明显更贵；稳定性通过，不是全屏动画性能目标。 |
| `XR24` 64×64 | 24.09 / 24.23 / 24.17 ms | 小 damage 已接近面板 vblank 周期，是普通拖动的正确基线。 |
| `AR24` 64×64 | 24.56 / 24.64 / 24.81 ms | alpha 小 damage 仍接近 vblank；最大个别 callback 不能单独等同于卡死。 |

因此固定面板、Labwc、PCManFM、wf-panel、Quick Settings 和普通线性 SHM 应用的当前
XR24/AR24+dumb-buffer+SHM-upload 路径已获得真实板端持续提交/release/page-flip 的证据；但它仍
不能证明 Qt/Chromium/视频/相机的 GPU-only DMA-BUF、explicit fence 或 fullscreen direct scan-out
已经实现。Gate 1 还应在有人观察面板时继续覆盖 PCManFM、wf-panel、Quick Settings 的人工
拖动/展开/折叠与长时间运行；任何全屏闪烁、边缘图案、profile breaker 或 PID 替换均为失败。

最近一次桌面侧观察中，先前全屏计数器事务后出现的边缘图案已经自行消失；这只能说明当前
可见状态恢复正常，不能把它倒推为 renderer 或 page-flip 的永久正确性证明。为避免在无人
观察时重新引入同类可见残留，`tdvp-display-smoke` 的候选除了 wall-clock 报告外，还将
plane detach 后的 buffer release 收紧为“matching page-flip event 加一个独立后续 vblank”。
候选仅完成构建、哈希核验和可回退部署，未为刷新统计字段再次运行 direct-KMS counter。
它将旧的 `elapsed_seconds` 改为 `nominal_seconds`，并输出
`wall_elapsed_ms`/`achieved_fps`；下一次维护模式验收必须在观察面板的条件下记录这些字段。

Gate 1 的 VGLite 数据也必须与这一次 workload 的时间边界绑定，而不是把整个登录期的旧
renderer log 混进平均值。候选新增只读的 `/usr/bin/tdvp-vglite-diagnostics-report`：它只解析
`TDVP_VGLITE_DIAG`，汇总新记录的 upload bytes、damage/copy 次数、`vglite_finish` 的
min/avg/max 耗时和 clip/blit 数，也汇总 state-recovery 尝试/失败次数。读回路径若在
`vg_lite_finish()` 中失败，还会写入 `texture_readback_finish` quarantine record；报告会把它与
`result != 0`、`pass_ok=0`、`state_recovery_ok=0`、恢复字段缺失或不可能的
`attempted=0, ok=0` 组合一律作为失败。它不打开 `/dev/vg_lite`、不取得 DRM master，也不控制
Labwc。下一次**有人观察面板**的验收应先由
root arm 一次性诊断 marker，再启动一个新的 VGLite 会话；登录用户随后在同一个 Labwc
`XDG_RUNTIME_DIR` 中运行 Gate：

```sh
sudo /usr/local/bin/tdvp-renderer-profile diagnostics-next-vglite-session
# 退出并重新进入一个 VGLite profile 会话；marker 只对这一轮 VGLite session 生效。
/usr/bin/tdvp-vglite-session-gate --expect-vglite-compositor \
  --width 1232 --height 568 --format xr24 --damage-size 64 --frames 120 --repeat 3 \
  --diagnostics-log "$XDG_RUNTIME_DIR/tdvp-labwc.log"
```

Gate 在启动 workload 前只记录该日志已有的行数；完成后报告器只解析新增行。因此没有启用
一次性诊断、没有新的成功 `vglite_finish`，或诊断本身报告错误，都会使 Gate 失败而不是制造
“无数据也 PASS”的结论。每个计入 `--require-page-flip-transition` 的 page-flip 还必须消费同一
`submitted_buffer` 的一个未消费、成功的 `vglite_finish` 记录；仅有无关 finish 和无关 page-flip
的混合日志同样会失败。完整的原始 log 仍要和 Gate stdout/stderr 一起保留；该汇总不是
page-flip 生命周期或 fence 正确性的替代证据。

`tdvp-vglite-client-churn` 与 `tdvp-vglite-inflight-close-gate` 保留为**隔离维护模式**的 driver
恢复工具：必须在 Labwc 已停止、或当前会话明确为 Pixman 且没有 VGLite owner 时运行。它们会拒绝
运行中的 VGLite Labwc，不能再被列为 live Gate 1 测试，更不能以它们的通过替代合成器持续渲染证明。
现场采集继续统一使用 `buildroot/tools/tdvp-hardware-audit.sh`；它只读记录 boot ID、watchdog、
profile、Labwc 状态、日志和 VGLite/DRM/VO dmesg，不打开 DRM master 或 `/dev/vg_lite`。

`tdvp-wayland-shm-bench` 的 `AR24` 分支也必须是有效的 Wayland client，而不仅是带有
非零 alpha 字节的压力数据：`wl_shm` 要求 alpha-bearing format 的 RGB 在电气值上已经按 A
预乘。候选已将 full-frame 和 small-damage 两条 AR24 填充路径都改为 8-bit round-to-nearest
预乘；`test-tdvp-wayland-shm-bench-format.sh` 断言该性质，且 RISC-V Buildroot 的
`-Wall -Wextra -Werror` 重编译通过。上述 AR24 full 与 small-damage 三轮真机记录进一步证明
该 client 路径已被实际执行；这只覆盖 CPU 可访问的 linear `wl_shm`，不能外推为 client DMA-BUF
import 通过。

候选镜像仍安装 `/usr/bin/tdvp-vglite-client-churn`，但它现在是隔离维护工具，而不是普通
图形登录用户应执行的 Gate 1 步骤。它只顺序执行并退出既有的 off-screen probe；自身不取得 DRM
master，也不创建 FB、modeset、atomic commit 或 page flip。它要求运行前没有 live VGLite Labwc
owner；若检测到 compositor 已持有 `/dev/vg_lite`，会拒绝运行而不是试图与它共用硬件 context。

```sh
/usr/bin/tdvp-vglite-client-churn --iterations 30 --frames 4
```

维护运行必须保存命令输出和 kernel `dmesg`；任一 iteration 失败、GPU/KMS handle error 或不能在
deadline 内完成，都是 driver recovery 维护验收失败。它不对 live compositor 的持续渲染、SHM buffer
release 或 page flip 做出结论。

该工具在启动第一个 child 前仍读取 watchdog 参数；参数缺失、为零，或比调用者的单 child deadline
更长时都会拒绝。这样外层 shell 的 timeout 不会被误当作能够打断内核无限 wait 的保护；但它绝不能
为了压力目的重新允许第二个 VGLite context。

Gate 1 在开始时固定同一个 Labwc PID；每轮 workload 后必须仍是该 PID，不能以“另一个同样导出
VGLite 环境变量的 Labwc”替代它。PASS 同时记录该 PID 的 `/proc` user/system CPU tick 增量与
前后 `VmRSS`，使 full-damage、small-damage、XR24 与 AR24 结果可以在同一 compositor 进程上比较。
这些数字是性能样本而不是阈值：是否优化仍须结合 `finish()`、frame callback、release 和 vblank/page
flip 时序判断。

在上述 live SHM 负载通过、boot 与 renderer-stack 备份已存在之后，异常 close recovery 注入只能
在**没有 VGLite Labwc owner 的隔离维护模式**下显式运行：

```sh
/usr/bin/tdvp-vglite-inflight-close-gate \
  --allow-inflight-close-recovery --timeout-seconds 15
```

该 gate 的第一个 child 只在离屏 dumb-buffer 上执行 `vg_lite_flush()`，使 `VG_LITE_SUBMIT` 已发生但
故意不执行 `VG_LITE_WAIT`、`vg_lite_free()`、`vg_lite_unmap()` 或 `vg_lite_close()`，随后以 `_exit()`
触发内核 file-release 路径。第二个 child 必须在 deadline 内完成正常的离屏 probe；这证明 `0058`
没有把设备永久隔离。它不直接操作 DRM/KMS，但为避免与单-context 合同冲突，运行前必须确认 Labwc
已停止且 `/dev/vg_lite` 没有 owner。recovery timeout 或第二个 probe 失败都使维护验收失败；该工具
运行期间的桌面状态不构成 Gate 1 的 live-compositor 结论。

当前锁定的 VGLite SDK 中，`vg_lite_blit()` 会对整个非空 source buffer 执行
`thead_csi_dcache_clean_invalid_range()`，而 `vg_lite_finish()` 在 GPU 完成后会对整个
render target 执行同类操作；公开的 `vg_lite_flush_mapped_buffer()` 也只能按整个 mapped
handle 发出 `VG_LITE_CACHE_FLUSH`。而锁定内核的 `VG_LITE_CACHE` 实现当前为空成功，
因此它不是可验证的 damage-range API，不能在每个小 damage 后额外调用来“优化”拖动。

renderer 只在本次 upload 后的第一个真实 texture blit 保留 SDK 的原始全 buffer clean；
紧随其后的 clip 与无 CPU 写入的静态帧不再重复它。真实 session 必须通过
`texture_cache_flushes <= texture_blit_attempts`、`copied_bytes/source_bytes`、CPU 和
`finish()` 分布证明该收敛实际发生且未损坏画面。全屏动态 SHM 仍受全 buffer clean、target
cache 维护和完整 raster 成本约束；应优先避免它，后续再以 direct scan-out/plane gate
解决，而不是猜测性跳过新 upload 的同步。

Gate 1 的 CPU/`finish()`/frame-callback 分布必须包含“小窗口拖动覆盖静态大 surface”
和“全屏动态 SHM”两种负载。若前一种场景在“每次 upload 最多一次 source clean”后仍由
cache 维护主导，VGLite SDK/内核责任域需要提供可验证的 dirty-range cache flush 或明确的
coherent allocation/import 合同；在此之前，wlroots 只可去除没有新 CPU 写入时的重复 clean，
不能跳过新 upload 的同步。

## 失败策略和验收

通用发布镜像在 Gate 0.5 及完整可复现构建通过前仍明确默认 Pixman；只有锁定 kernel、SDK、
wlroots 和 Labwc revision 的实验候选才能安装 VGLite 批准标记。当前真机候选是这个例外：
`tdvp-renderer-profile` 明确选择 `vglite`，`/etc/tdvp/labwc/vglite-enabled` 已存在，且 greetd
默认以 `tdvp` 执行 `tdvp-labwc-session`，因此开机不要求输入凭据。

图形登录模式与 renderer profile 是两条独立开关。`tdvp-graphical-login select greeter` 会原子地
将 `/etc/greetd/config.toml` 切为现有 `greeter` session；`select autologin` 则切回 `tdvp` 的
Labwc session。任选其一后执行 `systemctl restart greetd` 即生效，且该操作不改变
`tdvp-renderer-profile`、VGLite approval marker 或 circuit breaker。这使无人值守默认桌面和
保守的 greeter 回退各自可验证，不必修改 renderer 配置或重建镜像。

VGLite 不能在初始化失败后悄悄把**当前帧**切回 Pixman；这会破坏 renderer、texture、
allocator 与 KMS buffer 的所有权，也会让用户误以为已得到 GPU 加速。初始化失败仍应记录
原因并结束该 VGLite compositor process group。为避免把已认证用户送回 greeter，Labwc
session 会把非正常 VGLite exit 写入 `$XDG_STATE_HOME/tdvp-labwc/vglite.failed`；只有该
记录成功后，才以一次受控 self-exec 重启**同一登录会话**的 Pixman desktop。该 self-exec
只接受内部 `TDVP_LABWC_FORCE_PIXMAN=1`，不能强制 VGLite；它会在启动 Pixman 前移除仅供
VGLite 使用的 direct-scanout guard，因此不会重试或混用失败 renderer。若 breaker 无法写入，
会话按原始错误退出而不是冒险进入重启循环。

后续登录同样会明确解析为 Pixman。该 circuit breaker 不是隐式 GPU 成功：
`tdvp-renderer-profile status` 同时显示 configured/effective profile、批准状态和 breaker
状态，重新尝试 VGLite 必须由 root 清除失败标记。正常 logout 与 greetd/KMS maintenance
使用的预期终止信号不会触发 breaker。

对于 0059–0061 candidate 中的真实 VGLite completion 丢失，`vg_lite_finish()` 最迟在默认
5000 ms 返回失败；wlroots render pass 因而返回 false，Labwc 的 `0004` patch 在仅由
`TDVP_LABWC_VGLITE_FAILURE_RECOVERY=1` 开启的 VGLite 会话中计数该失败。三个**连续**失败
才令 Labwc 以非零状态结束 event loop，随后才由上述 session wrapper 写 breaker 并启动一次
Pixman。因此在最坏情况下该路径约需要三个 watchdog 窗口，而不是在一个正常的慢帧后任意
kill compositor；任一成功 output commit 都会把计数归零。这是 kernel 有界等待、renderer
错误返回、Labwc 退出和 session fallback 的明确责任链，仍须在 K230 以真实硬件故障注入和
Gate 1 验证，不能由 host-side sandbox 证明已经通过。

在不影响真机桌面的情况下，`buildroot/tools/test-tdvp-labwc-session-recovery.sh` 以
user-namespace `bwrap` sandbox 伪造 `setsid`、`dbus-run-session`、Labwc 和 profile helper。
它固定让第一轮 VGLite fake Labwc 返回 `77`，并断言得到一个持久 breaker、恰好一次 Pixman
self-restart、已移除的 VGLite direct-scanout guard、已移除的内部 force 标记以及可追溯的两份
session log。该主机测试不能替代硬件 Gate 1，但能防止修改启动器时退化这条恢复状态机。

当前 candidate 的该测试已在具备 RISC-V 交叉工具链且带 `bwrap` 的编译机实际通过：模拟的第一轮 VGLite
session 返回失败后，breaker 被写入；第二轮只启动一次 Pixman，且 VGLite 专用
direct-scanout guard 已移除。这个证据只覆盖启动器状态机，不覆盖 K230 GPU、page flip 或
真实桌面恢复，因此仍必须在板端 Gate 1 复验。

启用 VGLite 的最终板端验收包括：

1. `tdvp` 的 Gate 0 probe 连续运行通过；
2. Labwc、Foot、PCManFM、wf-panel-pi、Quick Settings 均仍为 `tdvp`；
3. 物理 90 度显示与触摸坐标保持正确；
4. Top-down / bottom-up Quick Settings 手势期间无全屏撕裂、无越帧堆积；
5. 记录 CPU 使用、RSS、每帧 `finish` 时间、DRM page flip/vblank 时序；
6. live SHM Gate 完成后，在无 VGLite owner 的隔离维护模式中显式 in-flight-close recovery 注入及其后续 normal probe 均通过；
7. 重启后仍为 VGLite renderer，且不依赖 root、direct KMS client 或
   Qt/GTK/Quick Settings 私有 workaround。

发布时 `vg-lite`（运行时）和 `wlroots-vglite`（compositor runtime）各有
唯一 ABI 所有者和版本化依赖；它们不能被静态塞进 Labwc 或应用程序。对应
IPK 进入匹配 K230 ABI 的 TDVP feed，固件只引用同一 release 的版本。
