# 2026-09-07 K230 实机检查与定点修复

本文保留 2026-09-07 的定点修复与测试结果，适用于下述基线及各节记录的会话。
其中的 Linux 摄像头链路和 Pixman 配置已退出当前 profile；现行分工见
[架构](architecture.zh-CN.md)。新镜像验收见 [发布契约](release-contract.zh-CN.md)。

基线为 `codex/cpu1-rtsmart-integration` 的
`8db973f4e948b9379ab729333259787632dd7626`；检查后设备已应用以下定点修复。
没有替换 nRF 固件，没有正式安装/启用相机服务，没有生成新的完整 SD 镜像。
相机已在隔离候选目录中完成实机采集，详见下文，不等于烧录镜像已具备该能力。

## CPU1：修复后实机通过

原故障：`ping` 有应答，但 `crc32 123456789` 在 Linux 侧 SIGSEGV。
mailbox ABI payload 偏移为 52（0x34），非缓存 device mapping 不支持 libc
`memcpy` 所生成的未对齐宽写入。改为 volatile byte stores，ABI、内核补丁和
RT-Smart 固件保持不变。目标反汇编确认实际使用 `sb`。

- 标准 CRC：`crc32=cbf43926`。
- 空输入、29 个长度 × 16 种源地址对齐（464 组）、4096 字节上限及非法参数拒绝通过。
- 修改前主机回归能捕获被禁止的普通 memcpy；修改后主机与实机均通过。
- 系统库替换后、UART1 DTB 重启后分别再次通过；最后一次真实应答序列为
  `468/468`，heartbeat 从 `63947` 增至 `64429`。
- Linux 仍只调度 CPU0；CPU1 是 RT-Smart 协处理器，不是第二个 Linux scheduler CPU。
  ASR/ISP 等业务 offload 没有因为基础 mailbox 通过而自动实现。

设备安装的库 SHA256：
`125b79871a3951d0e9200502ea651f4294ea3587f88e0b1febdd0b9b9b90e5a5`。
新增 `/usr/local/bin/tdvp-cpu1-acceptance`，SHA256：
`82ee8073f3ff19c61cef822357932c9b8c6daf3c1783521f5bd95c529a6324a1`。

## VGLite：有界桌面测试通过，正式配置未改为默认启用

Gate 0：BGRX/BGRA 两种离屏 workload 各 120 帧通过；单独的 in-flight-close
注入及后续正常任务恢复通过。没有在 VGLite Labwc 持有单硬件 context 时并发运行离屏客户端。

两次临时图形会话使用独立 `/run` profile/授权标记，并预设 180 秒恢复原配置。
测试后恢复原 greetd 配置。正式 renderer profile 仍为 Pixman，正式 VGLite enabled
标记仍不存在；临时测试授权没有伪装成发布验收记录。

第二次会话 Labwc PID `12892`，图面 `1232x568`，每项重复 2 轮，每轮 120 帧：

| wl_shm 格式 | 损伤区域 | 对应到成功 VGLite finish 的 page-flip | 结果 |
| --- | --- | ---: | --- |
| XR24 | 全屏 | 281 | PASS |
| XR24 | 16×16 | 259 | PASS |
| AR24 | 全屏 | 297 | PASS |
| AR24 | 16×16 | 259 | PASS |

所有轮次各收到 120 次 frame callback 和 120 次 buffer release，Labwc PID 不变。
GPU result/pass/recovery/readback 和 DRM commit failure 计数均为 0。
这些 workload 是 XDG toplevel，不替代交互式 layer-shell 窄损伤/视觉验收。

第一轮 AR24 的日志关联错误暴露了验收工具的取样边界问题：GPU finish 可能在
`--skip-lines` 之前而 page-flip 在之后。修复只保留历史未消费 finish 与 before
snapshot 作为上下文，不把旧 finish 计入新帧要求；已消费的旧 finish 仍不能复用。
另修复 awk 字符串比较导致耗时 extrema 错误，以及环境变量名
`TDVP_VGLITE_DIAGNOSTICS` 被误当作诊断记录的问题。新增对应回归，未修改
VGLite kernel、wlroots renderer 或 Labwc recovery patch。

原始日志保存在检查工作树 `.tmp/device-validation/vglite-session-20260907.log`
与 `vglite-session-20260907b.log`，不作为仓库源码提交。

### 补充：真实 layer-shell 会话验收

在 `cca890e` 基础上给同一个 SHM benchmark 增加 `--surface-role layer-shell`，
不是用普通 XDG 窗口模拟 panel。协议 XML 原样取自镜像已有的 gtk-layer-shell
v0.8.0，保留许可；由构建主机的 wayland-scanner 生成客户端代码，不增加 GTK
依赖或构建时下载。默认仍为 `xdg`。overlay 不抢键盘焦点，输入区域为空，
不预留桌面工作区域；若 compositor 给出不同的非零尺寸则拒绝通过。

实际 Linux CPU0 工具链/sysroot 在 Ubuntu 24.04.4 中以
`-Wall -Wextra -Werror` 交叉编译通过。候选 benchmark SHA256 为
`7fa4f9f0cce3595b5243d5de43a38f7e31591656791c5d74eb3de6b3ba900aaa`。
session Gate、diagnostics 和 renderer stack lock 回归均通过。Gate 测试在临时目录
按镜像的 0755 安装权限运行 helper；
该回归现已纳入 PR workflow。

临时 VGLite 会话 Labwc PID `6895`，layer-shell overlay 为 `1232x568`，
每项 2 轮 × 120 帧：

| 格式 | 更新区域 | 两轮平均 frame callback 间隔（ms） | GPU finish 关联 page-flip | 结果 |
| --- | --- | --- | ---: | --- |
| XR24 | 全屏 | 26.658 / 26.928 | 257 | PASS |
| XR24 | 16×16 | 24.390 / 24.320 | 259 | PASS |
| AR24 | 全屏 | 82.190 / 78.711 | 304 | PASS |
| AR24 | 16×16 | 26.538 / 26.441 | 261 | PASS |

所有轮次均 `submitted=120 callbacks=120 released=120`，PID 不变，GPU result、
pass、recovery、readback 及 DRM commit failure 计数均为 0。page-flip 统计包含
采样时段的其他桌面更新，不把它误报为 benchmark 独占帧数。XR24 第一组 RSS
18560→18648 KiB，其后三组为 18648→18648 KiB；这是短期有界测试，不能证明
长期无泄漏。最后默认 XDG 320×240、60 帧回归也通过。

**功能通过不是 60 FPS 性能验收**：AR24 全屏透明合成的回调间隔约 79–82 ms。
这次覆盖了真实 layer-shell 生命周期与局部更新路径，但没有像素截图对比，
不代替肉眼确认局部更新无残影、真实 Quick Settings 交互或 swaylock 解锁验收。
没有改动 VGLite kernel、wlroots renderer 或 Labwc runtime patch，也没有把
测试授权标记提升为正式 VGLite enabled 标记。

示例命令（仅在已授权的 VGLite 桌面会话中，以图形用户执行）：

```sh
tdvp-vglite-session-gate --expect-vglite-compositor \
  --surface-role layer-shell --width 1232 --height 568 \
  --format ar24 --damage-size 16 --frames 120 --repeat 2 \
  --diagnostics-log "$HOME/.cache/wayland-runtime/tdvp-labwc.log"
```

测试前预设 180 秒自动回退。完成后备份日志、主动恢复并逐字节确认原 greetd
配置，正式 profile 为 `configured=pixman effective=pixman vglite_enabled=no
breaker=clear`，Labwc PID `8078` 正常运行，再取消本次回退 timer。
候选客户端仅放在 `/run/tdvp-vglite-validation/`，未覆盖系统正式客户端。
原始日志为 `.tmp/device-validation/vglite-layer-shell-20260907.log`。
恢复后 CPU1 验收再次通过，序列 `935/935`，heartbeat `204256→204739`。

## nRF52840：UART1 已修复，无 AT 应答，蓝牙未通过

原 DTB SHA256：`6f02a5381f962d09b60a94ee234a3978e0f45123cefae3f305d267f90551aec5`，
与局域网构建基线一致。新 DTB：
`f9a8a0934bf3b0b5e86e36675f32ff7ad36113cdd9520d23fb14cb19f25e733e`。
`0065` 只启用 serial1/UART1、GPIO3 TX 和 GPIO4 RX，保留 UART0 Linux、UART3 CPU1。
最终 DTB guard 校验真实 phandle、pin function 和节点状态；6 个 DTB 属性破坏回归均被拒绝。

设备重启后确认 `ttyS1` 对应 `0x91401000`，pinctrl 显示 GPIO3/4 为
`91401000.serial` 持有的 alt3。两次 `AT+VER?` 的 TX 累计 18 字节，但 RX 始终为 0。
不能据此宣称 nRF 已识别，也不能仅凭 UART 存在宣称 BlueZ 蓝牙集成完成。

官方 nRF52840 AT 固件是 BLE/Meshtastic 应用桥，不是 HCI controller。
`MESHADV=OFF` 仅关闭 advertising，不等于关闭整个蓝牙无线电；没有将其包装成
完整 Bluetooth power switch。未执行 scan、pair、reset、DFU 或固件更换。
桌面现有 panel 配置也没有 Bluetooth widget，后续须与真实 backend 能力一起实现。

## LoRa：芯片识别通过，状态上报修复，RF 收发未验收

在原 profile 为 `lora`、电源为 `off` 时运行 `vpl-lora-probe`：
`command_sent=1 response_valid=1 firmware_major=7 firmware_minor=17`，
结束后恢复 `lora/off`。没有发射 LoRa 数据包。

原后台将 transport/driver/runtime 固定为 0，而 Quick Settings 单独把 available
置为 1。新增统一的只读状态采集，区分扩展坞连接、SPI/driver、已安装 runtime、
profile 所有权、电源及独立 acceptance。无后台自动 probe、无电源或 GPIO 写入。
实机候选程序返回 transport/driver/runtime/available=1，enabled=0，state=off，
acceptance=unverified；随后部署到状态服务。未生成 RF acceptance pass 标记。

## 摄像头：芯片识别和基础帧采集已通过，正式镜像集成未完成

用户确认原配 GC2093 及排线已连接。初次检查只有 `/dev/video0`，其 name 为 `mvx`；
无 `/dev/media*`。烧录镜像的 DTB 没有 GC2093 配置、I2C4 disabled，默认 sensor
仍为 OV5647。VVCAM 模块和 ISP runtime 文件虽存在，但没有形成采集链路。

另确认 SDK 的两个 ISP server 二进制均含 RVV 指令，例如
`CamDeviceSensorIsiGetMetadataWindow` 的 `0x7f1f6: vsetivli`，该函数入口没有
CPU ISA 分派保护。这是 CPU0 兼容性风险，不只是可忽略的 ELF 属性标签；尚未
在设备上启动相机去触发该路径，也没有声称已经观察到 ISP 的 SIGILL。
需要取得兼容 CPU0 的 ISP 构建/源码，或另行评估将 camera/ISP 放入 CPU1 的
ownership、内存与应用桥方案，不能通过添加 V4L2 节点掩盖这一点。

后续审查找到新的 Linux CPU0 候选：官方 `main` 的 v0.6.1 ISP
（`155359af`）为标量 RV64GC，可执行代码段未发现向量指令，下载文件也与固定
Git blob 一致。它与当前 sensor 回调表不兼容：新头文件插入四个 flip 回调，
但 API version 未递增。已新增隔离的旧 ABI 注册适配层，其九个回调槽位和浮点
参数传递在 Ubuntu 24.04 与真实 CPU0 上均通过模拟传感器测试。
这使“不迁移 CPU1、继续 Linux 采集”的路线可以继续验证。该阶段只完成 ABI
测试，没有替换设备 ISP、改相机 DTB、启动采集或纳入镜像。详见
[CPU0 camera ISP integration work](../user-space/tdvp-camera-isp/README.md)。

### 后续实机进展：GC2093 返回 0x2093

新增固定 I2C4 的访问层，校验 adapter 的物理 `of_node`，只访问 `0x37`，
不扫描其他总线、不使用 `I2C_SLAVE_FORCE`。九个成功/失败路径的回归在
Ubuntu 24.04 和真实 CPU0 均通过；完整插件动态加载、API version 导出、
旧 ABI 注册及 GC2093 模式枚举也通过，没有在此测试中配置传感器。

独立候选 DTB 保留触摸 I2C0、扩展坞/键盘 I2C1，将物理 I2C4 固定为 adapter 4，
启用 CSI2、GPIO7/8 I2C、GPIO13 MCLK1 和 GPIO21 reset。实际 DTB 比对在解析
phandle 后确认原有 268 个节点除明确列出的相机属性外保持一致。

第一版单节点 composite 时钟在实机错误地报告 594 MHz；发现后卸载模块、关闭
门控，没有继续芯片或采集测试。原因是 SDK mux 的 `determine_rate` 优先于
divider 的 `round_rate`。第二版将 mux/gate 和 divider 分开，未修改共用时钟
驱动或 PLL。MIPI 模块新增启用前/后的频率检查，错误频率在启用前以 `-ERANGE`
拒绝的路径也经过实机验证。

最终候选在设备上两次读取成功：

```text
GC2093 chip ID: PASS controller=91409000 bus=4 address=0x37 id=0x2093
sensor MCLK: 23760000 Hz
```

GPIO21 对应 gpio-533，由 reset consumer 持有并输出高电平。仅以 pinmux 的
UNCLAIMED 判断 GPIO 是否工作会误判；GPIO ownership 与 I2C 应答都已核对。
候选 DTB SHA256 为 `2d273d6f9871f000922db3c8ab723d6ae48db40b42c156411c92dc85c6ac4ce2`。
所有五个 VVCAM 模块已交叉编译，但实机仅加载 MIPI 模块用于时钟/复位，未加载
其余 ISP/VB/video 模块，未启动 ISP server 或采集。

两次诊断启动均自动把 SD 卡的启动 DTB 恢复为原 UART1 修复版
`f9a8a0934bf3b0b5e86e36675f32ff7ad36113cdd9520d23fb14cb19f25e733e`。
当前内核运行候选 DT，下一次重启回到原 DT。测试结束卸载 MIPI，时钟
prepare/enable 计数均回到 0、物理 mux gate 关闭，未安装正式相机服务或验收标记。
CPU1 再验收通过：`467/467`，heartbeat `15897→16377`；greetd、触摸和键盘状态正常。
原始日志：`.tmp/device-validation/camera-chip-id-20260907.log`。

上述芯片识别阶段的结论是：**硬件识别不等于 ISP 帧采集或新镜像集成完成**。

### 后续实机进展：Linux CPU0 连续获取 1080p 图像

基础 ISP event / VB ioctl ABI 比对通过。设备 CMA 为
`0x20000000–0x3fffffff`，与 CPU1 的 `0x10000000–0x13ffffff` 不重叠；
实际使用 `enable_cma=1`，没有改为固定 MMZ，也没有启动 CPU1 MPP。
加载完整候选模块后，`/dev/video1–4` 才是真正 VVCAM 节点，`/dev/video0` 仍为 mvx。

首轮采集暴露了两个额外问题：旧 ISP 直接改写所有 sensor MCLK；VVCAM 完帧的
时间戳和公开帧序号均为零，导致 FFmpeg 不断丢帧。已停止该轮并诊断重启清理时钟状态。
`0003` 利用旧 ISP 已存在的 `mode.clk=0` 分支跳过用户态时钟写入，保留内核受保护
的时钟供应；`0004` 在完帧路径添加单调完成时间戳和每次 stream 的帧序号，保持
ISP 私有 buffer index 不变。未改写 ISP 二进制、GC2093 寄存器表、曝光/增益算法。

之后在 `DevicePolicy=closed`、**不允许 `/dev/mem`** 的隔离 ISP 服务中，两次
60 帧实测分别为 30.006 / 29.927 FPS，1920×1080 NV12，每帧 3,110,400 字节，
序号均 0..59，时间戳严格递增。已将真实 NV12 帧转为 PNG 并检查，能看到键盘、
桌面和线缆，不是黑图或测试图；图片保存在本地诊断目录，没有提交至 Git。
FFmpeg 另一次正常采集三帧退出，PTS 为 0 / 33972 / 66658 微秒，三个完整帧 hash 不同。
`VIDIOC_G_PARM` 未实现的告警仍存在，未将其声称为完整 V4L2 接口验收。

卸载时的资源告警进一步定位为：V4L2 软件子设备登记的整段 ISP MMIO 包住了
真正 ISP/MIPI 的资源，卸载后导致后者的 managed release 找不到资源。`0005`
只删除软件子设备没有使用的 MMIO/IRQ 声明，保留真实驱动的资源保护。
最终五补丁队列完整重编后，两次“加载 → 60 帧采集 → 停止 → 全部卸载”均通过：

| 轮次 | 帧数/序号 | 完成时间戳（微秒） | 实测 FPS | 内核生命周期告警 |
| --- | --- | --- | ---: | --- |
| 1 | 60 / 0..59 | 944171454..946137775 | 30.005 | 无新增 |
| 2 | 60 / 0..59 | 953463834..955429962 | 30.008 | 无新增 |

每轮时钟寄存器在采集前后均为 `0x003a7709`，卸载后为 `0x003a7708`，门控确实关闭。
两个 capture service 都返回 `0/SUCCESS`。最终 subdev module SHA256：
`7db704f19d36f5e1a6841d310acc5d40f55700a86e1b5b8064d38989c0668768`；
插件 SHA256：`df65b714eeac692cd7f3b1cea863d0a7807f71f01fa1e26cbf57b8473f6d4977`。
原始日志位于 `.tmp/device-validation/camera-runtime-v4/`；详细修复和构建身份见
[相机候选说明](../user-space/tdvp-camera-isp/README.md)。

测试后未留下 ISP 服务、摄像头模块或正式 acceptance 标记。SD 卡已恢复 UART1
基线 DTB，临时恢复 unit 已移除；当前运行的相机候选 DT 只维持到下一次重启。
相机测试阶段 CPU1 多次重验通过，最终一次序列 `1401/1401`、heartbeat `114747→115228`。
下一步是正式 package / DTB 队列与增量构建集成，以及 Wayland/VGLite 共存测试，
不是继续把“只存在文件或节点”当作相机已经交付。

## 正式相机包与桌面入口验证

后续实现了独立的 `tdvp-camera-isp-runtime` 和 `tdvp-camera-isp` 包，替换旧的
VVCAM/OV5647 配置。前者下载并校验官方标量 ISP，后者构建 GC2093 ABI 插件和
五个内核模块。启动前校验 ISP 哈希，systemd 只允许访问指定的相机/I²C 设备，
不允许 `/dev/mem`。post-build 清除 additive vendor overlay 遗留的 `S31canaan_isp`。
真实构建产物与最终 ext4 镜像均增加相机文件、哈希、模块和服务启用检查。

在 Ubuntu 24.04.4 中复制独立 SDK output、只读挂载原 SDK，使用真实 Buildroot
Kconfig、generic-package 和 kernel-module 后端完成 clean build、install 和
reconfigure/rebuild。不是只调用一次交叉编译器。实际 target 相机检查通过，
41 个 Linux patch、真实 reconciliation、renderer stack lock、镜像源文件合同
也通过。新增 `0066` 生成的 DTB 与先前已实测的候选 **字节完全相同**：
`2d273d6f9871f000922db3c8ab723d6ae48db40b42c156411c92dc85c6ac4ce2`。
真实 DTB 的相机 guard、10 个负向变异及 UART1 guard 均通过。

真实安装产物在设备独立目录中测试，使用 `/run` 下的正式 unit 和只读 bind mount，
没有覆盖设备原系统文件或改写 SD boot DTB。最终两轮分别取得 60 帧、序号 0..59，
1080p NV12 实测 **29.956 / 30.013 FPS**。停止 module unit 会先停止 ISP，
全部模块卸载、MCLK 门控关闭，没有新增内核生命周期告警，greetd/Labwc 没有重启。

该历史候选的桌面入口通过查询找到真实 VVCAM capture 节点。
原有 MPV 参数覆盖了 low-latency 的 `fflags=+nobuffer`，并在关闭相机后继续归还
预读缓存，出现 descriptor/ownership 警告。最终入口保留低延迟选项、禁用线程预读，
将全分辨率采集转换为 640×360 BGRA 预览。以 `tdvp` 用户执行真实启动器，
完成 90 帧 Wayland SHM 播放，退出码 0，未出现上述缓冲区警告或 MPV error 级日志。

**限制仍明确保留：** 这不是完整镜像/开机验收，也不是 VGLite 共存或像素级验收。
预览播放阶段约 6.3 秒，不宣称 30 FPS；ISP 在慢消费者期间仍有 dequeue ret=16
日志，`VIDIOC_G_PARM` 未实现告警也仍存在。长期稳定性、缺失摄像头、进程重启
和正式镜像启动还需验证。测试结束已移除临时 units/udev 规则并卸载所有相机模块；
CPU1 仍 ready、序列 `1401/1401`，当前设备的持久化系统文件保持原状。
原始日志保存在 `.tmp/device-validation/camera-package/`。

## 主机验证与回退

局域网主机的独立 Ubuntu **24.04.4** 容器（Python **3.12.3**）通过 CPU1、LoRa、
VGLite diagnostics、真实 patch reconciliation、renderer lock、CPU0 DTS、镜像源文件
和 Wayland idle/lock contract 回归。40 个 overlay patches 结构校验通过。
设备重启后可见 swayidle 的 timeout 300 锁屏、timeout 330 关屏、resume 开屏参数，
并已出现 swaylock 进程；密码输入/解锁交互没有自动化代用户执行。
使用实际 SDK GCC **14.1.1**/sysroot 完成 hardware package 所有目标的交叉编译。
这不等同于完整 Linux/SD image 构建。

设备原文件保存在 `/var/lib/tdvp-repair-backups/20260907/`：原 CPU1 库、
`k230-canmv-rm69a10.original.dtb`、`vpl-hardwared.original`、`vpl-hwctl.original`、
`tdvp-vglite-diagnostics-report.original`。修复后的日志验收工具也已安装到设备。
boot 分区另保留 `k230-canmv-rm69a10.pre-uart1-20260907.dtb`。
实际 `k.dtb` 是到命名 DTB 的符号链接；没有误修改 rootfs 下的 `/boot` 空目录。
