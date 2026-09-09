# CPU1 AI 状态发布修复与远端验证（2026-09-09）

## 问题和修改范围

用户烧录 `tdvp-k230-labwc-desktop-1248157a7982a4cef495c06d253df8f05f3801a6`
后，真实 `/dev/tdvp-ai` 已能完成 KPU 固定模型推理，但硬件状态发布器仍沿用
“摄像头接管完成、AI 尚无模型”的阶段逻辑，返回 `kpu_available=0` 和
`kpu_runtime=cpu1-rtsmart-no-model`。

修复仅修改 `user-space/vicliu-pocket-linux-hardware/src/hardware/cpu1_vision_status.cpp`
及接口注释、回归测试。现有 `collect_state()` 同时供 `vpl-hwctl`、硬件 daemon
和 Quick Settings 的 GET_STATE 使用，三者不另造一套判断。
SDK 的 `stage_component_sources()` 从这个 user-space 源目录同步正式 package，
并校验源清单；没有依靠板上手改配置实现功能。

新逻辑要求：

- 实际 `/dev/tdvp-ai` 和 AI bridge driver 均存在。
- CPU1 资源所有权 ready；AI 状态独立读取 `/sys/class/misc/tdvp-ai/status`。
- 识别当前配对镜像 ABI 1、`cpu1-ai2d,fft,kpu-kws`、`kws-reference` 和 FFT 能力。
- 校验必填字段、重复字段、记录长度、数字范围、任务计数次序和阶段一致性。
- `idle/running/result` 且 AI/owner error 均为 0 才发布可用；pending、fault、
  接口缺失、读取失败（包括 sysfs 暂时 EAGAIN）或非法记录保守发布不可用。
- 无效记录的计数和错误字段发布 `unknown`，不复用上一次成功状态或伪造 0。

## 状态契约

| 字段 | 含义 |
| --- | --- |
| `kpu_available` | 当前 CPU1 固定 KWS 后端可用；不代表任意模型或即时获取独占客户端成功 |
| `kpu_runtime` | 有效 ABI 对应 `cpu1-rtsmart-kws-reference`，不再报告无模型 |
| `kpu_reference_runtime_available` | 当前固定参考模型运行路径可用 |
| `cpu1_ai_status_valid` | 本次 AI sysfs 快照通过结构校验 |
| `cpu1_ai_available` | 所有权、后端阶段和错误检查均通过 |
| `cpu1_ai_abi/backend/kpu_jobs/fft_jobs` | 从配对 AI 接口读取的 ABI 和能力 |
| `cpu1_ai_state/active` | bridge 阶段及请求在途状态；不是物理加速器瞬时 busy 寄存器 |
| `cpu1_ai_client_open/pending/detached` | 客户端、在途请求和脱离客户端的状态 |
| `cpu1_ai_submitted/accepted/completed` | 此 boot 的真实任务计数 |
| `cpu1_ai_error/owner_error` | 实际错误码，未读到有效数据时为 unknown |
| `cpu1_ai_ai2d_available/fft_available` | 当前配对后端的 AI2D/FFT 可用性 |

可用不等于验收通过。即使完成计数非零，`kpu_acceptance=unverified`、
`kpu_acceptance_state=cpu1-reference-unverified`，`kpu_functional` 和
`kpu_acceptance_passed` 仍为 0，不信任旧 Linux acceptance 标记。
现有 sysfs 未暴露当前请求的 operation，不能把共享 AI running 状态冒充 KPU
正在执行，故 `kpu_active` 保守保持 0，实际共享任务活动由 `cpu1_ai_active` 表达。
旧 Linux GNNE/AI2D 设备和 Linux acceptance service 字段仍为 0。
旧 `cpu1_ai_initialized` 保留其资源所有权初始化含义；客户端应使用新增 AI
接口状态判断模型运行路径，而不是单凭所有权初始化。

状态只是只读采样，不是获得资源的授权或提交保证。实际应用仍需打开异步设备、
处理占用/错误，并使用 poll/read/write 协议。状态发布器不打开 AI job device，
不启动摄像头、不写 mailbox、不改 CPU1 生命周期。

## 构建和自动测试

LAN `192.168.31.42` 的 Ubuntu 24.04 离线容器中执行：

1. `test-tdvp-cpu1-vision-status.sh`：原 camera/ownership observer 和新增 AI
   状态用例全部通过。覆盖独立端点、正常/在途/result/fault、owner 拒绝、
   缺失和重复字段、非法枚举/数字/能力、计数倒置、超长/空记录、旧标记拒绝和恢复。
2. 相同生产 C++ 状态实现与测试以 ASan/UBSan 编译运行，通过。
3. 真实 SDK GCC 14.1.1 / glibc 工具链执行正式 CMake 全包交叉构建和 DESTDIR 安装，通过。
4. 同一状态测试交叉编译为 RISC-V，在目标板以普通 tdvp 用户运行，通过。

现有 `.github/workflows/ci.yml` 已调用此测试入口，新增场景会随镜像 CI 执行。
本次没有重建内核、CPU1 固件或整卡镜像。

## 部署与回退

Boot ID：`e4b53bf7-e75c-4e82-a819-2a561e883743`。
先在临时目录运行候选 `vpl-hwctl status`，确认其读到真实端点，而已安装的
旧 daemon 仍在同一设备上返回旧值。对旧/新程序 SHA、boot ID、CPU1 idle、
VGLite PID/FD 做前置校验，备份后以同文件系统 rename 替换两个用户态 ELF。
只重启 `vicliu-pocket-linux-hardware.service`，新 PID 为 6769；其 `/proc/PID/exe`
与部署候选字节一致。

| 程序 | 新 SHA-256 |
| --- | --- |
| `vpl-hardwared` | `d5600db78f0f8582a7af3c72367544fd55572b9142f82773f46a6825225f889c` |
| `vpl-hwctl` | `a523c9e8a76f74e28cc15d5a9a14d4d860c0dc5a0908cb766193b840f14f5fc7` |
| 原生状态回归测试 | `4c907ab867cf2eb0cb229d3389dc8bca27bfec8125d0a807417fdb47daf06181` |

原程序保留于 `/root/tdvp-ai-statusfix-backup.7T0XRM`，部署入口设置验证失败自动恢复，
本次未触发回退。没有替换服务 unit、配置、库、nRF 客户端、Linux 模块或启动分区。
键盘亮度前后为 100；Labwc PID 431、`WLR_RENDERER=vglite`、FD 18 的 `/dev/vg_lite`
保持不变。greetd 配置、Labwc 和 CPU1 bridge 模块的哈希前后完全相同。

## 部署后真实任务与桌面接口验证

不是复用之前新卡检查的数值结果：部署后额外运行了以下测试，所有任务客户端
均为普通 tdvp 用户：

- 64 次 KWS 参考推理，1344 次 GNNE 硬件完成，最大绝对误差 0。
- 112 次 FFT/IFFT 数值任务、22 次 AI2D 用例以及 2 次 detached 丢弃任务，通过。
- 再读取 300 帧 1920×1080 NV12，共 933120000 字节，sequence 304..603 连续。
  未保存/下载画面。KPU、FFT、AI2D 前后采样均为 camera running。
- 通过正式 Quick Settings Unix socket 采样 24 次：全部 result=ok、status_valid=1，
  9 次 running、15 次 idle，运行中可见真实 pending/计数变化，不把 KPU acceptance 改成 passed。
- 最后正式 daemon 文件和 GET_STATE 均为 kpu_available=1、AI idle/pending=0，
  completed=468，与 kernel sysfs 一致（部署前 268，加本轮 200）。
- 最终 kernel KPU starts=completions=4116，vision 总送达 600 帧，所有权 ready，
  AI/owner/vision 错误均为 0。无失败 systemd 服务。
- 最终 GET_STATE 响应 4867 字节，小于固定 Quick Settings v0.2.3 的 16384 字节上限。

没有在真实加速器上注入超时/所有权故障或 reset；故障发布分支通过宿主机和
目标机的生产解析器 fixture 测试验证。固定 KWS 不是 ASR/STT；本次不增加任意
模型、摄像头预览应用或全新视觉应用。

原始设备日志在 `/tmp/tdvp-ai-statusfix.p7J5fg/logs`，本地副本在
`.tmp/device-validation/cpu1-ai-statusfix-20260909/logs`；均不含摄像头画面。
GitNexus 对新符号返回 UNKNOWN，detect_changes 拒绝实际存在的 linked worktree，
因此没有声称图谱审查通过；另以 Git worktree/diff、直接调用方、正式 staging
路径和上述测试检查本次范围。

## 蓝牙仍未修复

本次用户态 AI 状态修复不改变 nRF 行为。UART1 前后仍为 TX=9、RX=0，
没有再次盲发查询。硬件 daemon 重启会重做其既有 nRF9151 检测，UART2 的 TX
由 29 增至 58；这不是 nRF52840 的应答。

真实 nRF52840 尚无身份响应，桌面 Bluetooth 仍保守为 unavailable。需要 nRF
副板自身 USB 枚举/调试日志或连线信号证据才能继续定位；未刷固件、进 DFU、
复位或伪造 HCI。详见 `nrf52840-integration-gap.zh-CN.md`。不能把这次 KPU
状态修复和部署成功写成蓝牙也已修复。
