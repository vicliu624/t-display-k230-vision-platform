# RootFS Overlay

本目录定义 Buildroot 生成的 root filesystem 的最终 filesystem overlay。

Overlay 是 platform integration mechanism。它不是 application data directory，也不是 proper
Buildroot packages 的替代品。

英文版本：

- [README.md](README.md)

## 目的

只把 overlay 用于必须出现在最终 target filesystem 中、且不适合表达为 package install rules
的文件。

典型职责：

- Init scripts。
- Platform default configuration。
- Runtime launcher glue。
- Platform 所需 static directory layout。
- 随 firmware image versioned 的小型 platform-owned files。

## 期望 Target Layout

Overlay 可以在以下路径下提供文件：

```text
/etc/init.d/
/etc/vision-platform/
/opt/vision/bin/
/opt/vision/lib/
/opt/vision/models/
/opt/vision/examples/
/var/log/
/tmp/
```

Package-owned binaries 和 libraries 通常应该由 Buildroot package rules 安装，而不是手工复制到
overlay。

## Boot Behavior

默认 platform boot path 是：

```text
BusyBox init -> platform init script -> vision runtime or configured demo
```

Init script 必须只启动 platform-owned runtime behavior。它不得编码 demo-specific business
logic。

## 允许的文件

允许：

- 用于 platform startup 的 `/etc/init.d/S*` scripts。
- `/etc/vision-platform/*.conf` default configuration。
- Runtime 所需 empty directories。
- Platform-owned firmware 或 model files，但前提是它们被 versioned 且有意成为 system image
  的一部分。

## 禁止的文件

禁止：

- Package manager configuration。
- Desktop configuration。
- User login customization。
- App-specific runtime data。
- Generated logs、caches 或 temporary files。
- 从 developer workstation 复制来的文件。
- Per-user credentials。
- 应该位于 app 或 runtime config 中的 demo-specific policy。

## Determinism Rules

Overlay 必须从相同 source inputs 产生相同 filesystem output。

不要添加内容依赖以下因素的文件：

- Hostname。
- Current time。
- User home directory。
- Local absolute paths。
- Developer machine state。
- Unversioned downloads。

## Review Checklist

把文件加入 overlay 之前，必须能对以下问题全部回答“是”：

- 这个文件是否必须存在于 target root filesystem？
- 这个文件是 platform-owned，而不是 app-owned 吗？
- 这个文件是否足够小/简单，不值得使用 package install rule？
- 内容是否 deterministic？
- 它是否避免把 Linux internals 泄漏给 applications？
- Boot behavior 是否仍然以 runtime 为中心，而不是以 demo 为中心？

