/* SPDX-License-Identifier: MIT */
/* Bounded CPU1 diagnostic, not a Linux executable or the final vision service.
 * Does not claim KPU inference or Linux asynchronous delivery. Run only after
 * the Linux ownership/reservation gate has approved this firmware.
 */
#include "tdvp_cpu1_capture.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int inspect_frame(const struct tdvp_cpu1_frame *frame, void *context)
{
    unsigned int *count = context;
    uint32_t checksum = 2166136261U;
    unsigned int plane, row, col;

    for (plane = 0; plane < 2; ++plane)
        for (row = 0; row < (frame->height >> plane); ++row)
            for (col = 0; col < frame->width; col += 64)
                checksum = (checksum ^ frame->plane[plane][row * frame->stride[plane] + col]) * 16777619U;
    ++*count;
    printf("TDVP CPU1 frame=%u size=%ux%u stride=%u/%u pts=%" PRIu64 " sample-hash=%08x\n",
           *count, frame->width, frame->height, frame->stride[0], frame->stride[1], frame->pts, checksum);
    return 0;
}

int main(int argc, char **argv)
{
    struct tdvp_cpu1_capture capture = {0};
    unsigned int frames = 0, attempts = 0;
    int result, stopped;

    if (argc != 2 || strcmp(argv[1], "--ownership-verified")) {
        fprintf(stderr, "CPU1-only probe requires a Linux-reserved vision MMZ and exclusive camera ownership\n");
        return 2;
    }
    result = tdvp_cpu1_capture_start(&capture);
    if (result)
        return 1;
    while (frames < 30 && attempts++ < 60 && !capture.fault) {
        result = tdvp_cpu1_capture_next(&capture, inspect_frame, &frames);
        if (result)
            fprintf(stderr, "TDVP CPU1 dump attempt=%u result=%d\n", attempts, result);
    }
    stopped = tdvp_cpu1_capture_stop(&capture);
    if (frames != 30 || capture.fault || stopped) {
        fprintf(stderr, "TDVP CPU1 capture probe FAIL frames=%u fault=%d stop=%d\n", frames, capture.fault, stopped);
        return 1;
    }
    puts("TDVP CPU1 capture probe PASS 30 frames released (no inference/transport test)");
    return 0;
}
