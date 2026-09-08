# CPU1 远程部署验证：2026-09-08

## 结论与适用边界

**兼容当前 CPU0 启动契约的 CPU1 固件，可以通过 SSH 更新固定启动区，随后重启整板验证。**
此前把“禁止运行中热重置 CPU1”扩大为“只能让用户重刷整卡”，是不准确的。
以下是实际设备测试，不是仅构建成功；但它也不等价于新整卡镜像或整个 AI subsystem 验收。

实施前逐项核对实际 SD 分区、启动文件、桥接模块和固件 manifest；所有权契约、内存布局、
CPU0 内核/DTB、桥接模块不变，才执行这次仅 CPU1 的更新。若未来改变这些项目，必须重新
评估配对更新方案，不能直接套用固定区写入。

## 已完成的远程更新

设备实际磁盘 `/dev/mmcblk1`，512 字节扇区。CPU1 固定区域是 `[20480, 61440)` 扇区，
也就是 10–30 MiB；boot 分区从 61440 扇区开始，rootfs 从 262144 扇区开始。
每轮均备份、核对旧区、写入完整 20 MiB 候选、完整读回比较，再执行整板 `systemctl reboot`。
这是整板软件重启验证，**没有声称完成断电冷启动测试**。

没有写 GPT、SPL/U-Boot、环境区、boot 分区或 rootfs 分区；没有热重置/重新授予 CPU1、
释放未知 DMA 缓冲区、修改共享 PLL、刷写 nRF52840 固件。

| 项目 | SHA-256 |
| --- | --- |
| 实际 boot 分区（80 MiB，更新前后全量一致） | `082ad56db2decfc1f5dbe4874c674fe8b7dfddef09177b7dab8575437cb3feb8` |
| 前 10 MiB 启动区域备份（更新前后全量一致） | `76015f6e72d0bfcb89c3faa31fbe460e5288354a1791fc35cc52108cb84e4db7` |
| 实际 Linux vision 桥接模块 | `fbe0e67c956830bed0dd4111e04150435fc74607509e182322d43b3eda705713` |
| 03201ab raw CPU1 固件 | `9b00a8dbc3a8af3dd06fb10a9ec8fccd90a4b0195e54f52bdc64375481cdc483` |
| 共享映射修复 raw CPU1 固件 | `6dcd533cbb6fdac1521e8f396ba631aa31482e69b942a412dc74492344e52d45` |
| 增加采集阶段诊断的 raw CPU1 固件 | `8f8c6299425fefa1ec9f3b71155b96afbc0ad44588c7d816a1d2bda813028233` |

设备上的回退备份分别保存在 `/root/tdvp-cpu1-03201ab.3X68VR`、
`/root/tdvp-cpu1-shared-map.B7kjYd` 和 `/root/tdvp-cpu1-capture-trace.3ipAbG`。
本地 `.tmp/device-validation/remote-ai-*` 同时保留候选、校验身份和单次部署脚本。
这些脚本绑定具体旧 boot ID、旧哈希和设备布局，不是通用更新工具，不能再次盲目运行。

## 测试发现与修复

### 1. 03201ab：内核初始化恢复，worker 仍未上线

从旧卡 I2C4 clock 阶段停滞，推进到 `ownership_state=ready`、
`startup_stage=complete`、`startup_result=0`。CPU1 内核心跳持续推进，SSH 和 greetd 正常恢复。
但 `vision_state=pending`，共享 producer magic 一直是零。

这只证明内核初始化和进程创建成功；`lwp_execve()` 返回 PID 不能证明 worker 完成启动。

### 2. 共享映射修复：worker 已在板上进入 idle

实际固定 SDK 的 `kd_mpi_sys_mmap()` 使用 `/dev/mmz_userdev`；其
`mmz_userdev_mmap()` 要求已分配的 MMZ 块或显式映射白名单。
worker 却用它映射独立于 MMZ 的 ownership/control/frame transport 区域。
反汇编确认拒绝路径；设备只读检查显示 `map_mmz_list` 的 next/prev 都指向自身，即白名单为空。

新 `tdvp_cpu1_shared_map()` 只接受协议规定的三个完整固定区域，使用固定 SDK
`rt_dev_mem.c` 提供的 `/dev/mem` + `O_SYNC` 非缓存映射；映射完关闭描述符。
实际摄像头 MMZ 帧映射仍使用 MPI，没有扩大 Linux 可访问的范围。

更新后 boot ID `eb22af5c-7927-4bfc-9888-0c205db9c6fe`：

```text
ownership_state=ready
vision_state=idle
startup_stage=complete
startup_result=0
producer_magic=0x31535654
```

worker 心跳从 `0x913` 增加到 `0x11ab`，证明循环实际运行。

### 3. 真实帧测试失败，不能宣称摄像头或 AI 可用

手工 Linux 桥接探针请求 30 帧、总预算 15000 ms，没有 V4L2 或 Linux 摄像头接管。
收到 **0 帧** 后返回 I/O error；关闭 reader 后为：

```text
ownership_state=ready
vision_state=stale
vision_error=-110
reader_open=0
captured=0
published=0
```

CPU1 内核仍有心跳，但 worker 在开始采集后停止推进。没有再次发起采集、强制清池或热重启。
新增的可选采集阶段诊断位于 producer 预留字 `[0..1]`（版本、阶段），由同一个 CPU1 worker
单写，用于定位实际阻塞调用；不改变数据面 ABI，也不伪造心跳或放宽超时。

诊断固件实际启动后 boot ID `8da6899f-212c-4a40-9d98-aefa42feadb5`，再次先进入 idle，
再执行同一个有界探针。仍为 0 帧超时，实际 producer state 为 2（STARTING），
`0x1dff0044=1`（诊断版本），`0x1dff0048=8`（`TDVP_CAPTURE_VICAP_INIT`）。
因此已定位到 **`kd_mpi_vicap_init()` 尚未返回**，尚未调用 `kd_mpi_vicap_start_stream()`。
传感器信息查询不等于已经读到实物 chip ID，不能据此前置步骤通过宣称相机硬件识别成功。

## 构建与回归范围

- 两个新候选都通过 Ubuntu 24.04 容器中的生产 `build-rtsmart.sh`，真实 MPI、RT-Smart、
  OpenSBI 链接，以及实际 CPU0 DTB 配对检查；不是只编译一个替代 stub。
- 新共享映射回归检查三个固定范围、O_SYNC、拒绝越界/MMIO/MMZ、错误和描述符清理；已接入 CI。
- 采集回归使用固定 SDK 头文件，检查阶段发布和既有 25 个生命周期/故障用例。
- Ubuntu 24.04 的 transport、55 个 Linux ownership 拒绝路径、状态观察器/解析器、
  Linux frame probe 全部回归通过。输出中的模拟三帧是 host fixture，**不是硬件帧**。
- 旧 WSL 编译器因缺少 C++ `<charconv>` 无法完成状态解析测试，该项已在 Ubuntu 24.04 重跑通过。
- greeter 政策修复通过 Ubuntu 24.04 的 92 个实际 image source assertions、17 个 manifest
  requirements、9 个拒绝变体，以及 renderer-stack-lock 和 VGLite session-gate 测试。
  旧 WSL Python 3.6 不支持测试使用的 `subprocess.run(text=...)`，该项没有用旧环境结果替代。

## 未通过项

尚未获得 GC2093 真帧；没有 KPU 模型推理、AI2D/FFT 数值或语音识别验收结果。
因此还不能交付为“AI subsystem 已修复”。完整新镜像仍需构建后验证整卡启动和交互。

此次重启另外发现 greeter 的 labwc 环境为 `WLR_RENDERER=pixman`：旧 greeter 启动脚本
显式覆盖了全局 VGLite 设置，生产 image guard 也仍要求该旧值。这不是 CPU1 固件更新造成，
但不符合用户要求。已将 greeter 改为 VGLite-only、禁止 direct scanout、清除软件回退/加载器
覆盖环境，并同步更改 image guard 与负向测试；不修改 VGLite runtime、wlroots、Labwc 补丁。

在设备上仅替换两个同源码配置文件、保留备份，重启 greetd 后实际 labwc PID 1436：
`WLR_RENDERER=vglite`，`WLR_SCENE_DISABLE_DIRECT_SCANOUT=1`，FD 18 指向 `/dev/vg_lite`，
gtkgreet PID 1485 存活。这是渲染进程/设备句柄验证，不冒充本轮输入密码或屏幕截图验收。
登录策略仍为 greetd 密码认证，未启用自动登录。

测试结束后再次重启整板，清除本轮测试留下的停滞 worker，未重新请求采集。
最终 boot ID `61b3b89f-df0e-4ad8-9be1-aedd11a2937a`，状态为
`ownership_state=ready / vision_state=idle / reader_open=0`；greeter labwc PID 237
仍为 VGLite，FD 18 为 `/dev/vg_lite`，gtkgreet PID 292 正常运行。
这个 idle 是未发起采集的状态，不能掩盖前述 VICAP 初始化失败。

## 后续实机定位：校准数据库遗漏与日志断言

以下是上述快照之后继续通过 SSH 更新 CPU1 槽、整板重启获得的结果。
没有要求用户重刷整卡。临时诊断源码及固件仅保存在忽略的 `.tmp/device-validation/`
和 LAN 构建快照中，不是正式镜像源代码。

1. ioctl 诊断显示最后一次 `HalSystemInit` 请求 `0x40047601` 已经返回 0；不能把
   `kd_mpi_vicap_init()` 未返回直接归因于这个内核 ioctl 卡死。
2. CPU1 工作线程内存快照显示关闭状态与 `Assertion failed` 文本。随后断言包装诊断
   在实机确认 `common_tracer.c:93 / TCommonTracer_print / assert(handle)`：空日志句柄
   触发 abort，使原始错误被掩盖。诊断仍调用真实断言函数，没有让失败继续执行。
3. 在 boot ID `f1f26a65-9c48-416b-a7f8-76d98f1e0f21`，先 idle 再请求帧，记录到：

   ```text
   t_database.c:130: TDatabase_load(): RET_FAILURE(1); error: No such file or directory
   ```

   ownership 仍为 ready/error=0，实际帧数仍为 0，worker 仍退出。这个结果定位的是
   缺校准数据库，不是硬件或 AI 验收通过。

### 临时诊断自身的错误与撤回

首版扩展日志记录错误使用了控制区 `+0x1000`，它其实是现有 ownership 页，不能当作
空闲空间。raw SHA `400e75a3758f7505f56a2b5cc8f61316a55f0711b143a94d7f4d52e72016c443`
在 boot `0255f468-8303-41ec-93be-bd4bdc67c458` 的一次请求中触发 ownership fault=-5。
这是本次诊断引入的缺陷，不是原始镜像故障。已明确标为禁止部署的历史诊断，未提交。
当时立即停止请求，没有原地重授予、CPU1 热复位或回收 DMA 缓冲区。

替换诊断使用第一控制页内 `+0x400..+0x610`，位于 448 字节 frame ABI 之后、ownership
页之前；加入按真实头文件计算的编译期不重叠断言、整个 ownership 页的 canary 测试，
以及旧 `+0x1000` 选址必须编译失败的负向测试。重新写入、完整读回和整板重启后恢复
ready/idle，前面的真实缺文件错误是在这个修正版本上捕获的。最终修复候选完全不链接
这些临时 `__wrap_*` 诊断函数。

### 正式修复：只读 ROMFS 文件模式

固定 SDK 的 `database_param_remap()` 会把物理 `0x00300000` 起的 3 MiB 映射出来，
寻找 bootloader 预加载的校准参数。`VICAP_DATABASE_PARSE_HEADER` 不是“应用内置参数”，
该地址也不在 CPU1 资源分配内；不存在预加载签名时，库把 parse mode 改回文件模式。
随后 `kd_mpi_isp_calib_file_load()` 按 `/bin/%s.xml` 读文件。此前 ROMFS 只含 worker，
因此这条路径必然缺资源。

现在显式选择 `VICAP_DATABASE_PARSE_XML_JSON`，不进入上述固定物理地址探测；构建时将
同一固定 SDK 的以下文件原样嵌入 CPU1 ROMFS `/bin`，不依赖 Linux 文件系统或外置 SD：

| 文件 | SHA-256 |
| --- | --- |
| `gc2093-1920x1080.xml` | `ef5ecb378442897a81799a6e61c40d815be2aed7e4f15f932cc4ed25dfed7396` |
| `gc2093-1920x1080_auto.json` | `d6ae7404f8186dc33b3630b4c693d92aa76898cd6589bb327dfcc5b855924efc` |
| `gc2093-1920x1080_manual.json` | `9704b1a35982ee027c39886796e17e8b30574f9a237ce1893fd333d0f4de2be4` |

新增打包脚本先验证全部三个文件非空，再复制并逐字节比较；缺失/空文件的六个拒绝用例
已加入 CI。真实 CPU1 preflight 还调用使用 SDK 头文件的 25 项采集生命周期回归。
Ubuntu 24.04 上的完整 `test-tdvp-cpu1-rtsmart-build.sh` 已通过，包含连续两次真实固件构建、
旧 archive 成员清理和五个 RT-Smart/OpenSBI 构建失败保留测试。

正式修复候选 raw SHA 为 `1d1a5a6e838d9d5eea2877abea607dfeefe7ea58d08fac7394ccb48c643891ef`，
大小 3828520 字节；20 MiB slot SHA 为
`22846d26e70d6c488361a056a53559acfdd2e87d5765d4f722bceb546eb7aaf4`。
已经写入设备并全量读回匹配，boot 分区、前 10 MiB 和桥接模块未变；回退备份在
`/root/tdvp-cpu1-calibration.3vP3eV`。重启后的实际采集结果需单独记录，不能用以上构建
或资源打包成功代替。

用户协助登录的实际会话也已核对：tdvp 用户 Labwc PID 833 的环境是 VGLite，FD 18
指向 `/dev/vg_lite`；没有为诊断改回 Pixman 或启用自动登录。

### 实机继续定位：SDK 的 NV12 UV pitch 未填

补入校准文件的首次实机测试（boot `5203d7d7-27c8-45ab-ad98-3b24ca3ce523`）
不再出现上述数据库日志断言，转为受控 fault=-5、cleanup 到 VB exit，但 Linux 仍为
0 帧。为了避免 cleanup stage 掩盖首个错误，在 producer 原有 reserved[0..3] 内
增加 version=2/current-stage/first-failing-stage/raw-first-error；不改变 frame ABI 大小，
不写 ownership 页、不暴露地址，不覆盖第一条失败。生命周期测试覆盖 SDK 原始有符号
错误码保留，以及后续 cleanup 失败时保留 DMA storage 和第一条错误。

对应 raw `dc7de17959db33dd3fd77280160da01f6414abcd04e69f7aaf9d7eee0875b46b`
在 boot `389f036b-4b39-4082-b6a5-5ea0f10b54bb` 的一次实机请求返回：

```text
ownership_state=ready / ownership_error=0 / vision_state=fault
captured=0 / published=0 / frames_delivered=0
capture_trace_version=2 / current_stage=13 / first_failing_stage=10
raw_first_error=0xffffffde (-ERANGE)
```

这证明初始化、start-stream 已返回成功，失败发生在取帧之后的完整平面验证，不能继续
描述为 VICAP init 卡死。对实际链接的固定 `libisp` 中 `cp_vb_info()` 反汇编核对：它写
width/height/format、三个物理地址、PTS，但只把 width 写入 `stride[0]`，不写
`stride[1]`；调用前已清零的 UV pitch 因而保持 0。原适配器把它视为非法短行。

修复仅对已验证的固定 1920x1080 NV12 channel，在 Y pitch 恰等于 width 且 UV pitch
为 0 时补齐相同字节 pitch。不会推测物理地址、改写非零非法 pitch、取消 MMZ 检查或
扩大所有权范围。测试增加 1 个固定 SDK 格式成功用例，以及零 pitch 配合越界地址、
空地址、非零短 pitch 三个拒绝用例，共 29 项生命周期/故障测试，另有首错元数据测试。

Ubuntu 24.04 完整真实 CPU1 preflight 再次通过两次构建和五类构建失败传播测试；
pair guard 通过。最终候选 raw 为
`5e81f13449273e4ecc197ea4da161d382a23032947da192c1c31e5547f93a864`（3828520 字节），
worker 为 `95073296932fc5bd438851ccf8b0d5d5453d717497fb19d5861d465c41b20237`，
20 MiB slot 为 `33a5068bbe955534db76bc1074992f1e5dddbff7d6e73a586f7b71895d10b07f`。
在 `/root/tdvp-cpu1-nv12.4U2Kyn` 保留写入前备份，完整读回匹配、boot p1/前 10 MiB/
Linux bridge 均未变后才执行整板软件重启。

镜像源策略、VGLite session gate、PAM/auth ext4 guard、VolumePulse image guard
全部通过。VGLite stack lock 测试最初因 archive 快照没有 vendor 子模块及其 Git 元数据
无法完成；只读挂载准确固定子模块 `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7` 和
对应元数据、设置容器内精确 safe.directory 后重跑 PASS，没有修改 renderer 实现或锁。

### 连续帧检查暴露零 PTS，明确采用 CPU1 dequeue 时间

NV12 修复候选在 boot `de4f8e9c-e88a-4e98-8c72-7918539a8254` 首次让 Linux 收到帧：
探针接受 1 帧后因第二帧协议检查失败退出，status 为 delivered=2/captured=3/published=3。
采集 trace 无错误。关闭后回到 idle、stage=14，ownership 保持 ready；读取三个传输
header 确认 sequence 正常而 SDK PTS 均为 0。没有将这次部分成功记为 30 帧通过。
一次独立、关闭后重开的单帧检查获得 3110400 字节，sequence=5/PTS=0，luma=0..123，
证明数据路径而非连续时间语义；不将它作为视频质量或连续帧验收。

正式适配器改为使用 CPU1 `CLOCK_MONOTONIC` 在 dump 返回后读取的微秒时间，明确定义为
dequeue time，不声称传感器曝光时间，也不混入 Linux 时钟。检查负值、非法纳秒、微秒
转换溢出、零值、停滞与倒退；任何失败都释放已取得帧并报错，不以递增计数器伪造 PTS。
新增七类拒绝用例，现为 36 项生命周期/故障测试。frame ABI 的字段与大小不变，仅补充
pts 的时间域注释。原始探针未改，仍严格要求连续序号和递增 PTS。

Ubuntu 24.04 完整 CPU1 preflight、pair guard 再次通过。实际探针 parser/reader 测试
（17 类 I/O/time、15 类 malformed record、9 类 CLI/file lifecycle）、transport 环形
队列/lease/背压测试及镜像源策略也通过。最终 worker 用 nm 检查无任何 `__wrap_*`。

时间戳修复候选 raw SHA 为
`a55ef2f3caaefcd17eb3d0154b4c0a9f3b557c3421d63f538a526a4c9c789303`（3828568 字节），
worker SHA 为 `b1b81af629226818b281d233ae04f81bb8329a7299a0dd7a2cfd649bffa99958`，
slot SHA 为 `4b9a573d578ed09228e4c63f5ff7db967a14d334fcf148663936240a5e8f524c`。
`/root/tdvp-cpu1-clock.kDsvQ8` 保留回退备份；全槽读回匹配且 Linux boot、前缀、桥接
模块未变后执行整板软件重启。这不是断电冷启动或完整新 SD 镜像的验收。

### 本轮最终实机结果：30 帧及关闭后重开 300 帧通过

最终 boot ID 为 `688106d5-7e5a-44f3-bb08-bf3aa35a4c22`，运行的是上面的
`a55ef2f3...` CPU1 raw firmware。初始状态 ready/idle、reader=0、计数为 0。
运行的是原始严格探针，没有改动其 parser、序号或时间戳接受规则：

| 请求 | 退出码 | 接受序号 | PTS（CPU1 单调时钟，微秒） | Linux 接受字节 |
| --- | --- | --- | --- | --- |
| 30 帧，15 秒总预算 | 0 | 1..30 | 59669590..61570702 | 93312000 |
| 先确认关闭至 idle，再重开 300 帧，60 秒预算 | 0 | 34..333 | 89646189..118225885 | 933120000 |

两次请求之间的序号空隙来自关闭时已发布但未被该 reader 消费的帧，不是在单次流内
跳帧通过校验。每次请求内部序号连续、PTS 严格递增。测试逐字节计算帧 hash，加上
期间下载帧，产生明显消费者背压；这不是 Linux 30 FPS 性能基准。最终计数为：

```text
resource_owner=cpu1 / ownership_contract=2
ownership_state=ready / ownership_error=0
vision_state=idle / vision_error=0 / reader_open=0
frames_delivered=330 / captured=911 / published=336 / dropped=574
capture_trace_version=2 / current_stage=14 / first_failing_stage=0 / raw_first_error=0
```

旧 reader 的槽位未被背压覆盖，采集关闭后完成 stop/deinit/VB exit，再达到 STOPPED。
没有 CPU1 热复位、重新授予 ownership 或强制回收 DMA。实机自然背压路径通过，但不能
代替长期压力、拔摄像头等硬件故障注入测试。

两张末帧均为 1920x1080 NV12、3110400 字节：

- 30 帧末帧 SHA `b331418d7d93d10b4ff6b33330d4ee81b1ff874b33da8a0cc8526554f8043826`。
- 重开 300 帧末帧 SHA `e1019aa8045b13e55693e4e1d4139d89876630fbae7f611c66775769c2228598`。

第一张已用 ffmpeg 按 NV12 解码为 PNG 并人工目视确认：是真实键盘、置物台和线缆场景，
不是空帧或 fixture；有可识别细节和色彩。仅作基本实际画面确认，不代表 ISP 色彩、
镜头方向、低照度或 AI 模型准确率全部验收。原始帧仅保留在设备回退目录、忽略的本地
`.tmp/device-validation/remote-ai-clock/` 和 LAN 同一候选目录，没有提交私人画面。

最终 Labwc PID 235 是 greeter 会话，`WLR_RENDERER=vglite`，FD 18 指向 `/dev/vg_lite`。
此前用户登录桌面 PID 833 的 VGLite 验证已单独记录；不要把重启后的 greeter 进程
误称为已登录桌面。未配置自动登录、Pixman fallback 或重新安装 Linux Camera 应用。

本次正式变更范围为 CPU1 ROMFS 校准打包、capture adapter、可选首错诊断、对应测试
与文档，Linux bridge/DT/renderer runtime 未改。提交前 GitNexus impact 对未收录的
CPU1 符号返回 UNKNOWN，detect_changes 报 Windows linked worktree 不属于仓库；
`git worktree list --porcelain` 已确认属于同一 Git 仓库，因此采用逐文件 staged diff、
调用点核查、编译与实机回归，不把缺索引当作 LOW risk。未重建工程师脏工作树的索引。

**已验证的是 CPU1 摄像头采集及 Linux 异步拷贝接收。** KPU 固定模型执行、AI2D 数值
比对、FFT 数值比对、共享 SRAM/解压器专项验收以及最终完整 PR 镜像仍未全部完成；
蓝牙/nRF52840 在本轮未验证。不能把此次摄像头结果扩大为整个 AI subsystem 已完成。
