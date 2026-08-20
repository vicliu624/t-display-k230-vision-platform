# K230 Offline Streaming ASR

## Scope

This document defines the offline English speech-to-text foundation for the
T-Display K230 V1.3 image. The intended recognizer is a streaming Zipformer
transducer. Audio capture, feature extraction, encoder execution, decoding,
and text delivery remain local to the device.

The K230 AI acceptance workload installed by the image is a prerequisite, not
an ASR implementation. It proves that the selected Linux kernel, device tree,
GNNE and AI2D character devices, nncase K230 runtime, and a pinned KModel can
execute together on the physical device. A Zipformer model is accepted only
after it passes its own conversion and real-time measurements.

## Hardware Contract

The ASR service requires all of the following:

| Capability | Required evidence |
| --- | --- |
| KPU kernel interface | `/dev/k230-gnne` and `/dev/k230-ai2d` exist, and `vpl-hwctl status` reports `kpu_kernel_ready=1`. |
| KPU runtime | The pinned nncase K230 workload exists and `kpu_reference_runtime_available=1`. |
| KPU execution | `tdvp-kpu-acceptance.service` completes successfully and `kpu_acceptance_state=passed`. |
| Audio capture | ALSA exposes a capture PCM device that records 16 kHz, mono, signed 16-bit little-endian PCM. |
| Microphone route | The V1.3 ASoC graph, microphone power, clocking, and gain path have passed physical capture validation. |
| CPU topology | CPU0 runs Linux. CPU1 is usable only after a firmware lifecycle and CPU0-to-CPU1 transport have physical acceptance; it is not assumed by the first ASR execution path. |

The target service reports unavailable prerequisites directly. It does not
substitute a CPU-only recognizer for a requested KPU session.

## Execution Architecture

```text
ALSA PCM capture (16 kHz, mono, S16_LE)
        |
        v
bounded capture ring
        |
        +--> WebRTC VAD and optional WebRTC noise suppression
        |
        v
log-Mel feature extractor
        |
        v
Zipformer encoder chunks
        |
        +--> KPU KModel partitions through nncase
        +--> CPU0 for unsupported or stateful operators
        |
        v
RNN-T joiner and streaming decoder on CPU0
        |
        v
partial and final UTF-8 transcript events
```

The decoder owns beam state and endpointing. KPU input and output buffers are
owned by the inference worker. Audio callbacks never load models, allocate
unbounded memory, invoke the decoder, or wait on KPU completion.

## Software Boundaries

The ASR subsystem is a standalone C++ component:

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

`recognizer.hpp` is the product-facing API. Robot, terminal, and other
applications receive transcript events from the service API; they do not
include nncase headers or open ALSA devices. The service controls one active
microphone capture owner and accepts bounded recognition sessions.

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

The first implementation keeps its API stable while model internals remain
versioned under a model manifest.

## KPU Partitioning

The K230 SDK includes an nncase 2.11.0 K230 runtime and a verified example
that constructs an AI2D schedule, loads a KModel through
`nncase::runtime::interpreter`, then invokes its entry function. This proves
the runtime integration route used by the image.

It does not establish that a Zipformer graph, every convolution, every linear
layer, attention/cache operation, or transducer joiner is accepted by that
compiler version. The encoder is therefore partitioned through evidence:

1. Freeze a fixed-duration streaming chunk and explicit cache tensors in ONNX.
2. Compile the candidate chunk with the exact pinned nncase compiler.
3. Record the compiler report and retained CPU partitions.
4. Run deterministic KModel output comparisons on a K230 device.
5. Measure chunk latency, peak resident memory, KPU execution time, and CPU
   execution time under continuous audio input.

Conv, matrix, linear, and activation operations are candidates for KPU
partitioning. Dynamic beam search, token selection, endpoint state, and
variable-length stream control remain CPU work unless a measured model
partition proves otherwise.

## Model Conversion

Each deployable model has a manifest containing model source revision, ONNX
SHA-256, compiler version, compiler options, calibration-set SHA-256, KModel
SHA-256, expected tensor shapes, language, vocabulary revision, and benchmark
record.

```text
streaming Zipformer checkpoint
        |
        v
fixed-chunk ONNX encoder + explicit cache inputs/outputs
        |
        v
ONNX shape and numerical validation
        |
        v
INT8 calibration and quantization
        |
        v
pinned nncase compiler
        |
        v
KModel encoder partitions + CPU partition manifest
        |
        v
K230 output comparison and real-time benchmark
```

Calibration data must contain representative English speech, silence, and
device microphone captures. It must be held separately from test utterances.
The conversion pipeline fails when dynamic shapes are not made explicit,
unsupported operations are not assigned to a CPU partition, calibration data
does not cover the deployed audio distribution, tensor layout differs between
feature extraction and model input, or the target runtime/compiler versions
do not match the model manifest.

## Model Selection and Performance Gates

`zipformer-small-en` and `zipformer-base-en` remain evaluation candidates.
Neither is selected until the exact model revision is converted and benchmarked
on this board. The release record will contain parameter count, ONNX size,
KModel and CPU-partition sizes, peak RAM, chunk latency, real-time factor,
CPU utilization, KPU utilization where available, power measurement method,
and word error rate.

The first product gate is:

| Metric | Gate |
| --- | --- |
| Memory | less than 500 MiB resident memory for capture, model, and decoder together |
| Real-time factor | less than 1.0 for continuous English speech |
| Partial-result latency | less than 500 ms after an audio chunk becomes available |
| Correctness | deterministic target output comparison plus a versioned English WER set |

No model is included in a release merely because it compiles.

## Delivery Sequence

1. Complete KPU and microphone physical acceptance.
2. Validate the streaming Zipformer model and WER on PC Linux with a CPU
   reference implementation.
3. Export and quantify fixed streaming encoder chunks.
4. Convert and verify KPU partitions on K230.
5. Integrate the bounded C++ service and API with Robot.
6. Measure uninterrupted 10-second and long-running voice sessions, then
   publish model and performance manifests with the image.
