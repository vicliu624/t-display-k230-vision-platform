# 板级层

本目录存放 T-Display K230 Vision Platform 的板级集成文件。

板级层负责把 Buildroot 和 Linux 适配到真实物理开发板。它不定义应用行为。

## 板级目标

- [t-display-k230](t-display-k230/README.zh-CN.md)：LilyGO T-Display K230
  的板级事实、启动说明、镜像布局说明、Linux 移植说明和验证清单。

英文版本：

- [README.md](README.md)

## 目的

板级层连接以下内容：

- Buildroot 镜像生成。
- K230 启动链。
- Linux kernel 和 device tree。
- 板载外设。
- 平台 bring-up 要求。

## 职责

### 启动配置

允许放在这里：

- U-Boot 环境默认值。
- 启动脚本。
- 分区/镜像布局 hook。
- kernel command line 默认值。
- 目标板早期启动所需文件。

### Device Tree 管理

允许放在这里：

- LCD panel 配置。
- 摄像头 sensor 配置。
- MIPI/CSI/DSI 走线。
- GPIO 映射。
- I2C/SPI 总线映射。
- 按键或触摸输入映射。
- 背光和上电时序声明。
- display、camera、DMA 或 AI 所需的内存预留。

### 板级 Bring-Up 验证

板级层应该让以下验证变得可执行：

- Kernel 能在目标板启动。
- Display 能被 BSP 初始化。
- Camera 对 BSP 可见。
- Input 设备对 BSP 可见。
- 必要总线已启用。
- Runtime 能在 init 之后启动。

## 禁止事项

不要把以下内容放进板级层：

- 应用逻辑。
- 视觉 pipeline 逻辑。
- AI model 逻辑。
- SDK example 行为。
- 属于 runtime config 的用户态策略。
- 把 Linux 设备名暴露给应用的 workaround。
- demo 专用启动行为。

## 归属规则

板级文件可以描述硬件事实。它们不能定义产品行为。

如果一个变更回答的是“存在什么硬件，以及 Linux 如何启动它”，它可能属于这里。

如果一个变更回答的是“视觉应用应该做什么”，它不属于这里。

## Review 清单

接受板级层变更之前，必须能对以下问题全部回答“是”：

- 这个变更是否是为了让板子正确启动或正确暴露硬件？
- 这个行为是否对所有应用都稳定？
- BSP 是否仍然对 runtime 和 app 隐藏硬件细节？
- 这个变更是否独立于某一个 demo？
- Device tree 是否仍然是硬件描述，而不是 app policy 文件？

