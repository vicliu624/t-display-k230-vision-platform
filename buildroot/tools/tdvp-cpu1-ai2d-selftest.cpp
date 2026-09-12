// SPDX-License-Identifier: MIT
// Explicit paired-CPU1 diagnostic, not an image autostart or Linux AI backend.
// Link with --wrap=open,--wrap=close,--wrap=poll and the fixed RT-Smart nncase.
// A supervisor outlives a stuck execution thread. Faults retain its buffers;
// callers must publish terminal failure and keep the PROCESS alive, no kill.
#define _POSIX_C_SOURCE 200809L
#include "tdvp_cpu1_ai_guard.h"
#include "tdvp_cpu1_vision_layout.h"
#include <nncase/runtime/runtime_tensor.h>
#include <nncase/runtime/runtime_op_utility.h>
#include <nncase/functional/ai2d/ai2d_builder.h>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#ifndef BUILDING_RUNTIME
#error "The pinned AI2D library uses the runtime class layout"
#endif
using namespace nncase;
using namespace nncase::runtime;
using namespace nncase::runtime::k230;
using nncase::F::k230::ai2d_builder;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(sizeof(ai2d_builder) == 712 && offsetof(ai2d_builder, input_shape_) == 496 &&
              offsetof(ai2d_builder, output_shape_) == 600 && offsetof(ai2d_builder, dump_asm_) == 704,
              "pinned AI2D runtime ABI mismatch");
#pragma GCC diagnostic pop

namespace {
tdvp_cpu1_ai_guard guard{};
volatile uint32_t *record;
int (*owner_ready)(void *);
void *owner_context;
int ai2d_fd = -1;
unsigned int poll_completions;
bool attempted;
thread_local bool executing_ai2d;
void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
// reserved[6]: high bit means a stage; otherwise a mismatch byte index.
// reserved[7]: raw nncase error value at that stage, or expected/actual bytes.
void stage(uint32_t step) { record[6] = UINT32_C(0x80000000) | step; record[7] = 0; fence(); }
void park(void *) { const timespec delay{0, 20000000}; (void)nanosleep(&delay, nullptr); }
int clock_ms(void *, uint64_t *ms)
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1000000000L)
        return -EIO;
    if (uint64_t(now.tv_sec) > (UINT64_MAX - 999) / 1000) return -EOVERFLOW;
    *ms = uint64_t(now.tv_sec) * 1000 + uint64_t(now.tv_nsec) / 1000000;
    return 0;
}
int ready(void *) { return owner_ready ? owner_ready(owner_context) : -EPERM; }
[[noreturn]] void failed(int error)
{
    (void)tdvp_cpu1_ai_guard_fail(&guard, error);
    for (;;) park(nullptr); // No unwinding, cancellation, reset or MMZ free.
}
template<class T> T value(result<T> r)
{
    if (r.is_err()) { record[7] = uint32_t(r.unwrap_err().value()); fence(); failed(-EIO); }
    return std::move(r).unwrap();
}
void checked(result<void> r)
{ if (r.is_err()) { record[7] = uint32_t(r.unwrap_err().value()); fence(); failed(-EIO); } }
uint8_t pattern(unsigned int channel, unsigned int y, unsigned int x, unsigned int round)
{ return uint8_t(channel * 53 + y * 11 + x * 7 + round * 19); }
uintptr_t physical(runtime_tensor &tensor, size_t bytes)
{
    auto host = value(tensor.impl()->to_host());
    auto buffer = value(host->buffer().as_host());
    if (buffer.size_bytes() != bytes || !buffer.has_physical_address()) failed(-EPROTO);
    auto address = value(buffer.physical_address());
    if (address < TDVP_VISION_MMZ_BASE || address >= TDVP_VISION_MMZ_BASE + TDVP_VISION_MMZ_SIZE ||
        bytes > TDVP_VISION_MMZ_BASE + TDVP_VISION_MMZ_SIZE - address) failed(-ERANGE);
    return address;
}
struct tensors { runtime_tensor input, output; ai2d_builder *builder{}; };
tensors *retained; // Remains rooted if a library operation throws or blocks.
extern "C" int __real_poll(struct pollfd *, nfds_t, int);
int real_poll(void *, struct pollfd *fds, nfds_t count, int timeout)
{
    int result = __real_poll(fds, count, timeout), saved = errno;
    record[7] = UINT32_C(0x10000000) | (result < 0 ? uint32_t(saved) << 16 : uint16_t(fds[0].revents));
    fence(); errno = saved;
    return result;
}
tdvp_cpu1_ai_guard_ops ops{clock_ms, ready, real_poll, park, nullptr};
void *execute(void *)
{
    executing_ai2d = true;
    try {
        for (unsigned int kind = 0; kind < 3; ++kind)
        for (unsigned int round = 0; round < 4; ++round) {
            if (tdvp_cpu1_ai_guard_check(&guard, &ops)) failed(-EIO);
            record[3] = kind * 4 + round + 1; fence();
            const unsigned int h = kind == 1 ? 8 : kind == 2 ? 20 : 16;
            const unsigned int w = kind == 1 ? 8 : kind == 2 ? 24 : 16;
            dims_t input_shape{1, 3, 16, 16}, output_shape{1, 3, h, w};
            retained = new tensors;
            stage(1);
            retained->input = value(hrt::create(dt_uint8, input_shape, hrt::pool_shared));
            stage(2);
            retained->output = value(hrt::create(dt_uint8, output_shape, hrt::pool_shared));
            stage(3);
            const uintptr_t in = physical(retained->input, 3 * 16 * 16);
            const uintptr_t out = physical(retained->output, 3 * h * w);
            if (!(in + 3 * 16 * 16 <= out || out + 3 * h * w <= in)) failed(-ERANGE);
            record[8] = uint32_t(in); record[9] = uint32_t(out); fence();
            stage(4);
            auto input_map = value(hrt::map(retained->input, map_access_t::map_write));
            auto input = reinterpret_cast<uint8_t *>(input_map.buffer().data());
            for (unsigned int c = 0; c < 3; ++c)
            for (unsigned int y = 0; y < 16; ++y)
            for (unsigned int x = 0; x < 16; ++x) input[c * 256 + y * 16 + x] = pattern(c, y, x, round);
            stage(5); checked(input_map.unmap());
            stage(6);
            checked(hrt::sync(retained->input, sync_op_t::sync_write_back, true));
            stage(7); auto output_map = value(hrt::map(retained->output, map_access_t::map_write));
            std::memset(output_map.buffer().data(), 0xa5, 3 * h * w);
            stage(8); checked(output_map.unmap());
            stage(9);
            checked(hrt::sync(retained->output, sync_op_t::sync_write_back, true));
            ai2d_datatype_t dtype{ai2d_format::NCHW_FMT, ai2d_format::NCHW_FMT, dt_uint8, dt_uint8};
            ai2d_crop_param_t crop{kind == 1, 4, 4, 8, 8};
            ai2d_shift_param_t shift{};
            ai2d_pad_param_t pad{kind == 2, {{0, 0}, {0, 0}, {2, 2}, {4, 4}}, ai2d_pad_mode::constant, {17, 37, 59}};
            ai2d_resize_param_t resize{};
            ai2d_affine_param_t affine{};
            stage(10); retained->builder = new ai2d_builder(input_shape, output_shape, dtype, crop, shift, pad, resize, affine);
            stage(11); checked(retained->builder->build_schedule());
            if (tdvp_cpu1_ai_guard_check(&guard, &ops)) failed(-EIO);
            stage(12); checked(retained->builder->invoke(retained->input, retained->output));
            if (tdvp_cpu1_ai_guard_check(&guard, &ops)) failed(-EIO);
            stage(13); checked(hrt::sync(retained->output, sync_op_t::sync_invalidate, true));
            stage(14); auto mapped = value(hrt::map(retained->output, map_access_t::map_read));
            const auto actual = reinterpret_cast<const uint8_t *>(mapped.buffer().data());
            const unsigned int padding[] = {17, 37, 59};
            stage(15);
            for (unsigned int c = 0; c < 3; ++c)
            for (unsigned int y = 0; y < h; ++y)
            for (unsigned int x = 0; x < w; ++x) {
                unsigned int index = c * h * w + y * w + x;
                uint8_t expected = kind == 1 ? pattern(c, y + 4, x + 4, round) :
                    kind == 2 ? ((y < 2 || y >= 18 || x < 4 || x >= 20) ? padding[c] : pattern(c, y - 2, x - 4, round)) :
                    pattern(c, y, x, round);
                if (actual[index] != expected) {
                    record[6] = index; record[7] = (uint32_t(expected) << 8) | actual[index]; fence();
                    failed(-ERANGE);
                }
            }
            stage(16); checked(mapped.unmap());
            // Completion, cache synchronization and byte comparison succeeded.
            // These are the only paths which destroy builder/tensor storage.
            stage(17); delete retained->builder; retained->builder = nullptr;
            delete retained; retained = nullptr;
            record[2] = kind * 4 + round + 1; fence();
        }
        stage(18);
        if (__atomic_load_n(&poll_completions, __ATOMIC_ACQUIRE) < 12) failed(-EPROTO);
        if (tdvp_cpu1_ai_guard_finish(&guard, &ops)) failed(-EIO);
    } catch (...) { failed(-EIO); }
    return nullptr;
}
}

extern "C" int __real_open(const char *, int, ...);
extern "C" int __real_close(int);
extern "C" int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    bool has_mode = flags & O_CREAT;
#ifdef O_TMPFILE
    has_mode = has_mode || (flags & O_TMPFILE) == O_TMPFILE;
#endif
    if (has_mode) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
    if (executing_ai2d && !std::strcmp(path, "/dev/gnne_device")) { errno = EPERM; return -1; } // AI2D-only scope.
    int fd = has_mode ? __real_open(path, flags, mode) : __real_open(path, flags);
    if (executing_ai2d && fd >= 0 && !std::strcmp(path, "/dev/ai_2d_device")) {
        if (tdvp_cpu1_ai_guard_status(&guard) != TDVP_AI_ACTIVE || ai2d_fd >= 0) failed(-EBUSY);
        ai2d_fd = fd;
    }
    return fd;
}
extern "C" int __wrap_close(int fd)
{
    int result = __real_close(fd);
    if (executing_ai2d && fd == ai2d_fd) { if (result) failed(-EIO); ai2d_fd = -1; }
    return result;
}
extern "C" int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    if (!executing_ai2d) return __real_poll(fds, count, timeout);
    int result = tdvp_cpu1_ai_guard_wait(&guard, &ops, ai2d_fd, fds, count);
    __atomic_add_fetch(&poll_completions, 1U, __ATOMIC_RELEASE);
    return result; // Only genuine POLLIN is permitted to return to pinned nncase.
}

extern "C" int tdvp_cpu1_ai2d_selftest(volatile uint32_t *evidence, int (*check_owner)(void *), void *context)
{
    if (attempted || !evidence || !check_owner) return -EINVAL;
    attempted = true; record = evidence; owner_ready = check_owner; owner_context = context;
    for (unsigned int i = 0; i < 11; ++i) record[i] = 0;
    record[0] = UINT32_C(0x44324941); record[10] = 2; fence(); record[1] = 1; fence();
    uint64_t now;
    if (clock_ms(nullptr, &now) || tdvp_cpu1_ai_guard_begin(&guard, now, 30000)) return -EIO;
    pthread_attr_t attr;
    pthread_t thread;
    int result = pthread_attr_init(&attr);
    if (result) return tdvp_cpu1_ai_guard_fail(&guard, -result);
    result = pthread_attr_setstacksize(&attr, 256 * 1024);
    if (!result) result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (!result) result = pthread_create(&thread, &attr, execute, nullptr);
    (void)pthread_attr_destroy(&attr);
    if (result) return tdvp_cpu1_ai_guard_fail(&guard, -result);
    record[1] = 2; fence();
    for (;;) {
        int status = tdvp_cpu1_ai_guard_status(&guard);
        record[5] = __atomic_load_n(&poll_completions, __ATOMIC_ACQUIRE);
        if (status == TDVP_AI_DONE || status < 0) {
            record[4] = status < 0 ? uint32_t(status) : 0;
            fence(); record[1] = status < 0 ? 4 : 3; fence();
            return status < 0 ? status : 0; // Never kill/cancel the retained execution thread.
        }
        (void)tdvp_cpu1_ai_guard_check(&guard, &ops);
        park(nullptr);
    }
}
