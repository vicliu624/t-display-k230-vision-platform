# CPU0 SDK 本地验证记录（2026-09-09）

## 结论与样本范围

CPU0 SDK 导出、文件配对和隔离消费测试通过。此次只修改导出器、校验器、测试、
CI 接线及文档，未修改板端驱动、VGLite renderer、CPU1 固件或线上软件源。

平台源码基于 `6cd7b3407086f0fe723218fa505d7b0b4346fe72`。
该提交的[完整镜像 CI 已通过](https://github.com/vicliu624/t-display-k230-vision-platform/actions/runs/34351987911)，
此次新增 SDK 代码尚需随下一次 CI 完整构建生成配对产物。

本地使用 `192.168.31.42` 上现有的旧 SDK staging、锁定的 Xuantie V3.0.2 工具链，
以及一次性 rootfs 副本。副本通过当前的生产 post-fakeroot 和最终 ext4 校验器生成
包归属、权限和文件哈希数据。原 SDK 通过只读挂载提供输入。
测试中的 `local-sdk-validation.img.gz` 仅压缩了 rootfs，用于验证镜像哈希绑定，
不含完整 SD 卡布局，不能烧录或用作公开软件源基线。

以下结果证明这组真实工具链和库能被正确导出及消费；它们不代表最新 CI 镜像的
SDK 已经生成，也不代表设备安装 NetSurf、整机重启或所有依赖代码路径已通过验收。

## 已执行的检查

| 检查 | 结果 |
| --- | --- |
| 离线回归 | 18 项通过，包含生产迁移脚本的 5 类错误输入拒绝 |
| SDK 文件清单 | 23,960 个文件/链接记录通过内容、权限、链接边界和清单摘要校验 |
| 最终镜像库一致性 | 358 个共有的 `/usr/lib/*.so*` 普通文件与 ext4 清单哈希一致 |
| 隔离环境 | Ubuntu 24.04、UID 1000、断网、SDK 只读；隐藏 `/opt`，不挂载原构建目录 |
| 迁移路径 | `/sdk` 与 `/another/location/with-a-longer-name/tdvp-sdk` 均通过 |
| 基本应用 | C、pthread、C++、GTK/libmount/Wayland 编译及链接通过 |
| 构建工具 | CMake C/C++、CMake PkgConfig 导入目标、LTO 静态库归档及链接通过 |
| 开发元数据 | 204 个 pkg-config 模块解析通过，未发现主机头文件或库路径泄漏 |
| 编译器默认值 | 默认、`-mtune=c908`、`-mcpu=c908`、`-mcpu=c908v` 均保持标量；显式 RVV 产物被拒绝 |
| 示例依赖 | 39 项递归依赖的镜像身份、ABI、ISA 策略及运行库搜索路径检查通过 |
| Pixman 专项 | QEMU 关闭 V，程序确认 HWCAP.V=0，镜像库的 8×8 合成像素正确 |
| 重复导出 | 第二次导出被拒绝，已有发布目录的全部记录哈希保持一致 |
| 静态检查 | ShellCheck、actionlint、Python 语法、工作流 YAML 与 `git diff --check` 通过 |
| 既有镜像回归 | 认证/PAM、息屏锁屏、VGLite 锁定与恢复、profile、patch reconciliation 通过；46 个内核补丁结构合法 |

错误输入回归包含 SDK 不在配对目录、文件名错误、SHA-256 错误、损坏 gzip、
内外 manifest 不一致，并确认这些错误均在启动验证容器前被拒绝。
其他回归覆盖文件篡改、权限变化、额外/缺失文件、越界链接、镜像错配、
错误 ABI/ISA 和非空 RPATH/RUNPATH。空 RUNPATH 不包含搜索目录，允许保留。

## ISA 检查的专项结论

Pixman 0.44.2 的源码使用 `getauxval(AT_HWCAP)` 选择 RVV 实现。
整个共享库的 ISA 属性包含可选实现的指令集，单独检查该属性会误报标量路径。
校验器只对配对镜像中的这个版本与路径允许标准 RVV 子集，并要求镜像哈希匹配、
专项模拟测试通过。新应用和 CLI `--elf` 无此例外。
这项处理不改变 VGLite 桌面，也不接受供应商的额外向量指令集。

旧 SDK 样本的 `libvg_lite.so` 和 `libv4l2-drm++.so` 还声明了宽于标量基线的 ISA。
供应商构建文件包含 `-mcpu=c908v`；现有 Buildroot wrapper 会因此跳过默认 `-march`。
编译器宏检查已复现这个行为。本次反汇编未发现这两个库中的 RVV 指令，
因此没有据此宣称它们已发生运行故障。供应商编译选项仍值得单独收紧并重新验收。
此次导出的 SDK wrapper 明确补齐标量默认值，避免应用仅指定 `-mcpu` 时意外启用 RVV。

最新 CI 压缩包未在本轮完整下载成功，无法据此逐字节确认上述两个旧样本与最新镜像
完全相同。本轮没有修改它们，也没有把旧 SDK 的测试结果充作新卡验证结果。

## 后续顺序

1. 推送后由当前 PR 的 Action 完整构建镜像，并在同一次构建中导出 SDK。
2. 隔离检查通过后上传候选产物；进行新卡基础验收，再发布带 tag 的镜像与配套文件。
3. 软件源解析并锁定该 Release 的哈希，按镜像包归属构建应用，完成 NetSurf 安装和重启验收。

完整导出与验证命令见 [CPU0 应用 SDK](cpu0-application-sdk.zh-CN.md)。
本轮未发布 Release、修改软件源、使用签名私钥或操作远端设备。
