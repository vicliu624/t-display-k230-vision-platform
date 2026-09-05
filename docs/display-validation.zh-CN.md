# 显示验证

镜像通过 K230 DRM/KMS 路径验证内置 RM69A10 面板。固定面板的受控候选桌面使用
`DSI-1` 连接器、输出旋转 `90`、逻辑尺寸 `1232x568`；当前板端会话已经实测为
`WLR_RENDERER=vglite`。Pixman 是发布/故障恢复基线，而不是把普通 SHM 通过结果
自动解释为 VGLite 的依据。

## 镜像检查

镜像断言会检查下列已安装内容：

- 通过 greetd 和认证后的 Labwc 会话建立的 `/dev/dri/card0` 会话契约。
- `seatd`、`labwc`、`swaybg`、`wf-panel-pi`、其上游 `wfplug-*` 模块、`pcmanfm`、
  `foot` 和 `wlr-randr`。
- 包含 K230 输出参数的 `/etc/tdvp/labwc/environment`。
- 启动 Swaybg、PCManFM 桌面处理与 wf-panel-pi 的 `/etc/xdg/labwc/autostart`。
- GT9895 的 libinput 校准规则。

构建完成后在主机执行：

```sh
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

## 设备检查

在设备上执行：

```sh
systemctl status seatd greetd
pgrep -a labwc
cat /sys/class/drm/card0-DSI-1/status
```

Labwc 把桌面送到面板上。Raspberry Pi 维护的 wf-panel-pi 与其上游插件提供应用
菜单、状态模块和窗口列表，Foot 提供终端恢复路径。

发生 compositor 异常时，请在重启桌面前采集当前和上一轮 Labwc 会话日志：

```sh
cat "$XDG_RUNTIME_DIR/tdvp-labwc.log"
cat "$XDG_RUNTIME_DIR/tdvp-labwc.log.previous"
```

会话启动器只保留一份上一轮日志，并记录 compositor 进程组的退出状态；这样 libinput、
wlroots 和客户端启动错误不再依赖登录 VT 仍然可见。

## 维护模式 KMS 验收

`tdvp-display-smoke` 会取得 DRM master 并直接 modeset/atomic commit，**不能**在
greetd、Labwc 或图形桌面运行时执行；它不是桌面健康检查。它的 systemd 包装器会在
检测到上述进程时拒绝运行。需要验证 page-flip 时，使用单独命名的维护事务：

```sh
sudo systemctl start tdvp-kms-maintenance
sudo systemctl --no-pager status tdvp-kms-maintenance
sudo cat /run/tdvp/acceptance/kms-maintenance.status
sudo cat /run/tdvp/acceptance/kms-maintenance.log
sudo cat /run/tdvp/acceptance/kms.status
sudo cat /run/tdvp/acceptance/kms.log
```

该手动启动的维护服务会记录是否由它停止了 greetd，等待 Labwc 退出，先执行有界的
双 dumb-buffer `XR24` page-flip 测试，再在同一个维护窗口以内、直接 KMS client 已关闭后运行
120 帧的 vblank observer。这个刻意隔离的事务里没有图形客户端持有 `card0`，打开 primary node
可能自然成为 DRM master；包装器因此显式启用“仅维护模式”的例外。observer 在默认情况下仍会拒绝
DRM master，且从不 modeset 或 page-flip。前者验证“事件到达前不重写旧 framebuffer”，后者验证
`enable_vblank → VO IRQ1 → drm_crtc_handle_vblank` 在测试结束后仍保持有界 cadence；任一步失败
均为维护验收失败。服务通过 `ExecStopPost` 在成功与失败两种情况下恢复 greetd，恢复不依赖
SSH 客户端保持连接。常规桌面验证只检查 `greetd`、Labwc 和客户端是否存活，不直接打开
`/dev/dri/card0`。`tdvp-kms-maintenance`、`tdvp-kms-acceptance` 与 recovery helper 的
`--help`/`-h` 和未知参数均是纯 CLI 路径，不会创建状态、停止 greetd 或取得 DRM；直接运行
也会被拒绝。只有 systemd unit 注入的 maintenance marker 才能进入会临时隔离图形会话的事务，
两个 unit 名称均路由到同一条受保护的维护路径。

一次已经完成的真机维护事务证明 page-flip 生命周期：`XR24` 双 dumb-buffer 计数器提交并
释放了 300 帧，sequence 为 `44042..44965`、step `3..5`，submit-to-event 为
`0.239/7.796/23.279 ms`，之后 120 帧 vblank observer 的 event interval 为
`23945/23948.4/23952 us`。旧版计数器把 `300 / --fps` 的**名义** `10` 秒写成
`elapsed_seconds`；实际 event span 是 `22104 ms`，即约 `13.57 fps`，其 `71..119 ms`
帧间隔包含每帧全屏 CPU 填充和 sleep，不能用来推断 KMS page-flip 或 VGLite 的提交延迟。
当前候选 `tdvp-display-smoke` SHA-256 为
`4db5c18c7c241c3a50c92fc511418cdfcbcd5a39dae7005db638746a2e733145`，已将旧字段改为
`nominal_seconds`，并新增 `wall_elapsed_ms` 和 `achieved_fps`。由于全屏计数器刚出现过
短暂边缘残留、现已自行消失，本次没有仅为刷新该统计字段再次接管 DRM；下一次只应在有人
观察面板时运行受控维护事务。

主机侧状态机回归测试不需要连接目标硬件：

```sh
bash buildroot/tools/test-tdvp-kms-maintenance.sh
bash buildroot/tools/test-tdvp-vblank-observer-maintenance-master.sh
```

## 桌面 SHM frame-callback 验收

用户正常登录 Labwc 桌面后，应使用会话包装器执行：

```sh
tdvp-wayland-shm-bench-session --frames 120 --max-frame-ms 250
```

若由 root 经 SSH 做远程测量，必须显式指定桌面账户：

```sh
tdvp-wayland-shm-bench-session --user tdvp --frames 120 --max-frame-ms 250
```

包装器会定位存活的 Labwc 进程，读取其真实 `XDG_RUNTIME_DIR`，检查 Wayland
socket，并在由 root 启动时降权到该会话用户。它不会假定目录一定是
`/run/user/<uid>`：TDVP 的 greetd 会话刻意使用私有的用户 runtime 目录。已经处于
正确桌面环境时仍可直接运行底层客户端。

工具会临时创建一个 320×240 的 XDG 顶层窗口，在两个线性寻址的 `wl_shm` 缓冲间交替
提交，并等待 120 个 compositor frame callback。默认是 `XR24`
（`WL_SHM_FORMAT_XRGB8888`）；传入 `--format ar24` 后使用带非不透明 alpha 的
`AR24`（`WL_SHM_FORMAT_ARGB8888`）缓冲。client 会先要求真实 `wl_shm` global
通告所选格式，因此 alpha 路径不被支持时会明确失败。它不会打开 `/dev/dri/card0`，也
不会取得 DRM master，因此这是普通桌面健康检查，而不是维护模式 KMS 测试。成功时输出
callback 的最小、平均和最大延迟；缺少 callback、compositor 断开、缓冲释放停滞、格式
不受支持或 callback 超过设置上限时都会以非零状态退出。

每次运行会先输出 `SCHED online_cpus=... loadavg=...`，必须与结果一起保存：在单核系统
已经饱和时得到的 liveness PASS 不等于桌面性能验收。发布基线应关闭独立的 CPU 密集客户端，
连续运行至少三次 120 帧采样，并同时保留三行 `SCHED` 和最小/平均/最大延迟。`250 ms`
上限只是故障探测阈值，不是最终交互延迟预算；最终预算只能在 direct KMS page-flip 验收通过后，
基于干净、可重复的真机样本确定。

普通 SHM PASS 只证明固定面板上经 Labwc 的 SHM 工作流存活，并不自行识别所选 renderer；
VGLite 结论必须同时通过下述 Gate 1 的真实 Labwc 环境、profile 与 `/dev/vg_lite` 所有权检查。
它也不覆盖 client DMA-BUF import、显式 fence、modifier 协商、HDR/色彩管理或 direct scan-out；
这些仍是独立的验收域。

### 实验性 VGLite Gate 1（仅限获准的候选会话）

通用发布镜像以 Pixman 为回退基线，因而不能把普通 SHM benchmark 的 PASS 写成 VGLite 结论。
只有受控 candidate 已由 root 创建 VGLite approval marker、以 VGLite profile 新建图形会话，且未
触发会话熔断时，才由**该图形登录用户**运行：

```sh
tdvp-vglite-session-gate --expect-vglite-compositor \
  --width 1232 --height 568 --format xr24 --damage-size 0 --frames 120 --max-frame-ms 1000 --repeat 3
```

该命令在执行前后都读取真实 Labwc 环境，要求 `WLR_RENDERER=vglite`、三次连续渲染失败
后退回 Pixman 的恢复开关已启用，并要求 VGLite 会话禁用 direct scan-out；随后才运行该
会话的双 buffer `wl_shm` benchmark。包含内核 0059–0061 patch 的 candidate 还必须在首轮负载前
及每一轮 SHM 后只读检查已加载 VGLite module 的
`/sys/module/vglite/parameters/infinite_wait_watchdog_ms`，获准默认值必须精确为 `5000` ms。
这只是运行态身份和配置检查，不会故意制造 GPU hang。它不改变 renderer profile、不启停
compositor、不打开 DRM/KMS 或 `/dev/vg_lite`。`--repeat 3` 会将选择的 SHM workload 连续运行三轮，
并在每轮后重新验证 Labwc、profile 和 watchdog 参数；必须分别以 `--format xr24` 和
`--format ar24` 运行，且每一种格式都覆盖全帧 (`--damage-size 0`) 与同尺寸的局部 damage
(`--damage-size 64`)。分别保存每个格式/负载组合的命令输出、`$XDG_RUNTIME_DIR/tdvp-labwc.log`
与 kernel log。watchdog 参数缺失、不可读或不匹配，以及任意一次 callback/release 超时、格式不
受支持、Labwc 退出或 profile 自动退回 Pixman 都是 Gate 1 失败，不能以重新登录后的 Pixman
画面掩盖。K230 VGLite 是单 context driver：当 live Labwc 已持有 `/dev/vg_lite` 时，第二个
无 ioctl `open()` 应返回 `EBUSY`；该检查必须与 session gate 分开执行。`client-churn` 和
`inflight-close` 仅可在 Labwc 已停止、没有 VGLite owner 的隔离维护模式运行，不能作为 live Gate 1。

### 2026-09-05：受控 VGLite Gate 1 矩阵

在固定 RM69A10 面板的受控 VGLite candidate 上，保持同一 boot、同一 Labwc PID `252`，完成了
`XR24/AR24 × full/64×64 damage × 120 frames × repeat 3`。原始 stdout/stderr、pre/postflight
profile、vblank 及完整 kernel log 保存在板端
`/var/lib/tdvp/vglite-gate-20260904T164900Z/`。12 个 round 均得到 `120 submitted / 120 callbacks /
120 released`，每次 release 都在对应 callback 前到达；Labwc 的 RSS 始终为 `18892 kB`，profile
保持 `configured=vglite effective=vglite vglite_enabled=yes breaker=clear`，watchdog 始终为 `5000 ms`。

| 格式与负载 | 三轮平均 callback（ms） | 三轮平均 release（ms） | 结论 |
| --- | ---: | ---: | --- |
| XR24，全帧 | 45.74 / 46.03 / 45.75 | 31.39 / 30.26 / 30.63 | 稳定完成，不应作为满帧率全屏动画基线。 |
| AR24，全帧 | 66.76 / 68.05 / 68.40 | 56.93 / 55.87 / 57.82 | alpha 全屏合成稳定但明显更慢。 |
| XR24，64×64 | 24.09 / 24.23 / 24.17 | 1.13 / 0.88 / 0.90 | 小 damage 拖动已接近面板 vblank 周期。 |
| AR24，64×64 | 24.56 / 24.64 / 24.81 | 0.97 / 1.06 / 1.71 | alpha 小 damage 同样接近 vblank；个别尾延迟需要在长期交互中观察。 |

postflight 120-frame vblank observer 的 `event_interval_us` 为 min `23947`、avg `23950.2`、max
`23954`，delivery interval 最大 `27015` microseconds；完整 dmesg 不含 VGLite timeout/error、DRM
error 或 blocked task。这个结果证明当前 fixed-panel、linear SHM 的合成与 page-flip 生命周期没有
在该矩阵中停滞；它不覆盖 GPU-only DMA-BUF、fence、modifier 协商、HDR、多输出或 direct scan-out。

### 2026-09-03：低负载真机基线

在已部署 `0053-tdvp-drm-canaan-page-flip-lifecycle.patch` 的 RM69A10 真机上，关闭独立
CPU 密集的 `tdvp-sdr` 后，以候选观察器和当前 Pixman/Labwc 会话连续采集三组 120 帧样本。
DRM event 时间戳的平均间隔三轮均为 `23950.2 us`（约 41.75 Hz），最大值分别为
`23977 us`、`23962 us` 与 `23978 us`；同一时段双缓冲线性 `XR24` SHM 客户端的平均
frame callback 分别为 `22851.7 us`、`22995.4 us` 与 `22815.5 us`，最大值为
`41646 us`、`36972 us` 与 `36936 us`，均返回 `PASS`。

这组结果必须与此前 CPU 密集客户端在场的样本分开解释：后者的 SHM callback 平均
`103468.9 us`、最大 `355043 us`，而硬件 event 间隔仍紧密。故该对照将“触摸事件落后/
桌面像崩溃”的现象归因为单核用户态调度压力，而不是 vblank 丢失或 page-flip 已知失败。
它仍不是独占 KMS 动态 page-flip 验收，也不能替代后续 DMA-BUF、fence 或 direct scan-out
测试。

## 被动 DRM vblank 观察器

如需只检查内核侧、且不替换现有桌面 scan-out，可在 Labwc 正持有显卡时以 root 执行：

```sh
tdvp-vblank-observer --frames 120 --max-interval-ms 250
```

观察器只在现有 card 文件描述符上提交 `DRM_VBLANK_EVENT` 请求。它分别输出 DRM event
自带时间戳计算的 `event_interval_us`，以及用户态派发 event 时单调时钟计算的
`delivery_interval_us`。这两个字段必须分开判读：前者稳定而后者抖动，说明是 CPU 调度压力，
不是硬件 vblank 丢失。它不 modeset、不 page-flip、不分配缓冲、不创建 Wayland surface，也不会
取得 DRM master；若意外成为 master 会主动拒绝运行。该工具隔离验证
`enable_vblank → VO IRQ1 → drm_crtc_handle_vblank` 路径。它不等于 CRTC pending page-flip
event 路径已经正确；后者仍必须由受控 KMS 维护验收证明。

## 被动 KMS 格式、modifier 与 fence 观察器

如需在 Labwc 持有输出时无 master 地盘点 KMS 合同，以 root 执行：

```sh
tdvp-kms-capability-observer --device /dev/dri/card0
```

该工具以只读方式打开 card，拒绝 DRM master，只进行每文件 client-capability 与
property/blob 查询；它不分配缓冲、不提交 atomic state、不 page-flip，也不改变任何
plane。输出会列出每个 plane 的 XR24/AR24、`IN_FORMATS` modifier、plane 的
`IN_FENCE_FD`、CRTC 的 `OUT_FENCE_PTR`，以及 `DRM_CAP_SYNCOBJ`/
`DRM_CAP_SYNCOBJ_TIMELINE` capability。

在固定 RM69A10 硬件上，当前 primary plane 和两个 overlay plane 都声明了 modifier `0`
（linear）的 XR24/AR24，活动 CRTC 也声明了 output fence，相关 plane 具有 input fence
property。不过 2026-09-05 的同一次无副作用观察明确得到
`DRM_CAP_SYNCOBJ=0`、`DRM_CAP_SYNCOBJ_TIMELINE=0`。这说明 plane/CRTC 的 sync-file
property 存在，却不等价于可以启用基于 timeline syncobj 的 Wayland explicit-sync；当前
内核不具备其所需 backing capability。

因此该结果只建立未来 DMA-BUF/direct-scan-out 的**格式与 KMS property 前置条件**，没有建立
完整 fence 或 protocol 合同。它更不证明 wlroots 已导入 client DMA-BUF、传递
acquire/release fence、协商 modifier 或选择 direct-scan-out plane。Kernel future work 必须实际
验证 syncobj eventfd、sync-file import/export、fence fd ownership 和 release 时机；compositor
再对每个真实 producer 完成端到端验收。
