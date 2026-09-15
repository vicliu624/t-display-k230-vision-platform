/* SPDX-License-Identifier: MIT */
/* Manual Linux acceptance: real CPU1 FFT via write/poll/read, mixed with AI2D.
 * No model, SDMA, MMIO, physical address, reset or automatic menu/startup.
 * Closed-form vectors use one fixed LSB tolerance, including IFFT and full
 * stage scaling. This is not arbitrary-input accuracy or ASR acceptance. */
#define _GNU_SOURCE
#include "tdvp_ai_abi.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CAPACITY (sizeof(struct tdvp_ai_request) + 4096U * 4U)
static unsigned char *sent, *received;
static unsigned long long previous_id;
static unsigned int maximum_error;

static void die(const char *message) { perror(message); exit(1); }
static unsigned long long now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) die("clock");
    return (unsigned long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static void wait_for(int fd, short wanted)
{
    unsigned long long end = now_ms() + 15000;
    for (;;) {
        struct pollfd item = {fd, wanted, 0};
        int count = poll(&item, 1, 200);
        if (count < 0 && errno != EINTR) die("poll");
        if (item.revents & (POLLERR | POLLHUP | POLLNVAL)) { errno = EIO; die("CPU1 AI fault (no reset)"); }
        if (item.revents & wanted) return;
        if (now_ms() >= end) { errno = ETIMEDOUT; die("AI response deadline"); }
    }
}
static int open_service(void)
{
    unsigned long long end = now_ms() + 15000;
    for (;;) {
        int fd = open("/dev/tdvp-ai", O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) return fd;
        if ((errno != EAGAIN && errno != EBUSY) || now_ms() >= end) die("open AI service");
        const struct timespec delay = {0, 20000000}; nanosleep(&delay, NULL);
    }
}
static struct tdvp_ai_request *prepare(unsigned int n, unsigned int inverse, unsigned int scaled)
{
    memset(sent, 0, CAPACITY);
    struct tdvp_ai_request *r = (void *)sent;
    r->magic = TDVP_AI_MAGIC; r->version = TDVP_AI_VERSION; r->bytes = sizeof(*r);
    r->operation = TDVP_AI_FFT; r->format = TDVP_AI_COMPLEX_I16; r->budget_ms = 10000;
    r->input_width = r->output_width = n; r->input_height = r->output_height = 1;
    r->input_bytes = r->output_capacity = n * 4;
    r->flags = inverse | (scaled ? (n - 1U) << 8 : 0);
    return r;
}
static struct tdvp_ai_response *exchange(int fd)
{
    struct tdvp_ai_request *r = (void *)sent;
    size_t bytes = sizeof(*r) + r->input_bytes;
    wait_for(fd, POLLOUT);
    if (write(fd, sent, bytes) != (ssize_t)bytes) die("submit");
    if (write(fd, sent, bytes) != -1 || errno != EAGAIN) { errno = EPROTO; die("backpressure"); }
    wait_for(fd, POLLIN);
    ssize_t got = read(fd, received, CAPACITY);
    if (got < 0) die("read");
    struct tdvp_ai_response *result = (void *)received;
    if (got != (ssize_t)(sizeof(*result) + tdvp_ai_output_bytes(r)) || result->result ||
        !result->owner_cookie || !result->peer_cookie || !result->client_cookie || result->id <= previous_id ||
        result->operation != r->operation || result->format != r->format ||
        result->output_width != r->output_width || result->output_height != r->output_height ||
        result->output_bytes != tdvp_ai_output_bytes(r)) { errno = EPROTO; die("result metadata"); }
    previous_id = result->id;
    return result;
}
static void fft_case(int fd, unsigned int n, unsigned int inverse, unsigned int scaled, unsigned int vector)
{
    struct tdvp_ai_request *r = prepare(n, inverse, scaled);
    unsigned char *samples = sent + sizeof(*r);
    for (unsigned int i = 0; i < n; ++i) {
        int16_t real = vector == 1 ? 1 : (vector == 0 && i == 0) || (vector == 2 && i == n/4) ? (int)n : 0;
        int16_t imaginary = vector == 3 && i == n/4 ? (int)n : 0;
        memcpy(samples + 4*i, &real, 2); memcpy(samples + 4*i+2, &imaginary, 2);
    }
    struct tdvp_ai_response *result = exchange(fd);
    unsigned char *actual = received + sizeof(*result);
    int amplitude = scaled ? 1 : (int)n;
    for (unsigned int k = 0; k < n; ++k) {
        int er = 0, ei = 0;
        int16_t ar, ai;
        if (vector == 0) er = amplitude;
        else if (vector == 1) er = k == 0 ? amplitude : 0;
        else {
            int cosine = k % 4 == 0 ? 1 : k % 4 == 2 ? -1 : 0;
            int sine = k % 4 == 1 ? 1 : k % 4 == 3 ? -1 : 0;
            if (!inverse) sine = -sine;
            er = amplitude * (vector == 2 ? cosine : -sine);
            ei = amplitude * (vector == 2 ? sine : cosine);
        }
        memcpy(&ar, actual + 4*k, 2); memcpy(&ai, actual + 4*k+2, 2);
        unsigned int dr = abs((int)ar-er), di = abs((int)ai-ei);
        if (dr > maximum_error) maximum_error = dr;
        if (di > maximum_error) maximum_error = di;
        if (dr > 1 || di > 1) {
            fprintf(stderr, "FAIL FFT n=%u inverse=%u scaled=%u vector=%u bin=%u expected=%d,%d actual=%d,%d tolerance=1\n",
                n, inverse, scaled, vector, k, er, ei, ar, ai);
            exit(1);
        }
    }
    printf("PASS FFT job=%llu n=%u inverse=%u scaled=%u vector=%u cpu1_ms=%llu\n",
        (unsigned long long)result->id, n, inverse, scaled, vector, (unsigned long long)result->duration_ms);
    fflush(stdout);
}
static void ai2d_case(int fd, unsigned int round)
{
    struct tdvp_ai_request *r = prepare(64, 0, 0);
    r->operation = TDVP_AI_AI2D; r->format = TDVP_AI_CHW_U8;
    r->input_width = r->output_width = r->input_height = r->output_height = 16;
    r->crop_width = r->crop_height = 16; r->input_bytes = r->output_capacity = 768;
    for (unsigned int i = 0; i < 768; ++i) sent[sizeof(*r)+i] = (unsigned char)(i*7 + round*19);
    struct tdvp_ai_response *result = exchange(fd);
    if (memcmp(sent+sizeof(*r), received+sizeof(*result), 768)) { errno = ERANGE; die("interleaved AI2D"); }
    printf("PASS AI2D interleaved job=%llu\n", (unsigned long long)result->id);
    fflush(stdout);
}
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return 2;
    sent = calloc(1, CAPACITY); received = malloc(CAPACITY);
    if (!sent || !received) die("allocate");
    int fd = open_service();
    struct tdvp_ai_request *r = prepare(64, 0, 0);
    r->flags = 1U << 7; /* Never expose interrupt masking or raw cfg fields. */
    if (write(fd, sent, sizeof(*r)+r->input_bytes) != -1 || errno != EINVAL) { errno = EPROTO; die("FFT forbidden flag"); }
    unsigned int fft_jobs = 0, ai2d_jobs = 0;
    for (unsigned int n = 64; n <= 4096; n <<= 1) {
        for (unsigned int inverse = 0; inverse < 2; ++inverse)
        for (unsigned int scaled = 0; scaled < 2; ++scaled)
        for (unsigned int vector = 0; vector < 4; ++vector) {
            fft_case(fd, n, inverse, scaled, vector); ++fft_jobs;
        }
        ai2d_case(fd, ai2d_jobs++);
    }
    r = prepare(64, 0, 0);
    wait_for(fd, POLLOUT);
    if (write(fd, sent, sizeof(*r)+r->input_bytes) != (ssize_t)(sizeof(*r)+r->input_bytes)) die("detach submit");
    close(fd);
    fd = open_service();
    if (read(fd, received, CAPACITY) != -1 || errno != EAGAIN) { errno = EPROTO; die("detached FFT result leaked"); }
    close(fd); free(sent); free(received);
    printf("PASS CPU0 async FFT: %u numerical FFT/IFFT, %u interleaved AI2D, detached FFT discard; max_abs_error=%u tolerance=1\n",
        fft_jobs, ai2d_jobs, maximum_error);
    return 0;
}
