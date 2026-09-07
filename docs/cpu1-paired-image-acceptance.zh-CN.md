# CPU1 AI/视觉配对镜像：备用卡验收

## 候选镜像与证据边界

提交 `a57e99cfdb64a580e572a4d96e58c574aa1e9efe` 的完整镜像已在 LAN
Ubuntu 24.04 容器中构建并通过最终打包校验。以下哈希标识这一次本地构建，
不声称其他机器的重新构建必然字节相同，也不表示设备已通过验收。

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `tdvp-ai-vision-a57e99c.img.gz` | 124474712 | `d19280f4ace34a48c4c06e3753acb366dea18f715fe4d70e8e452b412291d91d` |
| 解压后的 `.img` | 1744850944 | `1ca3f5a8e1c3c1010512d6dc6197a41689b010c8122da18960ab069085fd058b` |
| 配套 `tdvp-cpu1-rtsmart.bin` | 3560728 | `37e40269dd159cd2b7a1f4e1c9b5bb5cc59ca44bdabeeca4f2f016b894ad3982` |

压缩文件是构建输出 `sysimage-sdcard.img.gz` 的重命名副本；内容未修改。
随镜像保存的 `tdvp-image-manifest` 记录 Linux、DTB、分区和固件哈希。
清单声明 CPU0 Linux、CPU1 RT-Smart、AI/视觉 ownership contract 2，以及
VGLite 桌面、greetd/gtkgreet；这些构建属性需要通过实际启动再次确认。

后续发现并修复了启动解压器的 DMA/shared SRAM 收尾检查缺口，详见
[启动 SRAM 交接保护](cpu1-boot-sram-handoff.zh-CN.md)。`a57e99c` 不包含该修复；
仍可用于保留回退卡的初始诊断，但不能作为最终 AI/shared SRAM 交付验收版本。

## 首次启动

1. 保留原卡，使用备用 microSD。核对烧录工具选择的物理磁盘，避免覆盖电脑磁盘。
2. 把完整镜像烧录到备用卡，而不是把文件复制进卡的文件系统。
   若烧录工具不支持 `.gz`，先解压并核对上表的原始镜像哈希。
3. 断电换卡，保持原配 GC2093 连接，接串口并从上电开始记录完整日志。
4. 确认能够进入 Linux 登录和桌面，再记录网络地址及 SSH 恢复情况。
   仅能连接原地址不足以证明启动了新镜像。核对实际固件/DT、桥接状态和资源声明。
5. 若 SSH 主机密钥改变，通过串口等可信连接核对新指纹，不禁用主机身份校验。

禁止在旧所有权镜像上单独替换 CPU1 固件、Linux 内核或 DTB。
失败时保留串口和内核日志，停止验收；关机/断电后换回原卡，不热卸载桥接驱动、
不重置共享 PLL/电源、不热重启 CPU1，也不改用 Pixman 桌面。

## 先观察，后请求采集

在新系统上先记录下列只读输出；其中 `dmesg` 和完整资源地址可能需要 root 权限：

```sh
uname -a
cat /proc/cmdline
cat /sys/class/misc/tdvp-vision/status
ls -l /dev/tdvp-vision
grep -E 'tdvp-cpu1-(kpu-sram|shared-sram|gnne-fft-ai2d)' /proc/iomem
dmesg
```

状态文件应是 version 1、CPU1 owner、contract 2。继续核对 Linux/CPU1
所有权状态、错误码和推进的 worker 心跳；不能只根据设备节点存在判定成功。
状态读取本身不启动相机；忙时可能返回 EAGAIN，可稍后重读，不能清除错误。
三个 Linux 资源树声明分别覆盖 KPU SRAM、shared SRAM 和 GNNE/FFT/AI2D
寄存器。这是协作式资源排他，不是任意特权 MMIO/DMA 的安全隔离证明。

## 手动 CPU1 帧传输探针

`buildroot/tools/tdvp-cpu1-frame-probe.c` 是独立命令行验收工具，
**不安装进镜像、不自动运行、不创建 Camera 菜单或 GUI demo**。
它只打开 `/dev/tdvp-vision`，通过现有桥接请求 CPU1 采集，使用非阻塞
`poll/read` 接收完整记录；关闭文件请求 STOP，但不释放 CPU1 的启动期资源所有权。

使用与候选 Linux 匹配的交叉工具链，在仓库根目录编译，输出到新建的验收目录：

```sh
"${CROSS_COMPILE}gcc" -std=c11 -O2 -Wall -Wextra -Werror \
    -march=rv64gc -mabi=lp64d \
    -Ibuildroot/k230-sdk-overlay/board/tdvp/cpu1/vision \
    buildroot/tools/tdvp-cpu1-frame-probe.c -o /path/to/new-output/tdvp-cpu1-frame-probe
```

`CROSS_COMPILE` 必须指向 CPU0 Linux/glibc 编译器前缀，不能使用 CPU1
RT-Smart/musl 工具链；CPU0 探针不要求 RVV。先确认新系统配对和所有权正常，
再把核对过哈希的二进制放入设备临时验收目录，以有 video 组权限的用户执行：

```sh
./tdvp-cpu1-frame-probe 30 15000
```

可选第三参数保存最后一帧原始 NV12：

```sh
./tdvp-cpu1-frame-probe 30 15000 ./capture-01.nv12
```

输出文件必须不存在；工具拒绝覆盖已有文件。保存发生在关闭采集端点之后，
写入失败的残缺文件不能作为验收证据。超时参数是整个收帧阶段的用户态预算，
不是硬实时内核截止时间，也不包括随后文件写入。

探针检查完整 64 字节头、1920×1080/stride 1920 NV12、3110400 字节像素、
保留字段、连续递增的传输序号、严格递增的 PTS；打印末帧 FNV-1a、亮度范围和
累计字节数。退出码 0 仅表示这次 CPU1 桥接帧传输检查通过。
静止或暗场不应仅因像素变化不足被误判失败；必须另行观察真实场景与帧内容对应。
NV12 文件应在主机按上述尺寸查看，不在设备安装 Linux 直连相机应用。

host 回归命令：

```sh
bash buildroot/tools/test-tdvp-cpu1-frame-probe.sh
```

它执行真实探针源代码的模拟 I/O/时间测试，不读取主机摄像头，也不能代替板级验收。

## 分项判定，不用一个 PASS 代表全部硬件

- 启动：CPU0 Linux 和 CPU1 RT-Smart 配对；所有权握手、供应时钟/电源和内存声明正常。
- 摄像头：GC2093 身份、真实图像、帧计数、慢读、关闭/重新打开及错误保留单独验证。
- 桌面共存：实际 renderer 是 VGLite；收帧期间桌面不回退，锁屏密码窗和解锁仍正常。
- AI：GNNE/AI2D/FFT 注册不等于计算成功。FFT 数值参考、固定模型、张量/缓存、
  异步结果和错误处理仍待验证；当前没有已选择并验证的 ASR 模型。
- 蓝牙/其他硬件：nRF52840 控制接口、Bluetooth 控制器及摄像头之外的设备另行检查。
  本探针不验证蓝牙/LoRa，也不授权刷写 nRF52840 固件。

对无法证明已经停止的加速器任务，超时不意味着可回收 DMA 缓冲区。
本轮不通过共享复位、自动重启或释放潜在在途内存来掩盖失败。
