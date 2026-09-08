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
