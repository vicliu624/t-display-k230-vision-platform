# CPU1 接管启动进度记录

## 背景与边界

2026-09-08 检查 a57e99c 配对新卡时，CPU1 进入 STARTING，但所有权心跳
停在 278，legacy mailbox 心跳停在 1375，Linux 最终报告 `-110`，帧数为 0。
UART3 的 functional/APB 时钟在 Linux debugfs 中均为 enabled。
现有证据只能定位为接管初始化未完成，不能证明某个具体 MPP/AI 调用是根因。

为下一次配对启动增加共享进度，不依赖 UART 输出，也不读取普通 CPU1
cached BSS 猜测执行位置。该改动本身不是“摄像头已经恢复”的证明。

## 读取方式

正常 Linux 登录后只读：

```sh
cat /sys/class/misc/tdvp-vision/status
```

增加三个字段，原来的状态格式版本和所有权契约保持不变：

```text
startup_trace_version=1
startup_stage=mmz
startup_result=1
```

- `startup_trace_version=0`：没有可识别的进度记录，stage 为 `unavailable`。
  此时 result=0 只是占位，不能解释成启动成功。
- result=1：已进入该步骤，尚未发布完成或返回错误。
- result=0：对应步骤已返回成功；`complete` 代表初始化与 worker launch
  均返回成功，仍不证明摄像头图像质量、KPU 模型或 ASR 功能通过。
- result<0：该步骤返回错误。故障后保留最后记录，不清零掩盖现场。

阶段覆盖 grant、I2C4 clock/map/registers/speed/bus registration、camera clocks、
cmpi、log、mmz、mmz-userdev、sysctrl、vb、camera pins、vicap、AI clocks、
GNNE、AI2D、FFT、worker launch 和 complete。阶段名由共享头统一定义。
卡在某个 entry 只能说明最后进入的调用，不能单独证明该调用内部的具体故障。

## 协议与安全约束

进度使用现有 CPU1 ownership record 的 reserved[0..2]：
magic=`0x31545354` (TST1)、stage、32 位有符号 result。
128 字节 record 和控制页布局不变，既有 sequence/fence 保护覆盖这三个字段。
旧固件保留零值，新 Linux 对其报告 unavailable，而不是虚构阶段。

只有 CPU1 启动线程在 STARTING/READY 下发布：不触碰 Linux 半页、不分配、
不打印、不调用外设、不增加 heartbeat、不更新时间戳、不重新握手。
因此频繁 trace 不能维持已经停止的心跳，也不能延长 60 秒启动/5 秒 peer 超时。
发生故障仍保留所有资源，禁止热重启、重复初始化或未知 DMA 活跃状态下回收内存。

Linux 在既有工作线程轮询中缓存 stable 且 cookie/peer_cookie 匹配的记录；
sysfs 只读缓存，不驱动状态机、不写共享页、不开始采集。
完整旧记录、撕裂写入和其他启动 cookie 不能替换当前启动的诊断历史。
必须同时查看 ownership/vision 的当前错误，不能把历史 stage 当成当前活性。

## 已验证

- 实际 CPU1 startup 函数的握手、旧 RAM 拒绝、超时、故障和不重启测试通过。
  连续 100 次 trace 增加发布序列但不改变 heartbeat，也不修改 Linux 半页。
- I2C4 的 27 个初始化/时钟场景、MPP 成功和失败路径、AI 顺序与锁定错误测试通过。
- Linux owner 的 55 个准备拒绝场景及撕裂记录、错误 cookie、超时资源保留测试通过。
- VGLite lock、背光、DTS 队列、CPU boot contract 与解压器 DMA 退出测试通过。
- Ubuntu 24.04 中实际 RISC-V Linux module `W=1` 构建通过：
  SHA-256 `098b7b8efd8a7c57be8f93cf5a7fd32771a626bc4607b549c93d5dee5038e5a0`。
- 生产 `build-rtsmart.sh` 在独立 SDK 副本完成 RT-Smart + OpenSBI 构建：
  payload 3,561,336 字节，入口 `0x10000000`，SHA-256
  `b58c2f6fb080dca6de41c13fdf6dfb641fba2f9a1b84eb1ba193480bc8b867cf`。

以上为源码/编译验证，尚未在设备上启动新配对镜像。不会在故障中的旧系统
单独替换或重启 CPU1 来获取 trace；后续必须完整配对启动后读取实际阶段。
