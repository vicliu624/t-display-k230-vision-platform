/* SPDX-License-Identifier: MIT */
/* RT-Smart user process: all ISP/MPI work stays on CPU1. Linux requests a
 * stream and consumes copies; no VO, display binding or Linux camera owner.
 * This initial worker transports frames; KPU model execution is a separate
 * integration gate, not implied by RUNNING or a successful frame capture.
 */
#define _POSIX_C_SOURCE 200809L
#include "tdvp_cpu1_transport.h"
#include "tdvp_cpu1_vision_layout.h"
#include "mpi_sys_api.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#define PEER_TIMEOUT_MS 2000U
#define MAX_DUMP_FAILURES 10U

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void worker_fence(void)
{
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
}

static void idle_tick(void)
{
    const struct timespec interval = {.tv_sec = 0, .tv_nsec = 20000000};
    (void)nanosleep(&interval, NULL);
}

int main(void)
{
    struct tdvp_cpu1_capture capture = {0};
    struct tdvp_cpu1_transport transport;
    volatile struct tdvp_vision_control *control;
    void *slots;
    uint64_t epoch = monotonic_ms(), peer_heartbeat = 0, peer_seen = 0;
    unsigned int failures = 0;
    int terminal_fault = 0;

    if (!epoch) {
        fputs("TDVP CPU1 vision: monotonic clock unavailable\n", stderr);
        return 1;
    }
    control = kd_mpi_sys_mmap(TDVP_VISION_CONTROL_BASE, TDVP_VISION_CONTROL_SIZE);
    slots = kd_mpi_sys_mmap(TDVP_VISION_SHARED_BASE, TDVP_VISION_SLOT_COUNT * TDVP_VISION_SLOT_BYTES);
    if (!control || control == MAP_FAILED || !slots || slots == MAP_FAILED) {
        fputs("TDVP CPU1 vision: noncached transport mappings unavailable\n", stderr);
        return 1;
    }
    if (tdvp_cpu1_transport_init(&transport, control, slots, epoch))
        return 1;
    puts("TDVP CPU1 vision: ready; waiting for Linux stream request (frame transport, no model loaded)");

    for (;;) {
        uint64_t now = monotonic_ms(), heartbeat;
        int requested, result;

        control->producer.heartbeat++;
        worker_fence();
        heartbeat = control->consumer.heartbeat;
        requested = control->consumer.epoch == epoch &&
                    control->consumer.command == TDVP_VISION_CMD_RUN;
        worker_fence();
        if (requested && heartbeat && heartbeat != peer_heartbeat) {
            peer_heartbeat = heartbeat;
            peer_seen = now;
        }
        requested = requested && now && peer_seen && now >= peer_seen &&
                    now - peer_seen < PEER_TIMEOUT_MS;
        if (!now)
            terminal_fault = -EIO;

        if ((!requested || terminal_fault) && capture.running) {
            tdvp_cpu1_transport_state(&transport, TDVP_VISION_STATE_STOPPING, terminal_fault);
            result = tdvp_cpu1_capture_stop(&capture);
            if (result)
                terminal_fault = result;
            if (!terminal_fault)
                tdvp_cpu1_transport_state(&transport, TDVP_VISION_STATE_IDLE, 0);
        }
        if (terminal_fault) {
            /* Do not exit/restart an ISP process after failed teardown: a
             * surviving DMA could still reference its buffers. Latch status
             * and keep heartbeat alive so Linux distinguishes it from a hang.
             */
            tdvp_cpu1_transport_state(&transport, TDVP_VISION_STATE_FAULT, terminal_fault);
            idle_tick();
            continue;
        }
        if (!requested) {
            idle_tick();
            continue;
        }
        if (!capture.running) {
            /* Only successful complete teardown reaches this restart path. */
            memset(&capture, 0, sizeof(capture));
            tdvp_cpu1_transport_state(&transport, TDVP_VISION_STATE_STARTING, 0);
            result = tdvp_cpu1_capture_start(&capture);
            if (result) {
                terminal_fault = result;
                continue;
            }
            failures = 0;
            tdvp_cpu1_transport_state(&transport, TDVP_VISION_STATE_RUNNING, 0);
        }
        result = tdvp_cpu1_capture_next(&capture, tdvp_cpu1_transport_publish, &transport);
        if (capture.fault || result == -EPROTO || result == -EOVERFLOW) {
            terminal_fault = capture.fault ? capture.fault : result;
        } else if (!result || result == -EAGAIN || result == -ECANCELED) {
            failures = 0; /* pressure/STOP are not a failed camera */
        } else if (++failures >= MAX_DUMP_FAILURES) {
            terminal_fault = result;
        }
    }
}
