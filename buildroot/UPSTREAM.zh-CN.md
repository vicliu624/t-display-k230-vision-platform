# Buildroot Upstream 固定版本

本文记录 T-Display K230 Vision Platform 使用的 official Buildroot version。

英文版本：

- [UPSTREAM.md](UPSTREAM.md)

## 当前 Baseline

平台固定到：

```text
Buildroot tag: 2025.02.14
Commit:        898251ee2b83a9cd5ae0ae5db57828035a5a6f85
Series:        2025.02.x LTS
```

Submodule path：

```text
buildroot/buildroot/
```

Upstream URL：

```text
https://gitlab.com/buildroot.org/buildroot.git
```

## 策略

Buildroot submodule 必须固定到 official release tag。

规则：

- 平台构建不得跟踪 `master`。
- 不得固定到任意 development commit。
- Platform firmware 优先使用 active LTS series。
- 只能通过明确的 platform upgrade change 升级 Buildroot。
- Platform customization 必须保留在外层 `buildroot/` br2-external tree。
- 绝不为了 platform behavior 修改 `buildroot/buildroot/` 中的 upstream files。

## 为什么选择 LTS

本项目是 firmware-oriented embedded vision platform。它的 system base 必须优先保证
deterministic behavior、repeatable builds 和 long-term maintenance，而不是追逐新的
Buildroot features。

2025.02.x series 是本项目当前的 LTS baseline。

## 验证固定版本

从 repository root 执行：

```sh
git submodule status buildroot/buildroot
git -C buildroot/buildroot describe --tags --exact-match
git -C buildroot/buildroot rev-parse HEAD
```

期望输出：

```text
2025.02.14
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

## 升级流程

Buildroot upgrade 是 platform event。不要隐式升级。

迁移到新的 official release tag：

```sh
git -C buildroot/buildroot fetch --tags
git -C buildroot/buildroot checkout <official-release-tag>
git add buildroot/buildroot
```

然后验证：

- Platform defconfig 可以恢复。
- `menuconfig` 可以使用外层 `BR2_EXTERNAL` tree 打开。
- System image 可以从 clean output directory 构建。
- Root filesystem layout 仍由平台拥有。
- Kernel configuration 仍保持最小。
- BSP/runtime packages 仍能集成。
- Camera、display、input 和 AI demo behavior 仍稳定。

Upgrade commit message 必须写明 Buildroot release tag。

