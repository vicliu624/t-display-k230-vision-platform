# K230 离线流式语音识别

## 范围

本文定义 T-Display K230 V1.3 镜像中的离线英文语音转文字基础能力。目标识别器为流式 Zipformer Transducer。音频采集、特征提取、编码器执行、解码和文本输出均在设备本地完成。

镜像内置的 KPU 验收负载不是语音识别实现。它用于验证同一台物理设备上的 Linux 内核、设备树、GNNE 和 AI2D 字符设备、nncase K230 运行时以及固定 KModel 能够一起执行。Zipformer 模型需要独立完成转换和实时性测量后才能进入镜像。

## 硬件合同

ASR 服务依赖以下全部条件：

| 能力 | 所需证据 |
| --- | --- |
| KPU 内核接口 | 存在 `/dev/k230-gnne` 和 `/dev/k230-ai2d`，且 `vpl-hwctl status` 中 `kpu_kernel_ready=1`。 |
| KPU 运行时 | 固定的 nncase K230 验收负载存在，且 `kpu_reference_runtime_available=1`。 |
| KPU 执行 | `tdvp-kpu-acceptance.service` 成功完成，且 `kpu_acceptance_state=passed`。 |
| 音频采集 | ALSA 暴露能够采集 16 kHz、单声道、PCM S16_LE 的设备。 |
| 麦克风路径 | V1.3 ASoC 图、麦克风供电、时钟和增益路径通过实际采集验证。 |
| CPU 拓扑 | CPU0 运行 Linux。CPU1 只有在固件生命周期和 CPU0 到 CPU1 传输完成物理验收后才能参与工作；首个 ASR 路径不依赖 CPU1。 |

服务直接报告缺失的前提条件。请求 KPU 会话时不会悄悄替换为 CPU 识别器。

## 执行架构

```text
ALSA PCM 采集（16 kHz、单声道、S16_LE）
        |
        v
有界采集环形缓冲区
        |
        +--> WebRTC VAD 与可选 WebRTC 降噪
        |
        v
log-Mel 特征提取
        |
        v
Zipformer 编码器分块
        |
        +--> 通过 nncase 调用 KPU KModel 分区
        +--> CPU0 执行不支持或有状态的算子
        |
        v
CPU0 RNN-T Joiner 与流式解码器
        |
        v
部分与最终 UTF-8 文本事件
```

解码器拥有 beam 状态和端点检测状态。KPU 输入输出缓冲区由推理工作线程管理。音频回调不加载模型、不进行无界内存分配、不调用解码器，也不等待 KPU 完成。

## 软件边界

ASR 是独立的 C++ 组件：

```text
user-space/vicliu-pocket-linux-asr/
├── include/vpl/asr/
│   ├── recognizer.hpp
│   ├── session.hpp
│   ├── transcript.hpp
│   └── availability.hpp
├── src/
│   ├── audio/alsa_capture.cpp
│   ├── audio/ring_buffer.cpp
│   ├── preprocess/noise_suppression.cpp
│   ├── preprocess/vad.cpp
│   ├── feature/log_mel.cpp
│   ├── inference/kmodel_encoder.cpp
│   ├── inference/nncase_runtime.cpp
│   ├── decoder/transducer_decoder.cpp
│   ├── session/recognizer_session.cpp
│   └── service/asr_daemon.cpp
└── tests/
```

`recognizer.hpp` 是面向产品的 API。Robot、Terminal 和其他程序从服务 API 接收文本事件，不直接包含 nncase 头文件或打开 ALSA 设备。服务拥有单一的麦克风采集权，并提供有界的识别会话。

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

K230 SDK 包含 nncase 2.11.0 K230 运行时及一个已验证的工作负载：它构建 AI2D 调度，通过 `nncase::runtime::interpreter` 加载 KModel，再调用入口函数。该负载证明了镜像所用的运行时集成路径。

它不能证明 Zipformer 图、每个卷积、线性层、注意力缓存操作或 Transducer Joiner 都能被该编译器版本接受。因此编码器分区必须通过实际证据完成：

1. 固定流式分块时长，并在 ONNX 中显式声明 cache 张量。
2. 使用固定版本的 nncase 编译候选分块。
3. 保存编译器报告与保留在 CPU 的分区。
4. 在 K230 上执行确定性的 KModel 输出对比。
5. 在连续音频下测量分块延迟、峰值驻留内存、KPU 执行时间和 CPU 执行时间。

卷积、矩阵计算、线性层和激活是 KPU 候选。动态 beam search、token 选择、端点状态和变长流控制保留在 CPU，除非模型分区实测证明可以改变。

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
| 内存 | 采集、模型和解码器合计驻留内存小于 500 MiB |
| 实时率 | 连续英文语音小于 1.0 |
| 部分结果延迟 | 音频分块可用后小于 500 ms |
| 正确性 | 确定性的目标输出对比和版本化英文 WER 集 |

模型仅能编译不能作为发布依据。

## 交付顺序

1. 完成 KPU 和麦克风的物理验收。
2. 在 PC Linux 上通过 CPU 参考实现验证流式 Zipformer 模型和 WER。
3. 导出并量化固定流式编码器分块。
4. 在 K230 上转换并验证 KPU 分区。
5. 将有界 C++ 服务和 API 接入 Robot。
6. 测量连续 10 秒和长时间语音会话，并与镜像一起发布模型和性能清单。
