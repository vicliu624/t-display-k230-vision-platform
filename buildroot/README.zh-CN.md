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

在 WSL 中执行这些命令，并让工作目录位于 ext4 文件系统。项目检出目录可以位于
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
