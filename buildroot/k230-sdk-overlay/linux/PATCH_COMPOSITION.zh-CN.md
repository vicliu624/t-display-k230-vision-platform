# T-Display K230 Linux 补丁组合规格

本文定义当前 K230 Linux `6.6.36` 补丁队列已经完成核对的组合规则。来源身份和每个文件的
精确哈希见 [PATCHES.zh-CN.md](PATCHES.zh-CN.md)。

## 应用顺序

```text
SDK 0001..0024
  -> SDK 0014 与 0017：GNNE/AI2D power domain 与 AI clock policy
  -> TDVP 0025：Canaan DRM 标准 GEM-DMA
  -> Lewis 0026..0035：RM69A10 DSI、VO、panel、DTS 与 GDMA XRGB 输入
  -> TDVP 0036..0040：TCA8418 键盘与 K230 pinctrl
  -> TDVP 0041：GT9895 touchscreen input
  -> TDVP 0042..0044：sound card、display backlight 与 keyboard backlight
  -> TDVP 0045..0046：BQ27220 telemetry profile 与 dock I2C device
  -> TDVP 0047：LR2021/nRF9151 互斥 pin 与 power profile
  -> TDVP 0048..0049：LR2021 SPI0 传输、复位时序与有边界的 IRQ 枚举
  -> TDVP 0050：AHT20 hwmon binding
  -> TDVP 0051..0052：受控 external I2S amplifier route
  -> TDVP 0053：CPU1 RT-Smart reserved memory 与受限 mailbox
```

Buildroot `linux/` package 目录是唯一的补丁输入。Buildroot 按文件名字典序应用其中的
`*.patch`；`BR2_LINUX_KERNEL_PATCH` 保持为空，避免把同一目录作为外部 local-patch
目录再次重放。`assert-k230-sdk-rm69a10-baseline.sh` 检查实际完成补丁后的源代码；补丁
可应用性以该检查为准。

## 已核对的交叉修改

| 范围 | 输入 | 生效约定 | 验证 |
| --- | --- | --- | --- |
| DRM file operation 与 GEM allocation | SDK `0005`；TDVP `0025` | SDK `0005` 保持 display runtime-PM 调用关闭。TDVP `0025` 移除 Canaan 自己复制的 FOPS、dumb-buffer allocation 与 mmap 实现，并使用 Linux 6.6 的 `DRM_GEM_DMA_DRIVER_OPS_VMAP` 和 `DEFINE_DRM_GEM_DMA_FOPS`。 | 检查 patch 后的 `canaan_drv.c`；通过 DRM smoke test 创建 XR24 dumb buffer。 |
| AI power、clock 与 KPU runtime | SDK `0014`、`0017`；TDVP kernel fragment 与 KPU package | SDK 在实际 DTS 中声明 AI power domain 与两路 AI clock，通过 runtime PM 启用它们，并为 GNNE 设置 800 MHz AI clock。TDVP 选择两个 driver，并打包 nncase runtime 与带版本的 kmodel workload。 | 检查 patch 后 DTS 与 driver、两个 KPU character device，并在设备上运行内置 workload。 |
| VO reset、timing 与 scanout | SDK `0005`、`0006`、`0013`、`0023`；Lewis `0027`、`0031`、`0034` | SDK 提供 VO reset path、单行 timing 修正、background handling 与 GDMA scanout address/pitch support。Lewis 将 XR24 注册到 OSD、定义其 byte order，并选择 RGB scanout colour conversion state。 | RM69A10 cold boot；XR24 colour bar；静态与动态 DRM frame。 |
| DSI PHY、mode 与 panel sequence | Lewis `0026`、`0028`、`0030`、`0032` | 两条 DSI data lane、RM69A10 init/reset sequence、板级 timing declaration 与 burst video mode 共同组成 panel link configuration。PHY 参数为板级参数，应和声明的 pixel clock 一起评估。 | 多次 cold boot；无 DSI timeout；全屏图案稳定。 |
| XRGB scanout 与硬件旋转 | SDK `0023`；Lewis `0027`、`0031`、`0035`；TDVP `0025` | SDK `0023` 通过 K230 GDMA 完成 plane rotation，再由 VO 扫描 rotated DMA buffer。Lewis `0035` 使 XR24、ABGR8888、XBGR8888 成为该 GDMA 代码可接受的 32-bit input。TDVP `0025` 提供 direct scanout 和 GDMA rotation buffer pool 消费的标准 GEM-DMA object。 | 查询 plane rotation property；对 XR24 提交 0/90/180/270 度；核对 source/destination dimension 与画面稳定性。 |
| Console orientation | Kernel fragment `fbcon=rotate:3` | framebuffer console 在机身横屏方向渲染像素。它不设置 KMS plane 的 `rotation` property，因此不会启动 GDMA rotation path。 | cold boot 后观察 kernel console、BusyBox login 与 shell。 |
| 图形客户端方向 | SDK `0023`；Lewis `0035` | DRM atomic client 设置 plane rotation property，并以匹配的 logical dimension 渲染。单个 frame 使用未旋转 plane 或一个 plane rotation transform。 | 用带文字和非对称图形的 DRM atomic test image 验证。 |
| RM69A10 board DTS 与 keyboard DTS | Lewis `0032`、`0033`；TDVP `0037` | Lewis 创建 `k230-canmv-rm69a10.dts`、启用 SDIO WiFi 并引入 display。TDVP 加入可拆卸键盘 transport 与 matrix，并在 shell profile 中保持 `i2c4` disabled。 | 反编译产物 DTB；probe WiFi 与 TCA8418；验证完整 matrix，包括 `0` 和 `Shift+0`。 |
| TCA8418 driver 扩展 | TDVP `0036`、`0038`、`0039`、`0040` | driver 在首个 I2C transaction 前执行 reset。FIFO polling 和 controller debounce 由 board DTS 选择，K230 pinctrl 接受标准 Schmitt property。 | 装上键盘后启动；使用 `evtest` 或 console input；验证全部黄字组合键。 |
| Audio 与 backlight | SDK `0013`；TDVP `0042`、`0043`、`0044` | internal-codec sound card、RM69A10 brightness path 和 keyboard PWM backlight 分别使用对应的 Linux class interface。 | 枚举 ALSA playback/capture，通过 `/sys/class/backlight` 调整 LCD brightness，并通过 PWM backlight class 调整 keyboard brightness。 |
| 扩展坞 GPIO 与电量计 | TDVP `0045`、`0046`；已有 `i2c_gpio_keyboard` node | `0x20` 的 XL9555 是标准 `pca953x` GPIO provider。`0x55` 的 BQ27220 使用其自身 standard-command register profile：瞬时电流是 `0x0c`，剩余容量是 `0x10`，不存在 data-memory programming path。静态 node 与已验收 keyboard I2C bus 共用总线，不改变 TCA8418 transport。 | 在已安装扩展坞上验证完整 keyboard regression、`gpioinfo` 可见 XL9555 chip，并在充放电时核对 BQ27220 `/sys/class/power_supply` 值与独立 I2C 读取一致。 |
| 扩展坞环境传感器 | TDVP `0050`；已有 `i2c_gpio_keyboard` node | `0x38` AHT20 使用内核的 `aosong,aht20` hwmon binding。driver 校验 AHT20 CRC，并发布标准温湿度属性。 | 在两个环境条件下，将 `temp1_input` 和 `humidity1_input` 与直接读取且 CRC 通过的 I2C frame 对比。 |
| LoRa 与 nRF9151 串口路径 | TDVP `0047` 与 `0048`；UART2、SPI0、K230 pinctrl 与 GPIO descriptor | LR2021 reset input 与 UART2 TX 都使用 IO5。`tdvp-radio-mux` 控制互斥的 `lora` 与 `nrf9151` profile，通过 sysfs 暴露当前 profile 和 LR2021 电源状态，并统一管理共享 GPIO。SPI0 使用 IO14 至 IO17 作为 LR2021 传输路径。 | 以 `lora` 启动，运行 `vpl-lora-probe` 并要求 LR2021 firmware version 既非全 `0x00` 也非全 `0xff`；切换 `nrf9151` 后确认 `/dev/ttyS2` 可与已供电 modem 交换 AT command；再切回 `lora` 并重复 LR2021 probe。 |
| CPU1 RT-Smart 协处理器 | TDVP `0053`；U-Boot raw 10--30 MiB payload；post-image RT-Smart builder | Linux 始终只有 `cpu@0`。`0053` 保留 `0x10000000..0x13ffffff` 给 CPU1；唯一 Linux 可见的 CPU1 地址是 `0x13ff0000` 的非缓存 64 KiB mailbox。U-Boot 在启动 Linux 前将 CPU1 reset 到已打包 OpenSBI/RT-Smart image。 | 冷启动后验证 10 MiB 的原始 payload、U-Boot `bootcmd_cpu1`、最终 DTB reservation 与 compatible、`/dev/tdvp-cpu1`，再运行 `tdvp-cpu1ctl status`、`ping` 与 `crc32`。 |

## 明确范围

### Touch

Lewis `0032` 提供 GT9895 DTS node。TDVP `0041` 提供相匹配的 four-byte-register driver，
`board/tdvp/fragment/linux.touch` 将其编入 kernel。release gate 检查 patch 后的 driver source、
Kconfig、最终 DTB 和 physical touch interaction，随后才接受 image。

### Camera

Lewis `0036-add-gc2093-camera.patch` 不在活动队列中。其来源说明 MCLK 和 reset
initialization 尚未完成；它还需要 `i2c4`，而 TDVP `0037` 在已验收 shell profile 中将 `i2c4`
设为 disabled。启用 camera 时需要补齐 electrical configuration，并提供独立的 DTB/CSI capture
acceptance test。

### Colour Conversion

Lewis `0034` 为 XR24 display path 关闭全局 VO YUV-to-RGB 和 OSD RGB-to-YUV control。当前
shell 与 DRM test 覆盖这条路径。camera composition、YUV overlay 和 multi-plane video 在进入
product image 前需要独立的 colour-pipeline acceptance test。

## 队列卫生

reconciliation script 在 Buildroot 应用补丁之前清除 display、touch、rotation 与 camera 的
legacy Lewis 文件名。staged queue 只包含 [PATCHES.zh-CN.md](PATCHES.zh-CN.md) 中列出的
命名文件，避免第二份语义相同的补丁悄悄改变应用顺序。

## 必经验收顺序

1. 从已固定的输入创建新的 SDK worktree。
2. 运行 `linux-patch` 与 `--patch-only` assertion。
3. 构建候选镜像并运行完整 assertion。
4. cold boot 后验证 console、login、WiFi、SSH 与 keyboard。
5. 分别运行静态 XR24 colour bar、带文字/非对称图形的 XR24 frame、动态 XR24 frame。所选
   rotation 的三类图像均稳定后，才能接受 hardware rotation。
