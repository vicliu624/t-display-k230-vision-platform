# 发布契约

每次 CI 运行都从当前 Labwc 桌面 profile 产生一张可启动的 K230 SD 卡镜像。标签构建会
将同一组产物作为 GitHub Release asset 发布。

## 文件

```text
vicliu-pocket-linux-k230-<revision>.img
vicliu-pocket-linux-k230-<revision>.img.gz
tdvp-image-manifest
tdvp-sdk-baseline-manifest
README.txt
SHA256SUMS
```

镜像包含启动 payload、K230 内核与板级设备树、systemd、网络与 SSH 恢复服务、seatd、
Labwc、Swaybg、SFWBar、Foot 和板级集成 package。

`tdvp-image-manifest` 记录 SDK 和 Linux commit、生成镜像的哈希、固定文件系统/GPT
标识和桌面 runtime 组件。`tdvp-sdk-baseline-manifest` 记录实际准备的源码输入。
`SHA256SUMS` 覆盖 release 中的每一个文件。

## CI

GitHub Actions 工作流依次执行：

1. 获取仓库并恢复源码/工具链下载缓存。
2. 根据固定 source lock 准备新的 SDK 工作目录。
3. 回放 K230 Linux 补丁队列并运行 patch-only 断言。
4. 构建完整的可启动 SD 卡镜像。
5. 断言生成镜像并收集 release bundle。
6. 为 Pull Request、分支构建和手动运行上传 bundle。
7. 为 `v*` 标签发布 bundle。

Buildroot 镜像内置 `opkg` 作为现场包管理工具。软件源配置属于明确的管理员策略；镜像不
安装未经验证的默认软件源。
