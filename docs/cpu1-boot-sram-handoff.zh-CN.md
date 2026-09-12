# CPU1 shared SRAM：启动解压器交接保护

## 发现与适用范围

K230 的 2 MiB shared SRAM 也供硬件解压器使用，解压器使用其中 768 KiB；
它不是禁用 Linux GNNE/AI2D 后就天然空闲的 CPU1 私有存储。
依据：[K230 datasheet 的 SRAM/SDMA 描述](https://www.kendryte.com/k230/en/main/00_hardware/K230_datasheet.html)。

本次核对的是锁定 SDK `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7` 的
`buildroot-overlay/boot/uboot/u-boot-2022.10-overlay/arch/riscv/cpu/k230/unzip.c`，
不是其他版本的上游 U-Boot。原文件 SHA-256：
`98e08062ba6cef480efdd4fe1a977d4b9fd69f797b586ec3ffbb1b0826ad06bd`。

实际 `a57e99c` 本地候选中的 `uboot/fn_ug_u-boot.bin`，在文件偏移
`0x254` 的压缩头为 `1f 8b 09`。锁定 BSP 的 `gunzip()` 对第三字节 `09`
选择 `k230_priv_unzip()`，因此这不是一个未被候选使用的死代码路径。
该硬件路径使用 SDMA 0/1、输入 SRAM `0x80280000` 和输出 SRAM `0x80200000`。

原控制流看到输出通道完成中断与 CRC 成功后，先释放两个 DMA 链表，随后恢复
SRAM 映射；没有在释放前显式确认两个通道都空闲。错误路径甚至在释放后才执行
原有复位。此源码证据说明原交接缺少必要检查，**不等于已经观测到板上发生 DMA
冲突或图像损坏**。Linux 的 `request_mem_region` 声明不能补上启动阶段的检查。

## 修改边界

产品 overlay 覆盖同一 `unzip.c`；保留原厂许可、格式解析、软件解压、DMA
配置及解码流程。相对锁定源的实质变化集中在 `k230_priv_unzip()` 收尾和新的
`tdvp_unzip_quiesce()`；另清理行尾空白，并修正两处原有寄存器指针的 signedness。

收尾顺序为：

1. 原有解压错误处理若被触发，在描述符仍保留时执行。
2. 只向解压使用的 SDMA 0/1 发送 STOP。
3. 使用 BSP 已采用的 `ch_status` bit 0 检查两个通道空闲；新增等待限于
   U-Boot 计时器报告的 100 ms，并在重试之间调用 `udelay(1)`。
4. 恢复原有 gzip SRAM 映射和通道 0 配置，读取寄存器确认写入结果。
5. 只有以上成功才释放描述符、失效目标缓存并返回加载器。

新增等待超时或读回失败打印：

```text
TDVP boot decompressor: DMA/SRAM handoff refused
```

随后进入 U-Boot `hang()`，不释放可能在途的描述符，不继续正常启动或把资源
交给 CPU1。它不是 CPU1 运行期复位/重启方案，也不改变 Linux 的 SDMA 归属。
本改动不新增 PLL、电源或其他 DMA 通道操作。

**限制：**原 BSP 的解压前 DMA 配置等待，以及解压错误后的 SDMA/GZIP 复位
等待仍有无软件截止时间的循环。本修复不声称整个 vendor 解压器都已具备有界
错误恢复，也不把 STOP 状态检查当作硬件时序、所有 SRAM 使用者或模型缓存行为
已通过实测的证明。发生启动失败应留存串口日志并使用旧卡回退，不能绕过保护。

## 已取得的证据

- `test-tdvp-boot-decompressor.sh` 提取实际生产函数体与寄存器结构进行编译，
  模拟 MMIO/时间/DMA 配置；10 个场景覆盖正常、延迟、分别卡住的通道、两种
  映射读回失败、CRC 错误、解压超时和计时回绕，并断言停止/读回发生在释放前。
- `test-tdvp-cpu1-boot-contract.sh` 覆盖有效配对和 16 个拒绝回归。
  新检查对比实际 U-Boot 构建目录的源码，并要求实际 `u-boot.bin` 与
  `spl/u-boot-spl.bin` 含有交接保护的失败分支文本；不会只接受 overlay 已放入。
  测试里的替代字节仅检查拒绝逻辑，不能证明固件已经编译。
  测试还复制生产 SDK 目录布局，并执行从 `post-image.sh` 提取的真实调用：
  SDK 会排除 `*-overlay`，因此同步后的板级脚本不能从自身相对路径寻找
  U-Boot overlay。打包入口明确传入 SDK staged overlay 作为第四个参数；
  缺失参考、参考变化、构建源码变化或二进制缺少保护仍必须失败。
- 2026-09-08，LAN Ubuntu 24.04 容器在禁用网络、独立复制的 U-Boot 构建目录中
  完成真实 `make all`。实际 SPL/U-Boot 与现有 CPU1 原始 payload 一起通过更新的
  boot contract 检查。原 `a57e99c` SDK 输出与完整候选镜像未被替换。

| 独立交叉构建产物 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `u-boot.bin` | 682392 | `ea4fd97d08a7a5c293d0cc22810d600e7d0da6930e34e04a75e1626288fcc0a6` |
| `spl/u-boot-spl.bin` | 206976 | `a95bad56ef932d74b750b8aba90302c66a24a16808f9297017c1f383fcd244bb` |

这些是构建证据，不是替换卡镜像，也不是板上启动成功的记录。其他 vendor 文件
仍有既存编译警告；没有以放宽测试的方式把它们标为已解决。

随后完整 SDK 构建在 `post-image` 校验处发现上述参考路径问题。核对项目 overlay、
SDK staged overlay、实际 U-Boot 构建源码三者 SHA-256 均为
`0081e8dbed71f8707178bd2e3b6af61c62bd915cdb5ab3c310375ea63dd93d6f`；
并不是实际 U-Boot 源码同步失败。明确传入参考目录后，真实完整构建产生的
SPL/U-Boot 与带 startup trace 的 CPU1 payload 通过同一校验器。SDK 布局测试、
16 个拒绝回归、解压器 10 场景、背光、patch reconciliation、VGLite lock 和
46 个 Linux patch 结构校验也通过；这仍不代替完整镜像打包和板上验收。

## 镜像版本不能混淆

已交付的 `a57e99c` 完整镜像**不包含本次启动保护**，其文件及哈希保持不变。
不能从它的启动结果反推本保护已验证；本修复必须经过后续完整镜像打包和整卡
冷启动验收。不要单独替换设备的 SPL/U-Boot 来拼成未审核的配对。

备用卡若已经开始验证 `a57e99c`，其串口、Linux 登录、所有权状态和初始摄像头
观测仍有诊断价值，但不能据此宣布最终 AI/shared SRAM 交付完成。最终候选还需
覆盖本保护、真实摄像头帧、VGLite 共存和 AI/FFT 数值及错误路径验证。

## 运行中设备的只读快照（2026-09-08）

为了区分“旧启动代码缺少检查”与“当前硬件确实仍在工作”，新增手动诊断：

```sh
sh buildroot/tools/tdvp-cpu1-boot-sram-snapshot.sh --read-mmio
```

它只接受 root、riscv64 和 `canaan,kendryte-k230` 设备树；没有显式参数时
不访问 MMIO。脚本不安装为开机服务，不修改 SPL/U-Boot、CPU1 payload、
设备树、时钟、电源、DMA 或解压器配置。所有 `devmem` 调用只有地址与
32-bit 宽度两个参数，没有第三个写入值。

寄存器依据是嘉楠发布的
[K230 Technical Reference Manual V0.3.1](https://download.kendryte.com/developer/k230/HDK/K230%E7%A1%AC%E4%BB%B6%E6%96%87%E6%A1%A3/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf)，
文件 SHA-256 `4a18fdbd1cd25c5a33ca4d54b78c95e0e338305bceb17397133370fa10c1e5f0`。
已核对 2.5.2.7 与 15.5 的完整相关寄存器表，包括 PDF 第 134、136、1186 页：
SDMA busy/pause 与解压状态是 RO；CH0 配置和 GZIP 输入长度是 RW，但此脚本
只读。不会读取 WO 的启动/停止字段，也不会写入 W1C 的中断状态。

在 boot ID `4b25f9ee-9e55-4ae5-bb6a-f9f016ba8f88` 上，实际脚本在 uptime
5467.70～5467.89 秒进行了三次采样，值均相同：

| 地址 | 含义 | 读值 |
| --- | --- | --- |
| `0x80800054`、`0x80800084`、`0x808000b4`、`0x808000e4` | SDMA 0～3 状态，bit 0 busy、bit 1 pause | 全部 `0x00000000` |
| `0x80800058` | CH0 配置，bit 10 解压模式 | `0x00000000` |
| `0x80808004` | GZIP 输入长度，bit 31 解压使能 | `0x00000000` |
| `0x8080800c` | 解压器原始状态 | `0x00001C00` |

最后一个值保留原始输出；不为手册未解释的位赋予成功、错误或总线空闲含义。
`snapshot_no_activity=1` 仅表示采样期间，已定义的 busy/pause 和解压使能位
没有置位。脚本同时明确输出：

```text
startup_order=not_proven
exclusive_ownership=not_proven
kpu_model_completion=not_tested
```

因此，当前没有观测到启动解压器仍占用这些资源；“旧 U-Boot”不能直接推导成
“现在一定有 DMA 冲突”。反过来，快照也不能证明启动时先停 DMA 后释放描述符、
保证未来没有其他使用者，或替代完整镜像冷启动验收。它不是启用 KPU 的通行条件。

另一个必须保留的边界：实际 Linux 的 `k230-gdma` 驱动占用
`0x80800000–0x80800fff`，桌面旋转使用的 GDMA 与 SDMA 共享控制器全局寄存器。
不能通过解绑该驱动、重置整个控制器或清除全部中断来“确保 CPU1 独占”。
本次仅审查了驱动及读取上述白名单，没有执行这些动作。

脚本运行前后，Labwc PID 656 的 fd 18 均打开 `/dev/vg_lite`；CPU1 AI 的
256 个已完成任务和摄像头向 Linux 交付的 300 帧计数未变化，所有权/AI/摄像头
错误均为 0。KPU 仍是 `unavailable`。本次没有新增模型推理或摄像头采集测试。

`test-tdvp-cpu1-boot-sram-snapshot.sh` 的 18 个模拟场景分别在 LAN 主机与
Ubuntu 24.04 构建容器中通过，覆盖参数/平台拒绝、7 个地址各读 3 次且无写值、
四个通道 busy、pause、两个解压使能条件、读取失败及非法返回值。
真实板上使用的是同一个生产脚本，而非测试替代实现。
