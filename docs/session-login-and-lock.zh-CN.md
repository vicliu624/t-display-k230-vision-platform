# 登录页、锁屏和息屏

## 产品默认行为

- 开机由 greetd 以专用 `greeter` 用户运行图形登录页，认证后进入 `tdvp` 的
  Labwc 桌面。默认配置不包含自动登录的 `initial_session`。
- 空闲 300 秒锁定当前 Wayland 会话，再过 30 秒关闭屏幕输出。唤醒不应该
  注销应用或重新创建桌面。
- 登录页和锁屏不是同一个进程。当前交付的 swaylock 使用纯色界面和键盘
  密码输入；窗口式、可见密码输入框的锁屏交互仍在迁移中，不能把恢复
  greeter 视作这一交互改造已经完成。

## PAM 权限修复

2026-09-07 在设备上复现：同一 `tdvp` 密码由 root 调用 `unix_chkpwd`
校验成功，普通桌面用户调用返回 9（认证信息不可用）。helper 是 root 所有
的 `0755`，无法替普通用户读取正确保持为 `0600` 的 `/etc/shadow`。

`swaylock.mk` 使用 Buildroot 的 `SWAYLOCK_PERMISSIONS`，在 fakeroot
阶段将 `/usr/sbin/unix_chkpwd` 设为 `root:root 4755`。权限只授予 PAM
专用密码校验 helper；不得把 swaylock/GTK/桌面合成器改成 setuid，不得将
shadow 改为普通用户可读，也不得用 `pam_permit` 绕过认证。

成品镜像的 `verify-auth-rootfs.sh` 直接检查 ext4 inode 的属主和权限，并核对
完整的 swaylock PAM 规则及默认 greeter 配置。静态源文件存在不再被当成
“锁屏认证可用”的证据。`test-tdvp-auth-image.sh` 使用真实 ext4 小镜像验证
正确状态及十种失败情况，不要求 CI 获得 root 权限。

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
自动登录模式保留为显式维护选项，不是新镜像的默认值。

远端本次已恢复 helper 权限，并验证普通用户正确密码成功、错误密码失败。
在用户允许结束当前桌面后重启了 greetd；日志记录了 greeter 会话和随后
通过 `greetd` PAM 打开的 `tdvp` 会话。旧配置及 helper 位于设备：
`/var/lib/tdvp-repair-backups/20260907/pam-greeter.3HP1qr/`。
这不代替后续窗口式锁屏的画面、输入和 Wayland 锁定状态验收。

## 窗口式锁屏候选（尚未设为产品默认）

候选使用 gtklock 4.0.0（`66321fb2bf0d5869d779e7ac6b4d8d9c272ea707`）和
gtk-session-lock 0.2.0（`b3544f361498d716b1ceef1ad6ac9bdf024bf782`）。
它是 ext-session-lock 客户端，不是覆盖在桌面上的普通 layer-shell 窗口。
密码框、解锁按钮、用户名称和时钟可见；配置禁止 idle-hide/start-hidden。
样式与 greeter 使用相同的字体和颜色，尺寸按 1232×568 横屏约束。

不能直接使用上游 4.0.0 的认证路径。候选包包含配对改动：

- `src/tdvp-auth.c` 替换 PAM 后端，在工作线程里同步检查当前 UID 的密码、
  账户状态和凭据刷新结果；不创建登录会话、不修改过期密码，不 fork GTK
  进程，也不使用原来的非阻塞消息管道。多条 conversation 消息正确区分
  用户名和密码，任何未知请求、分配错误或 PAM 错误均拒绝认证。
- `0001` 补丁将密码副本交给工作线程；GTK 控件操作和成功解锁决定都留在
  主循环。认证期间所有输出禁止再次提交，回调不保存可能已销毁的窗口指针。
  认证成功之前的 SIGTERM/普通 shutdown 不发送 Wayland unlock。
- 图形程序保持普通用户权限，只有 `unix_chkpwd` 通过 fakeroot 权限规则
  获得 root:root 4755。不得把测试用 PAM 配置目录适配器编进产品程序。

2026-09-07 验证：

- Ubuntu 24.04.4 原生 ASan/UBSan：真实后端的 PAM 生命周期、账户过期、
  分配失败、畸形 conversation 和 1,000 次交替重试通过。
- 从实际 patched source 原样提取的三个回调通过 15 种结果／输出数量组合、
  工作线程隔离、密码擦除和未经认证 shutdown 测试。该测试不是 GTK 画面
  测试，也没有模拟整个 compositor。
- 使用现有 SDK 的 GTK 3.24.43、PAM 1.6.1、Wayland 1.23.1 完成候选应用和
  Wayland 库的标量 RISC-V 交叉编译，ELF 属性不要求 V 扩展。
- 设备以真实 `tdvp` UID 运行同一后端：`tdvp` 密码被接受，错误密码被拒绝。
  探针仅通过链接适配器把 PAM 策略目录指向 `/tmp/tdvp-gtklock-pam.Emdekx`，
  使用真实设备 PAM/helper；未写 `/etc/pam.d/gtklock`，未切换或重启桌面。

复现主机回归：

```sh
bash buildroot/tools/test-tdvp-gtklock-auth.sh /path/to/gtklock-4.0.0 /path/to/security
patch --batch --fuzz=0 -d /path/to/fresh/gtklock-4.0.0 -p1 < \
  buildroot/k230-sdk-overlay/package/gtklock/0001-tdvp-authenticated-unlock-and-main-thread-ui.patch
bash buildroot/tools/test-tdvp-gtklock-ui-lifecycle.sh /path/to/patched/gtklock-4.0.0
```

仍未完成：包的产品 Kconfig/defconfig 选择、session-lock 包装器切换、实际
画面与输入焦点、息屏唤醒、触摸软键盘、真实 compositor 的崩溃保持锁定、
VGLite 共存及最终镜像验证。当前 `tdvp-session-lock` 仍运行 swaylock，
新包还没有通过生产 Buildroot recipe 完整构建；不能用手动 Meson 交叉编译
代替生产构建，也不能把这个候选称为已经交付的新锁屏。
