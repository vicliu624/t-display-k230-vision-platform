# 登录页、锁屏和息屏

## 产品默认行为

- 开机由 greetd 以专用 `greeter` 用户运行图形登录页，认证后进入所选账户的
  Labwc 桌面。登录页与桌面均使用 VGLite。默认配置不包含自动登录的 `initial_session`。
- 空闲 300 秒锁定当前 Wayland 会话，再过 30 秒关闭屏幕输出。唤醒不应该
  注销应用或重新创建桌面。
- 登录页由 greetd 管理；会话内的 `tdvp-session-lock` 使用 gtklock，
  展示用户名、密码输入框、时钟和 Unlock 按钮；唤醒后在这个窗口解锁。
  swaylock 暂留作维护工具，不再是自动/手动锁屏入口的默认程序。

## PAM 权限修复

2026-09-07 在设备上复现：同一 `tdvp` 密码由 root 调用 `unix_chkpwd`
校验成功，普通桌面用户调用返回 9（认证信息不可用）。helper 是 root 所有
的 `0755`，无法替普通用户读取正确保持为 `0600` 的 `/etc/shadow`。

`gtklock.mk` 和维护用的 `swaylock.mk` 使用 Buildroot 权限表，在 fakeroot
阶段将 `/usr/sbin/unix_chkpwd` 设为 `root:root 4755`。权限只授予 PAM
专用密码校验 helper；不得把 swaylock/GTK/桌面合成器改成 setuid，不得将
shadow 改为普通用户可读，也不得用 `pam_permit` 绕过认证。

成品镜像的 `verify-auth-rootfs.sh` 直接检查 ext4 inode 的属主和权限，并核对
完整的 gtklock/swaylock PAM 规则及默认 greeter 配置。静态源文件存在不再被当成
“锁屏认证可用”的证据。`test-tdvp-auth-image.sh` 使用真实 ext4 小镜像验证
正确状态及十三种失败情况，不要求 CI 获得 root 权限。

```sh
bash buildroot/tools/test-tdvp-auth-image.sh
bash buildroot/k230-sdk-overlay/board/tdvp/verify-auth-rootfs.sh /path/to/rootfs.ext2
```

## 维护操作

```sh
tdvp-graphical-login status
tdvp-graphical-login select greeter
```

选择模式只改变配置。`systemctl restart greetd` 才会应用于运行中的登录
服务，并会结束当前桌面；必须先确认应用数据可以丢弃或已经保存。
自动登录保留为显式维护选项；新镜像默认显示登录页。

2026-09-07 的远端修复已恢复 helper 权限，并验证普通用户正确密码成功、错误密码失败。
在用户允许结束当前桌面后重启了 greetd；日志记录了 greeter 会话和随后
通过 `greetd` PAM 打开的 `tdvp` 会话。旧配置及 helper 位于设备：
`/var/lib/tdvp-repair-backups/20260907/pam-greeter.3HP1qr/`。
窗口式锁屏另有以下运行验证，不能与恢复登录服务混为一谈。

## 窗口式锁屏实现与 2026-09-07 验证

使用 gtklock 4.0.0（`66321fb2bf0d5869d779e7ac6b4d8d9c272ea707`）和
gtk-session-lock 0.2.0（`b3544f361498d716b1ceef1ad6ac9bdf024bf782`）。
它通过 ext-session-lock 协议锁定会话，认证成功后再请求解锁。
密码框、解锁按钮、用户名称和时钟可见；配置禁止 idle-hide/start-hidden。
样式与 greeter 使用相同的字体和颜色，尺寸按 1232×568 横屏约束。

不能直接使用上游 4.0.0 的认证路径。产品包包含配对改动：

- `src/tdvp-auth.c` 替换 PAM 后端，在工作线程里同步检查当前 UID 的密码、
  账户状态和凭据刷新结果；不创建登录会话、不修改过期密码，不 fork GTK
  进程，也不使用原来的非阻塞消息管道。多条 conversation 消息正确区分
  用户名和密码，任何未知请求、分配错误或 PAM 错误均拒绝认证。
- `0001` 补丁将密码副本交给工作线程；GTK 控件操作和成功解锁决定都留在
  主循环。认证期间所有输出禁止再次提交，回调不保存可能已销毁的窗口指针。
  认证成功之前的 SIGTERM/普通 shutdown 不发送 Wayland unlock。
- daemonize 使用有界管道等待 compositor 的 locked 确认；父进程只在收到
  确认后返回成功，替代上游有竞态的 ready 信号及一秒等待。
- 图形程序保持普通用户权限，只有 `unix_chkpwd` 通过 fakeroot 权限规则
  获得 root:root 4755。不得把测试用 PAM 配置目录适配器编进产品程序。

2026-09-07 验证：

- Ubuntu 24.04.4 原生 ASan/UBSan：真实后端的 PAM 生命周期、账户过期、
  分配失败、畸形 conversation 和 1,000 次交替重试通过。
- 从实际 patched source 原样提取的三个回调通过 15 种结果／输出数量组合、
  工作线程隔离、密码擦除和未经认证 shutdown 测试。测试覆盖回调逻辑，
  GTK 画面和完整 compositor 需另做运行验证。
- 使用现有 SDK 的 GTK 3.24.43、PAM 1.6.1、Wayland 1.23.1 完成候选应用和
  Wayland 库的标量 RISC-V 交叉编译，ELF 属性不要求 V 扩展。
- 设备以真实 `tdvp` UID 运行同一后端：`tdvp` 密码被接受，错误密码被拒绝。
  早期探针使用隔离的 PAM 策略目录；随后已安装正式 `/etc/pam.d/gtklock`。
- 设备上的隔离 headless Labwc/pixman 会话验证：无需点击即可输入、错误
  密码保持锁定、正确密码发送 unlock、输出 off/on 后密码框可见。SIGTERM
  没有发送 unlock，compositor 保持黑屏锁定，替代 locker 可接管并认证解锁。
  新 daemonize 管道也验证了先收到 locked 再向启动方返回成功。
- 已安装到正式桌面，并原子切换 `/usr/local/bin/tdvp-session-lock`；没有
  重启 greetd 或结束当前会话。正式屏幕截图显示密码窗口，用户随后明确
  确认“能看到窗口并成功解锁”。原入口备份在设备
  `/var/lib/tdvp-repair-backups/20260907/window-lock.akeYo2/`。

复现主机回归：

```sh
bash buildroot/tools/test-tdvp-gtklock-auth.sh /path/to/gtklock-4.0.0 /path/to/security
patch --batch --fuzz=0 -d /path/to/fresh/gtklock-4.0.0 -p1 < \
  buildroot/k230-sdk-overlay/package/gtklock/0001-tdvp-authenticated-unlock-and-main-thread-ui.patch
bash buildroot/tools/test-tdvp-gtklock-ui-lifecycle.sh /path/to/patched/gtklock-4.0.0
```

以上设备、手动交叉编译及隔离 pixman 测试记录适用于 2026-09-07 的对应会话。
隔离测试只覆盖锁屏协议和认证，不能据此宣称 VGLite 共存已通过。

当前 defconfig、SDK 包注册、session-lock 包装器及镜像权限检查均已集成 gtklock。
每张新候选镜像仍需检查触摸软键盘、VGLite 共存及完整锁定/息屏/唤醒过程。
构建产物检查与实机检查的划分见 [发布契约](release-contract.zh-CN.md)。
