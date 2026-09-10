# 软件源状态与后续验收

## 当前结论（2026-09-10）

软件源的端到端验收失败。暂缓在交付卡上安装或升级软件包，包括 NetSurf。
最新候选镜像已经烧录并检查，发现预装包 `Status` 的 `hold` 列写错。
代码已修正，Ubuntu 24.04 原生 opkg 测试及板上临时目录回归通过；设备真实包数据库保持原样。
本次修复尚未生成新镜像，后续仍需 CI、最终产物检查和发布准备。
软件源随后使用已发布镜像的基础库和包数据库。线上软件源配置与发布内容保持不变。

详细范围与结果见 [opkg hold 修复验证记录](opkg-hold-validation-20260910.zh-CN.md)。

## 镜像实际配置

`post-build.sh` 写入 `/etc/opkg/tdvp-feed.conf` 的地址为：

```text
https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/stable/riscv64
```

`stable` 是可更新频道，`release.json` 声明
`publication_type=mutable-channel`。2026-09-09 的安装测试解析到 r6。
平台 ID 中末尾的 r1 是平台 ABI 标识的一部分；频道对应的 feed revision 单独记录。

入口 `/usr/local/sbin/tdvp-opkg` 按需导入发行公钥到 `/etc/opkg/gpg`，核对指纹后调用 opkg。
Software Manager 在 Foot 中使用同一入口。公钥指纹：

```text
2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500
```

索引签名、精确 ABI 依赖和 riscv64 架构检查继续保留。启动流程无需访问软件源。

## 本次故障的证据

安装 `tdvp-netsurf 3.10-1` 及 52 个依赖返回成功，基础库同时被替换。
随后启动日志显示 systemd 的 sd-gens 在 libmount 中触发非法指令并冻结：

| 项目 | 核对结果 |
| --- | --- |
| 故障库 | `libmount.so.1.1.0` |
| 库内偏移 | `0x182f8`，`mnt_table_parse_stream` 入口 |
| 故障指令 | `0xcc747057`，与 r6 包中该偏移一致 |
| ELF 属性 | 声明 RVV 1.0 |
| libmount IPK SHA-256 | `20dfd1e31d89e655dd34da19e7190dcd0b0aeb115fa92cef83ddf1cfd78337e4` |

CPU0 上的 Linux 无法执行该指令。抽查的 libblkid、GLib、GTK3、
Wayland client 和 libcrypto 也声明了向量要求，恢复兼容性需要核对整个安装依赖集合。

安装前数据库只有平台 ABI 包，部分镜像预装库缺少包归属。
安装前已发现这些库与 feed 字节不同，仍继续安装，暴露了验收流程中的拦截缺口。
浏览器启动和安装后的整机复查没有完成。

## 现有发布检查覆盖什么

`assert-tdvp-opkg-feed-release.sh` 下载 Packages.gz、Packages.asc、
Packages.gz.asc 和 release.json，验证两份索引签名、频道元数据、包架构、
精确平台 ABI 依赖及必需包名。

这个检查尚未覆盖每个 IPK 的 CPU 指令集、与实际镜像的基础库一致性、
完整文件归属及设备重启后的运行行为。这些项目需要补充实现和实测。

## 镜像侧修复进度

`post-build.sh` 从本次 Buildroot 的 `show-info` 保存已选包信息；`post-fakeroot.sh`
在账户、权限和服务链接最终确定后生成 opkg 数据库。预装组件使用
`tdvp-image-<Buildroot 包名>`，版本包含源码版本和最终文件摘要。
同一路径有多个 Buildroot 申领者时，由 `tdvp-image-base` 统一持有，并在清单中记录歧义。
镜像包登记为 `Essential: yes`、`Status: install hold installed`，同时登记 merged-/usr 的别名路径。
`Status` 的三列依次为安装意图、标志、安装状态；`hold` 位于第二列。

软件源后续必须按已发布清单生成依赖，不能沿用 r6 的运行库版本标签。
这一步支持应用引用预装组件；基础系统升级仍通过镜像交付。
管理员强制覆盖、包维护脚本的行为和完整依赖闭包还需各自的安全检查。

镜像守卫直接读取生成后的 ext4，核对每个已登记文件的内容、权限、符号链接和
opkg 数据库。collector 导出 `tdvp-image-base.json`、`tdvp-opkg-status`、
`tdvp-opkg-info.tar.gz` 和 `tdvp-buildroot-packages.json`，一并纳入 `SHA256SUMS`。
发布镜像的门禁与线上旧 feed 的签名/元数据检查已分开，设备签名校验继续保留。

提交 `d2d8395` 的 [CI 构建](https://github.com/vicliu624/t-display-k230-vision-platform/actions/runs/34336774692)
在最终 rootfs 校验中失败：`debugfs rdump` 导出文件时会清除 setuid/setgid，
导致 ext4 内实际为 `04755` 的文件在临时目录中变成 `0755`。校验器现已改为
批量只读查询 ext4 inode 的实际权限；导出副本仍用于核对文件内容和链接目标。
权限差异会列出文件路径、预期值和实际值，检查继续拒绝权限丢失和意外新增特权位。

当时 Ubuntu 24.04 容器分别以 root 和普通用户通过了 15 项回归，包括真实 opkg 的文件覆盖拒绝、
预装依赖解析、生产 post-fakeroot hook、特殊权限与内容篡改拒绝。
旧 SDK 完整 rootfs 副本在加入 `unix_chkpwd=04755` 场景后，重新生成并通过 ext4 校验，
覆盖 12,245 个路径和 165 条包记录。此前完整副本测试中的 helper 为 `0755`，未覆盖这个场景。
提交 `6cd7b34` 的 [CI 构建已通过](https://github.com/vicliu624/t-display-k230-vision-platform/actions/runs/34351987911)，
解决了上述 inode 权限校验问题。该次产物仍是候选镜像，未新增带 tag 的 Release，
当时也未完成新卡实机验收。

2026-09-10 对后续候选镜像 `0e68645191925abe5acd38cd21c0c2c4789d922c` 的新卡检查，
发现生成器与校验器都接受了错误的 `Status: hold ok installed`。opkg 0.7.0 读取后报告
`Internal error`，状态变为 `unknown ok installed`，但部分命令仍返回 0。
此前测试检查了文件冲突和返回码，遗漏了这个解析结果。

本次同步修正生成器和校验器，增加独立字段检查、真实解析结果、hold 升级保护、
Essential 删除保护及解除 hold 后可升级的正向对照。原生测试入口从源码重新编译 opkg，
在 Ubuntu 24.04 中以 root、UID 1000 各通过 20 项测试，无跳过。
CI 已将该入口移到完整镜像构建之前，原有 ext4 最终产物校验继续保留。
板上 opkg 使用临时目录中的 159 条包记录副本通过安装、删除和文件冲突回归；
真实 `/var/lib/opkg/status` 和 `libmount` 未修改。该结果只覆盖隔离测试，
新镜像整卡验收、配套软件源安装和重启验收仍待完成。

本轮已增加[配套 CPU0 SDK/sysroot 导出和隔离验证](cpu0-application-sdk.zh-CN.md)。
在 Ubuntu 24.04 中使用现有 SDK 与临时 rootfs 副本验证导出、迁移及应用编译；
这些本地样本不作为发布基线。后续 CI 必须从同一次完整构建中生成镜像与 SDK，
通过产物配对检查后再上传。已发布基线解析和软件源端到端验收仍待完成。

## 新镜像后的验收要求

### 发布基线要求（2026-09-09 确认）

对外的软件源必须对应一个已经公开发布、用户可以下载的镜像版本。
先完成镜像侧包管理修复与基础验收，再通过带 tag 的 GitHub Release 发布镜像、
SHA-256、配套 SDK/sysroot 和预装包/文件清单。软件源随后锁定这组发布物，
完成候选包构建、安装和重启验收，最后发布签名索引并更新适用的频道。

本地候选产物可以用于开发测试。临时 Action artifact、本机 SDK 目录或单独的
Git 提交号不足以满足对外软件源的镜像发布要求。发布基线解析和强制匹配检查
仍需实现；现有线上频道不代表已经满足这项要求。

镜像先发布时，应明确写出“软件源安装验收待完成”，保留签名校验，并阻止包安装
覆盖已登记的镜像文件。后续软件源验收通过后补充配套版本说明。

1. 保存新镜像的提交、manifest、哈希、基础库清单和包数据库。
2. 检查 CPU0 用户态依赖的 ELF 指令集，并核对与镜像同名文件的内容和归属。
3. 准备可恢复的测试卡或完整备份，在明确的包版本集合上测试安装。
4. 验证应用启动、桌面/VGLite、CPU1 AI、网络和登录，再重启复查。
5. 记录通过验收的镜像与 feed revision 组合后，恢复用户安装指引。

保留签名校验；安装过程遇到冲突应停止并核对原因。

## 对照源码

- [发行 defconfig](../buildroot/k230-sdk-overlay/configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig)：CPU0 指令集配置。
- [post-build](../buildroot/k230-sdk-overlay/board/tdvp/post-build.sh)：镜像实际写入的软件源地址。
- [发布检查](../buildroot/tools/assert-tdvp-opkg-feed-release.sh)：现有签名与元数据检查范围。
- [opkg 入口](../buildroot/k230-sdk-overlay/package/tdvp-opkg-trust/src/tdvp-opkg)：按需信任初始化与命令转发。
