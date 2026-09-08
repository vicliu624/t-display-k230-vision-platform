/* SPDX-License-Identifier: MIT */
/* Explicit Linux CLI acceptance; no Camera menu entry, no automatic startup.
 * write/poll/read only. Numerical errors stop the run; no reset/retry backend. */
#define _GNU_SOURCE
#include "tdvp_ai_abi.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static unsigned long long milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) { perror("clock"); exit(1); }
    return (unsigned long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static void die(const char *message) { perror(message); exit(1); }
static void wait_event(int fd, short wanted)
{
    unsigned long long deadline = milliseconds() + 15000;
    for (;;) {
        struct pollfd p = {fd, wanted, 0};
        int result = poll(&p, 1, 200);
        if (result < 0 && errno != EINTR) die("poll");
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { errno = EIO; die("AI service fault"); }
        if (p.revents & wanted) return;
        if (milliseconds() >= deadline) { errno = ETIMEDOUT; die("AI result deadline (no reset)"); }
    }
}
static int open_service(void)
{
    unsigned long long deadline = milliseconds() + 15000;
    for (;;) {
        int fd = open("/dev/tdvp-ai", O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) return fd;
        if ((errno != EAGAIN && errno != EBUSY) || milliseconds() >= deadline) die("open tdvp-ai");
        const struct timespec delay = {0, 20000000}; nanosleep(&delay, NULL);
    }
}
static unsigned char pattern(unsigned int c, unsigned int y, unsigned int x, unsigned int round)
{ return (unsigned char)(c * 53 + y * 11 + x * 7 + round * 19); }

int main(int argc, char **argv)
{
    unsigned int total = 15;
    if (argc == 2) {
        char *end;
        unsigned long count = strtoul(argv[1], &end, 10);
        if (*end || count < 1 || count > 100) return 2;
        total = count;
    } else if (argc != 1) return 2;
    size_t capacity = sizeof(struct tdvp_ai_request) + TDVP_AI_BUFFER_BYTES;
    unsigned char *sent = calloc(1, capacity), *received = malloc(capacity);
    if (!sent || !received) die("allocate");
    int fd = open_service();
    int second = open("/dev/tdvp-ai", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (second >= 0 || errno != EBUSY) { errno = EPROTO; die("exclusive client"); }
    unsigned long long previous = 0;
    for (unsigned int round = 0; round < total; ++round) {
        unsigned int kind = round % 5, side = kind == 4 ? 256 : 16;
        struct tdvp_ai_request *r = (void *)sent;
        memset(r, 0, sizeof(*r));
        r->magic = TDVP_AI_MAGIC; r->version = TDVP_AI_VERSION; r->bytes = sizeof(*r);
        r->operation = TDVP_AI_AI2D; r->format = TDVP_AI_CHW_U8; r->budget_ms = 10000;
        r->input_width = r->input_height = side;
        r->crop_x = r->crop_y = (kind == 1 || kind == 3) ? 4 : 0;
        r->crop_width = r->crop_height = r->crop_x ? 8 : side;
        r->pad_left = r->pad_right = (kind == 2 || kind == 3) ? 4 : 0;
        r->pad_top = r->pad_bottom = (kind == 2 || kind == 3) ? 2 : 0;
        r->pad_value[0] = 17; r->pad_value[1] = 37; r->pad_value[2] = 59;
        r->output_width = r->crop_width + r->pad_left + r->pad_right;
        r->output_height = r->crop_height + r->pad_top + r->pad_bottom;
        r->input_bytes = side * side * 3;
        r->output_capacity = r->output_width * r->output_height * 3;
        unsigned char *pixels = sent + sizeof(*r);
        for (unsigned int c = 0; c < 3; ++c)
            for (unsigned int y = 0; y < side; ++y)
                for (unsigned int x = 0; x < side; ++x) pixels[c * side * side + y * side + x] = pattern(c, y, x, round);
        wait_event(fd, POLLOUT);
        size_t sent_bytes = sizeof(*r) + r->input_bytes;
        if (!round) {
            r->operation = 2;
            if (write(fd, sent, sent_bytes) != -1 || errno != EOPNOTSUPP) { errno = EPROTO; die("KPU must remain unavailable"); }
            r->operation = TDVP_AI_AI2D;
            r->owner_cookie = 1;
            if (write(fd, sent, sent_bytes) != -1 || errno != EINVAL) { errno = EPROTO; die("forged owner rejection"); }
            r->owner_cookie = 0;
        }
        if (write(fd, sent, sent_bytes) != (ssize_t)sent_bytes) die("submit");
        if (write(fd, sent, sent_bytes) != -1 || errno != EAGAIN) { errno = EPROTO; die("outstanding lease overwritten"); }
        wait_event(fd, POLLIN);
        if (read(fd, received, sizeof(struct tdvp_ai_response)) != -1 || errno != EMSGSIZE) {
            errno = EPROTO; die("short read must retain result");
        }
        if (syscall(SYS_read, fd, (void *)1, capacity) != -1 || errno != EFAULT) {
            errno = EPROTO; die("failed copy must retain result");
        }
        ssize_t got = read(fd, received, capacity);
        if (got < 0) die("read result");
        struct tdvp_ai_response *result = (void *)received;
        if (got != (ssize_t)(sizeof(*result) + r->output_capacity) || result->result ||
            !result->owner_cookie || !result->peer_cookie || !result->client_cookie || result->id <= previous ||
            result->output_bytes != r->output_capacity || result->operation != r->operation ||
            result->output_width != r->output_width || result->output_height != r->output_height || result->format != r->format) {
            errno = EPROTO; die("response metadata");
        }
        previous = result->id;
        unsigned char *actual = received + sizeof(*result);
        for (unsigned int c = 0; c < 3; ++c)
        for (unsigned int y = 0; y < r->output_height; ++y)
        for (unsigned int x = 0; x < r->output_width; ++x) {
            unsigned int index = c * r->output_height * r->output_width + y * r->output_width + x;
            unsigned char expected = (x < r->pad_left || x >= r->pad_left + r->crop_width ||
                y < r->pad_top || y >= r->pad_top + r->crop_height) ? r->pad_value[c] :
                pattern(c, y - r->pad_top + r->crop_y, x - r->pad_left + r->crop_x, round);
            if (actual[index] != expected) {
                fprintf(stderr, "FAIL case=%u byte=%u expected=%u actual=%u\n", round, index, expected, actual[index]);
                return 1;
            }
        }
        printf("PASS case=%u kind=%u job=%llu bytes=%u cpu1_ms=%llu\n", round, kind,
            (unsigned long long)result->id, result->output_bytes, (unsigned long long)result->duration_ms);
        fflush(stdout);
    }
    // Safe close-in-flight test: no cancellation, reset or forced timeout.
    struct tdvp_ai_request *last = (void *)sent;
    wait_event(fd, POLLOUT);
    size_t bytes = sizeof(*last) + last->input_bytes;
    if (write(fd, sent, bytes) != (ssize_t)bytes) die("detach submit");
    close(fd);
    fd = open_service();
    if (read(fd, received, capacity) != -1 || errno != EAGAIN) { errno = EPROTO; die("stale result leaked to new client"); }
    close(fd); free(sent); free(received);
    printf("PASS CPU0 async AI2D: %u numerical cases; backpressure, short/EFAULT reads and detached-result discard\n", total);
    return 0;
}
