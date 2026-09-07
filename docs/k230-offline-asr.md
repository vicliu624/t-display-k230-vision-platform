# K230 Offline Streaming ASR

## Scope

This document describes planned offline speech-to-text, not an implemented
image capability. Streaming Zipformer is an English-model candidate, subject
to conversion, accuracy, memory and real-time validation. No release model
has been selected.

The architecture confirmed on 2026-09-07 gives CPU1/RT-Smart exclusive AI
subsystem ownership: KPU, AI2D, FFT, AI memory and inference services. Linux
uses asynchronous requests/results, with no production direct GNNE/AI2D
execution path. Audio capture/playback initially remains on Linux. Earlier
Linux KPU acceptance cannot establish CPU1 inference or ASR readiness.

## Hardware Contract

The ASR service requires all of the following:

| Capability | Required evidence |
| --- | --- |
| AI ownership | Matched Linux DT, CPU1 firmware and startup handoff; Linux does not bind AI accelerators; CPU1 owns their registers, interrupts and AI working memory. |
| KPU runtime | Pinned CPU1 nncase runtime matches the model compiler; the Linux client receives explicit capability and fault reports. |
| KPU execution | CPU1 executes a fixed model, checks outputs and returns results across cores; device registration alone is not inference. |
| FFT | CPU1 hardware FFT/IFFT output matches a numerical reference; acoustic features additionally validate INT16 scaling and error. |
| Audio capture | ALSA exposes a capture PCM device that records 16 kHz, mono, signed 16-bit little-endian PCM. |
| Microphone route | The V1.3 ASoC graph, microphone power, clocking, and gain path have passed physical capture validation. |
| CPU topology | CPU0 runs Linux; CPU1 runs preprocessing, CPU model partitions, KPU calls, decoding and postprocessing. Audio/result transport needs physical acceptance. |

The target service reports unavailable prerequisites directly. It does not
substitute a CPU-only recognizer for a requested KPU session.

## Execution Architecture

```text
CPU0/Linux ALSA PCM capture (16 kHz, mono, S16_LE)
        |
        v
bounded capture queue -> cross-core PCM -> CPU1 session
        |
        +--> CPU1 VAD and optional noise suppression (implementation unverified)
        |
        v
CPU1 log-Mel features (hardware FFT use requires validation)
        |
        v
Zipformer encoder chunks
        |
        +--> KPU KModel partitions through nncase
        +--> CPU1 for CPU partitions and stateful computation
        |
        v
RNN-T joiner and streaming decoder on CPU1
        |
        v
cross-core partial/final UTF-8 transcript events -> Linux applications
```

The CPU1 decoder owns beam state and endpointing. The CPU1 inference service
owns KPU buffers. Linux audio callbacks never load models, allocate unbounded
memory, invoke the decoder or wait on KPU. Vision and speech share a bounded
KPU scheduler; do not assume arbitrary inference preemption or permit vision
to block audio chunks indefinitely.

## Software Boundaries

These are logical boundaries, not existing implementation paths or APIs:

```text
Linux: application API, permissions, ALSA, PCM queue, cross-core client, text events
CPU1: preprocessing, features, model, nncase/KPU, decoding, endpointing, scheduler, AI memory
Protocol: version, session ID, sequence, format, timestamp, pressure, cancel, faults, results
```

The Linux client provides the product API. Applications receive transcripts
without nncase headers or direct ALSA access. The Linux service owns microphone
capture; the CPU1 AI service owns recognition computation. The C++ interface
below remains a draft:

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

Pin and validate the RT-Smart nncase runtime/compiler/model combination anew.
An earlier Linux runtime version or workload is not CPU1 migration evidence.
Tests must cover AI2D scheduling, KModel loading, actual KPU execution and
output comparisons.

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
variable-length stream control remain CPU1 work unless a measured model
partition proves otherwise. Never restore CPU0 direct KPU access.

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
| Memory | Validated separate CPU1 AI/MMZ, RT-Smart heap and Linux audio-queue budgets; record peak usage with vision active, without borrowing Linux CMA or unreserved memory |
| Real-time factor | less than 1.0 for continuous English speech |
| Partial-result latency | less than 500 ms after an audio chunk becomes available |
| Correctness | deterministic target output comparison plus a versioned English WER set |

No model is included merely because it compiles. The vision candidate MMZ is
128 MiB, shared with camera buffers, not a guaranteed ASR model capacity. The
old 500 MiB ASR ceiling does not apply. Recalculate the layout before model
selection and update Linux DT, CPU1 firmware and image guards together.

## Delivery Sequence

1. Complete CPU1 AI ownership, KPU/AI2D/FFT and cross-core physical acceptance,
   plus Linux microphone capture acceptance.
2. Validate the streaming Zipformer model and WER on PC Linux with a CPU
   reference implementation.
3. Export and quantify fixed streaming encoder chunks.
4. Execute and verify KPU partitions and CPU1 decoding on K230.
5. Integrate the bounded C++ service and API with Robot.
6. Measure uninterrupted 10-second and long-running sessions, including
   vision/VGLite coexistence, latency, accuracy and memory, then publish the
   model and performance manifests with the image.
