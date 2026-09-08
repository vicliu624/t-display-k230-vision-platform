// SPDX-License-Identifier: MIT
// Boot-lifetime supervisor + executor. Only AI2D is registered in this version.
// KPU model execution is deliberately NOT implied by resource ownership READY.
#define _POSIX_C_SOURCE 200809L
#include "tdvp_ai_abi.h"
#include "tdvp_ai_job.h"
#include "tdvp_cpu1_ai_guard.h"
#include "tdvp_cpu1_ai_service.h"
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
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef BUILDING_RUNTIME
#error "Pinned AI2D requires the runtime object layout"
#endif
using namespace nncase;
using namespace nncase::runtime;
using namespace nncase::runtime::k230;
using nncase::F::k230::ai2d_builder;
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
struct retained_job { runtime_tensor input, output; ai2d_builder *builder{}; };
retained_job *retained; // Root remains alive if execution blocks or fails.

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
void execute_one()
{
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
extern "C" int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0; bool has_mode = flags & O_CREAT;
#ifdef O_TMPFILE
    has_mode = has_mode || (flags & O_TMPFILE) == O_TMPFILE;
#endif
    if (has_mode) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
    if (executing_ai && !std::strcmp(path, "/dev/gnne_device")) { errno = EPERM; return -1; }
    int fd = has_mode ? __real_open(path, flags, mode) : __real_open(path, flags);
    if (executing_ai && fd >= 0 && !std::strcmp(path, "/dev/ai_2d_device")) {
        if (tdvp_cpu1_ai_guard_status(&guard) != TDVP_AI_ACTIVE || ai2d_fd >= 0) failed(-EBUSY);
        ai2d_fd = fd;
    }
    return fd;
}
extern "C" int __wrap_close(int fd)
{
    int result = __real_close(fd);
    if (executing_ai && fd == ai2d_fd) { if (result) failed(-EIO); ai2d_fd = -1; }
    return result;
}
extern "C" int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout)
{
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
    control->cpu1_side.capabilities = TDVP_AI_CAP_AI2D;
    uint64_t now = 0;
    int error = clock_ms(nullptr, &now);
    supervisor_owner = {ownership, owner_cookie, peer_cookie, 0, now};
    if (!error) error = tdvp_cpu1_ai_owner_check(&supervisor_owner, now);
    input = static_cast<unsigned char *>(tdvp_cpu1_ai_map_buffer(0));
    output = static_cast<unsigned char *>(tdvp_cpu1_ai_map_buffer(1));
    if (!input || input == MAP_FAILED || !output || output == MAP_FAILED) error = -ENOMEM;
    if (!error) error = tdvp_ai_job_init(&job, owner_cookie, peer_cookie, TDVP_AI_CAP_AI2D, now);
    pthread_t executor, supervisor;
    if (!error) error = launch(&executor, execute);
    if (!error) error = launch(&supervisor, supervise);
    if (error) publish_fault(error);
    fence(); control->cpu1_side.magic = TDVP_AI_MAGIC; fence();
    std::printf("TDVP CPU1 AI: asynchronous AI2D service %s (%d); KPU/FFT jobs unavailable\n", error ? "fault" : "started", error);
    return error;
}
