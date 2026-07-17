# T-Display K230 Vision Platform API Specification

This document defines the public API contract for the T-Display K230 Vision
Platform SDK.

Chinese version:

- [api.zh-CN.md](api.zh-CN.md)

The API is intentionally small. It exists to keep application developers away
from Linux internals while still giving them camera, display, input, AI, and
vision pipeline capabilities.

## 1. API Boundary Rules

Application code may include only public platform headers.

Allowed application dependencies:

- BSP public headers.
- Vision runtime public headers.
- SDK helper headers.
- Standard C/C++ headers.

Forbidden application dependencies:

- V4L2 headers.
- DRM/KMS headers.
- evdev headers.
- Linux ioctl contracts.
- Kernel driver private headers.
- Device tree names.
- `/dev/video*`, `/dev/dri/*`, `/dev/input/*` paths.
- sysfs/procfs probing.
- Buildroot package assumptions.

The BSP may use these Linux details internally, but it must translate them into
stable platform types and errors before returning to the runtime or SDK.

## 2. Naming and Versioning

Public C APIs use these prefixes:

- `tdvp_camera_` for camera BSP.
- `tdvp_display_` for display BSP.
- `tdvp_input_` for input BSP.
- `tdvp_lora_` for LoRa BSP.
- `tdvp_buffer_` for runtime buffer APIs.
- `tdvp_ai_` for runtime AI APIs.
- `tdvp_pipeline_` for runtime pipeline APIs.
- `tdvp_runtime_` for runtime event loop APIs.

Public headers should be installed under:

```text
include/tdvp/
```

API versioning:

```c
#define TDVP_API_VERSION_MAJOR 0
#define TDVP_API_VERSION_MINOR 1
#define TDVP_API_VERSION_PATCH 0
```

Compatibility rules:

- Patch versions may fix behavior without changing ABI.
- Minor versions may add fields or functions in backward-compatible ways.
- Major versions may break ABI only with a migration guide.
- Public structs that may evolve must contain a `size` field.
- Reserved fields must be zero-initialized by callers.

## 3. Common Types

```c
#ifndef TDVP_COMMON_H
#define TDVP_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TDVP_API_VERSION_MAJOR 0
#define TDVP_API_VERSION_MINOR 1
#define TDVP_API_VERSION_PATCH 0

typedef enum tdvp_status {
    TDVP_OK = 0,
    TDVP_ERR_INVALID_ARG = -1,
    TDVP_ERR_NO_MEMORY = -2,
    TDVP_ERR_NOT_FOUND = -3,
    TDVP_ERR_NOT_SUPPORTED = -4,
    TDVP_ERR_BUSY = -5,
    TDVP_ERR_TIMEOUT = -6,
    TDVP_ERR_IO = -7,
    TDVP_ERR_BAD_STATE = -8,
    TDVP_ERR_MODEL = -9,
    TDVP_ERR_INTERNAL = -10
} tdvp_status_t;

typedef enum tdvp_pixel_format {
    TDVP_PIXFMT_UNKNOWN = 0,
    TDVP_PIXFMT_RGB565,
    TDVP_PIXFMT_RGB888,
    TDVP_PIXFMT_BGR888,
    TDVP_PIXFMT_RGBA8888,
    TDVP_PIXFMT_YUYV,
    TDVP_PIXFMT_NV12,
    TDVP_PIXFMT_GRAYSCALE8
} tdvp_pixel_format_t;

typedef struct tdvp_size {
    uint32_t width;
    uint32_t height;
} tdvp_size_t;

typedef struct tdvp_rect {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} tdvp_rect_t;

typedef struct tdvp_frame {
    uint32_t size;
    tdvp_size_t image_size;
    tdvp_pixel_format_t pixel_format;
    uint32_t stride_bytes;
    uint64_t timestamp_ns;
    uint32_t sequence;

    /*
     * Opaque runtime/BSP-owned storage handle.
     * Applications must not interpret this value.
     */
    void *handle;

    /*
     * Optional CPU-accessible pointer.
     * May be NULL for hardware-only buffers.
     */
    void *data;
    size_t data_size;

    uint32_t reserved[8];
} tdvp_frame_t;

typedef struct tdvp_version {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} tdvp_version_t;

tdvp_version_t tdvp_get_api_version(void);
const char *tdvp_status_string(tdvp_status_t status);

#ifdef __cplusplus
}
#endif

#endif
```

## 4. BSP Camera API

The camera API owns sensor access and frame capture. It hides V4L2, media
controller, device node, and sensor-driver details.

```c
#ifndef TDVP_CAMERA_H
#define TDVP_CAMERA_H

#include "tdvp/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tdvp_camera tdvp_camera_t;

typedef enum tdvp_camera_id {
    TDVP_CAMERA_DEFAULT = 0,
    TDVP_CAMERA_FRONT = 1,
    TDVP_CAMERA_EXTERNAL = 2
} tdvp_camera_id_t;

typedef struct tdvp_camera_config {
    uint32_t size;
    tdvp_camera_id_t camera_id;
    tdvp_size_t frame_size;
    tdvp_pixel_format_t pixel_format;
    uint32_t fps;
    uint32_t buffer_count;
    uint32_t reserved[8];
} tdvp_camera_config_t;

typedef struct tdvp_camera_info {
    uint32_t size;
    char name[64];
    tdvp_size_t max_frame_size;
    uint32_t max_fps;
    uint32_t capability_flags;
    uint32_t reserved[8];
} tdvp_camera_info_t;

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

#ifdef __cplusplus
}
#endif

#endif
```

Camera rules:

- `tdvp_camera_read()` returns platform frame handles, not Linux buffers.
- Applications must call `tdvp_camera_release_frame()` after consuming a frame
  acquired directly from the BSP.
- Runtime-owned camera use should release frames through the runtime buffer
  lifecycle instead of application code.
- Camera configuration must be deterministic. Hidden device probing should not
  change public behavior across boots.

## 5. BSP Display API

The display API owns display device initialization and presentation. It hides
DRM/KMS, framebuffer, plane, connector, and mode-setting details.

```c
#ifndef TDVP_DISPLAY_H
#define TDVP_DISPLAY_H

#include "tdvp/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tdvp_display tdvp_display_t;

typedef enum tdvp_display_id {
    TDVP_DISPLAY_DEFAULT = 0,
    TDVP_DISPLAY_INTERNAL = 1,
    TDVP_DISPLAY_EXTERNAL = 2
} tdvp_display_id_t;

typedef enum tdvp_present_mode {
    TDVP_PRESENT_IMMEDIATE = 0,
    TDVP_PRESENT_VSYNC = 1
} tdvp_present_mode_t;

typedef struct tdvp_display_config {
    uint32_t size;
    tdvp_display_id_t display_id;
    tdvp_size_t output_size;
    tdvp_pixel_format_t pixel_format;
    tdvp_present_mode_t present_mode;
    uint32_t reserved[8];
} tdvp_display_config_t;

typedef struct tdvp_display_info {
    uint32_t size;
    char name[64];
    tdvp_size_t native_size;
    tdvp_pixel_format_t preferred_format;
    uint32_t capability_flags;
    uint32_t reserved[8];
} tdvp_display_info_t;

tdvp_status_t tdvp_display_open(const tdvp_display_config_t *config,
                                tdvp_display_t **out_display);

tdvp_status_t tdvp_display_close(tdvp_display_t *display);

tdvp_status_t tdvp_display_get_info(tdvp_display_t *display,
                                    tdvp_display_info_t *out_info);

tdvp_status_t tdvp_display_clear(tdvp_display_t *display,
                                 uint32_t rgba_color);

tdvp_status_t tdvp_display_present(tdvp_display_t *display,
                                   const tdvp_frame_t *frame,
                                   const tdvp_rect_t *dst_rect);

#ifdef __cplusplus
}
#endif

#endif
```

Display rules:

- Display format conversion is a BSP/runtime concern.
- Applications should not select DRM planes or display connectors.
- `dst_rect == NULL` means full-screen presentation.
- The display API must return stable errors for unsupported modes.

## 6. BSP Input API

The input API normalizes buttons, touch, and simple controls. It hides evdev
and device-node details.

```c
#ifndef TDVP_INPUT_H
#define TDVP_INPUT_H

#include "tdvp/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tdvp_input tdvp_input_t;

typedef enum tdvp_input_event_type {
    TDVP_INPUT_NONE = 0,
    TDVP_INPUT_BUTTON_DOWN,
    TDVP_INPUT_BUTTON_UP,
    TDVP_INPUT_TOUCH_DOWN,
    TDVP_INPUT_TOUCH_MOVE,
    TDVP_INPUT_TOUCH_UP,
    TDVP_INPUT_ROTARY_DELTA
} tdvp_input_event_type_t;

typedef enum tdvp_button {
    TDVP_BUTTON_UNKNOWN = 0,
    TDVP_BUTTON_A,
    TDVP_BUTTON_B,
    TDVP_BUTTON_MENU,
    TDVP_BUTTON_BACK,
    TDVP_BUTTON_POWER
} tdvp_button_t;

typedef struct tdvp_input_event {
    uint32_t size;
    tdvp_input_event_type_t type;
    uint64_t timestamp_ns;
    tdvp_button_t button;
    int32_t x;
    int32_t y;
    int32_t delta;
    uint32_t reserved[8];
} tdvp_input_event_t;

typedef struct tdvp_input_config {
    uint32_t size;
    uint32_t reserved[8];
} tdvp_input_config_t;

tdvp_status_t tdvp_input_open(const tdvp_input_config_t *config,
                              tdvp_input_t **out_input);

tdvp_status_t tdvp_input_close(tdvp_input_t *input);

tdvp_status_t tdvp_input_read(tdvp_input_t *input,
                              uint32_t timeout_ms,
                              tdvp_input_event_t *out_event);

#ifdef __cplusplus
}
#endif

#endif
```

Input rules:

- Application code must handle input by semantic event, not by Linux key code.
- BSP owns the mapping from device-specific events to platform events.
- Input reads may time out and return `TDVP_ERR_TIMEOUT`.

## 7. Board Capability LoRa API

LoRa is a board capability when the target T-Display K230 revision has a
populated LoRa module. It must follow the BSP abstraction rule and must not
expose SPI, GPIO, or ioctl details to applications.

```c
#ifndef TDVP_LORA_H
#define TDVP_LORA_H

#include "tdvp/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tdvp_lora tdvp_lora_t;

typedef struct tdvp_lora_config {
    uint32_t size;
    uint32_t frequency_hz;
    uint32_t bandwidth_hz;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    int8_t tx_power_dbm;
    uint8_t reserved0;
    uint32_t reserved[8];
} tdvp_lora_config_t;

tdvp_status_t tdvp_lora_open(const tdvp_lora_config_t *config,
                             tdvp_lora_t **out_lora);

tdvp_status_t tdvp_lora_close(tdvp_lora_t *lora);

tdvp_status_t tdvp_lora_send(tdvp_lora_t *lora,
                             const void *data,
                             size_t size,
                             uint32_t timeout_ms);

tdvp_status_t tdvp_lora_recv(tdvp_lora_t *lora,
                             void *buffer,
                             size_t buffer_size,
                             size_t *out_size,
                             uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
```

## 8. Runtime Buffer API

The buffer API provides frame ownership and lifecycle control. Applications may
inspect frame metadata, but physical memory and DMA details remain opaque.

```c
#ifndef TDVP_BUFFER_H
#define TDVP_BUFFER_H

#include "tdvp/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tdvp_buffer_pool tdvp_buffer_pool_t;

typedef enum tdvp_buffer_usage {
    TDVP_BUFFER_USAGE_CAMERA = 1 << 0,
    TDVP_BUFFER_USAGE_AI = 1 << 1,
    TDVP_BUFFER_USAGE_DISPLAY = 1 << 2,
    TDVP_BUFFER_USAGE_CPU_READ = 1 << 3,
    TDVP_BUFFER_USAGE_CPU_WRITE = 1 << 4
} tdvp_buffer_usage_t;

typedef struct tdvp_buffer_pool_config {
    uint32_t size;
    tdvp_size_t frame_size;
    tdvp_pixel_format_t pixel_format;
    uint32_t buffer_count;
    uint32_t usage_flags;
    uint32_t reserved[8];
} tdvp_buffer_pool_config_t;

tdvp_status_t tdvp_buffer_pool_create(
    const tdvp_buffer_pool_config_t *config,
    tdvp_buffer_pool_t **out_pool);

tdvp_status_t tdvp_buffer_pool_destroy(tdvp_buffer_pool_t *pool);

tdvp_status_t tdvp_buffer_acquire(tdvp_buffer_pool_t *pool,
                                  tdvp_frame_t *out_frame);

tdvp_status_t tdvp_buffer_release(tdvp_buffer_pool_t *pool,
                                  tdvp_frame_t *frame);

tdvp_status_t tdvp_buffer_map_cpu(tdvp_frame_t *frame,
                                  void **out_data,
                                  size_t *out_size);

tdvp_status_t tdvp_buffer_unmap_cpu(tdvp_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif
```

Buffer rules:

- A frame handle is opaque.
- CPU mapping may fail for hardware-only buffers.
- Buffer ownership must be explicit.
- Zero-copy is preferred, but never exposed as a required app behavior.

## 9. Runtime AI API

The AI API abstracts KPU, nncase, and optional software inference backends.
Applications must not bind directly to accelerator-specific runtime details.

```c
#ifndef TDVP_AI_H
#define TDVP_AI_H

#include "tdvp/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tdvp_ai_model tdvp_ai_model_t;
typedef struct tdvp_ai_context tdvp_ai_context_t;

typedef enum tdvp_ai_backend {
    TDVP_AI_BACKEND_AUTO = 0,
    TDVP_AI_BACKEND_KPU,
    TDVP_AI_BACKEND_NNCASE,
    TDVP_AI_BACKEND_TFLITE
} tdvp_ai_backend_t;

typedef enum tdvp_tensor_type {
    TDVP_TENSOR_UINT8 = 0,
    TDVP_TENSOR_INT8,
    TDVP_TENSOR_FLOAT32
} tdvp_tensor_type_t;

typedef struct tdvp_tensor {
    uint32_t size;
    tdvp_tensor_type_t type;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    void *data;
    size_t data_size;
    uint32_t reserved[8];
} tdvp_tensor_t;

typedef struct tdvp_ai_model_config {
    uint32_t size;
    const char *model_path;
    tdvp_ai_backend_t backend;
    uint32_t reserved[8];
} tdvp_ai_model_config_t;

typedef struct tdvp_ai_result {
    uint32_t size;
    uint32_t tensor_count;
    tdvp_tensor_t *tensors;
    uint32_t reserved[8];
} tdvp_ai_result_t;

tdvp_status_t tdvp_ai_model_load(const tdvp_ai_model_config_t *config,
                                 tdvp_ai_model_t **out_model);

tdvp_status_t tdvp_ai_model_unload(tdvp_ai_model_t *model);

tdvp_status_t tdvp_ai_context_create(tdvp_ai_model_t *model,
                                     tdvp_ai_context_t **out_context);

tdvp_status_t tdvp_ai_context_destroy(tdvp_ai_context_t *context);

tdvp_status_t tdvp_ai_infer(tdvp_ai_context_t *context,
                            const tdvp_frame_t *input_frame,
                            tdvp_ai_result_t *out_result);

tdvp_status_t tdvp_ai_result_release(tdvp_ai_context_t *context,
                                     tdvp_ai_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
```

AI rules:

- Model file format is selected by platform configuration and backend.
- Applications may request a backend, but `TDVP_AI_BACKEND_AUTO` is the default.
- Backend-specific handles must not appear in public application code.
- AI input conversion belongs in runtime preprocess stages, not in apps.

## 10. Vision Pipeline API

The pipeline API lets applications configure a standard camera-to-display
vision loop without owning hardware details.

```c
#ifndef TDVP_PIPELINE_H
#define TDVP_PIPELINE_H

#include "tdvp/common.h"
#include "tdvp/camera.h"
#include "tdvp/display.h"
#include "tdvp/input.h"
#include "tdvp/ai.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tdvp_pipeline tdvp_pipeline_t;

typedef enum tdvp_pipeline_stage {
    TDVP_PIPELINE_STAGE_CAPTURE = 0,
    TDVP_PIPELINE_STAGE_PREPROCESS,
    TDVP_PIPELINE_STAGE_INFER,
    TDVP_PIPELINE_STAGE_POSTPROCESS,
    TDVP_PIPELINE_STAGE_OVERLAY,
    TDVP_PIPELINE_STAGE_PRESENT
} tdvp_pipeline_stage_t;

typedef struct tdvp_pipeline_context {
    uint32_t size;
    tdvp_frame_t *frame;
    tdvp_ai_result_t *ai_result;
    void *user_data;
    uint32_t reserved[8];
} tdvp_pipeline_context_t;

typedef tdvp_status_t (*tdvp_pipeline_callback_t)(
    tdvp_pipeline_context_t *context);

typedef struct tdvp_pipeline_config {
    uint32_t size;
    tdvp_camera_config_t camera;
    tdvp_display_config_t display;
    const char *model_path;
    tdvp_ai_backend_t ai_backend;
    bool enable_display;
    bool enable_ai;
    void *user_data;
    uint32_t reserved[8];
} tdvp_pipeline_config_t;

tdvp_status_t tdvp_pipeline_create(const tdvp_pipeline_config_t *config,
                                   tdvp_pipeline_t **out_pipeline);

tdvp_status_t tdvp_pipeline_destroy(tdvp_pipeline_t *pipeline);

tdvp_status_t tdvp_pipeline_set_callback(
    tdvp_pipeline_t *pipeline,
    tdvp_pipeline_stage_t stage,
    tdvp_pipeline_callback_t callback);

tdvp_status_t tdvp_pipeline_start(tdvp_pipeline_t *pipeline);

tdvp_status_t tdvp_pipeline_stop(tdvp_pipeline_t *pipeline);

tdvp_status_t tdvp_pipeline_step(tdvp_pipeline_t *pipeline,
                                 uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
```

Pipeline rules:

- `tdvp_pipeline_step()` runs one deterministic iteration.
- Long-running applications should use the runtime event loop.
- Callbacks must not call Linux device APIs.
- A callback may read or annotate frame data, but must respect buffer ownership.

## 11. Runtime Event Loop API

The runtime API owns the public execution loop.

```c
#ifndef TDVP_RUNTIME_H
#define TDVP_RUNTIME_H

#include "tdvp/common.h"
#include "tdvp/pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tdvp_runtime tdvp_runtime_t;

typedef enum tdvp_runtime_event_type {
    TDVP_RUNTIME_EVENT_FRAME_READY = 0,
    TDVP_RUNTIME_EVENT_AI_RESULT,
    TDVP_RUNTIME_EVENT_INPUT,
    TDVP_RUNTIME_EVENT_ERROR,
    TDVP_RUNTIME_EVENT_STOPPED
} tdvp_runtime_event_type_t;

typedef struct tdvp_runtime_event {
    uint32_t size;
    tdvp_runtime_event_type_t type;
    tdvp_frame_t *frame;
    tdvp_ai_result_t *ai_result;
    tdvp_input_event_t *input;
    tdvp_status_t status;
    void *user_data;
    uint32_t reserved[8];
} tdvp_runtime_event_t;

typedef void (*tdvp_runtime_event_callback_t)(
    const tdvp_runtime_event_t *event);

typedef struct tdvp_runtime_config {
    uint32_t size;
    tdvp_pipeline_config_t pipeline;
    tdvp_runtime_event_callback_t event_callback;
    void *user_data;
    uint32_t reserved[8];
} tdvp_runtime_config_t;

tdvp_status_t tdvp_runtime_create(const tdvp_runtime_config_t *config,
                                  tdvp_runtime_t **out_runtime);

tdvp_status_t tdvp_runtime_destroy(tdvp_runtime_t *runtime);

tdvp_status_t tdvp_runtime_run(tdvp_runtime_t *runtime);

tdvp_status_t tdvp_runtime_request_stop(tdvp_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif
```

Runtime rules:

- Application callbacks are serialized unless documented otherwise.
- `tdvp_runtime_request_stop()` must be safe from a callback.
- Runtime owns camera/display/input polling cadence.
- Runtime must release all owned buffers during shutdown.

## 12. Minimal Application Example

```c
#include "tdvp/runtime.h"

static void on_event(const tdvp_runtime_event_t *event)
{
    if (event->type == TDVP_RUNTIME_EVENT_AI_RESULT) {
        /* Read normalized AI result tensors here. */
    }

    if (event->type == TDVP_RUNTIME_EVENT_ERROR) {
        /* Log or request stop from application policy. */
    }
}

int main(void)
{
    tdvp_runtime_config_t config = {0};
    config.size = sizeof(config);
    config.pipeline.camera.size = sizeof(config.pipeline.camera);
    config.pipeline.camera.camera_id = TDVP_CAMERA_DEFAULT;
    config.pipeline.camera.frame_size.width = 640;
    config.pipeline.camera.frame_size.height = 480;
    config.pipeline.camera.pixel_format = TDVP_PIXFMT_RGB565;
    config.pipeline.camera.fps = 30;
    config.pipeline.camera.buffer_count = 4;

    config.pipeline.display.size = sizeof(config.pipeline.display);
    config.pipeline.display.display_id = TDVP_DISPLAY_DEFAULT;
    config.pipeline.display.output_size.width = 320;
    config.pipeline.display.output_size.height = 240;
    config.pipeline.display.pixel_format = TDVP_PIXFMT_RGB565;
    config.pipeline.display.present_mode = TDVP_PRESENT_VSYNC;

    config.pipeline.enable_display = true;
    config.pipeline.enable_ai = false;
    config.event_callback = on_event;

    tdvp_runtime_t *runtime = NULL;
    tdvp_status_t status = tdvp_runtime_create(&config, &runtime);
    if (status != TDVP_OK) {
        return 1;
    }

    status = tdvp_runtime_run(runtime);
    tdvp_runtime_destroy(runtime);

    return status == TDVP_OK ? 0 : 1;
}
```

## 13. Strict Abstraction Checklist

A change is API-compatible only if all answers are yes:

- Can a developer use the camera without knowing `/dev/video*`?
- Can a developer render without knowing DRM connectors or planes?
- Can a developer read input without knowing evdev key codes?
- Can a developer run inference without knowing accelerator-specific handles?
- Can a developer build an app without changing Buildroot config?
- Can the runtime change internal buffer strategy without changing app code?
- Can the BSP change a kernel driver without changing app code?

If any answer is no, the design has leaked Linux internals or implementation
detail into the platform API.
