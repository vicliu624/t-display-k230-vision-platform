# CPU1 异步 AI 任务：状态核心与 KPU 参考结果

## 当前交付边界

`tdvp_ai_job.{c,h}` 是后续 CPU1 AI 服务使用的、单 supervisor 私有状态核心。
**它尚未接入 Linux 设备节点、跨核请求队列或生产 nncase executor；当前镜像不能
因此被描述为已经提供异步 KPU API。** `test-tdvp-cpu1-rtsmart-build.sh` 会运行其
回归，但没有把未接通的服务加入自启动。没有新增 Camera 应用或 Linux AI 后端。

本次不修改现有摄像头传输 ABI、所有权记录、VGLite/Wayland 或启动区。现有
`/dev/tdvp-vision` 仍只传输 CPU1 采集的帧；它不等于模型推理结果接口。

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

输入和输出各最多 3 MiB，时间预算为 1–60000 ms。**这是单任务接口上限，不是
模型总内存预算或一个已经部署的物理内存布局。** 模型、张量类型、形状、算子参数、
实际缓冲区边界与权限仍需 backend/适配层独立校验。

一个串行 supervisor 拥有状态核心；executor 使用不可变请求/票据副本。适配层
必须独立定期检查真实所有权快照与心跳，并调用 `tdvp_ai_job_tick()`；不能把
executor 没有返回当作继续等待的理由。`submit/start/complete` 内部的时间检查
不代替外部所有权检查。时钟回退、owner 丢失、执行阶段超时、非法完成元数据
都会锁存第一次错误。已有 RESULT 在没有 owner 故障时不会因过了原期限而失效。

`quiescent=1` 必须来自具体 backend 的完成证据；poll 超时/错误、应用关闭、
线程结束或 supervisor 超时都不是这个证据。迟到完成不解除 POISONED 状态。
队列尚未启动时的取消/超时则不涉及 DMA，可以在确认结果后释放。

初始化只允许用于经过验证的启动代次，存储需初始清零。不能在每次打开 Linux
客户端时重新初始化，也不能重启 CPU1 用户进程后新建一份对象来绕过旧 DMA 租约。
核心自身不证明系统级启动/停止或 DMA 静止；最终服务须保留现有所有权门禁及
失败后进程常驻的约束，不提供 CPU1 热复位或重新授予所有权。

## 已运行的回归

2026-09-08，LAN 主机 `192.168.31.42` 的 Ubuntu 24.04 容器中：

- 实际 C 状态核心：41 个场景通过。
- 相同测试开启 AddressSanitizer/UndefinedBehaviorSanitizer：41 个场景通过。
- 公共头文件以 C++17 编译通过，供 nncase executor 使用。
- 固定 RT-Smart 工具链交叉编译产生 RISC-V 对象；没有将对象当成实机执行证据。
- 更新后的生产入口在复用 SDK/output 下真实构建两次，通过全部既有拒绝回归，
  同时执行新的 41 场景测试。生成的 CPU1 payload SHA-256 仍为
  `b73b405e1f0b3ad1b059a166996afee076c98bdb286e6f14b1f0343c823ba4a6`；
  这证明此次没有暗中启用未接通的 AI 服务，不代表服务的实机验收。

```sh
bash buildroot/tools/test-tdvp-ai-job.sh
```

覆盖正常完成、结果租约、旧票据、关闭 queued/active 客户端、排队与执行超时的
不同处理、迟到完成、owner/peer 变化、时钟回退、缺少静止证明、越界输出、非法
错误码、未开放的 KPU/FFT 能力、任意大 opcode、大小/期限边界与 ID 回绕拒绝。

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
单独替换 SPL/U-Boot，也没有在板上执行此 KWS 模型。完整 KPU 模型数值测试、
Linux 异步请求/结果接口和 nRF52840 蓝牙仍不是本次已完成项。
