/* SPDX-License-Identifier: MIT */
#ifndef TDVP_CPU1_CAPTURE_H
#define TDVP_CPU1_CAPTURE_H

#include <stdint.h>

#define TDVP_CAPTURE_WIDTH 1920U
#define TDVP_CAPTURE_HEIGHT 1080U
#define TDVP_CAPTURE_FRAME_BYTES (TDVP_CAPTURE_WIDTH * TDVP_CAPTURE_HEIGHT * 3U / 2U)

/* One owner, in the RT-Smart userspace service. Never share this state with
 * Linux. A failed partial start/stop is terminal until the firmware reboots.
 */
struct tdvp_cpu1_capture {
    int attempted;
    int vb_ready;
    int vicap_attempted;
    int stream_attempted;
    int running;
    int fault;
};

struct tdvp_cpu1_frame {
    const uint8_t *plane[2];
    uint32_t stride[2];
    uint32_t width;
    uint32_t height;
    uint64_t pts;
};

/* The visitor may copy/preprocess the frame, but must not retain pointers.
 * It runs on CPU1, must be bounded, and must not wait for a Linux consumer.
 * The capture buffer is released on every path after a successful dump.
 */
typedef int (*tdvp_cpu1_frame_visitor)(const struct tdvp_cpu1_frame *, void *);

int tdvp_cpu1_capture_start(struct tdvp_cpu1_capture *capture);
int tdvp_cpu1_capture_next(struct tdvp_cpu1_capture *capture,
                           tdvp_cpu1_frame_visitor visit, void *context);
int tdvp_cpu1_capture_stop(struct tdvp_cpu1_capture *capture);

#endif
