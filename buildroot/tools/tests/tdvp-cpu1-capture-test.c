/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "tdvp_cpu1_capture.h"
#include "tdvp_cpu1_vision_layout.h"
#include "tdvp_cpu1_capture_trace.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_vicap_api.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

static int fail_stage, stage, wrong_sensor, dump_error, malformed, map_failure;
static int stop_error, deinit_error, unmap_error, release_error, visitor_error;
static int stops, deinits, exits, maps, unmaps, releases, visits;
static unsigned char y_plane[TDVP_CAPTURE_WIDTH * TDVP_CAPTURE_HEIGHT];
static unsigned char uv_plane[TDVP_CAPTURE_WIDTH * TDVP_CAPTURE_HEIGHT / 2];
static uint32_t progress[TDVP_CAPTURE_TRACE_WORDS];
static int check_progress;
static int clock_failure;
static uint64_t clock_us;

int clock_gettime(clockid_t id, struct timespec *now)
{
    assert(id == CLOCK_MONOTONIC);
    if (clock_failure == 1) return -1;
    if (clock_failure != 5 && clock_failure != 6) clock_us += 1000;
    now->tv_sec = (time_t)(clock_us / 1000000U);
    now->tv_nsec = (long)(clock_us % 1000000U) * 1000L;
    if (clock_failure == 2) now->tv_sec = -1;
    if (clock_failure == 3) now->tv_nsec = 1000000000L;
    if (clock_failure == 4) now->tv_sec = (time_t)(UINT64_MAX / 1000000U + 1);
    if (clock_failure == 6) now->tv_nsec -= 1000;
    if (clock_failure == 7) now->tv_sec = now->tv_nsec = 0;
    return 0;
}

static int step(void)
{
    const unsigned int expected[] = {TDVP_CAPTURE_SENSOR_INFO, TDVP_CAPTURE_VB_CONFIG,
        TDVP_CAPTURE_VB_INIT, TDVP_CAPTURE_DEVICE_ATTRIBUTES, TDVP_CAPTURE_ISP_DATABASE,
        TDVP_CAPTURE_CHANNEL_ATTRIBUTES, TDVP_CAPTURE_VICAP_INIT, TDVP_CAPTURE_START_STREAM};
    if (check_progress) {
        assert(stage < 8 && progress[0] == TDVP_CAPTURE_TRACE_VERSION);
        assert(progress[1] == expected[stage]);
    }
    return ++stage == fail_stage ? -EIO : 0;
}
static void reset(void)
{
    fail_stage = stage = wrong_sensor = dump_error = malformed = map_failure = 0;
    stop_error = deinit_error = unmap_error = release_error = visitor_error = 0;
    stops = deinits = exits = maps = unmaps = releases = visits = 0;
    clock_failure = 0;
    clock_us = 122456;
}

k_s32 kd_mpi_vicap_get_sensor_info(k_vicap_sensor_type type, k_vicap_sensor_info *info)
{
    assert(type == GC2093_MIPI_CSI2_1920X1080_30FPS_10BIT_LINEAR);
    info->width = TDVP_CAPTURE_WIDTH;
    info->height = TDVP_CAPTURE_HEIGHT;
    info->csi_num = wrong_sensor ? VICAP_CSI0 : VICAP_CSI2;
    return step();
}
k_s32 kd_mpi_vb_set_config(const k_vb_config *config)
{
    assert(config->max_pool_cnt == 1 && config->comm_pool[0].blk_cnt == 6);
    assert(config->comm_pool[0].blk_size >= TDVP_CAPTURE_FRAME_BYTES);
    assert(config->comm_pool[0].mode == VB_REMAP_MODE_NOCACHE);
    return step();
}
k_s32 kd_mpi_vb_init(void) { return step(); }
k_s32 kd_mpi_vicap_set_dev_attr(k_vicap_dev dev, k_vicap_dev_attr attr)
{
    assert(dev == 0 && attr.mode == VICAP_WORK_ONLINE_MODE);
    assert(!attr.dw_enable && !attr.pipe_ctrl.bits.af_enable);
    assert(attr.pipe_ctrl.bits.ae_enable && attr.pipe_ctrl.bits.awb_enable);
    return step();
}
k_s32 kd_mpi_vicap_set_database_parse_mode(k_vicap_dev dev, k_vicap_database_parse_mode mode)
{
    assert(dev == 0 && mode == VICAP_DATABASE_PARSE_XML_JSON);
    return step();
}
void kd_mpi_vicap_set_dump_reserved(k_vicap_dev dev, k_vicap_chn chn, k_bool reserved)
{
    assert(dev == 0 && chn == 0 && reserved);
}
k_s32 kd_mpi_vicap_set_chn_attr(k_vicap_dev dev, k_vicap_chn chn, k_vicap_chn_attr attr)
{
    assert(dev == 0 && chn == 0 && attr.fps == 30);
    assert(attr.pix_format == PIXEL_FORMAT_YUV_SEMIPLANAR_420);
    assert(attr.out_win.width == TDVP_CAPTURE_WIDTH && attr.out_win.height == TDVP_CAPTURE_HEIGHT);
    return step();
}
k_s32 kd_mpi_vicap_init(k_vicap_dev dev) { assert(dev == 0); return step(); }
k_s32 kd_mpi_vicap_start_stream(k_vicap_dev dev) { assert(dev == 0); return step(); }
k_s32 kd_mpi_vicap_stop_stream(k_vicap_dev dev) { assert(dev == 0); ++stops; return stop_error; }
k_s32 kd_mpi_vicap_deinit(k_vicap_dev dev) { assert(dev == 0); ++deinits; return deinit_error; }
k_s32 kd_mpi_vb_exit(void) { ++exits; return 0; }
k_s32 kd_mpi_vicap_dump_frame(k_vicap_dev dev, k_vicap_chn chn, k_vicap_dump_format format,
                             k_video_frame_info *owned, k_u32 timeout)
{
    assert(dev == 0 && chn == 0 && format == VICAP_DUMP_YUV && timeout == 200);
    if (dump_error)
        return dump_error;
    owned->v_frame.width = TDVP_CAPTURE_WIDTH;
    owned->v_frame.height = TDVP_CAPTURE_HEIGHT;
    owned->v_frame.pixel_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    owned->v_frame.stride[0] = owned->v_frame.stride[1] = TDVP_CAPTURE_WIDTH;
    owned->v_frame.phys_addr[0] = TDVP_VISION_MMZ_BASE;
    owned->v_frame.phys_addr[1] = TDVP_VISION_MMZ_BASE + sizeof(y_plane);
    owned->v_frame.pts = 0; /* Actual pinned board metadata, not our clock. */
    switch (malformed) {
    case 1: owned->v_frame.width--; break;
    case 2: owned->v_frame.pixel_format = PIXEL_FORMAT_RGB_888; break;
    case 3: owned->v_frame.stride[0] = 0; break;
    case 4: owned->v_frame.phys_addr[0] = TDVP_VISION_MMZ_BASE - 1; break;
    case 5: owned->v_frame.phys_addr[1] = TDVP_VISION_SHARED_BASE - 4096; break;
    case 6: owned->v_frame.phys_addr[1] = UINT64_MAX - 1; break;
    case 7: owned->v_frame.stride[1] = UINT32_MAX; break;
    case 8: owned->v_frame.stride[1] = 0; break; /* pinned cp_vb_info */
    case 9:
        owned->v_frame.stride[1] = 0;
        owned->v_frame.phys_addr[1] = TDVP_VISION_SHARED_BASE - 4096;
        break;
    case 10: owned->v_frame.stride[1] = TDVP_CAPTURE_WIDTH - 1; break;
    case 11:
        owned->v_frame.stride[1] = 0;
        owned->v_frame.phys_addr[1] = 0;
        break;
    }
    return 0;
}
void *kd_mpi_sys_mmap(k_u64 physical, k_u32 size)
{
    ++maps;
    if (maps == map_failure)
        return MAP_FAILED;
    if (physical == TDVP_VISION_MMZ_BASE) {
        assert(size == sizeof(y_plane));
        return y_plane;
    }
    assert(physical == TDVP_VISION_MMZ_BASE + sizeof(y_plane) && size == sizeof(uv_plane));
    return uv_plane;
}
k_s32 kd_mpi_sys_munmap(void *address, k_u32 size)
{
    assert((address == y_plane && size == sizeof(y_plane)) ||
           (address == uv_plane && size == sizeof(uv_plane)));
    ++unmaps;
    return unmap_error;
}
k_s32 kd_mpi_vicap_dump_release(k_vicap_dev dev, k_vicap_chn chn, const k_video_frame_info *owned)
{
    assert(dev == 0 && chn == 0 && owned);
    ++releases;
    return release_error;
}
static int visit(const struct tdvp_cpu1_frame *frame, void *context)
{
    assert(context == &visits);
    assert(frame->plane[0] == y_plane && frame->plane[1] == uv_plane);
    assert(frame->width == TDVP_CAPTURE_WIDTH && frame->height == TDVP_CAPTURE_HEIGHT);
    assert(frame->pts == 123456);
    ++visits;
    return visitor_error;
}

int main(void)
{
    struct tdvp_cpu1_capture capture;
    int test;

    reset();
    memset(&capture, 0, sizeof(capture));
    capture.trace = progress;
    check_progress = 1;
    assert(!tdvp_cpu1_capture_start(&capture));
    assert(progress[1] == TDVP_CAPTURE_RUNNING);
    assert(!tdvp_cpu1_capture_stop(&capture));
    assert(progress[1] == TDVP_CAPTURE_STOPPED);
    check_progress = 0;

    reset();
    memset(&capture, 0, sizeof(capture));
    memset(progress, 0, sizeof(progress));
    capture.trace = progress;
    fail_stage = 7;
    deinit_error = -EFAULT;
    assert(tdvp_cpu1_capture_start(&capture) == -EIO);
    assert(progress[0] == TDVP_CAPTURE_TRACE_VERSION);
    assert(progress[1] == TDVP_CAPTURE_VICAP_DEINIT);
    assert(progress[2] == TDVP_CAPTURE_VICAP_INIT && (int32_t)progress[3] == -EIO);
    assert(capture.vb_ready && !exits); /* cleanup failure retains DMA storage */

    reset();
    memset(&capture, 0, sizeof(capture));
    memset(progress, 0, sizeof(progress));
    capture.trace = progress;
    assert(!tdvp_cpu1_capture_start(&capture));
    stop_error = (int32_t)UINT32_C(0xa0158003);
    assert(tdvp_cpu1_capture_stop(&capture) == stop_error);
    assert(progress[2] == TDVP_CAPTURE_STOP_STREAM && progress[3] == UINT32_C(0xa0158003));
    assert(capture.vb_ready && !exits);

    for (test = 0; test <= 8; ++test) {
        reset();
        memset(&capture, 0, sizeof(capture));
        fail_stage = test;
        assert(tdvp_cpu1_capture_start(&capture) == (test ? -EIO : 0));
        assert(stage == (test ? test : 8));
        assert(tdvp_cpu1_capture_start(&capture) == -EINVAL);
        if (!test) {
            assert(capture.running);
            assert(!tdvp_cpu1_capture_next(&capture, visit, &visits));
            assert(visits == 1 && maps == 2 && unmaps == 2 && releases == 1);
            assert(!tdvp_cpu1_capture_stop(&capture));
            assert(stops == 1 && deinits == 1 && exits == 1);
        } else {
            assert(!capture.running && capture.fault == -EIO);
            assert(stops == (test == 8));
            assert(deinits == (test >= 7));
            assert(exits == (test > 3));
        }
        assert(tdvp_cpu1_capture_next(&capture, visit, &visits) == -EINVAL);
    }
    reset();
    memset(&capture, 0, sizeof(capture));
    wrong_sensor = 1;
    assert(tdvp_cpu1_capture_start(&capture) == -EPROTO && !exits && stage == 1);

    /* Frame validation, maps, visitor failure, dump timeout and release paths. */
    for (test = 1; test <= 13; ++test) {
        reset();
        memset(&capture, 0, sizeof(capture));
        assert(!tdvp_cpu1_capture_start(&capture));
        if (test <= 7) malformed = test;
        if (test == 8) map_failure = 1;
        if (test == 9) map_failure = 2;
        if (test == 10) visitor_error = -ENOSPC;
        if (test == 11) dump_error = -ETIMEDOUT;
        if (test == 12) unmap_error = -EIO;
        if (test == 13) release_error = -EIO;
        assert(tdvp_cpu1_capture_next(&capture, visit, &visits));
        assert(releases == (test != 11));
        assert(unmaps == maps - (map_failure ? 1 : 0));
        assert(visits == (test == 10 || test >= 12));
        (void)tdvp_cpu1_capture_stop(&capture);
        assert(exits == 1 && stops == 1 && deinits == 1);
    }
    for (test = 1; test <= 2; ++test) {
        reset();
        memset(&capture, 0, sizeof(capture));
        assert(!tdvp_cpu1_capture_start(&capture));
        if (test == 1) stop_error = -EIO;
        else deinit_error = -EIO;
        assert(tdvp_cpu1_capture_stop(&capture) == -EIO);
        assert(capture.vb_ready && !capture.running && !exits);
        assert(stops == 1 && deinits == (test == 2));
    }
    for (test = 8; test <= 11; ++test) {
        reset();
        memset(&capture, 0, sizeof(capture));
        assert(!tdvp_cpu1_capture_start(&capture));
        malformed = test;
        assert(tdvp_cpu1_capture_next(&capture, visit, &visits) ==
               (test == 8 ? 0 : -ERANGE));
        assert(visits == (test == 8) && releases == 1 && unmaps == maps);
        assert(tdvp_cpu1_capture_stop(&capture) == (test == 8 ? 0 : -ERANGE));
        assert(stops == 1 && deinits == 1 && exits == 1);
    }
    for (test = 1; test <= 7; ++test) {
        reset();
        memset(&capture, 0, sizeof(capture));
        assert(!tdvp_cpu1_capture_start(&capture));
        if (test == 5 || test == 6) {
            assert(!tdvp_cpu1_capture_next(&capture, visit, &visits));
            maps = unmaps = releases = visits = 0;
        }
        clock_failure = test;
        assert(tdvp_cpu1_capture_next(&capture, visit, &visits) == -EIO);
        assert(!visits && !maps && !unmaps && releases == 1);
        assert(tdvp_cpu1_capture_stop(&capture) == -EIO);
        assert(stops == 1 && deinits == 1 && exits == 1);
    }
    puts("CPU1 capture: PASS 36 lifecycle/fault cases and first-fault/raw-MPI metadata; no hardware or transport claim");
    return 0;
}
