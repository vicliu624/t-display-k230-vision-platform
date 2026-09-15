/* SPDX-License-Identifier: MIT */
#include "tdvp_ai_job.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const struct tdvp_ai_job_request request = {TDVP_AI_JOB_OP_AI2D, 768, 192, 100};
static unsigned int cases;

static struct tdvp_ai_job fresh(void)
{
    struct tdvp_ai_job job = {0};
    assert(tdvp_ai_job_init(&job, 11, 22, TDVP_AI_JOB_CAP(TDVP_AI_JOB_OP_AI2D), 10) == 0);
    return job;
}

static struct tdvp_ai_job_ticket submit(struct tdvp_ai_job *job)
{
    struct tdvp_ai_job_ticket ticket;
    assert(tdvp_ai_job_submit(job, 33, &request, 20, &ticket) == 0);
    assert(job->state == TDVP_AI_JOB_QUEUED && job->deadline == 120);
    return ticket;
}

static void poisoned(struct tdvp_ai_job *job, const struct tdvp_ai_job_ticket *ticket, int error)
{
    struct tdvp_ai_job saved = *job;
    struct tdvp_ai_job_ticket ignored;
    assert(job->state == TDVP_AI_JOB_POISONED && job->error == error && !job->output_bytes);
    assert(tdvp_ai_job_complete(job, ticket, 121, 1, 0, 192) == error);
    assert(tdvp_ai_job_ack(job, ticket) == error);
    assert(tdvp_ai_job_detach(job, ticket) == error);
    assert(tdvp_ai_job_start(job, ticket, 121) == error);
    assert(tdvp_ai_job_submit(job, 44, &request, 122, &ignored) == error);
    assert(tdvp_ai_job_tick(job, 122, 55, 66, 0) == error); /* First error is sticky. */
    assert(tdvp_ai_job_init(job, 55, 66, TDVP_AI_JOB_KNOWN_CAPS, 122) == -EALREADY);
    assert(memcmp(&saved, job, sizeof(saved)) == 0); /* Never reclaim the live lease. */
}

int main(void)
{
    struct tdvp_ai_job job = fresh();
    struct tdvp_ai_job_ticket ticket = submit(&job), other, ignored;
    struct tdvp_ai_job_request invalid;
    assert(tdvp_ai_job_start(&job, &ticket, 21) == 0);
    assert(tdvp_ai_job_ack(&job, &ticket) == -EBUSY);
    assert(tdvp_ai_job_complete(&job, &ticket, 22, 1, 0, 192) == 0);
    assert(job.state == TDVP_AI_JOB_RESULT && job.output_bytes == 192 && !job.error);
    assert(tdvp_ai_job_submit(&job, 33, &request, 23, &ignored) == -EBUSY);
    /* A failed result copy performs no ack. The result stays leased. */
    assert(tdvp_ai_job_tick(&job, 200, 11, 22, 1) == 0 && job.output_bytes == 192);
    assert(tdvp_ai_job_ack(&job, &ticket) == 0);
    assert(tdvp_ai_job_ack(&job, &ticket) == -ESTALE);
    assert(tdvp_ai_job_submit(&job, 44, &request, 201, &other) == 0);
    assert(other.id == ticket.id + 1 && other.client_cookie == 44);
    assert(tdvp_ai_job_start(&job, &ticket, 202) == -ESTALE);
    cases++;

    job = fresh(); ticket = submit(&job);
    assert(tdvp_ai_job_detach(&job, &ticket) == 0);
    assert(job.state == TDVP_AI_JOB_RESULT && job.error == -ECANCELED && job.detached);
    assert(tdvp_ai_job_start(&job, &ticket, 21) == -EBUSY);
    assert(tdvp_ai_job_ack(&job, &ticket) == 0);
    cases++;

    job = fresh(); ticket = submit(&job);
    assert(tdvp_ai_job_start(&job, &ticket, 21) == 0);
    assert(tdvp_ai_job_detach(&job, &ticket) == 0 && job.state == TDVP_AI_JOB_ACTIVE);
    assert(tdvp_ai_job_ack(&job, &ticket) == -EBUSY);
    assert(tdvp_ai_job_submit(&job, 44, &request, 22, &ignored) == -EBUSY);
    assert(tdvp_ai_job_complete(&job, &ticket, 23, 1, 0, 192) == 0);
    other = ticket; other.client_cookie = 44;
    assert(tdvp_ai_job_ack(&job, &other) == -ESTALE);
    assert(tdvp_ai_job_ack(&job, &ticket) == 0); /* Trusted detached-result discard. */
    cases++;

    job = fresh(); ticket = submit(&job);
    assert(tdvp_ai_job_tick(&job, 119, 11, 22, 1) == 0);
    assert(tdvp_ai_job_start(&job, &ticket, 120) == -ETIMEDOUT);
    assert(job.state == TDVP_AI_JOB_RESULT && job.error == -ETIMEDOUT);
    assert(tdvp_ai_job_complete(&job, &ticket, 121, 1, 0, 0) == -EBUSY);
    assert(tdvp_ai_job_ack(&job, &ticket) == 0);
    cases++;

    job = fresh(); ticket = submit(&job);
    assert(tdvp_ai_job_start(&job, &ticket, 21) == 0);
    assert(tdvp_ai_job_tick(&job, 119, 11, 22, 1) == 0);
    assert(tdvp_ai_job_tick(&job, 120, 11, 22, 1) == -ETIMEDOUT);
    poisoned(&job, &ticket, -ETIMEDOUT); cases++;

    job = fresh(); ticket = submit(&job);
    assert(tdvp_ai_job_start(&job, &ticket, 21) == 0);
    assert(tdvp_ai_job_complete(&job, &ticket, 120, 1, 0, 192) == -ETIMEDOUT);
    poisoned(&job, &ticket, -ETIMEDOUT); cases++;

    for (unsigned int reason = 0; reason < 5; ++reason) {
        job = fresh(); ticket = submit(&job);
        assert(tdvp_ai_job_start(&job, &ticket, 21) == 0);
        int error = reason == 4 ? -EIO : -EPIPE;
        assert(tdvp_ai_job_tick(&job, reason == 4 ? 20 : 22,
            reason == 0 ? 12 : 11, reason == 1 ? 23 : 22,
            reason == 2 ? 0 : reason == 3 ? 2 : 1) == error);
        poisoned(&job, &ticket, error); cases++;
    }

    for (unsigned int reason = 0; reason < 6; ++reason) {
        job = fresh(); ticket = submit(&job);
        assert(tdvp_ai_job_start(&job, &ticket, 21) == 0);
        int error = reason < 2 ? -EIO : -EPROTO;
        assert(tdvp_ai_job_complete(&job, &ticket, 22, reason == 0 ? 0 : reason == 1 ? 2 : 1,
            reason == 2 ? 1 : reason == 3 ? -4096 : reason == 4 ? -EIO : 0,
            reason == 5 ? 193 : 192) == error);
        poisoned(&job, &ticket, error); cases++;
    }

    job = fresh(); ticket = submit(&job);
    assert(tdvp_ai_job_start(&job, &ticket, 21) == 0);
    assert(tdvp_ai_job_complete(&job, &ticket, 22, 1, -EINVAL, 0) == 0);
    assert(job.state == TDVP_AI_JOB_RESULT && job.error == -EINVAL);
    assert(tdvp_ai_job_ack(&job, &ticket) == 0); cases++;

    for (unsigned int field = 0; field < 4; ++field) {
        job = fresh(); ticket = submit(&job); other = ticket;
        if (field == 0) ++other.owner_cookie;
        if (field == 1) ++other.peer_cookie;
        if (field == 2) ++other.client_cookie;
        if (field == 3) ++other.id;
        struct tdvp_ai_job saved = job;
        assert(tdvp_ai_job_start(&job, &other, 21) == -ESTALE);
        assert(tdvp_ai_job_complete(&job, &other, 21, 1, 0, 192) == -ESTALE);
        assert(tdvp_ai_job_ack(&job, &other) == -ESTALE);
        assert(tdvp_ai_job_detach(&job, &other) == -ESTALE);
        assert(memcmp(&saved, &job, sizeof(job)) == 0); cases++;
    }

    for (unsigned int reason = 0; reason < 12; ++reason) {
        job = fresh(); invalid = request;
        if (reason == 0) invalid.operation = 0;
        if (reason == 1) invalid.operation = TDVP_AI_JOB_OP_KPU;
        if (reason == 2) invalid.operation = TDVP_AI_JOB_OP_FFT;
        if (reason == 3) invalid.operation = 32;
        if (reason == 4) invalid.operation = UINT32_MAX;
        if (reason == 5) invalid.input_bytes = 0;
        if (reason == 6) invalid.input_bytes = TDVP_AI_JOB_MAX_BYTES + 1;
        if (reason == 7) invalid.output_capacity = 0;
        if (reason == 8) invalid.output_capacity = TDVP_AI_JOB_MAX_BYTES + 1;
        if (reason == 9) invalid.budget_ms = 0;
        if (reason == 10) invalid.budget_ms = TDVP_AI_JOB_MAX_MS + 1;
        if (reason == 11) invalid.budget_ms = UINT32_MAX;
        struct tdvp_ai_job saved = job;
        assert(tdvp_ai_job_submit(&job, 33, &invalid, 20, &ignored) ==
               (reason < 5 ? -EOPNOTSUPP : -EINVAL));
        assert(memcmp(&saved, &job, sizeof(job)) == 0); cases++;
    }

    job = fresh();
    invalid = request; invalid.input_bytes = invalid.output_capacity = TDVP_AI_JOB_MAX_BYTES;
    invalid.budget_ms = TDVP_AI_JOB_MAX_MS;
    assert(tdvp_ai_job_submit(&job, 33, &invalid, 20, &ticket) == 0);
    assert(job.deadline == 60020); cases++;

    job = fresh();
    assert(tdvp_ai_job_submit(&job, 33, &request, UINT64_MAX - 50, &ignored) == -EOVERFLOW);
    assert(job.state == TDVP_AI_JOB_READY && !job.last_id); cases++;

    job = fresh(); job.last_id = UINT64_MAX - 1; ticket = submit(&job);
    assert(ticket.id == UINT64_MAX);
    assert(tdvp_ai_job_detach(&job, &ticket) == 0);
    assert(tdvp_ai_job_ack(&job, &ticket) == 0);
    assert(tdvp_ai_job_submit(&job, 33, &request, 21, &ignored) == -EOVERFLOW);
    assert(job.state == TDVP_AI_JOB_POISONED && job.last_id == UINT64_MAX); cases++;

    job = fresh(); ticket = submit(&job);
    assert(tdvp_ai_job_init(&job, 55, 66, 0, 21) == -EALREADY);
    assert(job.state == TDVP_AI_JOB_QUEUED && job.ticket.id == ticket.id); cases++;

    job = fresh();
    assert(tdvp_ai_job_tick(&job, 11, 12, 22, 1) == -EPIPE);
    assert(job.state == TDVP_AI_JOB_POISONED); cases++;

    memset(&job, 0, sizeof(job));
    assert(tdvp_ai_job_init(&job, 11, 22, 0, 0) == 0);
    assert(tdvp_ai_job_submit(&job, 33, &request, 1, &ignored) == -EOPNOTSUPP); cases++;

    memset(&job, 0, sizeof(job));
    assert(tdvp_ai_job_init(NULL, 11, 22, 0, 0) == -EINVAL);
    assert(tdvp_ai_job_init(&job, 0, 22, 0, 0) == -EINVAL);
    assert(tdvp_ai_job_init(&job, 11, 0, 0, 0) == -EINVAL);
    assert(tdvp_ai_job_init(&job, 11, 22, UINT32_MAX, 0) == -EINVAL);
    assert(tdvp_ai_job_tick(&job, 0, 11, 22, 1) == -EINVAL);
    assert(tdvp_ai_job_ack(NULL, NULL) == -EINVAL);
    job = fresh();
    assert(tdvp_ai_job_submit(&job, 0, &request, 20, &ignored) == -EINVAL);
    assert(tdvp_ai_job_submit(&job, 33, NULL, 20, &ignored) == -EINVAL);
    assert(tdvp_ai_job_submit(&job, 33, &request, 20, NULL) == -EINVAL); cases++;

    printf("CPU1 async AI job core: PASS %u cases; no hardware executed\n", cases);
    return 0;
}
