# CPU1 异步 AI 任务：AI2D / FFT 服务与 KPU 参考结果

## 当前交付边界

生产 CPU1 vision worker 已集成启动期常驻的 AI supervisor/executor。Linux
通过 `/dev/tdvp-ai` 的 `write/poll/read` 异步提交请求，CPU1 使用固定 nncase 2.9.0
运行库调用真实 AI2D，然后返回结果。还可通过同一入口调用 CPU1 的 PIO 硬件
FFT 驱动。**目前开放 CHW uint8 的恒等、裁剪、逐通道常量填充，以及复数
int16 FFT/IFFT；尚未开放 KPU API，也不等于完整视觉模型流水线。**

`tdvp_ai_job.{c,h}` 继续作为 supervisor 私有的生命周期核心；Linux 接口编入
现有启动期锁定的 `tdvp_cpu1_vision.ko`，没有第二个 accelerator owner，也没有
独立可热重启的 CPU1 服务。没有新增 Camera 菜单、模型文件或 Linux AI 后端。

本次不修改现有摄像头传输 ABI、所有权记录、VGLite/Wayland 或启动区。现有
`/dev/tdvp-vision` 仍只传输 CPU1 采集的帧；它不等于模型推理结果接口。

## Linux 应用接口与内存边界

公共 ABI 位于 `board/tdvp/cpu1/vision/tdvp_ai_abi.h`，包安装时进入 staging 的
`usr/include/tdvp/`。两个设备节点均为 `root:video 0660`；普通 `tdvp` 用户属于
video 组，实测可提交和读取 AI2D 请求，不要求 root 或 `/dev/mem`。

1. 以 `O_RDWR | O_NONBLOCK | O_CLOEXEC` 打开 `/dev/tdvp-ai`。当前只允许一个
   客户端；已有客户端或未完成的断开任务会返回 `EBUSY`。
2. 等待 `POLLOUT`，一次 `write` 提交 128 字节请求头与紧随其后的 CHW uint8
   输入（FFT 使用下述 CI16 格式）。应用必须将四个 cookie/ID 字段置零，由内核
   绑定当前所有权代次和客户端。
3. 成功写入只表示已发布，不表示执行完成。等待 `POLLIN` 后，一次 `read`
   接收 128 字节结果头与实际输出数据。`POLLERR` 表示服务已锁存故障。
4. 上一个结果没有读完前，新的写入返回 `EAGAIN`；短缓冲区返回 `EMSGSIZE`，
   用户指针复制失败返回 `EFAULT`，两者都保留结果，可重新读取。
5. 关闭执行中的客户端不会取消硬件或释放在途缓冲区。旧任务真实完成、结果验证
   并丢弃之后，才允许新客户端打开，旧结果不会交给新客户端。

AI2D 输入/输出各限 3 MiB；宽高各为 1–1024，输入长度必须恰好为 `3*width*height`，
裁剪范围和填充后的尺寸必须一致。期限为 1–60000 ms。两端独立验证头部、长度、
维度和运算类型，拒绝溢出、越界和未开放的操作。接口上限并不代表所有合法形状
都已在硬件上验收；本轮实际数值覆盖的形状见下文。没有 resize、归一化、DMA-BUF、
用户物理地址、共享 MMZ 映射或零拷贝 API。

FFT 使用同一 128 字节头和任务租约，不改变既有 AI2D 字节布局：

- `operation=TDVP_AI_FFT`（3），`format=TDVP_AI_COMPLEX_I16`，输入/输出均为
  小端有符号 int16 实部、虚部交错排列；不把 vendor ioctl 结构暴露给 Linux。
- 两侧 width 为 N：64、128、256、512、1024、2048 或 4096；height 均为 1。
  输入长度恰好为 `4*N`，输出也是 `4*N`。所有 crop/pad 字段必须为零。
- flags bit 0 为逆变换，bits 8–19 为逐级右移掩码；超出 `log2(N)` 的阶段位及
  其他位全部拒绝。全阶段右移掩码为 `(N-1)<<8`，对应总计除以 N；flags=0 的
  正变换、flags=1 的逆变换均不额外归一化。中间 shift 组合虽有参数校验，本轮
  硬件数值覆盖的是不缩放与全阶段缩放，不代表所有组合已验收。
- CPU1 executor 自己打包固定 `k_fft_ioctl.h`，调用已经验证的 `/dev/fft_device`
  PIO 驱动。没有系统 SDMA、用户物理地址、共享 reset、IRQ mask 或 clock-gating
  参数。真实 ioctl 完成并通过所有权/期限检查后才发布输出。
- 能力掩码、manifest 的 `ai_job_backend=ai2d,fft` 和 rootfs 模块标记要求两侧
  配对更新。旧 AI2D 用户程序可继续使用原请求，但不能混装旧 CPU1 或旧内核桥接。

新增区域完全位于现有 32 MiB transport reservation 的未使用部分：

| 区域 | 起点 | 大小/终点（不包含） | 写入方 |
| --- | --- | --- | --- |
| 既有三个摄像头槽 | `0x1c000000` | 24 MiB，止于 `0x1d800000` | CPU1 |
| AI 输入 | `0x1d800000` | 3 MiB，止于 `0x1db00000` | Linux |
| AI 输出 | `0x1db00000` | 3 MiB，止于 `0x1de00000` | CPU1 |
| 既有 vision control | `0x1dff0000` | 既有记录不变 | 按原 ABI 分侧 |
| 既有 owner page | `0x1dff1000` | 4 KiB | 按原 ABI 分侧 |
| AI control page | `0x1dff2000` | 4 KiB，当前结构 512 字节 | 分侧记录 |

共享区域使用非缓存映射和完整 I/O 内存屏障；`submitted` 发布请求和输入，
`completed` 发布响应和输出，`released` 确认结果租约。CPU1 将输入复制进自己的
MMZ 张量，执行 cache writeback、真实 AI2D 完成检查和 output invalidate，最后
复制结果回传。Linux 不调用 AI2D/GNNE 驱动。摄像头与 AI 服务目前是可共存的
独立入口；尚未把某一摄像头帧自动接入 AI2D/KPU 模型，不应把并行测试称为完整
“摄像头 → 模型 → 推理结果”验收。

## 生命周期约束

| 状态 | 允许的下一步 | 缓冲区规则 |
| --- | --- | --- |
| READY | 有效、已接通 backend 的请求进入 QUEUED | 没有上一任务租约 |
| QUEUED | 启动进入 ACTIVE；取消/排队超时进入 RESULT | 未交给硬件；仍需明确确认取消结果 |
| ACTIVE | backend 证明完成且静止后进入 RESULT | 关闭应用不释放、不取消线程、不自动重试 |
| RESULT | 完整复制结果，或明确丢弃已断开客户端的结果后 ack | 读取失败或未读取时保留；不能覆盖 |
| POISONED | 本次服务生命周期无恢复 API | 保留任务和缓冲区，拒绝新任务与迟到完成 |

任务票据绑定 CPU1 owner cookie、Linux peer cookie、客户端 cookie、单调递增
64 位任务 ID。客户端 cookie 必须由可信适配层生成，不能由应用冒充；此核心不是
认证边界，也不直接解析共享内存或用户指针。任务 ID 到达上限后拒绝复用。

单任务接口上限不是模型总内存预算；未来模型后端仍需单独审计其内存、算子和
资源范围。当前 executor 的发布代次是 32 位，达到 `UINT32_MAX` 后会更早锁存
`EOVERFLOW`，不会借用 64 位任务 ID 回绕重用旧任务。

一个串行 supervisor 拥有状态核心；executor 使用不可变请求/票据副本。适配层
独立定期检查真实所有权快照与心跳，并调用 `tdvp_ai_job_tick()`；不能把
executor 没有返回当作继续等待的理由。`submit/start/complete` 内部的时间检查
不代替外部所有权检查。时钟回退、owner 丢失、执行阶段超时、非法完成元数据
都会锁存第一次错误。已有 RESULT 在没有 owner 故障时不会因过了原期限而失效。

`quiescent=1` 必须来自具体 backend 的完成证据；poll 超时/错误、应用关闭、
线程结束或 supervisor 超时都不是这个证据。迟到完成不解除 POISONED 状态。
队列尚未启动时的取消/超时则不涉及 DMA，可以在确认结果后释放。

初始化只允许用于经过验证的启动代次，存储需初始清零。不能在每次打开 Linux
客户端时重新初始化，也不能重启 CPU1 用户进程后新建一份对象来绕过旧 DMA 租约。
核心自身不证明系统级启动/停止或 DMA 静止；生产服务保留现有所有权门禁及
失败后进程常驻的约束，不提供 CPU1 热复位或重新授予所有权。

## 已运行的构建与回归

2026-09-08，LAN 主机 `192.168.31.42` 的 Ubuntu 24.04 容器中：

- 实际 C 状态核心：41 个场景通过。
- 相同测试开启 AddressSanitizer/UndefinedBehaviorSanitizer：41 个场景通过。
- 公共头文件以 C++17 编译通过，供 nncase executor 使用。
- 固定 RT-Smart 工具链交叉编译产生 RISC-V 对象；没有将对象当成实机执行证据。
- 实际 ABI 验证器：33 个场景，以及 C++17 ABI 编译、固定映射范围检查通过。
- 更新后的生产入口在复用 SDK/output 下真实构建两次，通过既有拒绝回归，并将
  AI2D 服务实际链接进生产 worker。三份 nncase archive SHA 固定，AI2D C++ 对象
  大小/成员偏移静态检查和 `.tbss` 布局检查通过；CPU1 不链接 VGLite/VO owner。
- 使用设备对应 Linux 6.6.36 构建目录交叉编译实际 bridge，运行真实包安装配方、
  退役 owner 清理、真实模块的 ext4 rootfs 校验和实际 DTB/manifest 配对检查。
  最终补跑包含两个缺少 AI 服务标记的真实 ELF 变体及 37 项错误配对拒绝测试。
- `test-tdvp-ai-abi.sh` 还编译真实 Linux CLI，并验证六种非法参数立即拒绝；这
  是无硬件回归，不能替代下述板上数值测试。

```sh
bash buildroot/tools/test-tdvp-ai-job.sh
bash buildroot/tools/test-tdvp-ai-abi.sh
```

覆盖正常完成、结果租约、旧票据、关闭 queued/active 客户端、排队与执行超时的
不同处理、迟到完成、owner/peer 变化、时钟回退、缺少静止证明、越界输出、非法
错误码、未开放的 KPU/FFT 能力、任意大 opcode、大小/期限边界与 ID 回绕拒绝。

## 2026-09-08 首轮 AI2D 部署与实测（dba055f）

在确认卡分区、现有 slot 哈希并备份后，配对更新 CPU1 10–30 MiB 原始 slot、
Linux bridge 和 udev 规则，再执行整板软件重启。slot 写入后读回一致；首 10 MiB
SPL/U-Boot 区域和 boot 分区哈希未变，没有 CPU1 热复位、模块热卸载或共享 PLL
调整。这个部署是局部配对升级，不是新完整 SD 镜像的冷启动验收。

| 已部署产物 | 字节数 / SHA-256 |
| --- | --- |
| CPU1 raw payload | 5481640 / `64260afc93ea21e216033f19a11869c6b82fe256287c5a3c4ded726136e48003` |
| 填充后的 20 MiB slot | `86e62062ff2a4db228e8228eca48a1b0d9b082ed5eccc0202c449a6411d3da16` |
| CPU1 worker ELF | `21c1182d75b22a571783dd687a176dd9a69d1f1f61f0a27de785eafeb2efd440` |
| Linux bridge | `a72d051cf06fe75c58ef524f5ec967f0cea7b8f0bc120331696b3b75e45cb67e` |
| Linux acceptance CLI | `50c24306accc8423a5d11036aa9e4ecfaef5297f08554445b40e734f54318555` |

测试启动 ID：`9254c367-f952-4a32-a343-cc83a2a3ab7f`。保留备份目录：
`/root/tdvp-cpu1-ai-service.DQSKHf`；CPU1 slot 及不可变前缀备份在
`/root/tdvp-cpu1-ai2d.Kg9xbR`。不能把这些路径中的旧 slot 单独恢复而遗留新的
Linux 模块；回退同样需要配对文件、读回校验和整板重启。

- 初始 1 + 15 组数值测试通过。
- 通过正常 greetd IPC/PAM 密码认证进入 `tdvp` 桌面，没有启用自动登录。桌面
  labwc PID 3292 的 `WLR_RENDERER=vglite`，实际 FD 18 打开 `/dev/vg_lite`，
  greetd 配置哈希前后一致。
- 摄像头采集期间运行 100 组数值测试，全部逐字节一致；包括 16×16 恒等、8×8
  裁剪、24×20 填充、16×12 裁剪加填充、256×256 恒等，输入图案逐轮变化。
  CPU1 报告的处理用时为 10–47 ms，不将其误作端到端延迟或性能承诺。
- 同时收到 300 帧 NV12，共 933120000 字节，序号 1–300、PTS 单调。AI 测试前后
  摄像头都处于 running，分别已有 0/79 帧送达，证明存在真实时间重叠。VGLite
  桌面 PID/设备 FD 未变；随后摄像头回到 idle、reader_open=0。
- 采集计数为 captured=721、published=303、dropped=417。传输限速/背压会丢弃
  未发布帧；本次证明的是 300 帧完整送达和共存，**不是 30 fps 无丢帧验收**。
- 另以普通 `tdvp` 用户运行 5 组数值测试通过，验证 `video` 组访问权限。
- 四轮 CLI 均覆盖单客户端、未开放 KPU opcode、伪造 owner、在途写入背压、
  短读/EFAULT 保留结果、关闭应用后丢弃旧结果。每轮另有一个关闭后丢弃的完成
  任务，故总计为 121 个数值比较 + 4 个 detached 任务。
- 最终 submitted=accepted=completed=125，AI idle/error=0、owner_error=0；
  vision idle/error=0、ownership ready。未发生需要复位的故障。

并行日志保留在备份目录的 `coexistence.wZ5h0w/`。CLI 是手动诊断程序，不安装
菜单、不自动运行。硬件故障/超时后的永久锁存规则有主机回归，本轮没有在运行中
故意卡住 DMA 或破坏所有权，因此不能声称这些硬件错误注入已通过。

## 2026-09-08 FFT/IFFT 接入、配对部署与共存复验

本次沿用上述 ABI v1 布局和预留内存，只扩展明确的 FFT 操作与能力位。没有修改
FFT 内核 PIO 驱动、所有权记录、摄像头槽、VGLite 或 SPL/U-Boot。Linux 的结果
长度按已验证的 operation 计算，不再把 FFT 输出当作三通道图像长度。

Ubuntu 24.04 容器中的真实生产 preflight 两次构建通过。主机 ABI 回归扩展到
93 项；提取并执行实际 `execute_fft()`，固定 SDK ioctl 结构、参数打包、输出边界、
open/ioctl/close/期限故障后的租约保留共 37 项通过。真实 Linux 模块安装、ext4
检查和 37 项 DTB/manifest 错误配对拒绝测试通过。诊断 CLI 也由对应 Linux 交叉
工具链构建；没有将主机执行结果冒充硬件计算。

| 已部署产物 | 字节数 / SHA-256 |
| --- | --- |
| CPU1 raw payload | 5481688 / `271cdb10f6394dc8bf2f61172973ae3e8e0cab7f9861d74832cf24b8e7cb83fc` |
| 填充后的 20 MiB slot | `984d9a8717cf05b6c4075fd327c862b94ea487b9f34f979708a2de0264bc7d66` |
| CPU1 worker ELF | `f1537c59e6d51638de3ad5a3b913b769cc716a212d0d779ea9ef64882d6396db` |
| Linux bridge | `312ad11230dde06828ff4cd3a4c16d3fe4db3f44d61337c7b8bae54715b9a259` |
| FFT job CLI | `ae20c528e7e0b34ecf290988d750db8bfa2f4f9ae8b3aba236a564a9f399f620` |

在备份现有 AI2D slot/模块后成对替换，slot 完整读回一致，首 10 MiB 和 boot p1
哈希仍分别为 `76015f6e72d0bfcb89c3faa31fbe460e5288354a1791fc35cc52108cb84e4db7`
及 `082ad56db2decfc1f5dbe4874c674fe8b7dfddef09177b7dab8575437cb3feb8`。整板软件
重启后的 boot ID 为 `4b25f9ee-9e55-4ae5-bb6a-f9f016ba8f88`。备份和日志保留在
`/root/tdvp-cpu1-ai-fft.dfJ7Rc`，包括 `module.before.ko`、`slot.before.bin` 和
前缀读回文件；没有热复位 CPU1 或热卸载内核模块。

真实 Linux CLI 为 `buildroot/tools/tdvp-cpu1-fft-job-probe.c`。每轮包含 112 个
FFT/IFFT 数值场景：7 个点数 × 2 个方向 × 2 种缩放 × 4 个闭式参考向量。
向量是首点实脉冲、实直流、N/4 位置实脉冲、N/4 位置虚脉冲；脉冲幅度 N，
直流幅度 1。每个输出 bin 的实/虚部都比较，阈值预先固定为 1 LSB。每完成一个
点数的 16 个 FFT 请求便穿插一个 16×16 AI2D 恒等任务，另有一个关闭后丢弃的
FFT 任务。因此每轮是 112 + 7 + 1 = 120 个完成任务。

- 第一轮 112/112 FFT/IFFT、7/7 AI2D 通过；FFT 最大误差 0，计数为 120/120/120。
- 正常密码登录后，labwc PID 656 的 renderer 为 vglite，FD 18 为 `/dev/vg_lite`；
  不修改 greetd、不使用自动登录。
- 第二轮以普通 `tdvp` 用户在摄像头采集期间运行，同样 112/112 FFT/IFFT、7/7
  AI2D 通过，FFT 最大误差仍为 0。两端状态检查、结果租约及非法控制位拒绝通过。
- 紧接着以同一普通用户运行上一版已部署的 AI2D CLI（SHA `50c24306...`），
  15/15 组逐字节回归及一个 detached 任务通过，证明原 AI2D 客户端仍可使用。
- 摄像头同时完整送达 300 帧、933120000 字节，sequence=1..300，
  PTS=226462195..250539680。FFT 前/后及 AI2D 后，摄像头均为 running，分别
  已送达 1、87、104 帧；桌面 PID/FD 未变。结束后 capture=687、published=303、
  dropped=384，仍不是无丢帧或 30 fps 性能验收。
- 最终 submitted=accepted=completed=256，AI idle/error=0、owner_error=0，
  摄像头 idle/error=0、ownership ready。总计 224 个 FFT 数值任务、29 个 AI2D
  数值任务和 3 个 detached 完成任务。并行日志在 `coexistence.CRndlP/`。

这验证了 FFT/IFFT 的上述闭式输入、缩放和跨核集成，不证明任意输入的量化误差、
全部幅度/溢出组合、语音前端或识别模型质量。KPU 与完整摄像头模型流水线仍未开放。

## 官方 KWS 模型的主机参考

来源是用户指定的 LilyGO 项目所使用的固定 RT-Smart SDK，不是其他板卡的模型：

- [官方板卡仓库](https://github.com/Xinyuan-LilyGO/T-Display-K230)
- [固定 SDK 的 kws.kmodel](https://github.com/Xinyuan-LilyGO/T-Display-K230_canmv_rt/blob/abb07090ad8a666ed7a5e097b3c714b918731645/canmv_k230/src/rtsmart/libs/kmodel/ai_poc/kmodel/kws.kmodel)
- Git blob：`1b512ad61328c4e618acf4ffe820f9fa0f5e0b7f`
- 文件大小：369560 字节。
- SHA-256：`b51a31c3310a052488cbce9fbbc52a1d9957f574bc31f1969a1757c7917ae8b4`
- 模型格式 v7，两个模块：stackvm 和 k230；后者包含 21 个函数。

使用 `nncase==2.9.0`、`nncase-kpu==2.9.0`、`numpy==1.26.4`。固定运行库没有
Python 3.12 wheel，因此在 Ubuntu 24.04 容器内独立构建 Python 3.10.21 作为
主机模拟器的运行环境，没有改变主机 Python，也没有使用 Ubuntu 18.04 编译固件。

| 固定输入/输出 | float32 形状 | 字节数 |
| --- | --- | ---: |
| 输入特征 | 1 × 30 × 40 | 4800 |
| 输入缓存 | 1 × 256 × 105 | 107520 |
| 输出分数 | 1 × 30 × 2 | 240 |
| 输出缓存 | 1 × 256 × 105 | 107520 |

`tdvp-cpu1-kws-reference.py` 运行四组确定性输入：零、带符号周期序列、稀疏脉冲、
第二种周期序列。后两组接收上一组输出缓存。四组全部通过，第二次独立运行也得到
相同的八个输出 SHA-256；脚本固定这些哈希并检查有限值/形状/类型，全部通过后
才发布 `manifest.json`。已有输出目录不允许覆盖。模型文件与生成的二进制结果
不写入 Git，也不自动放入设备镜像。

```sh
# 先配置所安装的固定 wheel 对应 PATH/LD_LIBRARY_PATH。
python3.10 buildroot/tools/tdvp-cpu1-kws-reference.py \
    /path/to/official/kws.kmodel /path/to/new-reference-directory
```

未来板上比较预设 `abs(actual-reference) <= 1e-5 + 1e-4*abs(reference)`，并必须
拒绝 NaN/Inf、形状/类型/长度错误。该容差没有被用来放宽主机哈希检查，也尚未
产生板上通过结论。这只是对编译后模型执行正确性的参考；不证明 KWS 识别质量，
更不等于 ASR/语音转文字完成。

## 仍然存在的 KPU 部署门槛

已反汇编固定 RT-Smart 运行库：K230 module 构造器打开 `/dev/gnne_device` 和
`/dev/mem`，映射 `0x80000000` 与 `0x80400000`。**这些映射不能证明模型指令
不使用 `0x80200000` 的共享 SRAM**。不能用主机模拟通过或 Linux reservation
替代启动 DMA/SRAM 交接的实机证明。

当前远端卡仍未包含新的启动解压交接保护，详见
[启动 SRAM 交接文档](cpu1-boot-sram-handoff.zh-CN.md)。本轮没有为绕过这一门槛
单独替换 SPL/U-Boot，也没有在板上执行此 KWS 模型。Linux 异步 AI2D 已接通，
但完整 KPU 模型数值测试、KPU 异步后端、摄像头到模型的完整流水线和
nRF52840 蓝牙仍不是本次已完成项。
