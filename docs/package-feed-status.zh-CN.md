# 软件源状态与后续验收

## 当前结论（2026-09-09）

软件源的端到端验收失败。暂缓在交付卡上安装或升级软件包，包括 NetSurf。
下一步先完成镜像侧包管理修复、CI 和发布准备，再提供新镜像进行整卡验证。
软件源随后使用已发布镜像的基础库和包数据库。线上软件源配置与发布内容保持不变。

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
镜像包登记为 `Essential: yes`、`hold ok installed`，同时登记 merged-/usr 的别名路径。

软件源后续必须按已发布清单生成依赖，不能沿用 r6 的运行库版本标签。
这一步支持应用引用预装组件；基础系统升级仍通过镜像交付。
管理员强制覆盖、包维护脚本的行为和完整依赖闭包还需各自的安全检查。

镜像守卫直接读取生成后的 ext4，核对每个已登记文件的内容、权限、符号链接和
opkg 数据库。collector 导出 `tdvp-image-base.json`、`tdvp-opkg-status`、
`tdvp-opkg-info.tar.gz` 和 `tdvp-buildroot-packages.json`，一并纳入 `SHA256SUMS`。
发布镜像的门禁与线上旧 feed 的签名/元数据检查已分开，设备签名校验继续保留。

Ubuntu 24.04 主机已通过 11 项回归，包括真实 opkg 的文件覆盖拒绝、预装依赖解析、
生产 post-fakeroot hook、ext4 导出与篡改拒绝；已用旧 SDK 的完整 rootfs 副本验证
12,245 个路径和 165 条包记录。该检查不代表本次新镜像已完成构建或实机验收。
配套可迁移 SDK/sysroot 的导出、已发布基线解析和软件源端到端验收仍待完成。

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
