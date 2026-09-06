# Buildroot 与 SDK 工作目录

工程会把固定版本的 K230 Linux SDK 准备到 ext4 工作目录中，并将本仓库的
板级 profile、软件包、内核 fragment 和镜像脚本叠加进去。当前 Buildroot
配置为：

```text
k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

## 工作流

```sh
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$HOME/work/tdvp-k230-labwc"
```

在 Linux/ext4 工作目录中执行这些命令，可以使用原生 Linux、容器或 WSL。
若要对齐 CI，应使用 Ubuntu 24.04 x86_64，以及 `.github/workflows/ci.yml` 调用的
主机准备脚本；旧版 WSL 上构建成功不等于已在 CI 用户空间中验证。项目检出目录可以位于
Windows 文件系统；`$HOME/work/tdvp-k230-labwc` 是唯一可丢弃的构建输入目录。
不要在 vendor SDK 内直接执行 `make`，也不要把 `output/<profile>` 目录当作工作目录
参数传入。

第一条命令会创建 SDK 工作目录、复制项目 overlay 与 user-space package 源码、将文本
构建输入规范化为 LF、写入 source manifest，并执行 staged package 断言。若 external
package 图、复制后的 package 源码或必需桌面输入与项目源码不一致，它会在编译开始前
失败。

第二条命令会再次比较当前项目与 staged manifest；只有两者一致才会同步 vendor SDK
并构建内核、rootfs、boot 分区和完整 SD 卡镜像。第三条命令审计镜像内容、固定启动
偏移、文件系统标识、桌面会话、网络恢复工具和已选板级服务。

### 构建契约

只有下列每一层按顺序通过，构建才可以被接受：

```text
项目源码
  -> ext4 staged overlay 与 user-space 源码
  -> 已同步的 vendor SDK Buildroot 输入
  -> 生成的 rootfs 与 SD 卡镜像
  -> 镜像断言
```

- staging 脚本会在复制完成后比较每个 `user-space/*/src` 目录的内容 manifest。
- 构建脚本会比较当前项目 manifest 与 staged manifest，并拒绝使用陈旧工作目录。
- `.gitattributes` 将文本构建输入固定为 LF；二进制资源保持原始字节。
- core patch 或 package 图变化时，脚本会在配置前丢弃生成的 Buildroot output。仅源码
  变化时，必须先重新 stage，之后才允许增量 package 构建。
- 部署和发布只消费通过最终镜像断言的镜像。远程实验结果不能被视为镜像产物。

如需快速判断下一次构建是否需要清理 output，而不进行编译，可执行：

```sh
TDVP_STAGE_DRY_RUN=1 \
  bash buildroot/tools/prepare-k230-sdk-worktree.sh "$HOME/work/tdvp-k230-labwc"
```

### 编译前检查交付规则

```sh
bash buildroot/tools/test-tdvp-image-source-contract.sh
bash buildroot/tools/test-tdvp-session-idle-contract.sh
bash buildroot/tools/test-tdvp-renderer-stack-lock.sh
```

源码契约测试从 greeter 和桌面 package recipe 提取安装路径，把真实源码文件复制到
临时 ext4 文件系统，并执行生产镜像校验器中对应的断言和文件提取函数。它会汇总全部
不匹配项，并验证错误的登录命令、用户和会话启动脚本会被拒绝。只需 Bash、Python 3
和 e2fsprogs，不需要编译器、挂载、root 权限或已有 SDK 输出。

它还会执行真实 post-image 清单生成器中的字面量元数据语句，并检查发布基线要求的
字面量字段。wlroots／Labwc 提交号和 renderer 策略字段还会分别与真实 package recipe
及交付环境配置比对，在完整构建前拦截清单字段缺失或内容漂移。

该快速检查覆盖直接安装的桌面策略文件，不覆盖编译生成的程序、生成式系统配置、以变量
生成的清单字段及产物哈希、完整
分区内容或硬件行为。仍须保留独立的 CPU1 固件预检和最终完整镜像/发布校验。应分别
记录这些阶段的结果，不能把其中一项通过表述为后续阶段也已通过。

## 固定输入

- `sdk-sources.lock`：固定的 SDK 与工具链输入。
- `patches/`：Buildroot core 与 Linux 补丁队列。
- `k230-sdk-overlay/`：板级文件、package recipe、配置 fragment 与镜像 hook。
- `tools/`：准备、构建、断言、发布和主机准备脚本。

构建产物位于：

```text
$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/
```

参见 [SDK 基线](SDK_BASELINE.zh-CN.md)和
[Overlay 说明](k230-sdk-overlay/README.zh-CN.md)。
