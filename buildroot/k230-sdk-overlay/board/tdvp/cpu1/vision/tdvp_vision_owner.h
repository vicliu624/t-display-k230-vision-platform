/* SPDX-License-Identifier: MIT */
#ifndef TDVP_VISION_OWNER_H
#define TDVP_VISION_OWNER_H
#include "tdvp_vision_abi.h"
#include "tdvp_cpu1_vision_layout.h"

/* Separate from frame leases and the user-writable legacy ping mailbox.
 * Linux must reserve BOTH MMZ and transport, verify the paired ownership
 * policy, and retain shared suppliers BEFORE publishing OFFER. CPU1 reads
 * only until it observes an advancing offer. This is a boot/liveness contract,
 * not authentication against a malicious kernel or arbitrary physical writes.
 */
#define TDVP_OWNER_BASE (TDVP_VISION_CONTROL_BASE + 0x1000UL)
#define TDVP_OWNER_WINDOW 0x1000UL
#define TDVP_OWNER_MAGIC 0x314f5654U /* TVO1 */
#define TDVP_OWNER_VERSION 1U
#define TDVP_OWNER_CONTRACT 1U /* paired GC2093/AI ownership, NOT model readiness */
#define TDVP_OWNER_BOOT_MS 60000U
#define TDVP_OWNER_PEER_MS 5000U

#define TDVP_OWNER_WAIT 0U
#define TDVP_OWNER_OFFER 1U
#define TDVP_OWNER_GRANT 2U
#define TDVP_OWNER_HELLO 3U
#define TDVP_OWNER_STARTING 4U
#define TDVP_OWNER_READY 5U /* selected initialization complete, NOT frame/model acceptance */
#define TDVP_OWNER_FAULT 6U
#define TDVP_OWNER_ERR_PROTOCOL 1U
#define TDVP_OWNER_ERR_TIMEOUT 2U
#define TDVP_OWNER_ERR_CLOCK 3U
#define TDVP_OWNER_ERR_INIT 4U
#define TDVP_OWNER_ERR_OVERFLOW 5U

struct tdvp_owner_record {
    tdvp_v_u32 magic, version, bytes, contract;
    tdvp_v_u64 cookie, peer_cookie, heartbeat;
    tdvp_v_u32 state, fault, sequence;
    tdvp_v_u32 reserved[19];
};
struct tdvp_owner_control {
    struct tdvp_owner_record linux_side;
    struct tdvp_owner_record cpu1_side;
};
struct tdvp_owner_session {
    struct tdvp_owner_record own;
    tdvp_v_u64 observed_cookie, observed_heartbeat;
    tdvp_v_u64 started, phase_started, peer_seen, last_now;
    unsigned int publish, peer_ready;
};

_Static_assert(sizeof(struct tdvp_owner_record) == 128, "ownership record ABI");
_Static_assert(sizeof(struct tdvp_owner_control) == 256, "ownership control ABI");
_Static_assert(TDVP_OWNER_BASE + TDVP_OWNER_WINDOW <=
               TDVP_VISION_CONTROL_BASE + TDVP_VISION_CONTROL_SIZE, "ownership window");
_Static_assert(sizeof(struct tdvp_vision_control) <= 0x1000, "frame/ownership overlap");

void tdvp_owner_cpu1_init(struct tdvp_owner_session *, tdvp_v_u64 cookie, tdvp_v_u64 now);
void tdvp_owner_linux_init(struct tdvp_owner_session *, tdvp_v_u64 cookie, tdvp_v_u64 now);
/* Stable snapshot or NULL if a writer was in flight. Returns 1 exactly once
 * when CPU1 may start initialization, 0 while waiting/running, -1 on fault.
 * Faults latch. Neither side may recycle resources or regrant in place.
 */
int tdvp_owner_cpu1_step(struct tdvp_owner_session *, const struct tdvp_owner_record *, tdvp_v_u64 now);
int tdvp_owner_linux_step(struct tdvp_owner_session *, const struct tdvp_owner_record *, tdvp_v_u64 now);
void tdvp_owner_fail(struct tdvp_owner_session *, tdvp_v_u32 fault);
int tdvp_owner_cpu1_ready(struct tdvp_owner_session *);
#endif
