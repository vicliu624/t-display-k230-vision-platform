# T-Display K230 V1.3 验收清单

先执行 [硬件基线验证](hardware-baseline-validation.zh-CN.md) 中的只读检查，
再安排功能测试。记录镜像、boot ID、实物型号和每项结果；这份清单本身不代表验收通过。

- [ ] 整卡启动与重启成功，SSH 可连接；CPU0 Linux 与 CPU1 固件配对。
- [ ] CPU1 的视觉与 AI 状态有效，真实 GC2093 帧可经 Linux 异步接口取得。
- [ ] 支持的 AI2D、FFT/IFFT、固定 KWS 作业通过数值检查，无资源冲突。
- [ ] 登录页和桌面均由 VGLite 合成，无 Pixman 会话；错误日志和运行句柄有记录。
- [ ] 登录所选账户、错误密码拒绝、空闲 300 秒锁屏、330 秒息屏、唤醒密码窗口和解锁正常。
- [ ] PCManFM、wf-panel-pi、Quick Settings 正常；菜单可启动 Foot。
- [ ] Menu、Fn、触摸、长按右键、工作区切换正常。
- [ ] Quick Settings 的键盘背光关闭/低亮/高亮与实物一致。
- [ ] Wi-Fi 连接与重连正常；接入 USB 网卡时按实际接口名验收。
- [ ] 音频播放、录制、音量控制及系统事件声音正常。
- [ ] 电量、充电和已安装传感器的状态与实物一致。
- [ ] nRF52840 UART 身份与 BLE 收发通过；当前仍是待完成项。
- [ ] LoRa RF 收发通过；仅枚举和电源状态通过的设备应继续标为未完成。
- [ ] 软件包安装、应用运行和重启通过；当前暂停，见 [软件源状态](package-feed-status.zh-CN.md)。

装配对应模块时，可核对 AHT20 `0x38`、BQ27220 `0x55`、
BQ25896 `0x6b` 的 sysfs 驱动绑定。地址表用于识别器件，主动探测前还需核对实际总线。
原始 DRM/KMS 测试会中断桌面，安排方法见 [显示验证](display-validation.zh-CN.md)。
