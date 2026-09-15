# CPU1 MMIO 解除映射：缺陷、修复与验收边界

日期：2026-09-08。这是一项 CPU1 启动修复的代码/构建回归记录，
**不是整卡冷启动、摄像头、AI 或蓝牙验收通过报告。**

## 交付前的测试缺口

此前远端单独部署验证过键盘背光程序，并观察过 VGLite 桌面运行；本地和 CI
执行过实际配对构建及成品审计，但没有完成该版整卡 CPU1 摄像头/AI 冷启动验收。
这些测试不能合并成“镜像已经通过远端全部硬件测试”的结论。

用户烧录 CI run `34149081510`（分支 head `717a76e`）后，实际 raw CPU1 槽位
SHA-256 为 `7fd2db97f6a5b92e51c885e832f4dbf141610ed2a043c4d063f84d50a5169b59`，
与该 CI 成品一致；不是仅凭 SSH 地址判断新旧卡。设备状态如下：

| 项目 | 新卡证据与结论 |
| --- | --- |
| 桌面 | 已登录，labwc 环境 `WLR_RENDERER=vglite`，持有 `/dev/vg_lite` |
| 键盘背光 | 用户实物确认“quick settings正常” |
| CPU1 | `startup_stage=i2c4-clock`、`startup_result=1`；ownership fault/-110 |
| 存活/摄像头 | 心跳 276、publication sequence 24 不推进；captured/published 为 0，未验收 |
| nRF52840 | 有界版本查询 TX=9/RX=0，无 HCI；未完成蓝牙集成 |

## 可以在构建机复现的缺陷

固定 SDK `abb07090ad8a666ed7a5e097b3c714b918731645` 的
`rt-thread/components/lwp/ioremap.c::_iounmap_range()` 会从传入地址调用
`rt_hw_mmu_unmap(..., ARCH_PAGE_SIZE)`。C908 `mmu.c::_rt_hw_mmu_unmap()`
根据首末地址计算页数，而映射返回值保留物理地址的页内偏移。

旧 I2C4 helper 映射 `0x911040a0` 的 4 字节，再对返回地址解除映射：
实际映射只有一页，但解除映射的起点偏移为 `0xa0`、跨度为 4096 字节，
计算结果为两页。第二页没有 PTE 时可能断言停住，有其他 PTE 时可能误解除邻页。
SDK 外层解除映射关闭中断，当前 RT_DEBUG 断言处理器自旋，足以解释心跳停住。
AI reset 对 `0x91101014` 的旧调用存在同类缺陷。

这是由固定 SDK 源码和可执行回归证实的越界缺陷，与现场最后阶段吻合。
**尚未取得现场 CPU1 assertion 串口或异常 PC，不把具体停机指令当成实测结论。**
旧 mock 仅返回数组并统计 map/unmap 次数，没有执行 SDK 页范围算法，因而漏检；
这是测试覆盖缺口，不是编译器或烧录工具可以替我们验证的行为。

## 修复范围

- I2C4：映射 `0x91104000` 整页，使用 `base + 0xa0` 访问原锁寄存器。
- AI reset：映射 `0x91101000` 整页，使用 `base + 0x14` 访问原 reset 寄存器。
- 两处都保留原始页对齐映射基址，仅用原基址解除映射。
- 不改变锁/中断顺序、寄存器写入范围、超时、所有权或错误保留策略。
  不修改 VGLite、Linux 桌面、共享 PLL、DDR 或 SDK 全局 MMU 实现。

GitNexus impact 没有索引这两个新增 CPU1 symbol，风险为 UNKNOWN，不能解释为
无影响。人工追踪确认它们分别进入 I2C4 BOARD 初始化和 AI 启动链路，属于启动
关键路径。提交前 detect_changes 拒绝识别该 linked worktree（Git worktree list
可以列出），因此使用限定文件 diff、源文件比对和下述回归复核，不声称图分析通过。

## 已执行的修复回归

环境：`vicliu@192.168.31.42`，实际编译容器为 Ubuntu 24.04
`tdvp-ci-prepared:20260907-hardware`，外层主机为 Debian；未在设备上热换 CPU1。

1. 新 `test-tdvp-cpu1-mmio-unmap.sh` 执行真实 production helpers，逐段提取固定 SDK
   `_iounmap_range`、MMU range/wrapper 和页大小定义。14 个生命周期场景通过；
   4 个旧偏移/邻页场景证实会触及第二页；重新植入两处旧调用的生产代码变体均被拒绝。
   仅分配器、叶 PTE 效果和硬件寄存器被模拟，不是 C908 cache/TLB/trap 或板级仿真。
2. I2C4、AI clock/reset/init、FFT PIO、真实 patched AI initializer、所有权及 Linux
   owner 回归通过；FFT PIO 模型通过不等于硬件数值精度通过。
3. 更新的 `test-tdvp-cpu1-rtsmart-build.sh` 使用生产入口和固定 SDK 连续构建两次真实
   RT-Smart/OpenSBI；新 MMIO 回归已经接入这个现有 CI preflight 入口。
   stale sensor archive member 清理和五条构建失败传播路径通过。
4. 实际 SDK 编译目录的两处 helper 与本次 staged 源码逐字节一致。
5. 键盘背光回归、VGLite renderer stack lock 及 46 个 Linux patch 结构校验通过。

本轮独立 CPU1 preflight payload：3,561,160 字节，SHA-256
`9b00a8dbc3a8af3dd06fb10a9ec8fccd90a4b0195e54f52bdc64375481cdc483`。
它不是整卡成品；后续镜像必须使用自身的配对 manifest 标识。

LAN 日志位于验证根目录 `hardware-fixes-20260907/mmio-unmap-tests.g0Lo4W/`。
完整生产 preflight 日志为 `real-preflight.WRHrLH.log`，SHA-256
`c42040f3fa80672dcae21e06ba850f13ec383cd858ab2d67e97632104ab0fc79`。
初次独立回归组在 renderer 测试因快照的空 vendor submodule 目录停止；补入只读
vendor 链接后重新执行 renderer 和 patch 校验通过。未跳过或放宽 production 检查。

## 仍必须通过的硬件条件

后续修复镜像只能标为“待硬件验收候选”。按
[配对镜像验收流程](cpu1-paired-image-acceptance.zh-CN.md) 完成整卡冷启动、
身份配对、持续心跳、成功所有权交接后，才允许请求实际 GC2093 帧。
必须继续验证 Linux 异步收帧、真实图像、AI 数值/模型、错误路径及 VGLite 共存。
蓝牙是独立未完成项，本修复既不包含也不授权 nRF 固件刷写。

fault/心跳停滞时不强行采集、不热重启 CPU1、不释放未知在途 DMA 缓冲；
不能用设备节点存在、编译 PASS 或新进度字段替代硬件功能验收。
