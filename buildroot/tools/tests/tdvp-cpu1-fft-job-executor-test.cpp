// SPDX-License-Identifier: MIT
// Executes the extracted production FFT executor against the pinned ioctl ABI.
// Fakes test packing/lease/error behavior, not FFT mathematics or hardware.
#include "tdvp_ai_abi.h"
#include "k_fft_ioctl.h"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>

static tdvp_ai_request request;
static tdvp_ai_response response;
static unsigned char input[16384], output[16384 + 16];
static k_fft_args_st *fft_retained;
static int guard, executor_ops, failure, checks, opens, ioctls, closes, finishes;
struct Fault {};
[[noreturn]] static void failed(int error) { assert(error < 0); throw Fault{}; }
static void check() { if (++checks == failure) failed(-ETIMEDOUT); }
static void fence() { }
static int fake_open(const char *name, int flags)
{
    assert(!std::strcmp(name, "/dev/fft_device") && flags == (O_RDWR | O_CLOEXEC));
    ++opens; return failure == 10 ? -1 : 41;
}
static int fake_ioctl(int fd, unsigned long command, k_fft_args_st *args)
{
    assert(fd == 41 && command == KD_IOC_CMD_FFT_IFFT && args == fft_retained); ++ioctls;
    unsigned int point = 0; for (unsigned int n = 64; n < request.input_width; n <<= 1) ++point;
    uint64_t expected = point | ((request.flags & TDVP_AI_FFT_INVERSE) ? 8 : 0) | (request.flags & TDVP_AI_FFT_SHIFT_MASK);
    assert(args->reg.cfg_value == expected);
    for (unsigned int i = 0; i < sizeof(args->rsv); ++i) assert(args->rsv[i] == 0);
    assert(!std::memcmp(args->data, input, request.input_bytes));
    for (unsigned int i = request.input_bytes; i < sizeof(args->data); ++i)
        assert(reinterpret_cast<unsigned char *>(args->data)[i] == 0);
    if (failure == 11) return -1;
    std::memset(args->data, 0x5a, request.input_bytes);
    return 0;
}
static int fake_close(int fd) { assert(fd == 41); ++closes; return failure == 12 ? -1 : 0; }
static int tdvp_cpu1_ai_guard_finish(int *, const int *) { ++finishes; return failure == 13 ? -EIO : 0; }
#define open fake_open
#define ioctl fake_ioctl
#define close fake_close
#include "execute_fft.inc"
#undef open
#undef ioctl
#undef close

int main()
{
    static_assert(sizeof(k_fft_cfg_reg_st) == 8 && offsetof(k_fft_args_st, data) == 16 && sizeof(k_fft_args_st) == 16400);
    unsigned int cases = 0;
    for (unsigned int n = 64; n <= 4096; n <<= 1)
    for (unsigned int flags = 0; flags < 4; ++flags) {
        request = {}; response = {};
        request.operation = TDVP_AI_FFT; request.input_width = request.output_width = n;
        request.input_bytes = n * 4; request.flags = (flags & 1) | ((flags & 2) ? (n - 1) << 8 : 0);
        checks = opens = ioctls = closes = finishes = 0;
        for (unsigned int i = 0; i < sizeof(input); ++i) input[i] = i * 7;
        std::memset(output, 0xa5, sizeof(output));
        execute_fft();
        assert(!fft_retained && opens == 1 && ioctls == 1 && closes == 1 && finishes == 1);
        assert(response.output_bytes == n * 4);
        for (unsigned int i = 0; i < sizeof(output); ++i) assert(output[i] == (i < n * 4 ? 0x5a : 0xa5));
        ++cases;
    }
    const int faults[] = {1, 2, 3, 4, 5, 10, 11, 12, 13};
    for (int fault : faults) {
        failure = fault; checks = opens = ioctls = closes = finishes = 0; response = {};
        bool caught = false;
        try { execute_fft(); } catch (const Fault &) { caught = true; }
        assert(caught);
        if (fault != 1 && fault != 13) assert(fft_retained); // No unwind/free on uncertainty.
        else assert(!fft_retained);
        if (fault <= 3 || fault == 10 || fault == 11) assert(!closes && !finishes);
        if (fault != 13) assert(response.output_bytes == 0 && !finishes);
        delete fft_retained; fft_retained = nullptr; // Test fake has no hardware lease.
        ++cases;
    }
    std::printf("CPU1 FFT executor: PASS %u pinned-ABI packing/extent/retained-fault cases (no hardware)\n", cases);
}
