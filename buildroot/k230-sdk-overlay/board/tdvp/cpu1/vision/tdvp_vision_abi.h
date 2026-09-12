/* SPDX-License-Identifier: MIT */
#ifndef TDVP_VISION_ABI_H
#define TDVP_VISION_ABI_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef __u32 tdvp_v_u32;
typedef __s32 tdvp_v_s32;
typedef __u64 tdvp_v_u64;
#else
#include <stdint.h>
typedef uint32_t tdvp_v_u32;
typedef int32_t tdvp_v_s32;
typedef uint64_t tdvp_v_u64;
#endif

#define TDVP_VISION_MAGIC 0x31535654U /* TVS1, little endian */
#define TDVP_VISION_ABI_VERSION 1U
#define TDVP_VISION_SLOT_COUNT 3U
#define TDVP_VISION_SLOT_BYTES 0x00800000U
#define TDVP_VISION_NV12 0x3231564eU
#define TDVP_VISION_CMD_STOP 0U
#define TDVP_VISION_CMD_RUN 1U
#define TDVP_VISION_STATE_IDLE 1U
#define TDVP_VISION_STATE_STARTING 2U
#define TDVP_VISION_STATE_RUNNING 3U
#define TDVP_VISION_STATE_STOPPING 4U
#define TDVP_VISION_STATE_FAULT 5U

/* Little-endian, naturally aligned 64-bit accesses on both RV64 cores.
 * Both sides map this transport NONCACHED and use full I/O fences. Never
 * map the MMZ or RT-Smart heap through this ABI. No pointers on the wire.
 * CPU1 and Linux write disjoint 128-byte regions; no cross-core RMW/locks.
 */
struct tdvp_vision_producer {
    tdvp_v_u32 magic;
    tdvp_v_u32 version;
    tdvp_v_u32 control_bytes;
    tdvp_v_u32 state;
    tdvp_v_u64 epoch;
    tdvp_v_u64 heartbeat;
    tdvp_v_u64 published;
    tdvp_v_u64 captured;
    tdvp_v_u64 dropped;
    tdvp_v_s32 fault;
    tdvp_v_u32 slot_count;
    tdvp_v_u32 slot_bytes;
    tdvp_v_u32 reserved[15];
};

struct tdvp_vision_consumer {
    tdvp_v_u64 epoch;
    tdvp_v_u64 released;
    tdvp_v_u64 heartbeat;
    tdvp_v_u32 command;
    tdvp_v_u32 reserved[25];
};

/* Published last via producer.published. Slot (sequence - 1) % 3 must not
 * be overwritten until Linux releases that sequence after finishing its
 * copy. No timeout may reclaim a leased slot. A stalled consumer drops NEW
 * frames, not old leased frames, so it never pins an ISP capture buffer.
 */
struct tdvp_vision_frame_header {
    tdvp_v_u64 sequence;
    tdvp_v_u64 pts; /* CPU1 monotonic dequeue time, us; NOT sensor exposure time. */
    tdvp_v_u32 width;
    tdvp_v_u32 height;
    tdvp_v_u32 stride;
    tdvp_v_u32 bytes;
    tdvp_v_u32 fourcc;
    tdvp_v_u32 reserved[7];
};

struct tdvp_vision_control {
    struct tdvp_vision_producer producer;
    struct tdvp_vision_consumer consumer;
    struct tdvp_vision_frame_header frame[TDVP_VISION_SLOT_COUNT];
};

_Static_assert(sizeof(struct tdvp_vision_producer) == 128, "producer ABI");
_Static_assert(sizeof(struct tdvp_vision_consumer) == 128, "consumer ABI");
_Static_assert(sizeof(struct tdvp_vision_frame_header) == 64, "frame ABI");
_Static_assert(sizeof(struct tdvp_vision_control) == 448, "control ABI");

#endif
