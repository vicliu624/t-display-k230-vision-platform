/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* Bounded hardware test. No crop/control/DRM operation or acceptance marker. */
#define FRAME_COUNT 60U
#define BUFFER_COUNT 4U
#define FRAME_BYTES (1920U * 1080U * 3U / 2U)

static int camera_ioctl(int fd, unsigned long request, void *arg)
{
    int rc;
    do { rc = ioctl(fd, request, arg); } while (rc < 0 && errno == EINTR);
    return rc;
}

static uint64_t monotonic_us(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

int main(int argc, char **argv)
{
    int fd = -1, output = -1, rc = 1;
    bool streaming = false;
    void *maps[BUFFER_COUNT] = {0};
    size_t lengths[BUFFER_COUNT] = {0};
    struct v4l2_capability cap = {0};
    struct v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    struct v4l2_requestbuffers req = {
        .count = BUFFER_COUNT, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP
    };
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    uint64_t first_ts = 0, previous_ts = 0, deadline;
    uint32_t first_sequence = 0, previous_sequence = 0;

    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s /dev/videoN [new-final-frame.nv12]\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    fd = open(argv[1], O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 || camera_ioctl(fd, VIDIOC_QUERYCAP, &cap))
        goto fail;
    uint32_t caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
        ? cap.device_caps : cap.capabilities;
    if (strncmp((char *)cap.driver, "vvcam", 5) ||
        !(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Refusing non-VVCAM capture device\n");
        goto out;
    }
    fmt.fmt.pix.width = 1920;
    fmt.fmt.pix.height = 1080;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (camera_ioctl(fd, VIDIOC_S_FMT, &fmt))
        goto fail;
    if (fmt.fmt.pix.width != 1920 || fmt.fmt.pix.height != 1080 ||
        fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_NV12 ||
        fmt.fmt.pix.bytesperline != 1920 || fmt.fmt.pix.sizeimage != FRAME_BYTES) {
        fprintf(stderr, "Unexpected format/stride/size; refusing raw-frame assumptions\n");
        goto out;
    }
    if (camera_ioctl(fd, VIDIOC_REQBUFS, &req))
        goto fail;
    if (req.count < 2 || req.count > BUFFER_COUNT) {
        fprintf(stderr, "Unexpected buffer count: %u\n", req.count);
        goto out;
    }
    for (unsigned i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf = {
            .type = type, .memory = V4L2_MEMORY_MMAP, .index = i
        };
        if (camera_ioctl(fd, VIDIOC_QUERYBUF, &buf))
            goto fail;
        if (buf.length < FRAME_BYTES || buf.length > 16U * 1024U * 1024U) {
            fprintf(stderr, "Unexpected mapped buffer length\n");
            goto out;
        }
        lengths[i] = buf.length;
        maps[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                       fd, buf.m.offset);
        if (maps[i] == MAP_FAILED || camera_ioctl(fd, VIDIOC_QBUF, &buf))
            goto fail;
    }
    if (camera_ioctl(fd, VIDIOC_STREAMON, &type))
        goto fail;
    streaming = true;
    deadline = monotonic_us() + 15000000U;
    for (unsigned frames = 0; frames < FRAME_COUNT;) {
        struct pollfd pollfd = {.fd = fd, .events = POLLIN};
        struct v4l2_buffer buf = {.type = type, .memory = V4L2_MEMORY_MMAP};
        if (monotonic_us() >= deadline) {
            fprintf(stderr, "Capture deadline exceeded\n");
            goto out;
        }
        int ready = poll(&pollfd, 1, 2000);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0 || !(pollfd.revents & POLLIN)) {
            fprintf(stderr, "Capture poll failed/timed out: %d\n", ready);
            goto out;
        }
        if (camera_ioctl(fd, VIDIOC_DQBUF, &buf)) {
            if (errno == EAGAIN)
                continue;
            goto fail;
        }
        uint64_t ts = (uint64_t)buf.timestamp.tv_sec * 1000000U +
                      (uint64_t)buf.timestamp.tv_usec;
        if (buf.index >= req.count || buf.bytesused != FRAME_BYTES ||
            buf.bytesused > lengths[buf.index] ||
            (buf.flags & V4L2_BUF_FLAG_ERROR) ||
            (buf.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) != V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC ||
            buf.timestamp.tv_sec < 0 || buf.timestamp.tv_usec < 0 ||
            buf.timestamp.tv_usec >= 1000000 || ts == 0 ||
            ts > monotonic_us() ||
            (frames && (ts <= previous_ts || buf.sequence != previous_sequence + 1U))) {
            fprintf(stderr, "Invalid frame metadata: frame=%u index=%u bytes=%u flags=%x seq=%u ts=%" PRIu64 "\n",
                    frames, buf.index, buf.bytesused, buf.flags, buf.sequence, ts);
            goto out;
        }
        if (frames == 0) {
            first_ts = ts;
            first_sequence = buf.sequence;
        }
        previous_ts = ts;
        previous_sequence = buf.sequence;
        if (frames == FRAME_COUNT - 1 && argc == 3) {
            output = open(argv[2], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (output < 0)
                goto fail;
            size_t written = 0;
            while (written < buf.bytesused) {
                ssize_t n = write(output, (uint8_t *)maps[buf.index] + written,
                                  buf.bytesused - written);
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0)
                    goto fail;
                written += (size_t)n;
            }
            if (close(output)) {
                output = -1;
                goto fail;
            }
            output = -1;
        }
        if (camera_ioctl(fd, VIDIOC_QBUF, &buf))
            goto fail;
        ++frames;
    }
    if (camera_ioctl(fd, VIDIOC_STREAMOFF, &type))
        goto fail;
    streaming = false;
    printf("PASS VVCAM capture: frames=%u format=1920x1080/NV12 bytes=%u sequence=%u..%u timestamps_us=%" PRIu64 "..%" PRIu64 " measured_fps=%.3f\n",
           FRAME_COUNT, FRAME_BYTES, first_sequence, previous_sequence, first_ts,
           previous_ts, (FRAME_COUNT - 1) * 1000000.0 / (previous_ts - first_ts));
    rc = 0;
    goto out;
fail:
    perror("VVCAM capture");
out:
    if (streaming)
        camera_ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned i = 0; i < BUFFER_COUNT; ++i)
        if (maps[i] && maps[i] != MAP_FAILED)
            munmap(maps[i], lengths[i]);
    if (fd >= 0)
        close(fd);
    if (output >= 0)
        close(output);
    return rc;
}
