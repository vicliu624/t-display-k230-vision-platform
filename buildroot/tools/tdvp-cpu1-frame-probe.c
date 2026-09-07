/* SPDX-License-Identifier: MIT */
/* Manual paired-board diagnostic. No V4L2, physical mappings, resets or UI.
 * Not installed or started by the image. Opening the bridge requests capture;
 * close requests STOP but does not release CPU1's boot-lifetime ownership.
 */
#define _POSIX_C_SOURCE 200809L
#include "tdvp_vision_abi.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The CPU1 frame wire ABI is little-endian"
#endif
#define FRAME_BYTES (1920U * 1080U * 3U / 2U)
#define RECORD_BYTES (sizeof(struct tdvp_vision_frame_header) + FRAME_BYTES)

struct frame_probe_evidence {
    uint64_t frames, first_sequence, sequence, first_pts, pts, hash, bytes;
    unsigned int luma_min, luma_max;
};

static int frame_probe_now(uint64_t *milliseconds)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -EIO;
    if (now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1000000000L ||
        (uint64_t)now.tv_sec > (UINT64_MAX - 999) / 1000U) return -EIO;
    *milliseconds = (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
    return 0;
}

static int frame_probe_validate(const void *record, size_t bytes, struct frame_probe_evidence *evidence)
{
    struct tdvp_vision_frame_header frame;
    const uint8_t *pixels = (const uint8_t *)record + sizeof(frame);
    uint64_t hash = UINT64_C(14695981039346656037);
    unsigned int minimum = 255, maximum = 0;
    if (bytes != RECORD_BYTES) return -EPROTO;
    memcpy(&frame, record, sizeof(frame));
    if (!frame.sequence || frame.width != 1920 || frame.height != 1080 ||
        frame.stride != 1920 || frame.bytes != FRAME_BYTES || frame.fourcc != TDVP_VISION_NV12)
        return -EPROTO;
    for (size_t i = 0; i < sizeof(frame.reserved) / sizeof(frame.reserved[0]); ++i)
        if (frame.reserved[i]) return -EPROTO;
    if (evidence->frames && (evidence->sequence == UINT64_MAX ||
        frame.sequence != evidence->sequence + 1 || frame.pts <= evidence->pts)) return -EPROTO;
    for (size_t i = 0; i < FRAME_BYTES; ++i) {
        hash = (hash ^ pixels[i]) * UINT64_C(1099511628211);
        if (i < 1920U * 1080U) {
            if (pixels[i] < minimum) minimum = pixels[i];
            if (pixels[i] > maximum) maximum = pixels[i];
        }
    }
    if (!evidence->frames) {
        evidence->first_sequence = frame.sequence;
        evidence->first_pts = frame.pts;
    }
    ++evidence->frames;
    evidence->bytes += FRAME_BYTES;
    evidence->sequence = frame.sequence;
    evidence->pts = frame.pts;
    evidence->hash = hash;
    evidence->luma_min = minimum;
    evidence->luma_max = maximum;
    return 0;
}

static int frame_probe_run(int fd, unsigned int frames, unsigned int timeout_ms,
                           void *record, struct frame_probe_evidence *evidence)
{
    uint64_t now, last, deadline;
    if (!frames || frames > 300 || !timeout_ms || timeout_ms > 120000) return -EINVAL;
    if (frame_probe_now(&last) || last > UINT64_MAX - timeout_ms) return -EIO;
    deadline = last + timeout_ms;
    while (evidence->frames < frames) {
        struct pollfd request = {.fd = fd, .events = POLLIN};
        int result;
        ssize_t bytes;
        if (frame_probe_now(&now) || now < last) return -EIO;
        last = now;
        if (now >= deadline) return -ETIMEDOUT;
        result = poll(&request, 1, (int)(deadline - now));
        if (result < 0) { if (errno == EINTR) continue; return -errno; }
        if (!result) return -ETIMEDOUT;
        if (request.revents & (POLLERR | POLLHUP | POLLNVAL)) return -EIO;
        if (!(request.revents & POLLIN)) return -EPROTO;
        /* Recheck after poll: late readiness is not an extension of the budget. */
        if (frame_probe_now(&now) || now < last) return -EIO;
        last = now;
        if (now >= deadline) return -ETIMEDOUT;
        bytes = read(fd, record, RECORD_BYTES);
        if (bytes < 0) { if (errno == EINTR || errno == EAGAIN) continue; return -errno; }
        if (!bytes) return -EPIPE;
        result = frame_probe_validate(record, (size_t)bytes, evidence);
        if (result) return result;
    }
    /* Processing/read overhead counts too; this is not a hard kernel deadline. */
    if (frame_probe_now(&now) || now < last) return -EIO;
    return now >= deadline ? -ETIMEDOUT : 0;
}

static unsigned int frame_probe_number(const char *text, unsigned int maximum)
{
    char *end;
    unsigned long value;
    if (!text[0] || strspn(text, "0123456789") != strlen(text)) return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    return errno || *end || !value || value > maximum ? 0 : (unsigned int)value;
}

int main(int argc, char **argv)
{
    struct frame_probe_evidence evidence = {0};
    struct stat info;
    unsigned int frames, timeout_ms;
    void *record;
    int fd, result;
    if ((argc != 3 && argc != 4) || !(frames = frame_probe_number(argv[1], 300)) ||
        !(timeout_ms = frame_probe_number(argv[2], 120000))) {
        fprintf(stderr, "usage: %s <frames:1..300> <total-timeout-ms:1..120000> [new-last-frame.nv12]\n", argv[0]);
        return 2;
    }
    record = malloc(RECORD_BYTES);
    if (!record) { perror("allocate frame"); return 1; }
    memset(record, 0, RECORD_BYTES); /* Prefault the ordinary user-copy buffer. */
    fd = open("/dev/tdvp-vision", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror("open CPU1 vision bridge"); free(record); return 1; }
    if (fstat(fd, &info) || !S_ISCHR(info.st_mode)) {
        fputs("CPU1 vision endpoint is not a character device\n", stderr);
        close(fd); free(record); return 1;
    }
    result = frame_probe_run(fd, frames, timeout_ms, record, &evidence);
    if (close(fd) && !result) result = -errno;
    if (result) {
        fprintf(stderr, "CPU1 frame transport failed after %" PRIu64 " frames: %s\n",
                evidence.frames, strerror(-result));
        free(record); return 1;
    }
    if (argc == 4) {
        const uint8_t *pixels = (const uint8_t *)record + sizeof(struct tdvp_vision_frame_header);
        size_t offset = 0;
        fd = open(argv[3], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) { perror("create new frame output"); free(record); return 1; }
        while (offset < FRAME_BYTES) {
            ssize_t bytes = write(fd, pixels + offset, FRAME_BYTES - offset);
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes <= 0) {
                fputs("frame output incomplete; do not use it as acceptance evidence\n", stderr);
                close(fd); free(record); return 1;
            }
            offset += (size_t)bytes;
        }
        if (close(fd)) { perror("close frame output"); free(record); return 1; }
    }
    printf("transport=cpu1-bridge frames=%" PRIu64 " bytes=%" PRIu64 " sequence=%" PRIu64 "..%" PRIu64
           " pts=%" PRIu64 "..%" PRIu64 " last_fnv1a64=%016" PRIx64 " luma=%u..%u\n",
           evidence.frames, evidence.bytes, evidence.first_sequence, evidence.sequence,
           evidence.first_pts, evidence.pts, evidence.hash, evidence.luma_min, evidence.luma_max);
    puts("PASS frame transport only; visual review, sensor identity and KPU/FFT/model acceptance remain separate");
    free(record);
    return 0;
}
