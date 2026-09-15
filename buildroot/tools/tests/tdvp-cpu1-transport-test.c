/* SPDX-License-Identifier: MIT */
#include "tdvp_cpu1_transport.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static _Alignas(128) struct tdvp_vision_control control;
static _Alignas(64) unsigned char slots[TDVP_VISION_SLOT_COUNT * TDVP_VISION_SLOT_BYTES];
/* Padded source rows ensure the transport strips padding, not blindly memcpy. */
static unsigned char y[(TDVP_CAPTURE_WIDTH + 32) * TDVP_CAPTURE_HEIGHT];
static unsigned char uv[(TDVP_CAPTURE_WIDTH + 32) * TDVP_CAPTURE_HEIGHT / 2];

int main(void)
{
    struct tdvp_cpu1_transport transport;
    struct tdvp_cpu1_frame frame = {
        .plane = {y, uv}, .stride = {TDVP_CAPTURE_WIDTH + 32, TDVP_CAPTURE_WIDTH + 32},
        .width = TDVP_CAPTURE_WIDTH, .height = TDVP_CAPTURE_HEIGHT, .pts = 999
    };
    struct tdvp_vision_consumer preserved;
    unsigned int row, slot;

    memset(&control.consumer, 0x5a, sizeof(control.consumer));
    preserved = control.consumer;
    assert(tdvp_cpu1_transport_init(&transport, &control, slots, 0) == -EINVAL);
    assert(!tdvp_cpu1_transport_init(&transport, &control, slots, 123));
    assert(!memcmp(&preserved, &control.consumer, sizeof(preserved)));
    assert(control.producer.magic == TDVP_VISION_MAGIC);
    assert(control.producer.control_bytes == 448 && control.producer.slot_count == 3);
    assert(tdvp_cpu1_transport_publish(&frame, &transport) == -ECANCELED);
    memset(&control.consumer, 0, sizeof(control.consumer));
    control.consumer.epoch = 123;
    control.consumer.command = TDVP_VISION_CMD_RUN;
    memset(y, 0xaa, sizeof(y));
    memset(uv, 0xbb, sizeof(uv));
    for (row = 0; row < TDVP_CAPTURE_HEIGHT; ++row)
        memset(y + row * frame.stride[0], 0x11, TDVP_CAPTURE_WIDTH);
    for (row = 0; row < TDVP_CAPTURE_HEIGHT / 2; ++row)
        memset(uv + row * frame.stride[1], 0x22, TDVP_CAPTURE_WIDTH);
    memset(slots, 0xcc, sizeof(slots));
    for (slot = 0; slot < 3; ++slot) {
        unsigned char *data = slots + slot * TDVP_VISION_SLOT_BYTES;
        assert(!tdvp_cpu1_transport_publish(&frame, &transport));
        assert(control.producer.published == slot + 1);
        assert(control.frame[slot].sequence == slot + 1 && control.frame[slot].pts == 999);
        assert(control.frame[slot].bytes == TDVP_CAPTURE_FRAME_BYTES);
        for (row = 0; row < TDVP_CAPTURE_WIDTH * TDVP_CAPTURE_HEIGHT; ++row)
            assert(data[row] == 0x11);
        for (; row < TDVP_CAPTURE_FRAME_BYTES; ++row)
            assert(data[row] == 0x22);
        assert(data[row] == 0xcc); /* not outside valid payload */
    }
    y[0] = 0x33;
    assert(tdvp_cpu1_transport_publish(&frame, &transport) == -EAGAIN);
    assert(control.producer.published == 3 && control.producer.dropped == 1 && slots[0] == 0x11);
    control.consumer.released = 4;
    assert(tdvp_cpu1_transport_publish(&frame, &transport) == -EPROTO);
    assert(slots[0] == 0x11);
    control.consumer.released = 1;
    assert(!tdvp_cpu1_transport_publish(&frame, &transport));
    assert(control.producer.published == 4 && slots[0] == 0x33);
    assert(slots[TDVP_VISION_SLOT_BYTES] == 0x11); /* slot 2 still leased */
    control.consumer.released = 0;
    assert(tdvp_cpu1_transport_publish(&frame, &transport) == -EPROTO);
    control.consumer.released = 4;
    control.consumer.command = TDVP_VISION_CMD_STOP;
    assert(tdvp_cpu1_transport_publish(&frame, &transport) == -ECANCELED);
    assert(control.producer.published == 4);
    control.consumer.command = TDVP_VISION_CMD_RUN;
    control.consumer.epoch = 124;
    assert(tdvp_cpu1_transport_publish(&frame, &transport) == -ECANCELED);
    control.consumer.epoch = 123;
    frame.stride[0] = 1;
    assert(tdvp_cpu1_transport_publish(&frame, &transport) == -EINVAL);
    frame.stride[0] = TDVP_CAPTURE_WIDTH + 32;
    transport.published = transport.released = control.consumer.released = UINT64_MAX;
    assert(tdvp_cpu1_transport_publish(&frame, &transport) == -EOVERFLOW);
    tdvp_cpu1_transport_state(&transport, TDVP_VISION_STATE_FAULT, -EIO);
    assert(control.producer.state == TDVP_VISION_STATE_FAULT && control.producer.fault == -EIO);
    puts("CPU1 transport: PASS layout, row copy, ring pressure, leases, epochs and malformed acknowledgements");
    return 0;
}
