# 分支策略

这个仓库要刻意把“最小平台基线”和“Debian 桌面系统”放在不同分支中，
避免桌面 rootfs、X11/Wayland、发行版包管理和大体积产物污染最小系统路线。

## `main`

`main` 是稳定的嵌入式平台基线。

它负责：

- 固定后的 Buildroot submodule 引用；
- TDVP 自己的 `BR2_EXTERNAL` 工程；
- K230 6.6.36 kernel / DTB / boot 集成；
- 板级硬件事实、验证记录和 bring-up 文档；
- BSP、runtime、SDK 和最小示例骨架；
- 用来验证硬件的 BusyBox 最小 rootfs 配置。

规则：

- `main` 必须保持为可启动的最小系统。
- 不在 `main` 中加入 Debian、Ubuntu、X11、Wayland、桌面包或大型发行版
  rootfs 产物。
- 生成的镜像、Buildroot output、下载缓存、本地 boot 二进制不进入 git。
- 如果桌面分支中发现了硬件修复，只有当它对最小平台也有价值时，才回合到
  `main`。

## `debian`

`debian` 是桌面 userspace 分支。

它可以负责：

- Debian riscv64 rootfs 构建脚本；
- 轻量桌面 profile 的包清单；
- X11 / Wayland / session 配置；
- 复用 `main` 中 kernel、DTB、U-Boot env 和 bootloader artifacts 的桌面镜像
  组装脚本；
- 桌面验证记录。

规则：

- 复用 `main` 中的 K230 kernel、DTB、显示修复和 boot chain。
- Debian 只作为 userspace/rootfs 层，不替代板级支持基线。
- 下载的 `.deb` 缓存、展开后的 rootfs tree、生成的 SD 卡镜像不进入 git。
- 如果某个修改属于板级、kernel、DTB、boot 或硬件抽象层修复，应先进入或
  回合到 `main`，然后再更新 `debian`。

## 合并方向

正常方向：

```text
main  -> debian
```

只有板级修复允许反向沉淀：

```text
debian 硬件发现 -> main 板级修复 -> debian 更新
```

这样 `main` 能保持小、可复现、适合 SDK/runtime 继续演进；`debian` 则可以
独立成长为真正可用的桌面镜像。
