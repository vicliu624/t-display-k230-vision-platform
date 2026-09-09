# K230 离线流式语音识别

## 范围

本文定义 T-Display K230 V1.3 的待实现离线语音转文字方案，不代表镜像已具有 ASR。流式 Zipformer Transducer 是英文模型候选，必须独立完成转换、精度、内存与实时性评估；尚未选定可发布模型。

2026-09-07 确认的新架构：CPU1/RT-Smart 独占 AI 子系统资源，统一管理 KPU、AI2D、FFT、AI 内存与推理服务。CPU0/Linux 通过异步接口使用 AI，不保留直接调用 GNNE/AI2D 的生产执行路径。音频采集与播放第一阶段仍归 Linux。旧 Linux 直连 KPU 的验收结果不能代替 CPU1 推理验收，更不能证明语音识别已经实现。

截至 2026-09-09，CPU1 已有固定 KWS 模型、有限 AI2D 和 FFT/IFFT 的
[异步作业与实机记录](cpu1-ai-jobs.zh-CN.md)。这些能力可供后续 ASR 开发复用；
声学特征、流式模型、解码器和文字事件接口仍待实现。

## 硬件合同

ASR 服务依赖以下全部条件：

| 能力 | 所需证据 |
| --- | --- |
| AI 所有权 | 匹配的 Linux DT、CPU1 固件和启动交接通过；Linux 不绑定 AI 加速器，CPU1 独占其寄存器、中断及 AI 工作内存。 |
| KPU 运行时 | CPU1 上的固定 nncase 运行时与 KModel 编译器版本匹配；Linux 客户端收到明确的服务能力与故障状态。 |
| KPU 执行 | 在 CPU1 实际执行固定模型并验证输出，经跨核接口返回结果；字符设备存在或驱动注册成功不算推理通过。 |
| FFT | CPU1 完成硬件 FFT/IFFT 与数值参考对比；声学特征还须独立验证 INT16 缩放与误差。 |
| 音频采集 | ALSA 暴露能够采集 16 kHz、单声道、PCM S16_LE 的设备。 |
| 麦克风路径 | V1.3 ASoC 图、麦克风供电、时钟和增益路径通过实际采集验证。 |
| CPU 拓扑 | CPU0 运行 Linux；CPU1 执行前处理、模型 CPU 分区、KPU 调用、解码与后处理。跨核音频和结果接口必须通过物理验收。 |

服务直接报告缺失的前提条件。请求 KPU 会话时不会悄悄替换为 CPU 识别器。

## 执行架构

```text
CPU0/Linux ALSA PCM 采集（16 kHz、单声道、S16_LE）
        |
        v
有界采集队列 → 跨核 PCM 传输 → CPU1 会话
        |
        +--> CPU1 VAD 与可选降噪（实现需验证）
        |
        v
CPU1 log-Mel 特征提取（FFT 加速需验证）
        |
        v
Zipformer 编码器分块
        |
        +--> 通过 nncase 调用 KPU KModel 分区
        +--> CPU1 执行未下放 KPU 的算子与有状态计算
        |
        v
CPU1 RNN-T Joiner 与流式解码器
        |
        v
跨核返回部分与最终 UTF-8 文本事件 → Linux 应用
```

CPU1 解码器拥有 beam 状态和端点检测状态。KPU 输入输出缓冲区由 CPU1 推理服务管理。Linux 音频回调不加载模型、不进行无界内存分配、不调用解码器，也不等待 KPU 完成。视觉和语音共用有界的 KPU 调度队列；不得假设任意推理可以抢占，也不能让视觉任务无限阻塞语音分块。

## 软件边界

以下为待实现方案的逻辑边界，目录与 API 仍需设计：

```text
Linux：应用 API、会话权限、ALSA 采集、PCM 队列、跨核客户端、文字事件
CPU1：前处理、特征、模型、nncase/KPU、解码、端点、任务调度、AI 内存
共同协议：版本、会话 ID、分块序号、格式、时戳、背压、取消、故障、结果
```

面向产品的 API 由 Linux 客户端提供。Robot、Terminal 和其他程序从服务 API 接收文本事件，不直接包含 nncase 头文件或打开 ALSA 设备。Linux 服务拥有单一的麦克风采集权；CPU1 AI 服务拥有识别会话的计算资源。下面的 C++ 接口仍是草案：

```cpp
namespace vpl::asr {

struct AudioFormat {
    unsigned int sample_rate = 16000;
    unsigned int channels = 1;
};

struct Availability {
    bool microphone_ready;
    bool kpu_ready;
    bool model_ready;
    std::string detail;
};

class RecognizerSession {
public:
    virtual ~RecognizerSession() = default;
    virtual bool push_pcm_s16le(const int16_t *samples, std::size_t count) = 0;
    virtual void finish() = 0;
};

}  // namespace vpl::asr
```

首个实现保持 API 稳定，模型内部通过模型清单独立版本化。

## KPU 分区

CPU1 必须重新固定并验证 RT-Smart 对应的 nncase 运行时、编译器和模型组合。此前 Linux 运行时的版本或工作负载不能直接作为迁移后的证据。测试须包含 AI2D 调度、KModel 加载、真实 KPU 执行和输出对比。

它不能证明 Zipformer 图、每个卷积、线性层、注意力缓存操作或 Transducer Joiner 都能被该编译器版本接受。因此编码器分区必须通过实际证据完成：

1. 固定流式分块时长，并在 ONNX 中显式声明 cache 张量。
2. 使用固定版本的 nncase 编译候选分块。
3. 保存编译器报告与保留在 CPU 的分区。
4. 在 K230 上执行确定性的 KModel 输出对比。
5. 在连续音频下测量分块延迟、峰值驻留内存、KPU 执行时间和 CPU 执行时间。

卷积、矩阵计算、线性层和激活是 KPU 候选。动态 beam search、token 选择、端点状态和变长流控制保留在 CPU1，除非模型分区实测证明可以改变；不得恢复 CPU0 直连 KPU 路径。

## 模型转换

每个可部署模型都有清单，记录模型源版本、ONNX SHA-256、编译器版本、编译选项、校准集 SHA-256、KModel SHA-256、预期张量形状、语言、词表版本和基准记录。

```text
流式 Zipformer checkpoint
        |
        v
固定分块 ONNX 编码器与显式 cache 输入输出
        |
        v
ONNX 形状与数值验证
        |
        v
INT8 校准与量化
        |
        v
固定版本 nncase 编译器
        |
        v
KModel 编码器分区与 CPU 分区清单
        |
        v
K230 输出对比与实时基准
```

校准数据需要覆盖代表性的英文语音、静音和设备麦克风录音，并与测试语句分离。动态形状未被显式化、未支持算子没有分配给 CPU 分区、校准集不能覆盖部署音频分布、特征提取与模型输入布局不一致、目标运行时与编译器版本不匹配时，转换流程必须失败。

## 模型选择与性能门槛

`zipformer-small-en` 和 `zipformer-base-en` 是待评估候选。只有实际模型版本完成转换并在这块板上测量后才能选择。发布记录将包含参数量、ONNX 大小、KModel 和 CPU 分区大小、峰值 RAM、分块延迟、实时率、CPU 占用、可获得时的 KPU 占用、功耗测量方法和 WER。

首个产品门槛：

| 指标 | 门槛 |
| --- | --- |
| 内存 | 在经验证的 CPU1 AI/MMZ、RT-Smart 堆和 Linux 音频队列各自预算内；记录与视觉并发时的峰值，不借用 Linux CMA 或未保留内存 |
| 实时率 | 连续英文语音小于 1.0 |
| 部分结果延迟 | 音频分块可用后小于 500 ms |
| 正确性 | 确定性的目标输出对比和版本化英文 WER 集 |

模型仅能编译不能作为发布依据。当前视觉候选 MMZ 为 128 MiB，摄像头缓冲区与其他 AI 作业共用该预算；语音模型可用容量需要单独核算。旧的 500 MiB ASR 上限已不适用，具体模型入选前必须重新计算内存布局，并成对更新 Linux DT、CPU1 固件和镜像校验。

## 交付顺序

1. 完成 CPU1 AI 所有权、KPU/AI2D/FFT 和跨核接口的物理验收，以及 Linux 麦克风采集验收。
2. 在 PC Linux 上通过 CPU 参考实现验证流式 Zipformer 模型和 WER。
3. 导出并量化固定流式编码器分块。
4. 在 K230 CPU1 上转换、执行并验证 KPU 分区与 CPU1 解码。
5. 将有界 C++ 服务和 API 接入 Robot。
6. 测量连续 10 秒和长时间语音会话，以及视觉和 VGLite 桌面并发时的延迟、准确率与内存，再与镜像一起发布模型和性能清单。
