/* SPDX-License-Identifier: MIT */
#ifndef TDVP_AI_JOB_H
#define TDVP_AI_JOB_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Supervisor-local state, NOT a shared-memory or userspace ABI. One serialized
 * supervisor owns all calls. Executors receive immutable ticket/request copies;
 * they never mutate this object. Zero-initialize once at a verified boot epoch.
 * Reinitializing a live/failed object is forbidden, including after client close.
 * No function here performs MMIO, DMA, memory allocation, freeing or resets.
 */
#define TDVP_AI_JOB_MAGIC 0x314a4154U
#define TDVP_AI_JOB_MAX_BYTES (3U * 1024U * 1024U)
#define TDVP_AI_JOB_MAX_MS 60000U
#define TDVP_AI_JOB_OP_AI2D 1U
#define TDVP_AI_JOB_OP_KPU 2U
#define TDVP_AI_JOB_OP_FFT 3U
#define TDVP_AI_JOB_CAP(op) (1U << (op))
#define TDVP_AI_JOB_KNOWN_CAPS (TDVP_AI_JOB_CAP(TDVP_AI_JOB_OP_AI2D) | \
    TDVP_AI_JOB_CAP(TDVP_AI_JOB_OP_KPU) | TDVP_AI_JOB_CAP(TDVP_AI_JOB_OP_FFT))

enum tdvp_ai_job_state {
    TDVP_AI_JOB_READY = 1, TDVP_AI_JOB_QUEUED, TDVP_AI_JOB_ACTIVE,
    TDVP_AI_JOB_RESULT, TDVP_AI_JOB_POISONED
};

struct tdvp_ai_job_request {
    uint32_t operation, input_bytes, output_capacity, budget_ms;
};
struct tdvp_ai_job_ticket {
    uint64_t owner_cookie, peer_cookie, client_cookie, id;
};
struct tdvp_ai_job {
    uint32_t magic, capabilities;
    enum tdvp_ai_job_state state;
    int error, detached;
    uint32_t output_bytes;
    uint64_t owner_cookie, peer_cookie, last_id, last_now, deadline;
    struct tdvp_ai_job_request request;
    struct tdvp_ai_job_ticket ticket;
};

/* capabilities means a wired, verified backend, not just device registration.
 * This core does not make KPU/AI2D/FFT available in the installed image. */
int tdvp_ai_job_init(struct tdvp_ai_job *job, uint64_t owner_cookie,
                    uint64_t peer_cookie, uint32_t capabilities, uint64_t now);
/* One outstanding job, including its unread result. Input bytes must be copied
 * to supervisor-owned storage before submit, and tensor/model/shape validation
 * belongs to the backend. No client pointers or mutable shared descriptors. */
int tdvp_ai_job_submit(struct tdvp_ai_job *job, uint64_t client_cookie,
                      const struct tdvp_ai_job_request *request, uint64_t now,
                      struct tdvp_ai_job_ticket *ticket);
/* Call tick independently of executor progress, after a stable ownership
 * snapshot/heartbeat check. Owner loss and clock rollback are terminal even
 * while idle. A queued timeout is safe to acknowledge; active timeout is not. */
int tdvp_ai_job_tick(struct tdvp_ai_job *job, uint64_t now,
                    uint64_t owner_cookie, uint64_t peer_cookie, int ready);
int tdvp_ai_job_start(struct tdvp_ai_job *job,
                     const struct tdvp_ai_job_ticket *ticket, uint64_t now);
/* quiescent must be proven by the backend; a poll timeout/error, thread exit,
 * client close or supervisor deadline is NEVER proof. A late completion cannot
 * unpoison the service or permit storage reuse. error is zero or negative errno;
 * errors publish no output bytes. Result storage remains leased until ack. */
int tdvp_ai_job_complete(struct tdvp_ai_job *job,
                        const struct tdvp_ai_job_ticket *ticket, uint64_t now,
                        int quiescent, int error, uint32_t output_bytes);
/* Called by the trusted adapter on close. Queue cancellation has no hardware
 * work. Active work continues under supervision, with storage retained. A new
 * client's open must not acknowledge an older client's outstanding ticket. */
int tdvp_ai_job_detach(struct tdvp_ai_job *job,
                      const struct tdvp_ai_job_ticket *ticket);
/* Only after copying the entire result, or explicitly discarding a detached,
 * quiescent result. Copy-to-user failure must leave RESULT and its lease intact.
 * Poisoned services have no acknowledge/reset/retry/reclaim API. */
int tdvp_ai_job_ack(struct tdvp_ai_job *job,
                   const struct tdvp_ai_job_ticket *ticket);
#ifdef __cplusplus
}
#endif
#endif
