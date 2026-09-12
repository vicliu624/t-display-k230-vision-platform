# 硬件基线验证

适用 profile：`k230_canmv_t_display_rm69a10_labwc_desktop_defconfig`。
每份记录应注明镜像 manifest、源码提交、boot ID、连接的硬件和测试期间替换过的文件。

## 主机检查

```sh
SDK_WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$SDK_WORKTREE"
bash buildroot/tools/assert-public-release.sh "$SDK_WORKTREE"
```

镜像校验检查 raw 启动布局、配对 CPU1 固件、rootfs、设备树、登录/锁屏权限、
VGLite 策略及硬件接口文件。发布检查还访问在线软件源并校验签名和索引元数据。
这些检查的覆盖边界见 [发布契约](release-contract.zh-CN.md)。

## 新卡只读检查

以下命令不切换会话、不提交 AI 作业：

```sh
uname -a
cat /proc/sys/kernel/random/boot_id
systemctl --no-pager status greetd sshd NetworkManager seatd vicliu-pocket-linux-hardware
nmcli device status
nmcli connection show --active
ls -l /dev/dri /dev/input /dev/tdvp-vision /dev/tdvp-ai
cat /sys/class/drm/card0-DSI-1/status
cat /proc/bus/input/devices
cat /sys/class/misc/tdvp-vision/status
cat /sys/class/misc/tdvp-ai/status
vpl-hwctl status
tdvp-renderer-profile status
arecord -l
aplay -l
ls /sys/bus/i2c/devices
```

Linux 预期只报告 CPU0。摄像头和 AI 的 Linux 入口是跨核设备，
旧 V4L2/vendor ISP 服务和 Linux KPU 验收工具不属于当前 profile。
驱动绑定与状态可读只证明接口存在；还需功能测试。

## 功能验收

- **CPU1：** 按 [视觉与 AI 作业说明](cpu1-ai-jobs.zh-CN.md) 检查真实帧和支持作业的数值。
  任务会占用 CPU1，开始前确认没有其他应用持有作业接口。
- **VGLite：** 检查登录页、桌面的渲染配置、运行进程、VGLite 设备句柄和错误日志，
  再执行 [显示验证](display-validation.zh-CN.md) 中的 Wayland 会话测试。
  桌面运行时禁止直接执行会请求 DRM master/modeset 的 `tdvp-display-smoke`；
  原始 KMS 测试使用专用维护流程，并事先保存桌面数据。
- **输入与会话：** 实测 Menu、Fn、触摸、工作区、登录、密码错误拒绝、
  空闲锁定、息屏、唤醒和正确密码解锁。
- **板级功能：** 实测键盘背光三个档位、Wi-Fi 连接、音频录放。
  USB 网卡按实际枚举名称检查。I2C 先检查 sysfs 绑定；主动探测前核对总线和资源归属。
- **待完成项：** nRF52840 蓝牙集成和 LoRa RF 收发单独记录，不能用设备节点存在代替收发验收。
  软件包安装与升级当前暂缓，见 [软件源状态](package-feed-status.zh-CN.md)。

[V1.3 验收清单](hardware-v1.3-acceptance.zh-CN.md) 用于汇总结果。
任何跳过或未连接项目都应标为“未测”，并附原因。
