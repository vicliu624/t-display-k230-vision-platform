# 键盘背光修复恢复与新卡检查（2026-09-08）

## 遗漏来源

在 `codex/vglite-layer-shell-delivery` 主工作树中找到了以前的背光修复，
但它仍是未提交的工作树改动，不属于 VGLite 交付提交 `e754716`。
因此正常 Git merge 并未把它带入 `codex/cpu1-rtsmart-integration`。
本次只移植背光相关片段，未修改源工作树，未复制旧版整个硬件服务目录。
当前 CPU1、LoRa、Bluetooth 状态代码继续保留。

## 实际修复范围

- 恢复板上验证过的 IO52/PWM4 路由、1 kHz PWM 和 0/33/100 三档。
- 硬件服务初始化时释放 generic pwm-backlight 的键盘消费者，接管
  `pwm3_5` 的通道 1；不操作显示背光、摄像头引脚、CPU1 或 AI 时钟。
- 恢复反向 PWM 对应的亮度表及旧 DTB 初始亮度识别。
- 修正 DT 的 pinctrl 引脚参数为 `pins = "io52"`：当前驱动使用 generic
  pinconf 字符串引脚映射，原来的 `<52>` 不能生成有效映射。
  `0047` 仅同步这一上下文行，不改变 radio selector 实现。
- 在旧修复基础上补充 IO52 和 PWM 的实际回读；设置失败不能返回成功。
  GET 读取 PWM 当前状态，不再依赖进程内亮度缓存，因此 CLI 更改和
  硬件服务重启后，Quick Settings 状态仍能同步。
- 将真实 `controls.cpp` 的隔离回归接入 PR CI，覆盖三档、旧/新 DTB、
  服务重新初始化、失败路径、PWM 回读及显示背光隔离。

这里保留了原修复的 root 服务 `/dev/mem` IO52 路由处理，以兼容已经烧录的
旧 DTB。它只映射 IOMUX 一页并写 IO52 寄存器，不是允许任意寄存器写入的
桌面接口。物理亮度仍需要用户观察或仪器测量，不能用 sysfs 回读代替。

## 构建验证

在 LAN 主机的 Ubuntu 24.04 容器中执行：

```sh
bash buildroot/tools/test-tdvp-keyboard-backlight.sh
bash buildroot/tools/test-tdvp-cpu0-dts-queue.sh
bash buildroot/tools/test-reconcile-k230-sdk-linux-patches.sh
bash buildroot/tools/test-tdvp-renderer-stack-lock.sh
bash buildroot/tools/test-tdvp-cpu1-vision-status.sh
bash buildroot/tools/test-tdvp-lora-status.sh
bash buildroot/tools/test-tdvp-session-idle-contract.sh
bash buildroot/tools/validate-k230-sdk-linux-patches.sh buildroot/k230-sdk-overlay/linux
```

以上通过，46 个 Linux patch 的 unified-diff 结构有效，真实 DTS 队列重放通过。
使用 a57e99c 配对镜像的 SDK sysroot 完成整个硬件服务的 RISC-V Release
交叉编译（`-Wall -Wextra -Werror`）。这不是新完整 SD 镜像构建。

## 设备上的实际修改与验证

设备：`tdvp@vicliu.i234.me:10022`。安装前备份路径：
`/root/tdvp-keyboard-backlight-20260908.KtytAT/`。

仅替换下列两个程序并重启 `vicliu-pocket-linux-hardware.service`：

| 程序 | 安装后 SHA-256 |
| --- | --- |
| `/usr/libexec/vicliu-pocket-linux-hardware/vpl-hardwared` | `3ba1620c18cd26c29c6bbf8da9ab74cf40c4400634191f6ada2b1ba89fad2297` |
| `/usr/local/bin/vpl-hwctl` | `dc62bbc815435027b75e7d7b58900535e24ae6ecc0f26ee6b2321fd26ce4433d` |

没有更新运行中内核/DTB，没有更换 SPL、U-Boot 或 CPU1 固件，也没有重启桌面。
IO52 回读由 `0x0000018f` 变为 `0x00001191`。以实际桌面用户 tdvp 通过
Quick Settings 的 Unix SOCK_SEQPACKET 接口发送 SET，验证了 peer-UID 权限路径：

| 请求亮度 | 接口结果/上报亮度 | period (ns) | duty (ns)，反向 PWM | enable |
| --- | --- | --- | --- | --- |
| 0 | ok / 0 | 1000000 | 1000000 | 1 |
| 100 | ok / 100 | 1000000 | 0 | 1 |
| 33 | ok / 33 | 1000000 | 670000 | 1 |

随后再次重启硬件服务，33% 保留，上报仍为 33%，最终留在低亮档。
物理三档亮度变化已请求用户确认；记录时尚未收到结果。
原版程序仍在上述备份目录，可用于恢复。当前部署是程序修复，并不代表
修正后的 DTB 已经在设备启动时生效。

## 同一新卡的其他检查（不能据此宣布整体验收通过）

- 卡上 CPU1 payload SHA-256 为
  `37e40269dd159cd2b7a1f4e1c9b5bb5cc59ca44bdabeeca4f2f016b894ad3982`，
  与 a57e99c 配对候选一致；不是仅凭旧的可复现编译时间判断版本。
- 桌面 labwc PID 368 持有 `/dev/vg_lite`；环境、会话日志均确认
  `WLR_RENDERER=vglite`。本次服务替换前后桌面 PID 没变，未使用 Pixman。
- CPU1 所有权共享页到达 STARTING (4)，但心跳停在 278，发布序列停在 18；
  独立 mailbox 心跳也持续停在 1375。Linux `tdvp-vision/status`
  为 `ownership_state=fault`、错误 `-110`，全部帧计数为 0。
  这证明接管初始化未完成，不能把遗留 mailbox 的静态 READY 当作活性通过。
  普通 CPU1 缓存 BSS 不能由 CPU0 的物理地址读值推断执行进度。
- Bluetooth 没有注册 HCI，`/sys/class/bluetooth` 不存在；未刷写 nRF52840。
- LoRa transport/control 被枚举，但仍处于 off，未作射频收发验收。

CPU1 故障继续单独定位；本次未打开帧采集、未进行 CPU1 热重启或故障后
共享 DMA 内存回收。背光修复不等于 CPU1 摄像头/KPU/Bluetooth 验收通过。
