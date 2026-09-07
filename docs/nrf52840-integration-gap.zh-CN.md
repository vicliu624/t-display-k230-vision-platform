# nRF52840：当前镜像的集成缺口与验证边界

核对日期：2026-09-08。设备是新卡 `a57e99c`；背光程序的独立修复不改变
该卡的 DTB、Linux、CPU1 或 nRF 固件。本记录不是蓝牙验收通过报告。

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
应用版本。nRF9151 的 GPIO2 使能不是 nRF52840 的使能，不能混用。

## 后续实现与验收要求

先确认实物与 nRF 固件身份，再确定主机协议：保留官方 AT 应用需要实现并
验证独立 BLE AT 后端；选择 HCI 则涉及另一套板级 nRF 固件及刷写验证。
两者能力和接口不同，不能把 AT 后端冒充 `org.bluez.Adapter1`。
任何固件更换必须先取得用户许可、明确恢复路径，并保留必要的供电控制。

无论选择哪条路线，都需要真实设备证明：身份/版本、稳定 UART 通信、BLE
扫描、指定测试外设连接、GATT 读写/通知、断线恢复，以及桌面状态与实际结果
一致。仅安装程序、返回模拟成功或生成 acceptance 标记不能替代这些验收。
