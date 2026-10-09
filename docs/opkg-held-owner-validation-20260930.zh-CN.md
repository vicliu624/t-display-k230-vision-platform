# 历史 runtime 包的基础文件归属保护

## 问题与修复范围

历史 runtime 包的已安装文件清单可能与当前镜像基础包重叠。opkg 0.7.0
初始化文件归属哈希时，会把先前所有者的记录移给后续包；随后升级或卸载
历史包，可能删掉基础库，或改坏基础包的归属清单。

`0004-preserve-essential-held-file-ownership.patch` 保护同时标记为
`Essential` 和 `hold` 的基础包：未受保护的包无法取得它的普通文件或
指向文件的符号链接的归属。受保护所有者后加载时，仍可从旧包收回归属。
共享目录和普通未受保护包的归属转移保持原有行为。

这一规则服务于镜像已有的基础包保护契约，不授权软件源升级基础 ABI。
强制覆盖选项仍属于破坏性操作；正常安装入口无需使用这些选项。

## 本地验证

Linux 构建机上的独立原生 opkg 构建完成以下检查，无需启动完整镜像构建：

- 镜像 ownership seed 和 ext4 校验：22 项通过。
- 真实 opkg 依赖与 hold 解析：8 项通过。
- 新增重复文件归属回归：5 项通过，包括两种实际哈希加载顺序、两种
  status 记录顺序、空载荷升级后卸载、冲突载荷拒绝、普通归属转移。
- feed 仓库已有镜像关联包组成与升级回归：9 项和 11 项通过。
- 230 包候选源逐包安装、查询、升级、卸载：920 次事务通过；每次检查
  基础文件字节、权限、符号链接、归属清单和受保护包状态。
- 7 个旧库包在重复归属夹具中执行普通 `opkg upgrade`：版本全部前进，
  159 个基础包的归属路径和受保护状态保留，基础文件内容未改变。

同一组新增回归在未加入 0004 的原生 opkg 上会复现基础文件丢失或清单损坏。
夹具手工构造重叠的已安装记录，不表示设备上曾实际出现该状态。

镜像绑定事务检查使用的输入：

```text
image inventory SHA256:
d7b45ae61e583e34cd7cbe0c76101589bb0e7c76ac8d4c716b7cd16a9a513daf
candidate feed Packages SHA256:
919d057e80a1c6108ddaadbd0372177b0673e094dddef677dd91f00062fd544e
experimental native opkg SHA256:
4cbd7f8247dc665d2ae07ae129236039d03e8590068fd8ba2626aa0340735c87
```

以上镜像 inventory 来自已有 CI 36676804712，实验在主机上调用加入 0004
的原生 opkg。该 CI 镜像自身尚未包含 0004；本地验证不能证明新目标镜像
已经构建、签名、部署或发布。

## 测试入口

```bash
bash buildroot/tools/test-tdvp-opkg-image-native.sh /path/to/opkg-0.7.0
python3 buildroot/tools/test-opkg-held-file-owner.py /path/to/native/opkg
```

原生入口在临时副本中反向移除可识别的补丁，再按顺序严格应用完整队列。
这样同时支持干净源码和已打补丁的源码，且避免后续补丁改变前序补丁的
上下文导致误判。原始源码目录保持不变。

本地检查通过后，仍需完成新的目标 opkg 构建、最终镜像与 SDK 身份绑定、
候选源签名和设备正常入口验收，才能发布配对产物。

## 协调升级补充验证（2026-10-10）

以已发布 `v2026.10.09-r12-rc2`（0701ec6）为基线，增加
`0005-prepare-coordinated-upgrade-candidates.patch`。四个已有保护
补丁保持不变；安装和卸载入口不改动。普通全系统升级和显式
多包升级进入批量准备，单包请求保留原来的约束路径。

批量准备先保存所有旧状态，再标记非 held/replaced 参与者。
恢复不升级的消费者约束后，对新候选重新检查反向依赖；出现
准备错误时恢复旧状态。新候选去重，避免重复参数污染快照。
这项修复不提供所有失败场景下的原子回滚承诺。

新增 `test-opkg-coordinated-upgrade.py`，通过当前 native 入口运行：
普通升级、提供者优先的多包、重复参数、未请求消费者、held
消费者、缺依赖拒绝，以及实际升级/配置。所有 IPK 都是本地
惰性 fixture，不包含程序或维护脚本。宿主构建关闭 SHA256/GPG，
fixture 使用 MD5；正式目标验证仍开启 SHA256 与签名。

完整原生入口通过 42 项测试（seed 22、替代依赖 8、所有权 5、
协调升级 7）。同一新增测试对旧原生工具失败三项，证明能捕获
原始回归。日志位于构建机 `opkg-formal-regression.ljN0yZ`。
首次本地环境缺 `/usr/sbin` 且 umask 为 0002，按 CI 的路径及
umask 022 重跑；随后补 fixture 校验和，明确离线 configure，
并以“不选新包、状态未变”判断 held 保护，完整入口最终通过。

独立 RISC-V 实验工具也已在发布 SDK 上构建并通过 CPU0 校验。
在隔离根、签名开启的 346 包连续候选历史中，普通 `opkg upgrade`
实际升级与配置通过，507 个 installed 版本一致；单提供者及
held 负例保留约束，libc、旧 opkg、Labwc 和镜像身份文件未变。
这组目标日志位于 `opkg-combined-upgrade-experiment.PPZJeo`。

新增补丁位于 overlay 全树输入摘要覆盖范围，已有增量清理
列表含 opkg。本地验证未触发完整镜像构建、未替换远端工具、
未修改 stable，也未合并 GitHub main 或 PR。
