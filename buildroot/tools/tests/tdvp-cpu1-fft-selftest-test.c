/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "tdvp_vision_abi.h"
#include "k_fft_ioctl.h"
static int test_open(const char *, int, ...);
static int test_ioctl(int, unsigned long, ...);
static int test_close(int);
#define open test_open
#define ioctl test_ioctl
#define close test_close
#include "../tdvp-cpu1-fft-selftest.c"
#undef open
#undef ioctl
#undef close

static int scenario, calls, closes;
static int test_open(const char *path, int flags, ...)
{
    assert(!strcmp(path, "/dev/fft_device") && flags == (O_RDWR | O_CLOEXEC));
    if (scenario == 1) { errno = EACCES; return -1; }
    return 7;
}
static int test_close(int fd)
{
    assert(fd == 7); ++closes;
    if (scenario == 4) { errno = EIO; return -1; }
    return 0;
}
static int test_ioctl(int fd, unsigned long command, ...)
{
    va_list list;
    va_start(list, command);
    k_fft_args_st *args = va_arg(list, k_fft_args_st *);
    va_end(list);
    assert(fd == 7 && command == KD_IOC_CMD_FFT_IFFT);
    ++calls;
    if (scenario == 2 || (scenario == 5 && calls == 3)) { errno = ETIMEDOUT; return -1; }
    uint64_t config = args->reg.cfg_value;
    assert(!(config & ~UINT64_C(0x77)) && (config & 7U) <= 6);
    unsigned int n = 64U << (config & 7U), im = (config >> 4) & 3U, om = (config >> 6) & 1U;
    double complex samples[FFT_MAX_POINT];
    /* Independent radix-2 software reference, not the hardware implementation
     * or the checker's closed-form expected values. Host validation only. */
    for (unsigned int i = 0; i < n; ++i) {
        int16_t re, imaginary = 0;
        unsigned int index = im == RIRI ? 2U * i : i;
        memcpy(&re, (unsigned char *)args->data + index * 2U, 2);
        if (im != RRRR) {
            index = im == RIRI ? 2U * i + 1U : n + i;
            memcpy(&imaginary, (unsigned char *)args->data + index * 2U, 2);
        }
        samples[i] = re + I * imaginary;
    }
    for (unsigned int i = 1, j = 0; i < n; ++i) {
        unsigned int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double complex tmp = samples[i]; samples[i] = samples[j]; samples[j] = tmp; }
    }
    for (unsigned int size = 2; size <= n; size <<= 1) {
        double angle = -2.0 * acos(-1.0) / size;
        double complex factor = cos(angle) + I * sin(angle);
        for (unsigned int base = 0; base < n; base += size) {
            double complex w = 1;
            for (unsigned int j = 0; j < size / 2; ++j) {
                double complex u = samples[base + j], v = samples[base + j + size / 2] * w;
                samples[base + j] = u + v; samples[base + j + size / 2] = u - v;
                w *= factor;
            }
        }
    }
    for (unsigned int i = 0; i < n; ++i) {
        int16_t re = (int16_t)lround(creal(samples[i])), imaginary = (int16_t)lround(cimag(samples[i]));
        unsigned int ri = om == RIRI_OUT ? 2U * i : i, ii = om == RIRI_OUT ? 2U * i + 1U : n + i;
        memcpy((unsigned char *)args->data + ri * 2U, &re, 2);
        memcpy((unsigned char *)args->data + ii * 2U, &imaginary, 2);
    }
    if (scenario == 3) args->data[0] ^= UINT64_C(0x40);
    return 0;
}
int main(void)
{
    struct tdvp_vision_control control, before;
    _Static_assert(4 + TDVP_FFT_EVIDENCE_WORDS <=
        sizeof(control.producer.reserved) / sizeof(control.producer.reserved[0]), "evidence exceeds producer");
    for (scenario = 0; scenario <= 5; ++scenario) {
        memset(&control, 0xa5, sizeof(control)); before = control;
        volatile uint32_t *record = control.producer.reserved + 4;
        calls = closes = 0;
        int result = tdvp_cpu1_fft_selftest(record);
        assert((scenario == 0 ? result == 0 : result < 0));
        assert(record[0] == TDVP_FFT_EVIDENCE_MAGIC && record[10] == 1);
        assert(record[1] == (scenario ? 4 : 3) && (int32_t)record[4] == result);
        assert(record[2] == (scenario == 0 || scenario == 4 ? 126 : scenario == 5 ? 2 : 0));
        assert(closes == (scenario != 1));
        assert(calls == (scenario == 0 || scenario == 4 ? 126 : scenario == 1 ? 0 : scenario == 5 ? 3 : 1));
        const size_t start = offsetof(struct tdvp_vision_control, producer.reserved) + 4 * sizeof(uint32_t);
        const size_t end = start + TDVP_FFT_EVIDENCE_WORDS * sizeof(uint32_t);
        assert(!memcmp(&before, &control, start));
        assert(!memcmp((char *)&before + end, (char *)&control + end, sizeof(control) - end));
    }
    puts("CPU1 FFT selftest: PASS independent software FFT, 126 configurations, five failures and evidence bounds; NOT silicon acceptance");
    return 0;
}
