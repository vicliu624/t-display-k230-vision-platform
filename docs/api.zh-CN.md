# T-Display K230 Vision Platform API 规格

本文定义 T-Display K230 Vision Platform SDK 的 public API contract。

英文版本：

- [api.md](api.md)

本 API 故意保持小。它的目标是让 application developers 远离 Linux internals，
同时仍然获得 camera、display、input、AI 和 vision pipeline 能力。

本中文文件是 API 规格的中文说明版；C header 草案和符号命名必须与 [api.md](api.md)
同步维护。

## 1. API Boundary Rules

Application code 只能 include public platform headers。

允许依赖：

- BSP public headers。
- Vision runtime public headers。
- SDK helper headers。
- Standard C/C++ headers。

禁止依赖：

- V4L2 headers。
- DRM/KMS headers。
- evdev headers。
- Linux ioctl contracts。
- Kernel driver private headers。
- Device tree names。
- `/dev/video*`、`/dev/dri/*`、`/dev/input/*` paths。
- sysfs/procfs probing。
- Buildroot package assumptions。

BSP 内部可以使用这些 Linux details，但必须在返回 runtime 或 SDK 前转换为稳定 platform types
和 errors。

## 2. Naming and Versioning

Public C APIs 使用以下 prefixes：

- `tdvp_camera_`：camera BSP。
- `tdvp_display_`：display BSP。
- `tdvp_input_`：input BSP。
- `tdvp_lora_`：LoRa BSP。
- `tdvp_buffer_`：runtime buffer APIs。
- `tdvp_ai_`：runtime AI APIs。
- `tdvp_pipeline_`：runtime pipeline APIs。
- `tdvp_runtime_`：runtime event loop APIs。

Public headers 安装在：

```text
include/tdvp/
```

API versioning：

```c
#define TDVP_API_VERSION_MAJOR 0
#define TDVP_API_VERSION_MINOR 1
#define TDVP_API_VERSION_PATCH 0
```

Compatibility rules：

- Patch versions 可以修复行为，但不得改变 ABI。
- Minor versions 可以以 backward-compatible 方式添加 fields 或 functions。
- Major versions 只有附带 migration guide 时才允许 break ABI。
- 可能演进的 public structs 必须包含 `size` field。
- Reserved fields 必须由 callers zero-initialize。

## 3. Common Types

Common types 定义平台统一 status、pixel format、size、rect、frame 和 version。

核心类型：

- `tdvp_status_t`
- `tdvp_pixel_format_t`
- `tdvp_size_t`
- `tdvp_rect_t`
- `tdvp_frame_t`
- `tdvp_version_t`

关键规则：

- `tdvp_frame_t.handle` 是 runtime/BSP-owned opaque handle，application 不得解释。
- `tdvp_frame_t.data` 是可选 CPU-accessible pointer，hardware-only buffer 时可以是 `NULL`。
- 所有 errors 必须通过 `tdvp_status_t` 返回。
- `tdvp_status_string()` 用于输出稳定错误文本。

## 4. BSP Camera API

Camera API 拥有 sensor access 和 frame capture。它隐藏 V4L2、media controller、device node
和 sensor-driver details。

主要符号：

```c
typedef struct tdvp_camera tdvp_camera_t;

tdvp_status_t tdvp_camera_open(const tdvp_camera_config_t *config,
                               tdvp_camera_t **out_camera);
tdvp_status_t tdvp_camera_close(tdvp_camera_t *camera);
tdvp_status_t tdvp_camera_get_info(tdvp_camera_t *camera,
                                   tdvp_camera_info_t *out_info);
tdvp_status_t tdvp_camera_start(tdvp_camera_t *camera);
tdvp_status_t tdvp_camera_stop(tdvp_camera_t *camera);
tdvp_status_t tdvp_camera_read(tdvp_camera_t *camera,
                               uint32_t timeout_ms,
                               tdvp_frame_t *out_frame);
tdvp_status_t tdvp_camera_release_frame(tdvp_camera_t *camera,
                                        tdvp_frame_t *frame);
```

规则：

- Application 不得知道 camera device node。
- `read` 返回 platform frame handle。
- Buffer ownership 由 BSP/runtime 管理。
- `release_frame` 后 application 不得继续访问 frame。

## 5. BSP Display API

Display API 拥有 display initialization 和 frame presentation。它隐藏 DRM/KMS、planes、
connectors、framebuffers 和 panel sequencing。

主要符号：

```c
typedef struct tdvp_display tdvp_display_t;

tdvp_status_t tdvp_display_open(const tdvp_display_config_t *config,
                                tdvp_display_t **out_display);
tdvp_status_t tdvp_display_close(tdvp_display_t *display);
tdvp_status_t tdvp_display_get_info(tdvp_display_t *display,
                                    tdvp_display_info_t *out_info);
tdvp_status_t tdvp_display_clear(tdvp_display_t *display, uint32_t color_rgba);
tdvp_status_t tdvp_display_present(tdvp_display_t *display,
                                   const tdvp_frame_t *frame,
                                   const tdvp_rect_t *src_rect,
                                   const tdvp_rect_t *dst_rect);
```

规则：

- Application 不得直接操作 DRM device。
- Display mode selection 由 BSP 控制。
- Backlight、panel reset、power sequencing 属于 BSP。
- Runtime 只看到 `tdvp_display_present()` 这样的稳定接口。

## 6. BSP Input API

Input API 将 buttons、touch、encoder 或其他 input sources 归一化为 platform events。

主要符号：

```c
typedef struct tdvp_input tdvp_input_t;

tdvp_status_t tdvp_input_open(const tdvp_input_config_t *config,
                              tdvp_input_t **out_input);
tdvp_status_t tdvp_input_close(tdvp_input_t *input);
tdvp_status_t tdvp_input_read(tdvp_input_t *input,
                              uint32_t timeout_ms,
                              tdvp_input_event_t *out_event);
```

规则：

- Application 不得打开 `/dev/input/*`。
- BSP 负责 evdev/code/value 到 platform event 的转换。
- Event names 必须稳定，不随 Linux key code 泄露到 app。

## 7. Board Capability APIs

LoRa、audio、storage、power、radio、sensor 等能力只有在目标 profile 实际存在并完成平台裁决后才进入
public API。

LoRa 示例：

```c
typedef struct tdvp_lora tdvp_lora_t;

tdvp_status_t tdvp_lora_open(const tdvp_lora_config_t *config,
                             tdvp_lora_t **out_lora);
tdvp_status_t tdvp_lora_close(tdvp_lora_t *lora);
tdvp_status_t tdvp_lora_send(tdvp_lora_t *lora,
                             const void *data,
                             size_t length,
                             uint32_t timeout_ms);
tdvp_status_t tdvp_lora_recv(tdvp_lora_t *lora,
                             void *buffer,
                             size_t buffer_size,
                             size_t *out_length,
                             uint32_t timeout_ms);
```

规则：

- 不因硬件可能存在就暴露 API。
- API 必须隐藏 SPI/I2C/GPIO/radio driver details。
- 能力缺失时返回 `TDVP_ERR_NOT_SUPPORTED`。

## 8. Runtime Buffer API

Buffer API 定义 runtime-owned frame/buffer ownership。

主要符号：

```c
typedef struct tdvp_buffer_pool tdvp_buffer_pool_t;

tdvp_status_t tdvp_buffer_pool_create(const tdvp_buffer_pool_config_t *config,
                                      tdvp_buffer_pool_t **out_pool);
tdvp_status_t tdvp_buffer_pool_destroy(tdvp_buffer_pool_t *pool);
tdvp_status_t tdvp_buffer_acquire(tdvp_buffer_pool_t *pool,
                                  uint32_t timeout_ms,
                                  tdvp_frame_t *out_frame);
tdvp_status_t tdvp_buffer_release(tdvp_buffer_pool_t *pool,
                                  tdvp_frame_t *frame);
```

规则：

- Application 不拥有 physical buffer details。
- Zero-copy 是 runtime/BSP optimization，不是 app contract。
- Buffer state transitions 必须由 runtime 管理。
- Error buffer 不得未经 reset 重新使用。

## 9. Runtime AI API

AI API 抽象 KPU、nncase、tflite 或其他 backend。

主要符号：

```c
typedef struct tdvp_ai_model tdvp_ai_model_t;
typedef struct tdvp_ai_session tdvp_ai_session_t;

tdvp_status_t tdvp_ai_model_load(const tdvp_ai_model_config_t *config,
                                 tdvp_ai_model_t **out_model);
tdvp_status_t tdvp_ai_model_unload(tdvp_ai_model_t *model);
tdvp_status_t tdvp_ai_session_create(tdvp_ai_model_t *model,
                                     const tdvp_ai_session_config_t *config,
                                     tdvp_ai_session_t **out_session);
tdvp_status_t tdvp_ai_session_destroy(tdvp_ai_session_t *session);
tdvp_status_t tdvp_ai_run(tdvp_ai_session_t *session,
                          const tdvp_frame_t *input_frame,
                          tdvp_ai_result_t *out_result);
```

规则：

- Application 不得持有 backend-private handles。
- Model path、tensor shape、backend selection 必须通过 platform config 管理。
- Runtime 可以选择 hardware acceleration，但不得让 app 依赖硬件细节。

## 10. Vision Pipeline API

Pipeline API 定义 camera -> preprocess -> AI -> overlay -> display 的标准执行模型。

主要符号：

```c
typedef struct tdvp_pipeline tdvp_pipeline_t;

tdvp_status_t tdvp_pipeline_create(const tdvp_pipeline_config_t *config,
                                   tdvp_pipeline_t **out_pipeline);
tdvp_status_t tdvp_pipeline_destroy(tdvp_pipeline_t *pipeline);
tdvp_status_t tdvp_pipeline_start(tdvp_pipeline_t *pipeline);
tdvp_status_t tdvp_pipeline_stop(tdvp_pipeline_t *pipeline);
tdvp_status_t tdvp_pipeline_step(tdvp_pipeline_t *pipeline,
                                 uint32_t timeout_ms);
```

规则：

- Stage order 必须稳定。
- Application extension 应通过 callbacks/stages 进入。
- App 不得绕过 runtime 创建自己的 camera/display/AI loop。
- Pipeline errors 必须转换成 platform status 和 event。

## 11. Runtime Event Loop API

Runtime Event Loop 是 application 的默认主循环。

主要符号：

```c
typedef struct tdvp_runtime tdvp_runtime_t;

tdvp_status_t tdvp_runtime_create(const tdvp_runtime_config_t *config,
                                  tdvp_runtime_t **out_runtime);
tdvp_status_t tdvp_runtime_destroy(tdvp_runtime_t *runtime);
tdvp_status_t tdvp_runtime_run(tdvp_runtime_t *runtime);
tdvp_status_t tdvp_runtime_request_stop(tdvp_runtime_t *runtime);
```

规则：

- Public event loop 默认为单一入口。
- Runtime 内部可以有 workers，但 application callbacks 默认串行。
- Callback 不得无限阻塞。
- Stop request 必须可预测地结束 runtime。

## 12. Minimal Application Example

Application 只 include platform header：

```c
#include "tdvp/runtime.h"
```

Minimal shape：

```c
int main(void)
{
    tdvp_runtime_config_t config = {0};
    config.size = sizeof(config);

    tdvp_runtime_t *runtime = NULL;
    if (tdvp_runtime_create(&config, &runtime) != TDVP_OK) {
        return 1;
    }

    tdvp_status_t status = tdvp_runtime_run(runtime);
    tdvp_runtime_destroy(runtime);

    return status == TDVP_OK ? 0 : 1;
}
```

## 13. Strict Abstraction Checklist

接受任何 API 或 example 前必须检查：

- Application 是否只 include `tdvp/*` 和 standard headers？
- 是否没有 `/dev/video*`、`/dev/dri/*`、`/dev/input/*` hardcode？
- 是否没有 V4L2、DRM/KMS、evdev、ioctl 直接调用？
- 是否没有把 Buildroot package choice 暴露成 app contract？
- Buffer ownership 是否通过 runtime/BSP APIs 表达？
- AI backend 是否被 wrapper 隐藏？
- Display/camera/input details 是否被 BSP 隐藏？
- Error 是否通过 `tdvp_status_t` 返回？
- Public structs 是否包含 `size` 和 reserved fields？

如果一个 change 迫使 application developer 理解 Linux internals，它就是 API regression。
