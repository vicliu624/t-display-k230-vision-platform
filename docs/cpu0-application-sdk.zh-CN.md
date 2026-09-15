# CPU0 应用 SDK

发布收集器现已将 `<release-name>-cpu0-sdk.tar.gz` 与镜像一同导出。
输入来自同一次 Buildroot 构建输出及最终 ext4。导出过程不会重新构建镜像，
也不会修改原 SDK 工作目录。

## 内容与配对依据

- 锁定版本的 Xuantie GCC/binutils；使用该镜像构建选定的开发 sysroot，
  移除发行包中其他 ABI 的原始 sysroot。
- 使用相对路径重新编译的 Buildroot 编译器入口，默认 `lp64d` 和标量
  `rv64imafdc_zicsr_zifencei`，保留栈保护、PIE 和 RELRO。
- pkg-config、CMake 工具链文件和 Bash 环境设置脚本。
- staging 与镜像在 `/usr/lib/*.so*` 上共有的普通文件，使用最终镜像字节并核对哈希。
  头文件、静态库和链接脚本保留构建来源；SDK 副本统一使用普通主机权限。
- 镜像/包元数据，以及完整的 SDK 文件哈希清单。外置 `tdvp-sdk-manifest.json`
  与压缩包中的同名文件完全一致。

JSON 清单绑定压缩镜像、镜像 manifest、staged 源码清单、预装包记录和 Buildroot
所选包版本，并记录实际构建配置哈希、导出脚本哈希、编译器版本、目标 ISA/ABI
与最终镜像库哈希。`SHA256SUMS` 同时覆盖 SDK 压缩包和 JSON 清单。

## 本地导出与验证

先完成正常镜像构建，再按入门文档执行 `collect-release-bundle.sh`。
以下命令复现 CI 的交付检查：

```sh
docker build -t tdvp-sdk-validation:ubuntu24.04 \
  -f buildroot/tools/sdk/Dockerfile.validation buildroot/tools/sdk
bash buildroot/tools/test-tdvp-sdk-relocation.sh \
  output/RELEASE/RELEASE-cpu0-sdk.tar.gz output/RELEASE
```

校验器先验证发布文件哈希，再把 SDK 挂载到 Ubuntu 24.04 中两个不同长度的路径。
容器使用普通 UID、只读挂载、断网环境，隐藏 `/opt`，不挂载原构建目录。
C/C++、GTK/libmount/Wayland 和 CMake 编译都必须通过。应用 ELF 检查拒绝 RVV、
不支持的 ISA 属性、错误 ABI 和内嵌运行库搜索路径；递归检查示例依赖与最终镜像库哈希。
CI 在上传产物和按 tag 发布 Release 前执行这些检查。

发布目录使用 `0755`，清单列出的公开交付文件使用 `0644`。collector 在发布副本上
设置这些权限，源镜像可继续保留构建过程产生的 `0600`。编译前的跨 UID 回归检查
UID 1001 生成的文件能由 UID 1000 读取，覆盖 `umask 022/077`；SDK 解压保留清单权限。

镜像库的 ISA 元数据有一项经过核对的例外：Pixman 0.44.2 的 `pixman-riscv.c`
仅在 Linux 报告 HWCAP.V 时选择向量实现。依赖检查要求包版本、库路径及配对镜像
哈希全部匹配，只允许标准 RVV 子集，不放行额外的供应商指令集。
QEMU 专项测试关闭 V，确认 HWCAP.V=0，再调用镜像库完成 8×8 合成并核对像素。
这覆盖了受测的标量分派路径，未覆盖所有 Pixman 入口或 K230 实物硬件，
也不会改变 VGLite 桌面渲染器。CLI `--elf` 与新应用仍严格执行标量限制。

附加消费端测试检查全部导出的 pkg-config 模块、LTO 静态库、CMake 的 GTK/Wayland
导入目标，以及主机 pkg-config 环境污染。离线回归覆盖镜像与 manifest 错配、文件内容
与权限变化、越界链接、重复导出、错误 ISA/ABI，以及非空运行库搜索路径。

## 应用使用与范围

解压路径不要含空格或 shell 特殊字符。按包内 README 加载 `environment-setup.sh`，
使用其编译器、pkg-config 或 CMake 工具链文件。移动 SDK 后重新加载环境，并为应用
创建新的构建目录。需要在主机运行的代码生成器由主机提供，不要运行 sysroot 中的目标程序。

这套 SDK 用于 CPU0 应用。CPU1 固件与 AI 模型开发继续使用各自工具链。
应用显式覆盖 ISA、手写汇编、包维护脚本及完整依赖闭包还需要软件源侧检查。
交付检查不运行板上的应用，也不代替新卡实机验收。

完整 SDK 与镜像在带 tag 的 Release 发布前均为候选产物。对外软件源必须锁定发布后的
文件哈希；NetSurf 安装、HTTPS 页面访问与重启验收随后进行。
SDK 保留供应商文档和 wrapper 的准确源码/许可证，不宣称包含完整 Buildroot legal-info/源码包。

实现参考：[Buildroot SDK 导出](https://buildroot.org/downloads/manual/manual.html#_exporting_the_sdk)
和 [GCC sysroot/路径选项](https://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html)。
