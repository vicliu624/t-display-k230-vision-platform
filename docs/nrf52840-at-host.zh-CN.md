# nRF52840 官方 AT 主机端

这是 Linux 主机端会话组件与命令行入口，不是 BlueZ/HCI 适配器。
当前只完成主机协议组件；Quick Settings/顶栏尚未接入，实机 UART 对端仍未完成
身份识别，不能称为蓝牙已集成或射频验收通过。

## 固定协议依据

参考用户指定的 LilyGO 官方仓库及其 nRF 项目提交
`4646a728580739d487126f47a521e9b8032b3c2c` 的
[AT 协议](https://github.com/Xinyuan-LilyGO/T-Display-K230-nRF52840/blob/4646a728580739d487126f47a521e9b8032b3c2c/docs/AT_COMMANDS.md)和
[实现](https://github.com/Xinyuan-LilyGO/T-Display-K230-nRF52840/blob/4646a728580739d487126f47a521e9b8032b3c2c/src/main.cpp)。

- 115200 8N1、无 RTS/CTS，命令 CRLF；USB CDC 不是主机 AT 接口。
- `OK` 仅表示受理；扫描等待 DONE、连接等待 CONNECTED、GATT 等待带句柄和
  状态码的结果。正常发现结束允许 ATT attribute-not-found `0x10a`，读写只接受 0。
- 官方 `handle_connect` 把数字开头的参数解析为索引，包括数字开头的 MAC。
  主机必须在本会话的完整扫描结果中唯一匹配用户指定地址，再发送索引。
- 固件没有通用蓝牙电源总开关。停止 Meshtastic 广播不等于关闭整个 BLE 栈，
  此组件不冒充 BlueZ 的 Powered 属性，也不提供虚假的 power 命令。

## 使用与边界

镜像包 CMake 安装 `/usr/local/bin/tdvp-nrf52840`；未添加后台自启动、自动扫描、
自动连接或桌面菜单。默认执行只识别版本和读取状态：

```sh
tdvp-nrf52840
```

同一进程保持一个 UART 会话；每个命令是一个独立的命令行参数。确认测试外设
及无线操作范围后，才能执行相应操作。下面地址只是格式示例，不是已授权或
已识别的外设；句柄也必须从实际发现结果取得：

```sh
tdvp-nrf52840 --timeout-ms 10000 \
  'scan 5' 'connect AA:BB:CC:DD:EE:FF' \
  'services' 'characteristics 0' 'descriptors 0' \
  'read 37' 'cccd 38 notify' 'listen 5' 'cccd 38 off' 'disconnect'
```

`write HANDLE HEX` 仅发送带响应的写请求，最多 244 字节；不是任意 AT passthrough。
输出为 JSON Lines，响应、异步事件和已完成的请求分开表示，控制字符转义。
这是低层固定命令序列入口，不是交互式 BLE 浏览器；还需要常驻服务/应用接口、
GUI 选择与状态呈现、配对交互和真实外设数据验收。

所有权和失败处理：

- advisory flock + TIOCEXCL，禁止同组件并发；不声称能够撤销另一程序已打开的
  文件描述符。部署常驻服务前仍必须协调现有串口所有者。
- 首次接管先清理本机队列，严格核对官方版本前缀、OK 和完整状态；不识别为
  官方协议时不发送 BLE 命令。版本字符串不是密码学身份认证。
- 发现预存扫描/GATT 或中央连接，不擅自接管、断开或重置；可以读取状态。
- 绝对截止时间、行/累计字节限制；有界轮询且不调用阻塞 tcdrain。
- 错误、超时、复位事件或不匹配结果会使当前会话不可再用；没有自动重试。
  固件没有请求 ID，跨进程重开不能保证消除旧异步结果；因此不能把反复启动
  CLI 当成超时恢复机制，需要明确的设备空闲证据与所有权恢复设计。
- 普通退出及 SIGINT/SIGTERM 会恢复 termios、释放占用。SIGKILL/断电不能保证
  termios 恢复；结束程序也不等于撤销 nRF 已接受的无线操作。
- 不自动断开已连接外设。失败或中途退出可能留下连接；恢复需要明确判断，
  不能擅自复位/DFU 或更改主机供电引脚。

## 可重复验证

```sh
bash buildroot/tools/test-tdvp-nrf52840-at.sh
```

测试编译正式传输代码和 CLI，使用真实 POSIX PTY 与模拟 nRF 响应，覆盖分段
应答、身份拒绝、时限、取消、串口锁、注入拒绝、受理/完成分离、地址解析、
GATT 数据格式/句柄/状态、通知和断线。测试不访问真实 UART、不发射无线信号，
不生成蓝牙 acceptance 标记。CI 在完整镜像构建前执行该测试；镜像 preflight
另检查安装入口是可执行的 RISC-V ELF，但这仍不是硬件功能验收。

主机代码可以在等待 nRF USB 枚举时继续验证；正式上线仍必须取得真实模块
身份、稳定 UART、扫描、指定外设连接、GATT 读写/通知与断线恢复的证据，
并完成桌面接口接入。当前实机边界见
[nRF 集成缺口记录](nrf52840-integration-gap.zh-CN.md)。

## 2026-09-08 验证结果

- LAN `192.168.31.42` 的 Ubuntu 24.04 离线构建容器中，33 个正式传输代码 PTY
  场景全部通过；相同场景在 ASan/UBSan 下全部通过。
- 使用现有真实 SDK 的 GCC 14.1.1 / Linux glibc 工具链，完整编译硬件服务包，
  再执行正式 CMake DESTDIR 安装；入口确认为 ELF64 RISC-V double-float ABI。
- 该入口 SHA-256：
  `82f96103e79f32493de4b0d3b28d5a0a9d3eecce3f99923b65e3ce0915b09424`。
  LAN 候选目录为 `hardware-fixes-20260907/nrf-at-host.D41PSE`。
- 已部署到远端 `/usr/local/bin/tdvp-nrf52840`，保留副本于
  `/root/tdvp-nrf-at-host.X5HXzS`；安装前该路径不存在，没有覆盖旧程序。
  boot ID `940f0880-845c-4650-9a94-745faa0cf070`，仅执行 `--help` 验证动态加载
  和参数入口成功，UART1 计数前后完全相同。没有把此项称为 UART/BLE 通信通过。
- 部署后 CPU1 AI idle/error=0、completed=596；原 VGLite Labwc PID 961、
  `/dev/vg_lite` FD 18 不变。本轮没有再次发送无应答的 UART 查询。
- CI 增加了该 PTY 回归，镜像 preflight 增加安装入口检查；尚未据此完成新一轮
  整卡构建或硬件无线验收。GitNexus 对新增符号返回 UNKNOWN，linked worktree
  的 detect_changes 被工具拒绝；另外逐文件核对增量仅为 AT 组件、构建/验证和
  文档，没有改写现有 BlueZ/Quick Settings、VGLite 或 CPU1 实现。
