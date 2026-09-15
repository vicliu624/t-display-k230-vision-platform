/* SPDX-License-Identifier: MIT */
#include "tdvp_ai_job.h"
#include <errno.h>
#include <string.h>

static int valid(const struct tdvp_ai_job *job)
{
    return job && job->magic == TDVP_AI_JOB_MAGIC &&
           job->state >= TDVP_AI_JOB_READY && job->state <= TDVP_AI_JOB_POISONED;
}

static int poison(struct tdvp_ai_job *job, int error)
{
    if (job->state != TDVP_AI_JOB_POISONED) {
        job->error = error;
        job->output_bytes = 0;
        job->state = TDVP_AI_JOB_POISONED;
    }
    return job->error;
}

static int match(const struct tdvp_ai_job *job, const struct tdvp_ai_job_ticket *ticket)
{
    if (!valid(job) || !ticket) return -EINVAL;
    if (!ticket->id || !ticket->client_cookie ||
        ticket->owner_cookie != job->owner_cookie || ticket->peer_cookie != job->peer_cookie ||
        ticket->id != job->ticket.id || ticket->client_cookie != job->ticket.client_cookie)
        return -ESTALE;
    return 0;
}

int tdvp_ai_job_init(struct tdvp_ai_job *job, uint64_t owner_cookie,
                    uint64_t peer_cookie, uint32_t capabilities, uint64_t now)
{
    if (!job || !owner_cookie || !peer_cookie || (capabilities & ~TDVP_AI_JOB_KNOWN_CAPS))
        return -EINVAL;
    if (job->magic) return -EALREADY;
    memset(job, 0, sizeof(*job));
    job->magic = TDVP_AI_JOB_MAGIC;
    job->capabilities = capabilities;
    job->owner_cookie = owner_cookie;
    job->peer_cookie = peer_cookie;
    job->last_now = now;
    job->state = TDVP_AI_JOB_READY;
    return 0;
}

int tdvp_ai_job_tick(struct tdvp_ai_job *job, uint64_t now,
                    uint64_t owner_cookie, uint64_t peer_cookie, int ready)
{
    if (!valid(job)) return -EINVAL;
    if (job->state == TDVP_AI_JOB_POISONED) return job->error;
    if (ready != 1 || !owner_cookie || !peer_cookie ||
        job->owner_cookie != owner_cookie || job->peer_cookie != peer_cookie)
        return poison(job, -EPIPE);
    if (now < job->last_now) return poison(job, -EIO);
    job->last_now = now;
    if ((job->state == TDVP_AI_JOB_QUEUED || job->state == TDVP_AI_JOB_ACTIVE) &&
        now >= job->deadline) {
        if (job->state == TDVP_AI_JOB_ACTIVE) return poison(job, -ETIMEDOUT);
        job->error = -ETIMEDOUT;
        job->output_bytes = 0;
        job->state = TDVP_AI_JOB_RESULT; /* Never submitted to hardware. */
        return -ETIMEDOUT;
    }
    return 0;
}

int tdvp_ai_job_submit(struct tdvp_ai_job *job, uint64_t client_cookie,
                      const struct tdvp_ai_job_request *request, uint64_t now,
                      struct tdvp_ai_job_ticket *ticket)
{
    int result;
    if (!valid(job) || !request || !ticket || !client_cookie) return -EINVAL;
    if (job->state == TDVP_AI_JOB_POISONED) return job->error;
    if (job->state != TDVP_AI_JOB_READY) return -EBUSY;
    /* Validate before shifting: arbitrary opcodes must not cause undefined C. */
    if (request->operation < TDVP_AI_JOB_OP_AI2D || request->operation > TDVP_AI_JOB_OP_FFT ||
        !(job->capabilities & TDVP_AI_JOB_CAP(request->operation))) return -EOPNOTSUPP;
    if (!request->input_bytes || request->input_bytes > TDVP_AI_JOB_MAX_BYTES ||
        !request->output_capacity || request->output_capacity > TDVP_AI_JOB_MAX_BYTES ||
        !request->budget_ms || request->budget_ms > TDVP_AI_JOB_MAX_MS) return -EINVAL;
    result = tdvp_ai_job_tick(job, now, job->owner_cookie, job->peer_cookie, 1);
    if (result) return result;
    if (now > UINT64_MAX - request->budget_ms) return -EOVERFLOW;
    if (job->last_id == UINT64_MAX) return poison(job, -EOVERFLOW);
    job->request = *request;
    job->ticket.owner_cookie = job->owner_cookie;
    job->ticket.peer_cookie = job->peer_cookie;
    job->ticket.client_cookie = client_cookie;
    job->ticket.id = ++job->last_id;
    job->deadline = now + request->budget_ms;
    job->error = job->detached = 0;
    job->output_bytes = 0;
    job->state = TDVP_AI_JOB_QUEUED;
    *ticket = job->ticket;
    return 0;
}

int tdvp_ai_job_start(struct tdvp_ai_job *job,
                     const struct tdvp_ai_job_ticket *ticket, uint64_t now)
{
    int result = match(job, ticket);
    if (result) return result;
    result = tdvp_ai_job_tick(job, now, job->owner_cookie, job->peer_cookie, 1);
    if (result) return result;
    if (job->state != TDVP_AI_JOB_QUEUED) return -EBUSY;
    job->state = TDVP_AI_JOB_ACTIVE;
    return 0;
}

int tdvp_ai_job_complete(struct tdvp_ai_job *job,
                        const struct tdvp_ai_job_ticket *ticket, uint64_t now,
                        int quiescent, int error, uint32_t output_bytes)
{
    int result = match(job, ticket);
    if (result) return result;
    result = tdvp_ai_job_tick(job, now, job->owner_cookie, job->peer_cookie, 1);
    if (result) return result;
    if (job->state != TDVP_AI_JOB_ACTIVE) return -EBUSY;
    if (quiescent != 1) return poison(job, -EIO);
    if (error > 0 || error < -4095 || output_bytes > job->request.output_capacity ||
        (error && output_bytes)) return poison(job, -EPROTO);
    job->error = error;
    job->output_bytes = output_bytes;
    job->state = TDVP_AI_JOB_RESULT;
    return 0;
}

int tdvp_ai_job_detach(struct tdvp_ai_job *job,
                      const struct tdvp_ai_job_ticket *ticket)
{
    int result = match(job, ticket);
    if (result) return result;
    if (job->state == TDVP_AI_JOB_POISONED) return job->error;
    if (job->state == TDVP_AI_JOB_READY) return -ENOENT;
    job->detached = 1;
    if (job->state == TDVP_AI_JOB_QUEUED) {
        job->error = -ECANCELED;
        job->output_bytes = 0;
        job->state = TDVP_AI_JOB_RESULT;
    }
    return 0;
}

int tdvp_ai_job_ack(struct tdvp_ai_job *job,
                   const struct tdvp_ai_job_ticket *ticket)
{
    int result = match(job, ticket);
    if (result) return result;
    if (job->state == TDVP_AI_JOB_POISONED) return job->error;
    if (job->state != TDVP_AI_JOB_RESULT) return -EBUSY;
    memset(&job->request, 0, sizeof(job->request));
    memset(&job->ticket, 0, sizeof(job->ticket));
    job->error = job->detached = 0;
    job->output_bytes = 0;
    job->deadline = 0;
    job->state = TDVP_AI_JOB_READY;
    return 0;
}
