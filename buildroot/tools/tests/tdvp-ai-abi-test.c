/* SPDX-License-Identifier: MIT */
#include "tdvp_ai_abi.h"
#include "tdvp_cpu1_ai_guard.h" /* public protocol must not collide with guard */
#include "tdvp_vision_abi.h"
#include "tdvp_vision_owner.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct tdvp_ai_request base = {0}, request;
    unsigned int cases = 0;
    _Static_assert(TDVP_VISION_SHARED_BASE + TDVP_VISION_SLOT_COUNT * TDVP_VISION_SLOT_BYTES == TDVP_AI_INPUT_BASE, "frame/AI boundary");
    _Static_assert(TDVP_OWNER_BASE + TDVP_OWNER_WINDOW == TDVP_AI_CONTROL_BASE, "owner/AI boundary");
    _Static_assert(TDVP_AI_CONTROL_BASE + TDVP_AI_WINDOW <= TDVP_VISION_CONTROL_BASE + TDVP_VISION_CONTROL_SIZE, "AI control extent");
    base.magic = TDVP_AI_MAGIC; base.version = TDVP_AI_VERSION; base.bytes = sizeof(base);
    base.operation = TDVP_AI_AI2D; base.format = TDVP_AI_CHW_U8; base.budget_ms = 10000;
    base.input_width = base.input_height = base.output_width = base.output_height = 16;
    base.crop_width = base.crop_height = 16; base.input_bytes = base.output_capacity = 768;
    assert(tdvp_ai_validate_request(&base) == 0); ++cases;
    request = base; request.crop_x = request.crop_y = 4;
    request.crop_width = request.crop_height = request.output_width = request.output_height = 8;
    request.output_capacity = 192;
    assert(tdvp_ai_validate_request(&request) == 0); ++cases;
    request.pad_left = request.pad_right = 4; request.pad_top = request.pad_bottom = 2;
    request.output_width = 16; request.output_height = 12; request.output_capacity = 576;
    request.pad_value[0] = 17; request.pad_value[1] = 37; request.pad_value[2] = 255;
    assert(tdvp_ai_validate_request(&request) == 0); ++cases;
    request = base;
    request.input_width = request.input_height = request.output_width = request.output_height = 1024;
    request.crop_width = request.crop_height = 1024;
    request.input_bytes = request.output_capacity = TDVP_AI_BUFFER_BYTES;
    assert(tdvp_ai_validate_request(&request) == 0); ++cases;
    for (unsigned int failure = 0; failure < 28; ++failure) {
        request = base;
        switch (failure) {
        case 0: request.magic = 0; break;
        case 1: request.version = 2; break;
        case 2: --request.bytes; break;
        case 3: request.flags = 1; break;
        case 4: request.format = 0; break;
        case 5: request.operation = 2; break; /* KPU not implemented here. */
        case 6: request.operation = 3; break; /* FFT not implemented here. */
        case 7: request.operation = UINT32_MAX; break;
        case 8: request.budget_ms = 0; break;
        case 9: request.budget_ms = 60001; break;
        case 10: request.input_width = 0; break;
        case 11: request.input_height = 1025; break;
        case 12: request.input_width = UINT32_MAX; break;
        case 13: request.output_width = 0; break;
        case 14: request.output_height = UINT32_MAX; break;
        case 15: request.crop_width = 0; break;
        case 16: request.crop_height = 17; break;
        case 17: request.crop_x = UINT32_MAX; break;
        case 18: request.crop_y = 1; break;
        case 19: request.pad_left = UINT32_MAX; break;
        case 20: request.pad_right = UINT32_MAX; request.pad_left = 1; break;
        case 21: request.pad_bottom = UINT32_MAX; request.pad_top = 1; break;
        case 22: request.pad_value[0] = 256; break;
        case 23: request.pad_value[1] = UINT32_MAX; break;
        case 24: request.pad_value[2] = 256; break;
        case 25: ++request.input_bytes; break;
        case 26: --request.output_capacity; break;
        case 27: request.output_capacity = TDVP_AI_BUFFER_BYTES + 1; break;
        }
        assert(tdvp_ai_validate_request(&request) < 0); ++cases;
    }
    assert(tdvp_ai_validate_request(NULL) == -EINVAL); ++cases;
    printf("CPU1 AI request ABI: PASS %u cases, frame/owner separation and overflow refusals\n", cases);
    return 0;
}
