# CPU1 AI2D 实机诊断记录（2026-09-08）

本记录针对 Linux CPU0 + RT-Smart CPU1 的配对系统。它不是完整镜像冷启动、KPU 模型推理、语音识别或蓝牙验收。正式 worker 仍然是帧传输服务；本目录的 AI2D 工具不由镜像自动安装或启动。

## 固定依赖和安全边界

- 官方板级入口：<https://github.com/Xinyuan-LilyGO/T-Display-K230>。
- RT-Smart SDK：`Xinyuan-LilyGO/T-Display-K230_canmv_rt`，提交 `abb07090ad8a666ed7a5e097b3c714b918731645`。
- 使用该 SDK 的 nncase **2.9.0 RT-Smart** 头文件和三个归档，不使用旧 Linux nncase 2.11。
- 在 LAN 编译机的 Ubuntu 24.04 容器内交叉编译；诊断候选经过两次真实 CPU1 构建、增量 archive 清理回归、失败传播测试和配对 DT 校验。
- 仅允许 CPU1 MMZ `0x14000000..0x1c000000` 的 DDR 输入/输出；验证物理区间、长度和不重叠，拒绝打开 GNNE。没有测试共享 SRAM 或 KPU 模型。
- 临时诊断通过 CPU1 专用 10–30 MiB 原始槽部署。每次检查磁盘、分区边界、root PARTUUID、boot ID、服务状态和新旧 SHA-256，完整备份、写回、读回比较后整机软件重启。
- 不热复位 CPU1，不取消在途任务、不回收未经证实完成的 DMA 缓冲区，不改 SPL/U-Boot、Linux、VGLite、共享 PLL 或电源设置。
- 诊断失败时，执行线程驻留，主线程发布 terminal fault 并保活，拒绝新的摄像头请求；不能通过杀进程、关闭资源或重试来恢复。

未变化的区域：

| 区域 | SHA-256 |
| --- | --- |
| 前 10 MiB 启动区 | `76015f6e72d0bfcb89c3faa31fbe460e5288354a1791fc35cc52108cb84e4db7` |
| 实际 boot 分区 mmcblk1p1 | `082ad56db2decfc1f5dbe4874c674fe8b7dfddef09177b7dab8575437cb3feb8` |
| Linux vision bridge 模块 | `fbe0e67c956830bed0dd4111e04150435fc74607509e182322d43b3eda705713` |

## 发现并修正的诊断/链接缺口

### 1. AI2D C++ ABI 必须匹配预编译库

固定 SDK 的 `ai2d_builder` 在是否定义 `BUILDING_RUNTIME` 时，`sizeof` 都是 712，但字段偏移不同。库构造函数实际使用：

| 字段 | 库要求的偏移 | 未定义 BUILDING_RUNTIME 的头文件偏移 |
| --- | ---: | ---: |
| input_shape_ | 496 | 456 |
| output_shape_ | 600 | 560 |
| dump_asm_ | 704 | 664 |

链接审计和诊断明确使用该宏，并对大小及三个偏移作编译期断言；仅链接成功或仅检查大小不能证明 ABI 正确。

### 2. 官方应用链接脚本没有收集 TLS 段

加入调用方 TLS 后，原脚本使 `.tbss` 与 nncase 的 `.tbss.*` 孤立段使用相同 VMA。在第一版候选中两个段均位于 `0x200326ab8`。该候选只上传过文件，**没有写入启动槽，也没有运行**。

`tdvp-cpu1-nncase-tls.lds` 在原脚本之前传给链接器，显式收集 `.tdata/.tdata.*` 和 `.tbss/.tbss.*`，保留原入口、加载及数据布局规则。修正后，worker 只有一个 40 字节 TLS-BSS 段；两个心跳变量、诊断线程标记和库异常状态分别位于 TLS 偏移 0、8、16、24，不再重叠。

交叉链接回归还构建一个不带修复的反例（不安装、不执行），确认旧脚本会留下孤立 TLS 段。

### 3. 无期限 poll 不能直接改成“超时后返回”

固定 AI2D 库调用 `poll(..., -1)` 后没有检查返回值或 revents，直接继续读取中断状态。因而不能简单返回 0/-1，让库把失败当完成并释放存储。

`tdvp_cpu1_ai_guard` 使用固定总截止时间、最多 50 ms 的 poll 片段、前后所有权检查和第一错误锁存。只有真实可读完成才返回给库；错误后保持执行栈和内存，监督线程发布故障。它不是硬件中止或 DMA 回收协议。

### 4. RT-Smart 的可读事件别名

固定 `lwp_syscall.c` 的 `dfs2musl_events` 将 DFS POLLIN 转换为 `0xC3`（POLLIN、POLLPRI、POLLRDNORM、POLLRDBAND）。最初的诊断保护层只接受 `0x01`，造成假故障。

修正为只接受 `0x01` 或该固定 SDK 的完整 `0xC3` 表示。包含 POLLOUT、POLLERR、POLLHUP、POLLNVAL 或缺少 POLLIN 的值均拒绝。22 项主机回归覆盖这些组合，并可直接抽取、编译固定 SDK 的真实转换函数，而不只依赖 mock。

## 诊断内容及证据区

三类 NCHW uint8 DDR→DDR 操作，各执行 4 轮，共 12 项：

- identity：`1×3×16×16`。
- crop：从 `(4,4)` 裁出 `8×8`。
- pad：上下各 2、左右各 4，输出 `20×24`，三个通道常数为 17/37/59。

每轮改变输入数据，将输出预填充为 `0xA5`，明确执行缓存同步，逐字节核对输出。只有完成事件、同步及逐字节比较均成功，才释放该轮对象。

证据只占用 `tdvp_vision_control.producer.reserved[4..14]`（11 个 32 位字，`0x1dff0054..0x1dff007c`），不占用 `0x1dff1000` 的所有权页。

| 字 | 含义 |
| --- | --- |
| 0 | magic `0x44324941` |
| 1 | 1 prepared / 2 running / 3 passed / 4 fault |
| 2 / 3 | 已完成数量 / 当前项编号 |
| 4 / 5 | 有符号错误 / 完成事件计数 |
| 6 | 高位为 1 时是步骤号，否则是 mismatch 的字节索引 |
| 7 | 库原始错误；或 `0x10000000` 标记的 poll 信息；或 expected/actual 字节 |
| 8 / 9 | 输入 / 输出物理地址 |
| 10 | 证据格式版本，目前为 2 |

步骤 1–3 是分配/地址检查，4–9 是输入输出映射和写回，10 是构造，11 是 build_schedule，12 是 invoke，13–16 是输出失效/映射/比较/解除映射，17 是成功清理，18 是最终完成检查。

## 实机运行记录

原正式固件 raw SHA-256：`b73b405e1f0b3ad1b059a166996afee076c98bdb286e6f14b1f0343c823ba4a6`；20 MiB 槽 SHA-256：`3de08b824e7b011b36b10117d9b31353c3f2dee7f61f7c46cc567bf9284a869f`。

1. V1，boot ID `29a948c7-b3c8-496e-a19a-81393cb7d81d`，raw `2502ccde254f3e5d39552ad501b34ee2b36dcbd038f4dd10beeff6e62cc78681`，槽 `06530afd93364aa44c315f6f72424be11ee5dba0fb0216203dd387df031e11c5`：第一项 fault/-5，完成数量和完成事件均为 0；缓冲物理地址 `0x14000020`、`0x14000D00`。没有继续请求摄像头。Labwc greeter PID 235 的 fd 18 为 `/dev/vg_lite`。
2. V2，boot ID `c4b8004d-e49d-490e-9fc4-810a91c47847`，raw `f408ee4a3fab8579e75a9b30c4cbba241fce13e5733e73c111ba255f1cd0e3a3`，槽 `de1f89114f8cf07becd40af8c5cdf01bd631a40c4fbffc46fa7a3e9e0226c54e`：定位至 invoke（步骤 12），仍为 -5；随后定位到上述 poll 事件别名兼容缺口。这两轮不能算 AI2D 验收通过。

3. V3，boot ID `425a9d68-3187-405a-87a1-4fa340a4a2f9`，worker `ec42cc628625bdc991da445286fa8265514e7642175122abfb6c1e33f8c9f691`，raw `cdeff5b540b7f6554991e4c39882dcf840cbc67c1a32d3d1fde7edff8e050067`（5,476,664 字节），槽 `63aee2ac2b9162030ab8b27961d8c19775f5ef3bd424be20e63583758bfb85df`：**12/12 全部通过**，完成事件 12，错误 0，最终步骤 18，owner ready / vision idle。最后一轮物理地址为 `0x14000D00`、`0x14000750`。这验证了 CPU1 上固定 nncase 的 AI2D DDR 数值处理、缓存同步以及重复任务的完成事件，而非仅设备节点存在。

同一 V3 boot 中，随后执行 `frame-probe 300 60000`：300 帧通过、序号 `1..300`、PTS `115428565..139104809`、共传输 933,120,000 字节，最后一帧 FNV-1a64 `df0cd123d188147a`，保存的 NV12 SHA-256 `2689fa234a0d2fec77519c274856cd436e16ef730075eba7b3fad4b5d5d9296c`。关闭 reader 后瞬时状态为 stopping/error 0，下一次检查已为 idle/error 0。该结果证明 AI2D 诊断没有使普通摄像头线程落入 poll 驻留分支，不代表帧率性能达标。

确认 idle 后再次启动 30 帧，通过序号 `304..333`、PTS `189721805..191656555`、93,312,000 字节；保存的 NV12 SHA-256 `24424976357b7ae3829526f7e9b9529b4c89974090f51c9899c247ed97bd6301`。最终 reader 关闭后回到 idle/error 0，累计 delivered 330；Labwc 仍持有 `/dev/vg_lite`。

恢复正式槽并完成读回比较、整机软件重启，最终 boot ID 为 `784ddb25-eae6-463b-95d7-6c1259f07c50`。诊断 magic 为 0，owner ready / vision idle / error 0。恢复后再读 30 帧成功：序号 `1..30`、PTS `61940681..63838829`、93,312,000 字节，保存的 NV12 SHA-256 `8d89031b575b246ee689c99d1cb81eff2a7b6c1d26c25fc8bbe77bbe87b1c64a`。登录界面 Labwc 的 fd 18 仍为 `/dev/vg_lite`。

最终提交内容另经无临时挂钩的正式构建入口两次构建，包含 22 项 guard 回归。正式 worker SHA-256 仍为 `b1b81af629226818b281d233ae04f81bb8329a7299a0dd7a2cfd649bffa99958`、raw 固件仍为上述 `b73b405e…`，与恢复的版本完全一致。AI2D 诊断和它的开机调用没有泄漏到正式镜像。

结论：AI2D 的上述 12 项 DDR 数值处理及其后摄像头停启已在 CPU1 实机验证；**KPU 模型推理、Linux 异步 AI 作业接口、共享 SRAM 的完整启动交接以及整卡冷启动仍是独立待完成项目**。本次没有部署蓝牙修复或修改 nRF 固件。

## 重现主机测试

```sh
bash buildroot/tools/test-tdvp-cpu1-ai-guard.sh "$pinned_rt_thread"
bash buildroot/tools/test-tdvp-cpu1-ai2d-link.sh "$pinned_sdk" "$musl_cross_prefix" "$output_parent"
bash buildroot/tools/test-tdvp-cpu1-rtsmart-build.sh "$cpu1_output"
```

链接工具不下载依赖、不修改 SDK、不安装或执行目标程序。部署诊断必须使用保留执行线程的所有权感知调用方，不能直接把 link-probe 当成验收程序。临时 worker 挂钩和部署脚本位于隔离候选目录，不写入正式镜像启动路径。
