/* SPDX-License-Identifier: MIT */
/* Explicit CPU1 RT-Smart diagnostic, never auto-started by production.
 * Uses the paired, bounded /dev/fft_device PIO driver. No SDMA, MMIO,
 * physical addresses, reset, model, or Linux execution backend here.
 * Forward FFT, shift=0: impulse, DC and quarter-offset impulse have exact
 * closed-form integer references. One LSB absolute tolerance is fixed
 * before hardware testing. This does not cover IFFT or scaling semantics.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "k_fft_ioctl.h"

#define TDVP_FFT_EVIDENCE_WORDS 11U
#define TDVP_FFT_EVIDENCE_MAGIC UINT32_C(0x31544646)
#define TDVP_FFT_MAX_ERROR 1

static void fft_check_fence(void)
{
#ifdef __riscv
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

/* Optional CPU1-only evidence: magic/state/completed/current/result/max-error/
 * mismatch-index/expected-RI/actual-RI/reserved/reference-version.
 * State 1=prepared, 2=in ioctl, 3=all passed, 4=failed. State published last.
 */
int tdvp_cpu1_fft_selftest(volatile uint32_t *evidence)
{
    k_fft_args_st *args;
    unsigned int completed = 0, maximum_error = 0;
    int result = 0, fd;
    if (evidence) {
        for (unsigned int i = 0; i < TDVP_FFT_EVIDENCE_WORDS; ++i) evidence[i] = 0;
        evidence[0] = TDVP_FFT_EVIDENCE_MAGIC;
        evidence[10] = 1;
        fft_check_fence();
        evidence[1] = 1;
        fft_check_fence();
    }
    args = calloc(1, sizeof(*args));
    if (!args) { result = -ENOMEM; goto done; }
    fd = open("/dev/fft_device", O_RDWR | O_CLOEXEC);
    if (fd < 0) { result = errno ? -errno : -EIO; goto free; }
    for (unsigned int point = FFT_N64; point <= FFT_N4096; ++point)
    for (unsigned int input = RIRI; input <= RR_II; ++input)
    for (unsigned int output = RIRI_OUT; output <= RR_II_OUT; ++output)
    for (unsigned int vector = 0; vector < 3; ++vector) {
        unsigned int n = 64U << point;
        memset(args, 0, sizeof(*args));
        args->reg.cfg_value = point | (input << 4) | (output << 6);
        /* Packed shorts are copied through bytes to avoid aliasing an
         * uint64_t object through an incompatible int16_t pointer. */
        for (unsigned int i = 0; i < n; ++i) {
            int16_t real = vector == 1 ? 1 :
                i == (vector == 0 ? 0U : n / 4U) ? 128 : 0;
            unsigned int index = input == RIRI ? 2U * i : i;
            memcpy((unsigned char *)args->data + index * sizeof(real), &real, sizeof(real));
        }
        if (evidence) {
            evidence[3] = completed + 1;
            fft_check_fence(); evidence[1] = 2; fft_check_fence();
        }
        if (ioctl(fd, KD_IOC_CMD_FFT_IFFT, args)) {
            result = errno ? -errno : -EIO;
            goto close;
        }
        for (unsigned int i = 0; i < n; ++i) {
            int16_t real, imaginary;
            int expected_real, expected_imaginary = 0;
            unsigned int real_index = output == RIRI_OUT ? 2U * i : i;
            unsigned int imaginary_index = output == RIRI_OUT ? 2U * i + 1U : n + i;
            memcpy(&real, (unsigned char *)args->data + real_index * sizeof(real), sizeof(real));
            memcpy(&imaginary, (unsigned char *)args->data + imaginary_index * sizeof(imaginary), sizeof(imaginary));
            if (vector == 0) expected_real = 128;
            else if (vector == 1) expected_real = i == 0 ? (int)n : 0;
            else {
                expected_real = i % 4U == 0 ? 128 : i % 4U == 2 ? -128 : 0;
                expected_imaginary = i % 4U == 1 ? -128 : i % 4U == 3 ? 128 : 0;
            }
            unsigned int error_real = (unsigned int)abs((int)real - expected_real);
            unsigned int error_imaginary = (unsigned int)abs((int)imaginary - expected_imaginary);
            if (error_real > maximum_error) maximum_error = error_real;
            if (error_imaginary > maximum_error) maximum_error = error_imaginary;
            if (error_real > TDVP_FFT_MAX_ERROR || error_imaginary > TDVP_FFT_MAX_ERROR) {
                result = -ERANGE;
                if (evidence) {
                    evidence[6] = i;
                    evidence[7] = (uint16_t)expected_real | ((uint32_t)(uint16_t)expected_imaginary << 16);
                    evidence[8] = (uint16_t)real | ((uint32_t)(uint16_t)imaginary << 16);
                }
                fprintf(stderr, "CPU1 FFT mismatch case=%u n=%u input=%u output=%u vector=%u bin=%u expected=%d,%d actual=%d,%d\n",
                        completed + 1, n, input, output, vector, i,
                        expected_real, expected_imaginary, real, imaginary);
                goto close;
            }
        }
        ++completed;
        if (evidence) { evidence[2] = completed; fft_check_fence(); }
    }
close:
    /* Paired driver uses PIO, owns no DMA allocations, and latches hardware
     * faults itself. Closing this handle performs no recovery or reset. */
    if (close(fd) && !result) result = errno ? -errno : -EIO;
free:
    free(args);
done:
    if (evidence) {
        evidence[4] = (uint32_t)result;
        evidence[5] = maximum_error;
        fft_check_fence(); evidence[1] = result ? 4 : 3; fft_check_fence();
    }
    printf("CPU1 FFT numerical check: %s jobs=%u/126 max_abs_error=%u tolerance=%u result=%d\n",
           result ? "FAIL" : "PASS", completed, maximum_error, TDVP_FFT_MAX_ERROR, result);
    return result;
}

#ifdef TDVP_FFT_SELFTEST_MAIN
int main(void) { return tdvp_cpu1_fft_selftest(NULL) ? 1 : 0; }
#endif
