# nRF52840：当前镜像的集成缺口与验证边界

核对日期：2026-09-08。初次检查基于新卡 `a57e99c`；随后 CPU1 与 Linux bridge
已做配对远端升级，下面分别记录初次检查和升级后的复查。nRF 固件未改写。
蓝牙功能尚未通过验收。

## 两个独立问题

1. **UART 对端未完成身份识别。** 新卡 UART1/IO3/IO4 已启用并绑定驱动，
   但一次有界 `AT+VER?` 查询没有收到任何字节。不能据此判断模块不存在，
   也不能假定其运行的是官方 AT 应用或某种 HCI 固件。
2. **当前桌面蓝牙后端与官方固件的协议不同。**
   `user-space/vicliu-pocket-linux-hardware/src/hardware/bluetooth.cpp` 中的
   `bluetooth_hci_present()` 只检查 `/sys/class/bluetooth/hci*`，随后
   `append_bluetooth_state()` 查询 `org.bluez.Adapter1`。官方 nRF 应用则
   通过 UART AT 提供 BLE Central/GATT 和 Meshtastic BLE bridge。
   即使问题 1 修复，当前后端也不会自动把这个 AT 模块变成 BlueZ adapter。

因此“系统有 `/dev/ttyS1`”“安装了 BlueZ”“桌面显示蓝牙图标”均不能单独
证明 nRF52840 的发现、连接、GATT 数据或 Linux 应用接口已经集成。

## 官方依据

官方 nRF 固件核对到提交 `4646a728580739d487126f47a521e9b8032b3c2c`：
[固定版本 README](https://github.com/Xinyuan-LilyGO/T-Display-K230-nRF52840/blob/4646a728580739d487126f47a521e9b8032b3c2c/README.MD)。

- K230 GPIO3 TX → nRF P0.11 RX；GPIO4 RX ← P0.12 TX；115200 8N1，无硬件流控。
- 主机接口是 Serial1 AT，USB CDC Serial 提供独立调试日志。
- 文档明确指出：仅有 UART DFU bootloader、没有 AT 应用时，AT 也不会应答。
  这只是当前无应答的一种可能，尚未在用户设备上确认。
- nRF P0.04 还用于外部 5V 升压使能，给主机供电。替换成不保留这个板级
  行为的固件可能影响供电，不能直接套用通用 nRF HCI 示例。

本地官方 K230 仓库核对到 `d03068ed3b71dde2789b283a0df2c00500bfc0b4`：
`k230_launcher/k230_phone_ui/src/ui_nrf52840_manager.c` 使用 `/dev/ttyS1`
查询 `AT+VER?` 并识别 `+VER:K230_NRF52840_AT,`，没有把这个 AT 串口
挂接成 Linux HCI。其 UART、Meshtastic、BLE 和 DFU 操作有明确资源归属。

## 新卡已取得的设备证据

- DT `/soc/serial@91401000/status` 为 `okay`；ttyS1 绑定 `dw-apb-uart`。
- pinctrl 的 IO3/IO4 均归 `91401000.serial`，function 为 `alt3`。
- 查询前未发现 UART1 正被其他进程打开。
- 以独占串口、有界等待、恢复原 termios 的临时诊断程序发送
  `AT+VER?\r\n`，UART 计数增加 TX 9、RX 0，2.5 秒内无应答。
  未发送 reset、DFU、scan、connect 或射频命令；未重复刷写或猜测波特率。
- `/sys/class/bluetooth` 不存在；没有 HCI。BlueZ 工具已安装，但服务未运行。
  当前后端在没有 HCI 时不会仅为界面显示强行启动 bluetoothd。

该查询程序只做受控现场诊断，不作为后台服务或自动探测固件的交付实现。
需要通过实物确认或 nRF USB CDC 日志进一步确定模块、供电、bootloader 和
应用版本。GPIO2 控制 nRF9151，使能控制必须按器件区分。

## AI 配对升级后、已登录桌面的复查

2026-09-08，boot ID `940f0880-845c-4650-9a94-745faa0cf070`。
CPU1/KPU 修复的构建、部署和数值结果见
[远端验证记录](cpu1-kpu-remote-validation-20260908.zh-CN.md)。
本次只复查状态和 UART 查询，不把此前 AI 数值结果计为新跑的测试。

- Labwc PID 961，`WLR_RENDERER=vglite`，FD 18 打开 `/dev/vg_lite`。
- AI `submitted=accepted=completed=596`，`error=owner_error=0`；
  KPU `starts=completions=6804`。Vision ownership ready、startup complete、
  error=0，累计送达 600 帧。
- pinctrl 中 IO3/IO4 仍归 `91401000.serial`、alt3；UART1 时钟 50 MHz，
  pclk 100 MHz，均已使能；查询前没有进程打开 `/dev/ttyS1`。
- 使用官方 K230 提交 `d03068ed3b71dde2789b283a0df2c00500bfc0b4` 的
  [未改写查询工具](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/d03068ed3b71dde2789b283a0df2c00500bfc0b4/k230_launcher/k230_phone_ui/src/k230_nrf52840_dfu.cpp)，
  只走 `--at 'AT+VER?' --at-read-ms 1200 -p /dev/ttyS1` 分支。
  独立 guard 获取 advisory flock、保留 termios，并给子进程设置 5 秒 alarm；
  不调用默认固件更新路径。程序 SHA-256 为
  `c58fad9c295dbdf70f17754309dbed0701e0463249e98a5ca321f77055c2ac80`，
  guard 为 `41049d556f617e259f117e0ca572a6dceafbb5eb5588278b41e5fbf8f90d6c79`。

```text
before: UART1 tx:0 rx:0
at error: no AT response
OFFICIAL_AT_QUERY_EXIT=1
after:  UART1 tx:9 rx:0
PASS UART termios restored
PASS UART1 released
```

查询后 VGLite PID/FD 不变，AI idle/error=0，完成计数仍为 596。
没有发送 DFU/reset、改变引脚/供电、扫描、配对或写 GATT。
K230 的 USB 枚举仅看到 Realtek 8152 网卡和根集线器，未看到 nRF USB CDC；
这不证明 nRF 副板不存在，其独立 USB-C 可能没有接到该 USB host。

该结果排除了“仅自写探针的版本解析有误”这一解释，但不证明 TX/RX 的实际
电气波形正确，也不能确定出厂固件身份。下一项需要外部证据：把 nRF 副板的
独立 USB-C 接到电脑，确认 USB 设备名/VID/PID/串口，必要时读取 115200 CDC
日志。不要为读取日志短接复位、进入 DFU、使用 1200-baud touch 或刷写文件。
身份确认后仍需实现和实测 AT BLE 后端；不能把 UART 静默和 HCI-only 后端
混为一个已经解决的问题。

## 后续实现与验收要求

先确认实物与 nRF 固件身份，再确定主机协议：保留官方 AT 应用需要实现并
验证独立 BLE AT 后端；选择 HCI 则涉及另一套板级 nRF 固件及刷写验证。
两者能力和接口不同，不能把 AT 后端冒充 `org.bluez.Adapter1`。
任何固件更换必须先取得用户许可、明确恢复路径，并保留必要的供电控制。

无论选择哪条路线，都需要真实设备证明：身份/版本、稳定 UART 通信、BLE
扫描、指定测试外设连接、GATT 读写/通知、断线恢复，以及桌面状态与实际结果
一致。仅安装程序、返回模拟成功或生成 acceptance 标记不能替代这些验收。
