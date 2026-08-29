# 快速开始

## 构建与收集

SDK 必须在 Linux/WSL 的 ext4 工作目录构建；Windows 仓库的 `output/` 只用于最终
发布交付。

```sh
git clone --recurse-submodules <repository-url>
cd t-display-k230-vision-platform
WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$WORKTREE"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$WORKTREE"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

原始镜像位于
`$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/sysimage-sdcard.img`。
镜像校验通过后运行收集脚本，以 Windows 可见的 `output/` bundle 与 `SHA256SUMS` 作为
刷写交付物。

将镜像写入已经确认的**整张** SD 卡设备，不要写入某个分区：

```sh
sudo dd if="$IMAGE" of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

## 首次启动与桌面

串口是 `ttyS0`，参数 `115200 8N1`。开发恢复账户为 `root` / `tdvp`；设备离开可信
开发网络前必须修改此凭据。

Greeter 会认证实际 Linux 账户并用该账户启动会话。首次插入未分区的大容量卡时，根分区
2 与 ext4 扩展可能自动重启一次，绝不会创建 `/data`。检查：

```sh
systemctl --no-pager status greetd NetworkManager tdvp-rootfs-expand
cat /var/lib/tdvp/rootfs-expand.status
```

PCManFM 负责壁纸、桌面和 Files；Raspberry Pi 的 `wf-panel-pi` 负责顶部栏；Labwc 负责
窗口管理。LilyGO Menu 键打开应用菜单；桌面空白处长按等价于右键。Fn 是黄色字符的真实
键盘修饰键；全屏程序可用 `Alt+F4` 关闭。

## 网络、浏览器与软件包

NetworkManager 是唯一网络管理者。通过状态栏选择 Wi-Fi 或打开 **Edit Connections**；它
运行上游 `nm-connection-editor`：

```sh
nmcli device status
nmcli device wifi list
nmcli device wifi connect "SSID" password "PASSPHRASE"
```

Cog 提供轻量 HTTPS 浏览器；窗口控制会显示在顶部栏下方。调节顶部栏的输出音量时会播放
标准系统事件声音。

发行版软件源需要签名校验。不要创建独立的 `wpa_supplicant` 配置，也不要关闭签名校验：

```sh
sudo opkg update
sudo opkg list
sudo opkg install <package>
```
