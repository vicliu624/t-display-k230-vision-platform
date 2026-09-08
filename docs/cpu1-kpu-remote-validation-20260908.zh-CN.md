# CPU1 KPU 固定模型：远端部署与验证记录

## 范围和不变项

KPU 请求走现有 Linux `/dev/tdvp-ai` 异步桥接，由 CPU1 RT-Smart supervisor/executor
执行固定 SDK 的 KWS 模型。摄像头继续由 CPU1 托管；Linux 不增加 Camera 菜单、
AI 驱动或替代后端。没有修改 VGLite、Wayland、greetd/PAM 配置或共享 PLL/reset。

模型来源为 [LilyGO 固定 SDK](https://github.com/Xinyuan-LilyGO/T-Display-K230_canmv_rt/blob/abb07090ad8a666ed7a5e097b3c714b918731645/canmv_k230/src/rtsmart/libs/kmodel/ai_poc/kmodel/kws.kmodel)，
369560 字节，SHA-256 `b51a31c3310a052488cbce9fbbc52a1d9957f574bc31f1969a1757c7917ae8b4`。
Linux 只能输入固定形状的特征/状态，不能提供模型、代码或 DMA 地址。
KWS 不是 ASR/STT；本次不验收语音识别质量或摄像头到视觉模型的完整流水线。

## 构建和保护

LAN `192.168.31.42` 的 Ubuntu 24.04 离线容器中实际构建 RT-Smart 两次，复用
SDK/output；实际 Linux 6.6.36 模块、包安装、ext4 校验和 39 项错误配对拒绝通过。
初始化/完成保护的 58 个场景及 ASan/UBSan、固定 SDK 状态寄存器布局检查通过。
真实 nncase ELF 的初始化/提交调用都解析到生产 wrappers；单独移除提交 wrapper
的真实 ELF 被同一个审计器拒绝。该负向 ELF 没有执行或安装进 ROMFS。

初始化、提交、poll 完成、输出发布分别检查期限/所有权。软件事件在提交前有界
排空；提交后必须收到真实事件，且 GNNE 没有 work/reset/exception/AXI 错误，
才能记一次完成。超时或不确定状态永久锁存，保留 executor 栈和缓冲区，不重试、
不释放在途 DMA、不热复位 CPU1、不 reset Linux 使用的 GDMA/SDMA 控制器。

## 首次实机拒绝及修复依据

首次候选 raw SHA `e5564a9e03d3ea1bed312ee94d53cca0ada8165f8d6cbd9e1a10594f56408107`，
20 MiB slot SHA `57620b83583e8c71cfa68ffd752ef343a29d2891b2e16afc1003a930ac6a3ae0`。
boot ID `3b12f8dc-f106-4dc3-8ad0-89ab20514216`；备份/日志位于
`/root/tdvp-cpu1-kpu.2vFVx3`。

普通 `tdvp` 用户的第一个合法请求在 stage=4 被拒绝：`error=-34`（ERANGE）、
starts=0、completions=0、submitted=accepted=1、completed=0。未发生硬件提交。
Linux 状态仍 ownership ready、owner_error=0，摄像头 idle/error=0；VGLite
桌面正常。这一轮是失败，不计入数值通过数量。CLI 当时显示的 multibyte 错误文字
来自预期 NaN/Inf 拒绝留下的 errno，真实故障应以 sysfs 的 -34 为准。

不是 PC 单位缩放问题。固定 ELF 的模型起点为 `0x2005a1940`，诊断低 32 位代码
起点为 `0x005a6050`；固定运行库的 `initialize_core` 直接从模型 section 取地址，
`gnne_enable` 最终写 32 位 PC 寄存器。默认 `load_model(span)` 的 pinned-section
路径可能保留该虚拟地址，不应允许截断后提交。

官方示例的 `AIBase` 使用 `load_model(istream)`。
[nncase 2.9.0 interpreter](https://github.com/kendryte/nncase/blob/v2.9.0/src/Native/src/runtime/interpreter.cpp)
明确把 `copy_buffer=true` 转为 stream 路径；
[section 加载实现](https://github.com/kendryte/nncase/blob/v2.9.0/src/Native/src/runtime/runtime_section_context.cpp)
在非 pinned 情况分配共享段、writeback，并返回物理地址。该路径也与实际固定
archive 反汇编一致。因此只改为 `load_model(span, true)`，没有放宽 MMZ 地址边界、
修改模型、比较器或预设容差。

## 修正候选的身份

| 产物 | 字节数 / SHA-256 |
| --- | --- |
| CPU1 raw | 10501656 / `f792e0a574709428ab9cebdd40f20bc78119b5f09a0bc2156790d16d0605bafd` |
| 20 MiB slot | `589010ad5c439f2c832dc173263353daee7173c8cb6834363f76a69ca71b1f24` |
| CPU1 worker | `39cec4aeb618f520179c672092f9cf80c3d114a77168875291be8225d434f586` |
| Linux bridge | `d11b317782c6d91335b02651318a7fb0ef2cfb9670597aab06a8568e1668bf59` |
| 首轮 Linux KPU CLI | `b6a00705b4af41d8173e64e22b634a5bb02b551b4d759a2cea2016a579bd9002` |
| 最终 Linux KPU CLI（修正 poll 错误文字） | `7f2c48f27b210e12b17cd4009ff2be9090b67ed16496974f0754dca78fc87f22` |

本次部署仅写既有 CPU1 10–30 MiB slot，精确匹配的 Linux bridge 保持不变。
每轮检查启动 ID、分区几何、旧 SHA，保留旧 slot/模块，整槽写回读取比较后才
整板软件重启。首 10 MiB SPL/U-Boot 和 boot p1 不改写，SHA 分别保持
`76015f6e72d0bfcb89c3faa31fbe460e5288354a1791fc35cc52108cb84e4db7`、
`082ad56db2decfc1f5dbe4874c674fe8b7dfddef09177b7dab8575437cb3feb8`。
修正版备份目录为 `/root/tdvp-cpu1-kpu-copy.3alXpl`；旧部署脚本绑定旧状态，不可重放。

这属于远端局部配对升级，不是新完整 SD 镜像的断电冷启动验收。手动只读 SDMA/
解压器快照未见活动，但不证明所有启动顺序/系统级独占；新的启动交接保护仍需
在最终完整镜像中单独验收。不能用模型数值结果替代该启动门槛。

## 实机数值结果

修正候选的 boot ID 为 `940f0880-845c-4650-9a94-745faa0cf070`。
通过正常 greetd IPC/PAM 密码认证进入 `tdvp` 桌面；Labwc PID 961 的
`WLR_RENDERER=vglite`，FD 18 打开 `/dev/vg_lite`，两轮共存测试前后均未变化。
greetd 配置 SHA 保持 `4ce0a78389af398d6e3cf0467a0d0f1633e47f6245c8ae31f67c366de149b421`；
没有启用自动登录或更换 renderer。

所有 AI 客户端均以普通 `tdvp`/video 组权限运行，不使用 Linux `/dev/mem`。
四组固定参考由 nncase/nncase-kpu 2.9.0、numpy 1.26.4 预先生成，16 个输入/输出
文件的 SHA 和长度逐一核对。每组比对全部 26940 个 float32 输出，预设容差为
`1e-5 + 1e-4*abs(reference)`，不因实机结果修改。

| 轮次 | KPU 数值请求 | 硬件完成次数 | 最大绝对误差 |
| --- | ---: | ---: | ---: |
| 首轮 4 组 | 4 | 84 | 0 |
| 第一轮摄像头共存，64 × 4 组 | 256 | 5376 | 0 |
| 最终 CLI + 第二轮共存，16 × 4 组 | 64 | 1344 | 0 |
| 合计 | 324 | 6804 | 0 |

每个请求都有 21 次 guarded GNNE start/真实事件/状态完成，最终代码地址为
`0x14050020..0x14050146`，在 CPU1 MMZ 内；raw GNNE status 为 `0x00000c00`。
首轮 CPU1 报告 20–22 ms，第一轮共存为 20–97 ms；这不是端到端延迟或性能保证。
NaN/Inf 输入在 Linux 发布之前按预期拒绝，没有增加任务计数或锁存 CPU1 故障。

两轮每轮另执行 112 个 FFT/IFFT 数值请求、7 个穿插 AI2D、15 个 AI2D 数值请求，
加一个 FFT 和一个 AI2D detached 结果丢弃任务，全部通过。合计：
324 KPU + 224 FFT/IFFT + 44 AI2D + 4 detached = 596 个完成任务。
FFT 数值最大误差 0，AI2D 输出逐字节一致。

每轮通过 CPU1 bridge 收到完整 300 帧 NV12、933120000 字节：

- 第一轮 sequence=1..300，PTS=345284504..370281260。KPU 前/后摄像头均为
  running，送达计数 0/235；FFT 开始时仍在采集，但 FFT/AI2D 结束时采集已完成。
  因此不把第一轮所有 AI2D 请求都描述为与摄像头同时运行。
- 第二轮 sequence=304..603，PTS=605622858..628030988。KPU 前、KPU 后、FFT 后、
  AI2D 后摄像头全部为 running，累计送达计数 300/364/457/471，证明三类 AI
  运算阶段都与 CPU1 摄像头采集发生了实际重叠。
- 最终 frames_delivered=600、captured=1389、published=606、dropped=781。
  包含 bridge 背压丢弃；不是 30 fps 无丢帧或画面质量验收。

最终 `submitted=accepted=completed=596`，`kpu_starts=kpu_completions=6804`，
AI idle/error=0、client_open=0、pending=0、detached=0；vision idle/error=0、
reader_open=0、ownership ready/error=0、startup complete/result=0。
日志目录为 `coexistence.el27GC` 和 `coexistence.8kxSb1`，均在上述修正版备份目录中。
所有 324 个实际输出张量保留于 `evidence.tar`，未保存/外传摄像头图像。
该归档为 35309056 字节，SHA-256
`ee92980f48dcb09056f9a2f31ab2e8fc9a16141487bbb7a13260a0687c6c5fb9`。
下载后另用 SHA-256 对照每个场景的两份主机参考拼接结果，324/324 个实际输出
文件均为 107760 字节且哈希一致；这项独立核对不复用板上 float 比较器。

最终 CLI 仅修正 poll revents 错误分支使用旧 errno 的诊断问题，未改模型、请求、
参考数据或数值比较器；新的 CLI 重新交叉编译并在第二轮 64 组中实测通过。

GitNexus 的 CPU1 符号 impact 为 UNKNOWN；linked-worktree 的 all/compare(main)
检测被工具误判为非 worktree，而 Git 自身 worktree list 确认分支路径有效。
canonical 工作树返回的 HIGH 报告属于其他已有变化，不能冒充本次增量分析。
因此另按文件白名单、调用链和真实测试人工复核：变更限于 CPU1 AI service/ABI、
匹配 Linux bridge、构建/配对/诊断测试与本文档，没有改 VGLite runtime 或桌面配置。

这些结果证明固定 KWS 模型的 CPU1/KPU 执行、异步跨核返回和上述共存场景，
**不证明任意模型、KWS 识别准确率、ASR/STT、摄像头直连视觉推理流水线、
硬件故障注入后恢复或完整镜像冷启动已验收**。nRF52840 蓝牙是另一个待处理项。
