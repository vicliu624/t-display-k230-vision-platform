/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Discovery is not capture acceptance. Never substitute the MVX codec video0. */
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--media0-check") == 0) {
        struct media_device_info info = {0};
        int fd = open("/dev/media0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) { perror("open /dev/media0"); return 1; }
        int rc = ioctl(fd, MEDIA_IOC_DEVICE_INFO, &info);
        close(fd);
        if (rc || strcmp(info.model, "verisilicon_media") != 0) {
            fputs("Scalar ISP requires the VVCAM graph at /dev/media0\n", stderr);
            return 1;
        }
        return 0;
    }
    if (argc != 1) {
        fputs("usage: tdvp-camera-device [--media0-check]\n", stderr);
        return 2;
    }
    for (unsigned int i = 0; i < 64; ++i) {
        char path[96], name[64];
        snprintf(path, sizeof(path), "/sys/class/video4linux/video%u/name", i);
        FILE *file = fopen(path, "r");
        if (!file) continue;
        char *line = fgets(name, sizeof(name), file);
        fclose(file);
        if (!line || strcmp(name, "vvcam-video.0.0\n") != 0) continue;
        snprintf(path, sizeof(path), "/dev/video%u", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) { perror(path); return 1; }
        struct v4l2_capability cap = {0};
        int rc = ioctl(fd, VIDIOC_QUERYCAP, &cap);
        close(fd);
        unsigned int caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ?
                            cap.device_caps : cap.capabilities;
        if (rc || strncmp((char *)cap.driver, "vvcam", 5) != 0 ||
            !(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
            fputs("GC2093 node lacks the expected VVCAM capture interface\n", stderr);
            return 1;
        }
        puts(path);
        return 0;
    }
    fputs("No GC2093 VVCAM capture device is available\n", stderr);
    return 1;
}
