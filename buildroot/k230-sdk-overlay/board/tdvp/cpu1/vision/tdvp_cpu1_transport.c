/* SPDX-License-Identifier: MIT */
#include "tdvp_cpu1_transport.h"
#include "tdvp_cpu1_vision_layout.h"
#include <errno.h>
#include <string.h>

_Static_assert(TDVP_VISION_SHARED_BASE + TDVP_VISION_SLOT_COUNT * TDVP_VISION_SLOT_BYTES <=
               TDVP_VISION_CONTROL_BASE, "frames overlap transport control");
_Static_assert(TDVP_CAPTURE_FRAME_BYTES <= TDVP_VISION_SLOT_BYTES, "frame exceeds slot");

static void transport_fence(void)
{
#ifdef __riscv
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

void tdvp_cpu1_transport_state(struct tdvp_cpu1_transport *transport, uint32_t state, int fault)
{
    transport->control->producer.fault = fault;
    transport_fence();
    transport->control->producer.state = state;
    transport_fence();
}

int tdvp_cpu1_transport_init(struct tdvp_cpu1_transport *transport,
                             volatile struct tdvp_vision_control *control,
                             void *slots, uint64_t epoch)
{
    if (!transport || !control || !slots || !epoch ||
        (uintptr_t)control % 128U || (uintptr_t)slots % 64U)
        return -EINVAL;
    memset(transport, 0, sizeof(*transport));
    transport->control = control;
    transport->slots = slots;
    transport->epoch = epoch;
    /* Do not clear Linux-owned bytes, even during firmware initialization. */
    control->producer.magic = 0;
    transport_fence();
    memset((void *)&control->producer, 0, sizeof(control->producer));
    memset((void *)control->frame, 0, sizeof(control->frame));
    control->producer.epoch = epoch;
    control->producer.version = TDVP_VISION_ABI_VERSION;
    control->producer.control_bytes = sizeof(*control);
    control->producer.slot_count = TDVP_VISION_SLOT_COUNT;
    control->producer.slot_bytes = TDVP_VISION_SLOT_BYTES;
    control->producer.state = TDVP_VISION_STATE_IDLE;
    transport_fence();
    control->producer.magic = TDVP_VISION_MAGIC;
    transport_fence();
    return 0;
}

int tdvp_cpu1_transport_publish(const struct tdvp_cpu1_frame *frame, void *context)
{
    struct tdvp_cpu1_transport *transport = context;
    volatile struct tdvp_vision_control *control = transport->control;
    volatile struct tdvp_vision_frame_header *header;
    uint64_t released, sequence;
    uint8_t *destination;
    unsigned int plane, row;

    if (!frame || frame->width != TDVP_CAPTURE_WIDTH || frame->height != TDVP_CAPTURE_HEIGHT ||
        !frame->plane[0] || !frame->plane[1] || frame->stride[0] < frame->width ||
        frame->stride[1] < frame->width)
        return -EINVAL;
    control->producer.captured++;
    transport_fence();
    if (control->consumer.epoch != transport->epoch ||
        control->consumer.command != TDVP_VISION_CMD_RUN)
        return -ECANCELED;
    released = control->consumer.released;
    transport_fence();
    if (released < transport->released || released > transport->published)
        return -EPROTO;
    transport->released = released;
    if (transport->published == UINT64_MAX)
        return -EOVERFLOW; /* no sequence reuse within an epoch */
    if (transport->published - released >= TDVP_VISION_SLOT_COUNT) {
        control->producer.dropped++;
        transport_fence();
        return -EAGAIN;
    }
    sequence = transport->published + 1;
    header = &control->frame[(sequence - 1) % TDVP_VISION_SLOT_COUNT];
    destination = transport->slots + ((sequence - 1) % TDVP_VISION_SLOT_COUNT) * TDVP_VISION_SLOT_BYTES;
    for (plane = 0; plane < 2; ++plane) {
        for (row = 0; row < (frame->height >> plane); ++row) {
            memcpy(destination, frame->plane[plane] + (size_t)row * frame->stride[plane], frame->width);
            destination += frame->width;
        }
    }
    header->sequence = sequence;
    header->pts = frame->pts;
    header->width = frame->width;
    header->height = frame->height;
    header->stride = frame->width;
    header->bytes = TDVP_CAPTURE_FRAME_BYTES;
    header->fourcc = TDVP_VISION_NV12;
    transport_fence();
    control->producer.published = sequence;
    transport_fence();
    transport->published = sequence;
    return 0;
}
