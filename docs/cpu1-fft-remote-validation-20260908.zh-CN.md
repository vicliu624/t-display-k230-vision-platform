# CPU1 FFT：注册接口修复与远端数值验证

## 范围

本轮接续 `fe2c514` 的真实 GC2093 采集验证，检查 CPU1 `/dev/fft_device` 的硬件 FFT。
不是 Linux FFT 后端，不使用系统 SDMA，不改变共享 PLL/电源或执行 AI 运行期 reset。
FFT 驱动既有 PIO、超时、所有权检查与错误锁存规则保持；本轮修正注册和中断完成顺序。

独立诊断源码在 `buildroot/tools/tdvp-cpu1-fft-selftest.c`。正式 worker 不引用它，
镜像不会自动启动数值测试。临时实机候选才在 ownership-ready 后调用一次，写入
producer reserved[4..14] 的 11 个字；编译期按真实 control 结构检查范围，主机测试
检查前后区域 canary。它不使用 ownership 页，也不改既有 capture trace[0..3]。

测试固定以下条件，不根据实机结果放宽阈值：

- 正向 FFT，shift=0；64/128/256/512/1024/2048/4096 点。
- RIRI、纯实数 RRRR、RR_II 三种输入，以及 RIRI_OUT、RR_II_OUT 两种输出。
- 首点幅度 128 的脉冲、全 1 直流、N/4 位置幅度 128 的脉冲。
- 所有输出 bin 与闭式数学结果比较，实部和虚部绝对误差各不超过 1 LSB。
- 共 126 组；首个系统调用失败或数值失配立即停止，不重试错误硬件。

点数、排列及逐级 shift 字段与固定 MPP `k_fft_ioctl.h` 对照，也参考
[K230 官方 FFT API](https://www.kendryte.com/k230/en/v1.6/01_software/board/mpp/K230_FFT_API_Reference.html)。
本轮不覆盖 IFFT、非零 shift、所有幅度/溢出组合或语音识别模型。

主机测试使用独立 double-complex radix-2 软件 FFT，而不是复用诊断的闭式期望计算。
覆盖全部 126 组，并注入 open 拒绝、ioctl 超时、错误结果、close 失败与中途超时。
这是验收程序回归，不是硬件通过。Ubuntu 24.04 容器中已通过。

## 实机发现：设备注册清空 fops

首次诊断候选 raw SHA：
`bc80ac7f5ad8eb26b4a2c09c612dcff27e06fcd861f7e191f6464e46d6571b09`，
slot SHA：`a597f9df562c50f181992ee5656340cb89a5dade95ba3de6cf17fe2e78db713f`。
boot ID `01488eca-84ef-4ad5-8352-6bafddc2a0f7`。

证据记录停在 prepared，current-case=0/completed=0；没有进入第一组 ioctl。
随后 ownership fault=-110。停止所有追加请求，没有热复位、重授予或 DMA 回收。
当前 CPU1 线程为 tdvp-vision-worker，栈中有 `sys_open` 和 open syscall 上下文。

固定 RT-Smart `rt-thread/src/device.c:49` 的 `rt_device_register()` 在
`RT_USING_POSIX` 下明确执行：

```c
dev->fops = RT_NULL;
rt_wqueue_init(&(dev->wait_queue));
```

原 FFT 初始化在这个调用前赋值 `fft_device.fops = &fft_ops`，因此被清空。
按该诊断 ELF 和真实结构布局，设备 ops/fops 所在地址的实机读取也均为零。
devfs 在 fops 为空时回退到普通 device-open；此配置的普通设备 ops 指针为空，
不能作为有效后备接口。源码和栈证据把问题定位到设备打开路径，而不是 FFT 算法。

正式修复把 fops 安装移到 `rt_device_register()` 成功之后，AI-ready／worker launch
之前；注册失败不安装接口，IRQ 仍保持屏蔽直至合法任务开始。

之前的 FFT mock 保留了注册前的 fops，漏掉这个 SDK 行为。现在回归从固定 SDK
真实 Git blob 中提取并执行整个 POSIX 注册块；还构造恢复旧顺序的临时 driver，
确认其 open gate 断言失败。完整 CPU1 preflight 同时提取 `rt-thread/src/device.c`，
不会仅在复用完整 SDK 文件树时通过。这个缺口先在 Ubuntu 24.04 的本地 preflight
暴露，补齐后通过，没有等 GitHub Action 才发现。

## 配对部署

每轮仅更新已验证的 `/dev/mmcblk1` 10–30 MiB CPU1 槽，备份、核对旧 boot ID／旧槽、
完整回读后才整板软件重启。boot p1、前 10 MiB、Linux bridge 与上一轮验证一致；
不是整卡新镜像或断电冷启动验收。

修复后的诊断候选 raw SHA：
`23bc2867ef3425334a43ce0605f04599fd90fcee07853397f9b517dc46d3f73e`，3828680 字节；
slot SHA：`890498f92e8afdd2ff316a56726026450a80e1f2ef673b8e2160d947f32ea310`。
诊断 worker SHA 两轮相同：
`d2499cb79d0ef5c189f12c1250d075fa42f5f61c6c18605f7913a56918c933c5`；变化只在内核注册顺序。
回退备份保留在 `/root/tdvp-cpu1-fft-numeric.rhpUIQ`、
`/root/tdvp-cpu1-fft-fixed.NaIKmp`。部署脚本绑定具体旧状态，不可重复盲用。

GitNexus 的旧索引没有这些 CPU1 符号，impact 返回 UNKNOWN；已人工核对直接调用为
AI init → FFT init → worker 打开设备。没有把缺索引当作无影响或修改工程师的脏工作树。

## 第二个实机缺口：PLIC completion 时仍屏蔽 IRQ

注册修复后的 boot ID `865d4dbc-a9d2-4789-a27d-11168201ec79`：
第一组 64 点 RIRI/RIRI_OUT 首点脉冲全部输出正确，最大误差 0；第二组直流任务
超时，completed=1/current=2/result=-2。此处 -2 来自驱动的 `-RT_ETIMEOUT`，
不是文件不存在；RT-Smart 错误码与 POSIX errno 的对应仍需单独处理。
驱动按既有规则锁存 AI fault，未追加请求或热复位。

固定 SDK `mpp/kernel/fft/src/fft_dev.c` 的 ISR 在清除 FFT 中断后重新启用 IRQ。
`bsp/maix3/c908/trap.c` 的 `generic_handle_irq()` 则在设备 ISR 返回后调用
`plic_complete()`。我们的 ISR 缺失最后的 unmask，使 dispatcher 对禁用源的
completion 被忽略，gateway 无法继续递交第二个任务的中断。这与
[RISC-V PLIC 规范的 Interrupt Completion 规则](https://github.com/riscv/riscv-plic-spec/blob/master/riscv-plic.adoc#8-interrupt-completion)
一致。修复仅恢复 ISR 返回前的 unmask；ioctl 收尾仍停止 FFT、屏蔽 IRQ。

回归模型补上 claim/completion 状态：禁用时 completion 无效，未完成 claim
阻止后续 IRQ。正常连续 84 个配置调用通过；只删除 ISR unmask 的负向变异在第二次
ioctl 失败。没有放宽超时、增加硬件重试或引入共享 reset。

### 实机数值结果：126/126，最大误差 0

PLIC 修复候选 raw SHA：
`2142b23e1f4bf45f3caf10315ed85dbbbf0a9e01038d6c3d71f8bf42d856fccb`，3828696 字节；
slot SHA：`f33d9cdb0069840af9d898e4de2192242831d794bd3f24279a957154368a9732`。
诊断 worker 仍为 `d2499cb79d0ef5c189f12c1250d075fa42f5f61c6c18605f7913a56918c933c5`，
没有修改输入向量、比较器或阈值。

boot ID `7d8e12b8-87e3-4c78-b707-820aec0913db` 的实机证据：

```text
magic=0x31544646 state=3 completed=126 current=126
result=0 max_abs_error=0 reference_version=1
ownership_state=ready ownership_error=0
vision_state=idle vision_error=0 reader_open=0
```

接着通过 Linux bridge 接收 30 帧，sequence=1..30，
PTS=106230793..108232417，93312000 字节，最后帧 FNV1a64=05ea1ceb95d673b6。
关闭后再次确认 idle、reader=0、error=0。greeter 的 Labwc PID 236 持有
`/dev/vg_lite` fd 18；不是已登录用户桌面的截图验收。
备份保留 `/root/tdvp-cpu1-fft-plic.hZpQCd`。

## GNNE / AI2D 的同类注册问题

顺查 `0003-rtsmart-ai-initialization-errors.patch`，GNNE 与 AI2D 配对初始化
也在 register 前赋值 fops。改为：映射/事件 → register（初始化 wait queue）→
fops → 安装 ISR → unmask。注册失败仍不发布 ready，不启用 IRQ。
补丁添加行总数不变，不改变其他 driver 分支或运行期 poll/lock 语义。

两个 initializer 的测试也执行固定 SDK 的真实 POSIX 注册块，并在 IRQ 安装/
启用时断言有效 fops、wait queue；分别把 GNNE、AI2D 的旧顺序变异回来，均必须
失败。Ubuntu 24.04 的完整 CPU1 preflight 两次构建及 Linux DT 配对通过。
这本身不是 AI2D 数据处理或 KPU 模型推理验收。

### 同次实机启动的接口与 FFT 复验

临时接口候选 raw SHA：
`f2a75e74cc875c39420f98c6cd461e525543379ddfc0dcfcf0932447f24dfb7c`，3828664 字节；
slot SHA：`d73530885c82dd16e6c433f3b1f0ae6176aa6bda3c62de90637f36130eb408be`；
诊断 worker SHA：`12fc1f0895e2bf6e335ec8ee68f2f960088c56e1d80699f52144cd580bc596cd`。

boot ID `a112132a-7aeb-4c06-b5d3-19c78d48fb10`：先对 `/dev/gnne_device` 和
`/dev/ai_2d_device` 各执行 open、poll(timeout=0)、close，要求无等待、无意外事件、
无错误；不调用 ioctl、不锁定加速器、不申请 DMA、不加载模型。然后执行原样的 FFT
126 组数值检查。全部成功后才在同一个 evidence 的第 9 字标记 interfaces=2。

实机读取 state=3、completed/current=126、result=0、max-error=0、interfaces=2；
ownership ready、vision idle、两侧 error=0。备份目录
`/root/tdvp-cpu1-ai-interfaces.EMoyhj`。诊断入口只在临时 LAN 候选中，不并入正式 worker。

## 正式候选与验收边界

恢复正式 worker 的候选 raw SHA：
`b73b405e1f0b3ad1b059a166996afee076c98bdb286e6f14b1f0343c823ba4a6`，3828552 字节；
slot SHA：`3de08b824e7b011b36b10117d9b31353c3f2dee7f61f7c46cc567bf9284a869f`。
正式 worker SHA：`b1b81af629226818b281d233ae04f81bb8329a7299a0dd7a2cfd649bffa99958`，
与此前摄像头通过的生产 worker 相同，没有自动 FFT / 接口诊断入口。
整槽写入、回读、boot p1 / 前 10 MiB / Linux bridge 不变的检查已通过，
备份保留 `/root/tdvp-cpu1-ai-production.z6mnCt`。

正式候选启动 boot ID `0c810bc9-1668-4647-ad3a-0bb41c98a1f6`：ownership ready、
vision idle、error=0；FFT evidence 为零，确认未自动跑诊断。greeter Labwc PID 235
持有 `/dev/vg_lite` fd 18。30 帧通过，sequence=1..30、PTS=70340452..72496208、
93312000 字节，最后帧 SHA256：
`f274b133e42ff64e363289e88fe08b19c35572326e046fdb6d3f499ba87e4bfb`。

随后 300 帧请求误用了 15000 ms **整轮**预算，在 169 帧时超时退出，不能记为通过。
`frame_probe_run()` 的 deadline 从整轮开始计算，包含每帧 Linux read、逐字节 FNV
及亮度扫描开销；不是每帧等待时限。退出后 ownership ready、vision idle、reader=0、
所有错误为零，first capture fault=0；frames_delivered=199（30+169）。保留这个未达
15 秒整轮要求的结果，不修改代码/驱动超时，也不把后续较长预算当作性能达标。

同一次正式启动重新打开，明确设置 300 帧 / 60000 ms 整轮预算后通过：
sequence=206..505、PTS=212577200..235886522、933120000 字节，
FNV1a64=69a2481f26683342，最后帧 SHA256：
`21bd508f661eb16ec23d6a0fbddb109f568e490db962d4e22062089c5b478714`。
最终 ownership ready、vision idle、reader=0、两侧 error=0、capture first fault=0；
delivered=499（30 成功 + 169 预算内收到但整轮失败 + 300 成功），captured=1151、
published=508、dropped=642。该 bridge/逐帧校验负载包含自然背压，未证明 30 FPS。
登录界面仍为 greeter Labwc PID 235，`/dev/vg_lite` fd 18；没有自动登录或改回其他 renderer。

同一份最终源码在 Ubuntu 24.04 再次完整 preflight 两次构建，raw/slot SHA 与上面
部署版本一致。Linux 46 个 patch、CPU1 4 个 patch 的结构检查及真实 reconciliation
测试通过；VGLite session Gate 静态测试通过。renderer-stack-lock 首次在仅 CPU1 的
稀疏候选中缺少 vendor VGLite 源，补充只读挂载相同固定 SDK
`5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7` 的真实 package/Git 元数据后通过，
未跳过身份哈希校验或修改 VGLite 源。

不能把本轮结果扩展为 KPU 模型、AI2D 数据处理、语音识别或全部 AI subsystem 通过。
完整新镜像的 SPL/U-Boot handoff、断电冷启动，以及 nRF52840 蓝牙仍是后续独立验收项。

提交前调用 GitNexus detect_changes 的 linked-worktree all / compare(main) 均被工具
错误拒绝为“not a worktree”，Git 自身 worktree list 确认路径和分支有效。canonical
VGLite 工作树相对 main 的结果为 HIGH，属于另一个工作树的已有变更，不可冒充本次
CPU1 增量报告。按当前 git diff、文件白名单、真实 preflight 和实机记录人工复核；
未改动 Linux/VGLite/桌面实现，未重建工程师的旧索引。
