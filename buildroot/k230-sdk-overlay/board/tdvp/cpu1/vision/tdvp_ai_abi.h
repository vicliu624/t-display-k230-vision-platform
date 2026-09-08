/* SPDX-License-Identifier: MIT */
#ifndef TDVP_AI_ABI_H
#define TDVP_AI_ABI_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
typedef __u32 tdvp_ai_u32;
typedef __s32 tdvp_ai_s32;
typedef __u64 tdvp_ai_u64;
#else
#include <stdint.h>
#include <errno.h>
typedef uint32_t tdvp_ai_u32;
typedef int32_t tdvp_ai_s32;
typedef uint64_t tdvp_ai_u64;
#endif

#define TDVP_AI_MAGIC 0x31494154U /* TAI1 */
#define TDVP_AI_VERSION 1U
#define TDVP_AI_WINDOW 0x1000U
#define TDVP_AI_CONTROL_BASE 0x1dff2000UL
#define TDVP_AI_INPUT_BASE 0x1d800000UL
#define TDVP_AI_OUTPUT_BASE 0x1db00000UL
#define TDVP_AI_BUFFER_BYTES 0x00300000UL
#define TDVP_AI_AI2D 1U
#define TDVP_AI_CAP_AI2D (1U << TDVP_AI_AI2D)
#define TDVP_AI_CHW_U8 0x33574843U
#define TDVP_AI_STATE_IDLE 1U
#define TDVP_AI_RUNNING 2U
#define TDVP_AI_RESULT 3U
#define TDVP_AI_FAULT 4U

/* Little endian RV64. Both cores map NONCACHED, with full I/O fences.
 * Linux writes linux_side, request, input; CPU1 writes cpu1_side, response,
 * output. No cross-core RMW, user physical pointers or MMZ mappings.
 * submitted publishes request+input; completed publishes response+output.
 * Neither buffer can be reused until released acknowledges completed.
 */
struct tdvp_ai_linux_line {
    tdvp_ai_u32 magic, version, bytes, reserved0;
    tdvp_ai_u64 owner_cookie, peer_cookie, heartbeat, submitted, released;
    tdvp_ai_u32 reserved[18];
};
struct tdvp_ai_cpu1_line {
    tdvp_ai_u32 magic, version, bytes, state;
    tdvp_ai_u64 owner_cookie, peer_cookie, heartbeat, accepted, completed;
    tdvp_ai_s32 fault;
    tdvp_ai_u32 capabilities;
    tdvp_ai_u32 reserved[16];
};
/* Linux write(): this 128-byte header followed by packed NCHW U8 input.
 * User supplies zero for all four cookie/id fields; kernel fills them.
 * Dimensions are bounded independently on both sides before data access.
 * Crop happens before constant per-channel padding; no resize/normalization.
 */
struct tdvp_ai_request {
    tdvp_ai_u32 magic, version, bytes, operation;
    tdvp_ai_u64 owner_cookie, peer_cookie, client_cookie, id;
    tdvp_ai_u32 input_bytes, output_capacity, budget_ms, flags;
    tdvp_ai_u32 input_width, input_height, output_width, output_height;
    tdvp_ai_u32 crop_x, crop_y, crop_width, crop_height;
    tdvp_ai_u32 pad_left, pad_right, pad_top, pad_bottom;
    tdvp_ai_u32 pad_value[3], format;
};
/* Linux read(): one complete response + output bytes. Short buffers and
 * failed user copies do not release the result. Negative result has no data.
 */
struct tdvp_ai_response {
    tdvp_ai_u64 owner_cookie, peer_cookie, client_cookie, id;
    tdvp_ai_s32 result;
    tdvp_ai_u32 output_bytes, operation, output_width, output_height, format;
    tdvp_ai_u64 duration_ms;
    tdvp_ai_u32 reserved[16];
};
struct tdvp_ai_control {
    struct tdvp_ai_linux_line linux_side;
    struct tdvp_ai_cpu1_line cpu1_side;
    struct tdvp_ai_request request;
    struct tdvp_ai_response response;
};

static inline int tdvp_ai_validate_request(const struct tdvp_ai_request *r)
{
    tdvp_ai_u64 input, output;
    if (!r || r->magic != TDVP_AI_MAGIC || r->version != TDVP_AI_VERSION ||
        r->bytes != sizeof(*r) || r->flags || r->format != TDVP_AI_CHW_U8)
        return -EINVAL;
    if (r->operation != TDVP_AI_AI2D) return -EOPNOTSUPP;
    if (!r->budget_ms || r->budget_ms > 60000U || !r->input_width || !r->input_height ||
        r->input_width > 1024U || r->input_height > 1024U ||
        !r->output_width || !r->output_height || r->output_width > 1024U || r->output_height > 1024U ||
        !r->crop_width || !r->crop_height || r->crop_width > r->input_width ||
        r->crop_height > r->input_height || r->crop_x > r->input_width - r->crop_width ||
        r->crop_y > r->input_height - r->crop_height ||
        (tdvp_ai_u64)r->crop_width + r->pad_left + r->pad_right != r->output_width ||
        (tdvp_ai_u64)r->crop_height + r->pad_top + r->pad_bottom != r->output_height ||
        r->pad_value[0] > 255U || r->pad_value[1] > 255U || r->pad_value[2] > 255U)
        return -EINVAL;
    input = (tdvp_ai_u64)r->input_width * r->input_height * 3U;
    output = (tdvp_ai_u64)r->output_width * r->output_height * 3U;
    if (input != r->input_bytes || input > TDVP_AI_BUFFER_BYTES ||
        output > r->output_capacity || r->output_capacity > TDVP_AI_BUFFER_BYTES)
        return -EMSGSIZE;
    return 0;
}

#ifdef __cplusplus
#define TDVP_AI_ASSERT static_assert
#else
#define TDVP_AI_ASSERT _Static_assert
#endif
TDVP_AI_ASSERT(sizeof(struct tdvp_ai_linux_line) == 128, "AI Linux line ABI");
TDVP_AI_ASSERT(sizeof(struct tdvp_ai_cpu1_line) == 128, "AI CPU1 line ABI");
TDVP_AI_ASSERT(sizeof(struct tdvp_ai_request) == 128, "AI request ABI");
TDVP_AI_ASSERT(sizeof(struct tdvp_ai_response) == 128, "AI response ABI");
TDVP_AI_ASSERT(sizeof(struct tdvp_ai_control) == 512, "AI control ABI");
TDVP_AI_ASSERT(TDVP_AI_INPUT_BASE + TDVP_AI_BUFFER_BYTES == TDVP_AI_OUTPUT_BASE, "AI buffer separation");
TDVP_AI_ASSERT(TDVP_AI_OUTPUT_BASE + TDVP_AI_BUFFER_BYTES <= 0x1dff0000UL, "AI buffers/control separation");
#undef TDVP_AI_ASSERT
#endif
