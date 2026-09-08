/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "tdvp_ai_abi.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void fail(const char *message)
{
    fprintf(stderr, "FAIL KPU reference: %s (%s)\n", message, strerror(errno));
    exit(1); /* Linux close detaches the job; it never resets/reclaims CPU1 DMA. */
}
static void wait_ready(int fd, short event)
{
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) fail("clock");
    for (;;) {
        if (clock_gettime(CLOCK_MONOTONIC, &now)) fail("clock");
        if (now.tv_sec < start.tv_sec || now.tv_sec - start.tv_sec >= 20) {
            errno = ETIMEDOUT; fail("bridge deadline");
        }
        struct pollfd p = {fd, event, 0};
        int result = poll(&p, 1, 1000);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) fail("bridge poll syscall");
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            /* poll revents does not set errno; do not print a stale errno from
             * the intentionally rejected NaN/Inf request. The exact CPU1
             * fault is exposed by /sys/class/misc/tdvp-ai/status. */
            errno = EIO; fail("bridge fault; inspect /sys/class/misc/tdvp-ai/status");
        }
        if (result == 1 && p.revents & event) return;
    }
}
static void read_fixture(const char *directory, unsigned int scenario, const char *kind,
                         unsigned int index, void *buffer, size_t bytes)
{
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/case%u.%s%u.f32", directory, scenario, kind, index);
    if (length < 0 || (size_t)length >= sizeof(path)) { errno = ENAMETOOLONG; fail("reference path"); }
    FILE *file = fopen(path, "rb");
    if (!file || fread(buffer, 1, bytes, file) != bytes || fgetc(file) != EOF || ferror(file))
        fail("exact reference length");
    if (fclose(file)) fail("reference close");
}
static float compare_output(const unsigned char *actual, const unsigned char *reference, size_t bytes)
{
    float largest = 0;
    for (size_t i = 0; i < bytes; i += sizeof(float)) {
        float a, b;
        memcpy(&a, actual + i, sizeof(a)); memcpy(&b, reference + i, sizeof(b));
        float difference = fabsf(a - b), limit = 1e-5f + 1e-4f * fabsf(b);
        if (!isfinite(a) || !isfinite(b) || !isfinite(difference) || difference > limit) {
            fprintf(stderr, "element=%zu actual=%.9g expected=%.9g tolerance=%.9g\n", i / 4, a, b, limit);
            errno = EILSEQ; fail("numeric mismatch");
        }
        if (difference > largest) largest = difference;
    }
    return largest;
}
int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: %s <pinned-reference-directory> <new-output-directory> [rounds 1..64]\n", argv[0]);
        return 2;
    }
    unsigned long rounds = 1;
    if (argc == 4) {
        char *end; errno = 0; rounds = strtoul(argv[3], &end, 10);
        if (errno || !argv[3][0] || *end || rounds < 1 || rounds > 64) return 2;
    }
    unsigned char *inputs = malloc(4 * TDVP_AI_KWS_INPUT_BYTES), *references = malloc(4 * TDVP_AI_KWS_OUTPUT_BYTES);
    const size_t send_bytes = sizeof(struct tdvp_ai_request) + TDVP_AI_KWS_INPUT_BYTES;
    const size_t receive_bytes = sizeof(struct tdvp_ai_response) + TDVP_AI_KWS_OUTPUT_BYTES;
    unsigned char *send = malloc(send_bytes), *receive = malloc(receive_bytes);
    if (!inputs || !references || !send || !receive) fail("allocation");
    for (unsigned int scenario = 0; scenario < 4; ++scenario) {
        unsigned char *in = inputs + scenario * TDVP_AI_KWS_INPUT_BYTES;
        unsigned char *out = references + scenario * TDVP_AI_KWS_OUTPUT_BYTES;
        read_fixture(argv[1], scenario, "input", 0, in, 4800);
        read_fixture(argv[1], scenario, "input", 1, in + 4800, 107520);
        read_fixture(argv[1], scenario, "output", 0, out, 240);
        read_fixture(argv[1], scenario, "output", 1, out + 240, 107520);
        if (!tdvp_ai_kws_input_valid(in, TDVP_AI_KWS_INPUT_BYTES)) { errno = EILSEQ; fail("input reference"); }
        (void)compare_output(out, out, TDVP_AI_KWS_OUTPUT_BYTES); /* reject nonfinite references before hardware */
    }
    if (mkdir(argv[2], 0700)) fail("new output directory required");
    int fd = open("/dev/tdvp-ai", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) fail("open bridge");
    unsigned long long previous = 0, hardware = 0;
    for (unsigned long round = 0; round < rounds; ++round) {
        for (unsigned int scenario = 0; scenario < 4; ++scenario) {
            struct tdvp_ai_request *r = (void *)send;
            memset(r, 0, sizeof(*r));
            r->magic = TDVP_AI_MAGIC; r->version = TDVP_AI_VERSION; r->bytes = sizeof(*r);
            r->operation = TDVP_AI_KPU; r->format = TDVP_AI_KWS_F32; r->budget_ms = 10000;
            r->input_width = 40; r->input_height = r->output_height = 30; r->output_width = 2;
            r->input_bytes = TDVP_AI_KWS_INPUT_BYTES; r->output_capacity = TDVP_AI_KWS_OUTPUT_BYTES;
            memcpy(send + sizeof(*r), inputs + scenario * TDVP_AI_KWS_INPUT_BYTES, r->input_bytes);
            wait_ready(fd, POLLOUT);
            if (!round && !scenario) {
                unsigned char *p = send + sizeof(*r); unsigned char saved[4]; memcpy(saved, p, 4);
                p[0] = 0; p[1] = 0; p[2] = 0x80; p[3] = 0x7f;
                if (write(fd, send, send_bytes) != -1 || errno != EILSEQ) fail("NaN/Inf must be rejected before submission");
                memcpy(p, saved, 4);
            }
            if (write(fd, send, send_bytes) != (ssize_t)send_bytes) fail("submit");
            wait_ready(fd, POLLIN);
            ssize_t bytes = read(fd, receive, receive_bytes);
            struct tdvp_ai_response *response = (void *)receive;
            if (bytes != (ssize_t)receive_bytes || response->result ||
                response->operation != r->operation || response->format != r->format ||
                response->output_width != 2 || response->output_height != 30 ||
                response->output_bytes != TDVP_AI_KWS_OUTPUT_BYTES ||
                !response->owner_cookie || !response->peer_cookie || !response->client_cookie ||
                response->id <= previous || response->duration_ms > r->budget_ms ||
                !tdvp_ai_response_hardware_valid(response)) { errno = EPROTO; fail("response and KPU counters"); }
            for (size_t i = 0; i < sizeof(response->reserved) / sizeof(response->reserved[0]); ++i)
                if (response->reserved[i]) { errno = EPROTO; fail("reserved response field"); }
            previous = response->id; hardware += response->hardware_completions;
            const unsigned char *actual = receive + sizeof(*response);
            float error = compare_output(actual, references + scenario * TDVP_AI_KWS_OUTPUT_BYTES, TDVP_AI_KWS_OUTPUT_BYTES);
            char path[1024];
            int length = snprintf(path, sizeof(path), "%s/round%lu.case%u.f32", argv[2], round, scenario);
            if (length < 0 || (size_t)length >= sizeof(path)) { errno = ENAMETOOLONG; fail("output path"); }
            FILE *file = fopen(path, "wbx");
            if (!file || fwrite(actual, 1, TDVP_AI_KWS_OUTPUT_BYTES, file) != TDVP_AI_KWS_OUTPUT_BYTES || fclose(file))
                fail("save actual tensors");
            printf("PASS round=%lu case=%u id=%llu KPU_starts=%u KPU_completions=%u duration_ms=%llu max_abs_error=%.9g\n",
                round, scenario, (unsigned long long)response->id, response->hardware_starts,
                response->hardware_completions, (unsigned long long)response->duration_ms, error);
            fflush(stdout);
        }
    }
    if (close(fd)) fail("close bridge");
    free(inputs); free(references); free(send); free(receive);
    printf("PASS CPU1 KPU: %lu KWS reference cases, %llu hardware completions; not ASR/STT acceptance\n", rounds * 4, hardware);
    return 0;
}
