// SPDX-License-Identifier: MIT
// Boot-lifetime supervisor + executor. AI2D, PIO FFT and pinned KWS inference.
// Model execution is deliberately NOT implied by resource ownership READY.
#define _POSIX_C_SOURCE 200809L
#include "tdvp_ai_abi.h"
#include "tdvp_ai_job.h"
#include "tdvp_cpu1_ai_guard.h"
#include "tdvp_cpu1_ai_service.h"
#include "tdvp_cpu1_kpu_guard.h"
#include "tdvp_cpu1_vision_layout.h"
#include <nncase/runtime/runtime_tensor.h>
#include <nncase/runtime/interpreter.h>
#include <nncase/runtime/runtime_op_utility.h>
#include <nncase/runtime/k230/gnne.h>
#include <nncase/functional/ai2d/ai2d_builder.h>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <k_fft_ioctl.h>
#include <time.h>
#include <unistd.h>

#ifndef BUILDING_RUNTIME
#error "Pinned AI2D requires the runtime object layout"
#endif
using namespace nncase;
using namespace nncase::runtime;
using namespace nncase::runtime::k230;
using nncase::F::k230::ai2d_builder;
extern "C" const unsigned char tdvp_cpu1_kws_model_start[], tdvp_cpu1_kws_model_end[];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(sizeof(ai2d_builder) == 712 && offsetof(ai2d_builder, input_shape_) == 496 &&
    offsetof(ai2d_builder, output_shape_) == 600 && offsetof(ai2d_builder, dump_asm_) == 704,
    "Pinned AI2D runtime ABI drift");
#pragma GCC diagnostic pop

namespace {
volatile tdvp_ai_control *control;
unsigned char *input, *output;
tdvp_ai_job job{};
tdvp_cpu1_ai_guard guard{};
tdvp_ai_owner_view supervisor_owner{}, executor_owner{};
tdvp_ai_request request{};
tdvp_ai_response response{};
unsigned int generation;
bool attempted, connected;
thread_local bool executing_ai;
int ai2d_fd = -1; // Only the single executing_ai thread accesses this descriptor.
int kpu_fd = -1;
bool kpu_inflight;
uint32_t kpu_starts, kpu_completions;
struct retained_kpu_job { interpreter model; runtime_tensor inputs[2], outputs[2]; };
retained_kpu_job *kws; // Model, weights and tensors persist for this boot, including faults.
struct retained_job { runtime_tensor input, output; ai2d_builder *builder{}; };
retained_job *retained; // Root remains alive if execution blocks or fails.
k_fft_args_st *fft_retained; // No DMA; still retained on any uncertain completion.
static_assert(sizeof(k_fft_cfg_reg_st) == 8 && offsetof(k_fft_args_st, data) == 16 &&
    sizeof(k_fft_args_st) == 16400 && FFT_N64 == 0 && FFT_N4096 == 6 &&
    RIRI == 0 && RIRI_OUT == 0 && FFT_MODE == 0 && IFFT_MODE == 1, "Pinned FFT ioctl ABI drift");

void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
void park(void *) { const timespec delay{0, 10000000}; (void)nanosleep(&delay, nullptr); }
int clock_ms(void *, uint64_t *ms)
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec < 0 || now.tv_nsec < 0 ||
        now.tv_nsec >= 1000000000L || uint64_t(now.tv_sec) > (UINT64_MAX - 999) / 1000) return -EIO;
    *ms = uint64_t(now.tv_sec) * 1000 + uint64_t(now.tv_nsec) / 1000000;
    return *ms ? 0 : -EIO;
}
int ready(void *context)
{
    uint64_t now;
    if (clock_ms(nullptr, &now)) return -EIO;
    return tdvp_cpu1_ai_owner_check(static_cast<tdvp_ai_owner_view *>(context), now);
}
extern "C" int __real_poll(struct pollfd *, nfds_t, int);
int real_poll(void *, struct pollfd *fds, nfds_t count, int timeout)
{ return __real_poll(fds, count, timeout); }
tdvp_cpu1_ai_guard_ops executor_ops{clock_ms, ready, real_poll, park, &executor_owner};
tdvp_cpu1_ai_guard_ops supervisor_ops{clock_ms, ready, real_poll, park, &supervisor_owner};
[[noreturn]] void failed(int error)
{
    (void)tdvp_cpu1_ai_guard_fail(&guard, error);
    for (;;) park(nullptr); // No unwinding/freeing after uncertain completion.
}
void check()
{
    int error = tdvp_cpu1_ai_guard_check(&guard, &executor_ops);
    if (error) failed(error);
}
template<class T> T value(result<T> r)
{ if (r.is_err()) failed(-EIO); return std::move(r).unwrap(); }
void checked(result<void> r) { if (r.is_err()) failed(-EIO); }
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
void execute_kpu()
{
    check();
    control->cpu1_side.kpu_stage = 1; fence(); /* load/validate model */
    // Linux cannot supply a model, code address, register value or DMA pointer.
    // The two explicit input tensors also keep streaming state client-owned.
    for (size_t offset = 0; offset < TDVP_AI_KWS_INPUT_BYTES; offset += sizeof(float)) {
        float number; std::memcpy(&number, input + offset, sizeof(number));
        if (!std::isfinite(number)) failed(-EILSEQ);
    }
    const dims_t in_shapes[2]{{1, 30, 40}, {1, 256, 105}};
    const dims_t out_shapes[2]{{1, 30, 2}, {1, 256, 105}};
    const size_t in_bytes[2]{4800, 107520}, out_bytes[2]{240, 107520};
    if (!kws) {
        kws = new retained_kpu_job;
        const size_t model_bytes = uintptr_t(tdvp_cpu1_kws_model_end) - uintptr_t(tdvp_cpu1_kws_model_start);
        if (model_bytes != 369560) failed(-EPROTO);
        // The default span overload pins the ELF's virtual address. Its shared
        // attach path may return that address as GNNE text. copy_buffer=true
        // uses the official stream path: shared sections are allocated in MMZ,
        // flushed and represented by physical addresses before any KPU start.
        checked(kws->model.load_model({reinterpret_cast<const gsl::byte *>(tdvp_cpu1_kws_model_start), model_bytes}, true));
        check();
        if (kws->model.inputs_size() != 2 || kws->model.outputs_size() != 2) failed(-EPROTO);
        uintptr_t addresses[4]; size_t sizes[4];
        for (size_t i = 0; i < 2; ++i) {
            if (kws->model.input_shape(i) != in_shapes[i] || kws->model.output_shape(i) != out_shapes[i] ||
                kws->model.input_desc(i).datatype != dt_float32 ||
                kws->model.output_desc(i).datatype != dt_float32) failed(-EPROTO);
            kws->inputs[i] = value(hrt::create(dt_float32, in_shapes[i], hrt::pool_shared)); check();
            kws->outputs[i] = value(hrt::create(dt_float32, out_shapes[i], hrt::pool_shared)); check();
            addresses[i] = physical(kws->inputs[i], in_bytes[i]); sizes[i] = in_bytes[i];
            addresses[i + 2] = physical(kws->outputs[i], out_bytes[i]); sizes[i + 2] = out_bytes[i];
            checked(kws->model.input_tensor(i, kws->inputs[i]));
            checked(kws->model.output_tensor(i, kws->outputs[i]));
        }
        for (size_t i = 0; i < 4; ++i)
            for (size_t j = i + 1; j < 4; ++j)
                if (!(addresses[i] + sizes[i] <= addresses[j] || addresses[j] + sizes[j] <= addresses[i]))
                    failed(-ERANGE);
    }
    size_t offset = 0;
    control->cpu1_side.kpu_stage = 2; fence(); /* tensors */
    for (size_t i = 0; i < 2; ++i) {
        auto mapped = value(hrt::map(kws->inputs[i], map_access_t::map_write));
        if (mapped.buffer().size() != in_bytes[i]) failed(-EPROTO);
        std::memcpy(mapped.buffer().data(), input + offset, in_bytes[i]); offset += in_bytes[i];
        checked(mapped.unmap());
        checked(hrt::sync(kws->inputs[i], sync_op_t::sync_write_back, true)); check();
        auto cleared = value(hrt::map(kws->outputs[i], map_access_t::map_write));
        if (cleared.buffer().size() != out_bytes[i]) failed(-EPROTO);
        std::memset(cleared.buffer().data(), 0xa5, out_bytes[i]);
        checked(cleared.unmap());
        checked(hrt::sync(kws->outputs[i], sync_op_t::sync_write_back, true)); check();
    }
    const uint32_t starts = kpu_starts, completions = kpu_completions;
    if (kpu_inflight || starts != completions) failed(-EPROTO);
    checked(kws->model.run()); check();
    if (kpu_inflight || kpu_starts <= starts || kpu_starts != kpu_completions) failed(-EPROTO);
    offset = 0;
    control->cpu1_side.kpu_stage = 6; fence(); /* output validation */
    for (size_t i = 0; i < 2; ++i) {
        checked(hrt::sync(kws->outputs[i], sync_op_t::sync_invalidate, true)); check();
        auto mapped = value(hrt::map(kws->outputs[i], map_access_t::map_read));
        if (mapped.buffer().size() != out_bytes[i]) failed(-EPROTO);
        for (size_t n = 0; n < out_bytes[i]; n += sizeof(float)) {
            float number; std::memcpy(&number, mapped.buffer().data() + n, sizeof(number));
            if (!std::isfinite(number)) failed(-EILSEQ);
        }
        check(); std::memcpy(output + offset, mapped.buffer().data(), out_bytes[i]); offset += out_bytes[i];
        checked(mapped.unmap()); check();
    }
    fence(); response.output_bytes = uint32_t(offset);
    response.hardware_starts = kpu_starts - starts;
    response.hardware_completions = kpu_completions - completions;
    control->cpu1_side.kpu_stage = 7; fence();
    if (tdvp_cpu1_ai_guard_finish(&guard, &executor_ops)) failed(-EIO);
}
void execute_fft()
{
    check();
    const size_t bytes = tdvp_ai_output_bytes(&request);
    unsigned int point = 0;
    for (unsigned int n = 64; n < request.input_width; n <<= 1) ++point;
    fft_retained = new k_fft_args_st{};
    fft_retained->reg.cfg_value = point | ((request.flags & TDVP_AI_FFT_INVERSE) ? 8U : 0U) |
        (request.flags & TDVP_AI_FFT_SHIFT_MASK);
    std::memcpy(fft_retained->data, input, request.input_bytes);
    check();
    int fd = open("/dev/fft_device", O_RDWR | O_CLOEXEC);
    if (fd < 0) failed(-EIO);
    check();
    // The pinned PIO driver returns only after FIFO output, IRQ completion and
    // FFT-local disable. It never transfers using Linux's system SDMA.
    if (ioctl(fd, KD_IOC_CMD_FFT_IFFT, fft_retained)) failed(-EIO);
    check();
    std::memcpy(output, fft_retained->data, bytes); fence();
    if (close(fd)) failed(-EIO);
    check();
    delete fft_retained; fft_retained = nullptr;
    response.output_bytes = uint32_t(bytes);
    if (tdvp_cpu1_ai_guard_finish(&guard, &executor_ops)) failed(-EIO);
}
void execute_one()
{
    if (request.operation == TDVP_AI_KPU) { execute_kpu(); return; }
    if (request.operation == TDVP_AI_FFT) { execute_fft(); return; }
    check();
    const size_t bytes = size_t(request.output_width) * request.output_height * 3;
    dims_t in_shape{1, 3, request.input_height, request.input_width};
    dims_t out_shape{1, 3, request.output_height, request.output_width};
    retained = new retained_job;
    retained->input = value(hrt::create(dt_uint8, in_shape, hrt::pool_shared)); check();
    retained->output = value(hrt::create(dt_uint8, out_shape, hrt::pool_shared)); check();
    uintptr_t in = physical(retained->input, request.input_bytes), out = physical(retained->output, bytes);
    if (!(in + request.input_bytes <= out || out + bytes <= in)) failed(-ERANGE);
    auto mapped_input = value(hrt::map(retained->input, map_access_t::map_write));
    std::memcpy(mapped_input.buffer().data(), input, request.input_bytes);
    checked(mapped_input.unmap());
    checked(hrt::sync(retained->input, sync_op_t::sync_write_back, true)); check();
    auto mapped_output = value(hrt::map(retained->output, map_access_t::map_write));
    std::memset(mapped_output.buffer().data(), 0xa5, bytes);
    checked(mapped_output.unmap());
    checked(hrt::sync(retained->output, sync_op_t::sync_write_back, true)); check();
    ai2d_datatype_t dtype{ai2d_format::NCHW_FMT, ai2d_format::NCHW_FMT, dt_uint8, dt_uint8};
    ai2d_crop_param_t crop{request.crop_x || request.crop_y || request.crop_width != request.input_width ||
        request.crop_height != request.input_height, int(request.crop_x), int(request.crop_y),
        int(request.crop_width), int(request.crop_height)};
    ai2d_shift_param_t shift{};
    ai2d_pad_param_t pad{bool(request.pad_left || request.pad_right || request.pad_top || request.pad_bottom),
        {{0, 0}, {0, 0}, {int(request.pad_top), int(request.pad_bottom)}, {int(request.pad_left), int(request.pad_right)}},
        ai2d_pad_mode::constant, {int(request.pad_value[0]), int(request.pad_value[1]), int(request.pad_value[2])}};
    ai2d_resize_param_t resize{}; ai2d_affine_param_t affine{};
    retained->builder = new ai2d_builder(in_shape, out_shape, dtype, crop, shift, pad, resize, affine);
    checked(retained->builder->build_schedule()); check();
    checked(retained->builder->invoke(retained->input, retained->output)); check();
    checked(hrt::sync(retained->output, sync_op_t::sync_invalidate, true));
    auto result_map = value(hrt::map(retained->output, map_access_t::map_read));
    check(); std::memcpy(output, result_map.buffer().data(), bytes); fence();
    checked(result_map.unmap()); check();
    // Only real completion reaches destruction. Faults above park in place.
    delete retained->builder; retained->builder = nullptr;
    delete retained; retained = nullptr;
    response.output_bytes = uint32_t(bytes);
    if (tdvp_cpu1_ai_guard_finish(&guard, &executor_ops)) failed(-EIO);
}
void *execute(void *)
{
    unsigned int seen = 0;
    executing_ai = true;
    for (;;) {
        unsigned int next = __atomic_load_n(&generation, __ATOMIC_ACQUIRE);
        if (next == seen) { park(nullptr); continue; }
        seen = next;
        try { execute_one(); }
        catch (...) { failed(-EFAULT); }
    }
}
void publish_fault(int error)
{
    if (control->cpu1_side.state == TDVP_AI_FAULT) return;
    control->cpu1_side.fault = error < 0 ? error : -EIO;
    fence(); control->cpu1_side.state = TDVP_AI_FAULT; fence();
}
void *supervise(void *)
{
    uint64_t released = 0;
    for (;;) {
        uint64_t now = 0;
        ++control->cpu1_side.heartbeat; fence();
        if (control->cpu1_side.state == TDVP_AI_FAULT) { park(nullptr); continue; }
        int error = clock_ms(nullptr, &now);
        if (!error) error = tdvp_cpu1_ai_owner_check(&supervisor_owner, now);
        if (!error) error = tdvp_ai_job_tick(&job, now, supervisor_owner.owner_cookie, supervisor_owner.peer_cookie, 1);
        if (error) { (void)tdvp_cpu1_ai_guard_fail(&guard, error); publish_fault(error); park(nullptr); continue; }
        const volatile auto &peer = control->linux_side;
        if (!connected && (peer.magic != TDVP_AI_MAGIC || peer.owner_cookie != job.owner_cookie ||
                           peer.peer_cookie != job.peer_cookie)) { park(nullptr); continue; }
        if (peer.magic != TDVP_AI_MAGIC || peer.version != TDVP_AI_VERSION || peer.bytes != sizeof(*control) ||
            peer.owner_cookie != job.owner_cookie || peer.peer_cookie != job.peer_cookie) {
            (void)tdvp_cpu1_ai_guard_fail(&guard, -EPIPE); publish_fault(-EPIPE); park(nullptr); continue;
        }
        connected = true;
        uint64_t ack = peer.released, submitted = peer.submitted; fence();
        if (ack < released || ack > control->cpu1_side.completed || submitted < job.last_id ||
            (submitted > job.last_id && (job.last_id == UINT64_MAX || submitted != job.last_id + 1))) {
            (void)tdvp_cpu1_ai_guard_fail(&guard, -EPROTO); publish_fault(-EPROTO); park(nullptr); continue;
        }
        if (job.state == TDVP_AI_JOB_ACTIVE) {
            int status = tdvp_cpu1_ai_guard_status(&guard);
            if (status == TDVP_AI_ACTIVE) (void)tdvp_cpu1_ai_guard_check(&guard, &supervisor_ops);
            status = tdvp_cpu1_ai_guard_status(&guard);
            if (status < 0) { publish_fault(status); park(nullptr); continue; }
            if (status == TDVP_AI_DONE) {
                error = tdvp_ai_job_complete(&job, &job.ticket, now, 1, 0, response.output_bytes);
                if (error) { publish_fault(error); park(nullptr); continue; }
                response.duration_ms = now - guard.started_ms;
                std::memcpy((void *)&control->response, &response, sizeof(response)); fence();
                control->cpu1_side.state = TDVP_AI_RESULT; fence();
                control->cpu1_side.completed = job.ticket.id; fence();
            }
        }
        if (job.state == TDVP_AI_JOB_RESULT && ack == job.ticket.id) {
            error = tdvp_ai_job_ack(&job, &job.ticket);
            if (error) { publish_fault(error); park(nullptr); continue; }
            released = ack; control->cpu1_side.state = TDVP_AI_STATE_IDLE; fence();
        }
        if (job.state == TDVP_AI_JOB_READY && submitted > job.last_id) {
            if (ack != job.last_id) { publish_fault(-EPROTO); park(nullptr); continue; }
            std::memcpy(&request, (const void *)&control->request, sizeof(request)); fence();
            if (submitted != peer.submitted || tdvp_ai_validate_request(&request) || request.id != submitted ||
                request.owner_cookie != job.owner_cookie || request.peer_cookie != job.peer_cookie || !request.client_cookie) {
                publish_fault(-EPROTO); park(nullptr); continue;
            }
            tdvp_ai_job_request work{request.operation, request.input_bytes, request.output_capacity, request.budget_ms};
            tdvp_ai_job_ticket ticket{};
            error = tdvp_ai_job_submit(&job, request.client_cookie, &work, now, &ticket);
            if (!error && ticket.id != submitted) error = -EPROTO;
            if (!error) error = tdvp_ai_job_start(&job, &ticket, now);
            if (!error) error = tdvp_cpu1_ai_guard_begin(&guard, now, request.budget_ms);
            if (error || generation == UINT32_MAX) { publish_fault(error ? error : -EOVERFLOW); park(nullptr); continue; }
            response = {};
            response.owner_cookie = ticket.owner_cookie; response.peer_cookie = ticket.peer_cookie;
            response.client_cookie = ticket.client_cookie; response.id = ticket.id;
            response.operation = request.operation; response.output_width = request.output_width;
            response.output_height = request.output_height; response.format = request.format;
            executor_owner = supervisor_owner;
            control->cpu1_side.accepted = submitted; control->cpu1_side.state = TDVP_AI_RUNNING; fence();
            __atomic_add_fetch(&generation, 1U, __ATOMIC_RELEASE);
        }
        park(nullptr);
    }
}
int launch(pthread_t *thread, void *(*entry)(void *))
{
    pthread_attr_t attr;
    int result = pthread_attr_init(&attr);
    if (result) return -result;
    result = pthread_attr_setstacksize(&attr, 256 * 1024);
    if (!result) result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (!result) result = pthread_create(thread, &attr, entry, nullptr);
    (void)pthread_attr_destroy(&attr);
    return -result;
}
}

extern "C" int __real_open(const char *, int, ...);
extern "C" int __real_close(int);
/* This C symbol is exported by the hash-pinned gnne.c object. The SDK header
 * exposes the enum but not the helper declaration. Its actual RISC-V body is
 * a single 64-bit control write, unlike gnne_disable's unbounded busy wait. */
extern "C" void gnne_ctrl_set(gnne_ctrl_function_t);
namespace {
int kpu_status(void *, uint64_t *status)
{
    static_assert(sizeof(gnne_status) == 16 && GNNE_STATUS_IDLE == 0 &&
        GNNE_RESET_STATUS_NORMAL == 0 && GNNE_EXCEPTION_OK == 0 &&
        uint64_t(GNNE_CTRL_ENABLE_CLEAR) == (UINT64_C(1) << 32), "Pinned GNNE control ABI drift");
    fence(); *status = gnne_get_status().data[0]; fence();
    control->cpu1_side.kpu_status_lo = uint32_t(*status);
    control->cpu1_side.kpu_status_hi = uint32_t(*status >> 32); fence();
    return 0;
}
int kpu_disable(void *)
{
    fence(); gnne_ctrl_set(GNNE_CTRL_ENABLE_CLEAR); fence();
    return 0;
}
}
extern "C" void __wrap_gnne_init()
{
    // No raw gnne_init/gnne_disable call: either one would enter the old loop.
    if (!executing_ai || request.operation != TDVP_AI_KPU || kpu_fd < 0 || kpu_inflight) failed(-EPERM);
    control->cpu1_side.kpu_stage = 3; fence(); /* bounded initialization */
    const tdvp_cpu1_kpu_init_ops hardware{kpu_status, kpu_disable, nullptr};
    int error = tdvp_cpu1_kpu_prepare(&guard, &executor_ops, &hardware);
    if (error) failed(error); // Retain the entire executor stack and model.
}
extern "C" int __real_gnne_enable(uint64_t, uint64_t, uint64_t);
extern "C" int __wrap_gnne_enable(uint64_t start, uint64_t end, uint64_t breakpoint)
{
    if (!executing_ai || request.operation != TDVP_AI_KPU || kpu_fd < 0 || kpu_inflight ||
        kpu_starts != kpu_completions || kpu_starts == UINT32_MAX) failed(-EPROTO);
    check();
    control->cpu1_side.kpu_code_start = uint32_t(start);
    control->cpu1_side.kpu_code_end = uint32_t(end);
    control->cpu1_side.kpu_stage = 4; fence(); /* submission */
    // nncase's pinned code uses MMZ for executable KPU text. Never truncate a
    // userspace virtual pointer or permit Linux/display memory as KPU code.
    if (start < TDVP_VISION_MMZ_BASE || end <= start ||
        end > TDVP_VISION_MMZ_BASE + TDVP_VISION_MMZ_SIZE || breakpoint) failed(-ERANGE);
    // Drain an old software event before any new hardware submission. A stream
    // of interrupts is an error, not a reason to reset a shared subsystem.
    bool drained = false;
    for (unsigned int attempt = 0; attempt < 4; ++attempt) {
        pollfd old{kpu_fd, POLLIN, 0};
        int result = __real_poll(&old, 1, 0); check();
        if (!result && !old.revents) { drained = true; break; }
        if (result != 1 || (old.revents != POLLIN && old.revents != TDVP_AI_RTSMART_POLLIN)) failed(-EIO);
    }
    if (!drained) failed(-ESTALE);
    uint64_t status;
    if (kpu_status(nullptr, &status) ||
        (status & (TDVP_KPU_WORK_MASK | TDVP_KPU_RESET_MASK | TDVP_KPU_EXCEPTION_MASK | TDVP_KPU_AXI_ERROR)))
        failed(-EIO);
    check(); kpu_inflight = true;
    if (__real_gnne_enable(start, end, breakpoint)) failed(-EIO);
    ++kpu_starts;
    control->cpu1_side.kpu_starts = kpu_starts; fence();
    return 0;
}
extern "C" int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0; bool has_mode = flags & O_CREAT;
#ifdef O_TMPFILE
    has_mode = has_mode || (flags & O_TMPFILE) == O_TMPFILE;
#endif
    if (has_mode) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
    if (executing_ai && !std::strcmp(path, "/dev/gnne_device") && request.operation != TDVP_AI_KPU) {
        errno = EPERM; return -1;
    }
    int fd = has_mode ? __real_open(path, flags, mode) : __real_open(path, flags);
    if (executing_ai && fd >= 0 && !std::strcmp(path, "/dev/ai_2d_device")) {
        if (tdvp_cpu1_ai_guard_status(&guard) != TDVP_AI_ACTIVE || ai2d_fd >= 0) failed(-EBUSY);
        ai2d_fd = fd;
    }
    if (executing_ai && fd >= 0 && !std::strcmp(path, "/dev/gnne_device")) {
        if (tdvp_cpu1_ai_guard_status(&guard) != TDVP_AI_ACTIVE || kpu_fd >= 0) failed(-EBUSY);
        kpu_fd = fd;
    }
    return fd;
}
extern "C" int __wrap_close(int fd)
{
    if (executing_ai && fd == kpu_fd && kpu_inflight) failed(-EBUSY);
    int result = __real_close(fd);
    if (executing_ai && fd == ai2d_fd) { if (result) failed(-EIO); ai2d_fd = -1; }
    if (executing_ai && fd == kpu_fd) { if (result) failed(-EIO); kpu_fd = -1; }
    return result;
}
extern "C" int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    if (executing_ai && fds && count == 1 && kpu_fd >= 0 && fds[0].fd == kpu_fd) {
        if (!kpu_inflight || request.operation != TDVP_AI_KPU) failed(-EPROTO);
        control->cpu1_side.kpu_stage = 5; fence(); /* real completion event */
        int result = tdvp_cpu1_ai_guard_wait(&guard, &executor_ops, kpu_fd, fds, count);
        const tdvp_cpu1_kpu_init_ops hardware{kpu_status, nullptr, nullptr};
        int error = tdvp_cpu1_kpu_complete(&guard, &executor_ops, &hardware, result);
        if (error) failed(error);
        kpu_inflight = false; ++kpu_completions;
        control->cpu1_side.kpu_completions = kpu_completions; fence();
        return result;
    }
    return executing_ai ? tdvp_cpu1_ai_guard_wait(&guard, &executor_ops, ai2d_fd, fds, count) :
        __real_poll(fds, count, timeout);
}
extern "C" int tdvp_cpu1_ai_service_start(void *vision_control, void *ownership,
                                         uint64_t owner_cookie, uint64_t peer_cookie)
{
    if (attempted || !vision_control || !ownership || !owner_cookie || !peer_cookie) return -EINVAL;
    attempted = true;
    control = reinterpret_cast<volatile tdvp_ai_control *>(static_cast<unsigned char *>(vision_control) +
        (TDVP_AI_CONTROL_BASE - TDVP_VISION_CONTROL_BASE));
    control->cpu1_side.magic = 0; fence();
    std::memset((void *)&control->cpu1_side, 0, sizeof(control->cpu1_side));
    std::memset((void *)&control->response, 0, sizeof(control->response));
    control->cpu1_side.version = TDVP_AI_VERSION; control->cpu1_side.bytes = sizeof(*control);
    control->cpu1_side.owner_cookie = owner_cookie; control->cpu1_side.peer_cookie = peer_cookie;
    control->cpu1_side.heartbeat = 1; control->cpu1_side.state = TDVP_AI_STATE_IDLE;
    control->cpu1_side.capabilities = TDVP_AI_CAPABILITIES;
    uint64_t now = 0;
    int error = clock_ms(nullptr, &now);
    supervisor_owner = {ownership, owner_cookie, peer_cookie, 0, now};
    if (!error) error = tdvp_cpu1_ai_owner_check(&supervisor_owner, now);
    input = static_cast<unsigned char *>(tdvp_cpu1_ai_map_buffer(0));
    output = static_cast<unsigned char *>(tdvp_cpu1_ai_map_buffer(1));
    if (!input || input == MAP_FAILED || !output || output == MAP_FAILED) error = -ENOMEM;
    if (!error) error = tdvp_ai_job_init(&job, owner_cookie, peer_cookie, TDVP_AI_CAPABILITIES, now);
    pthread_t executor, supervisor;
    if (!error) error = launch(&executor, execute);
    if (!error) error = launch(&supervisor, supervise);
    if (error) publish_fault(error);
    fence(); control->cpu1_side.magic = TDVP_AI_MAGIC; fence();
    std::printf("TDVP CPU1 AI: asynchronous AI2D/FFT/KPU-KWS service %s (%d)\n", error ? "fault" : "started", error);
    return error;
}
