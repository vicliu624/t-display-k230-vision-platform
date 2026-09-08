/* SPDX-License-Identifier: MIT */
#include "tdvp_cpu1_capture.h"
#include "tdvp_cpu1_vision_layout.h"
#include "tdvp_cpu1_capture_trace.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_vicap_api.h"

#define CAPTURE_DEVICE VICAP_DEV_ID_0
#define CAPTURE_CHANNEL VICAP_CHN_ID_0
#define CAPTURE_BUFFERS 6U
#define CAPTURE_DUMP_TIMEOUT_MS 200

static void capture_progress(struct tdvp_cpu1_capture *capture, uint32_t stage)
{
    if (!capture->trace)
        return;
    capture->trace[0] = TDVP_CAPTURE_TRACE_VERSION;
    capture->trace[1] = stage;
#ifdef __riscv
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

static int record_fault(struct tdvp_cpu1_capture *capture, const char *stage, int code)
{
    fprintf(stderr, "TDVP CPU1 capture: %s failed: %d\n", stage, code);
    if (!capture->fault)
        capture->fault = code;
    return code;
}

int tdvp_cpu1_capture_stop(struct tdvp_cpu1_capture *capture)
{
    int result;

    if (!capture)
        return -EINVAL;
    capture->running = 0;
    /* Do NOT deinit/release a pool after a failed stop: DMA may still own it. */
    if (capture->stream_attempted) {
        capture_progress(capture, TDVP_CAPTURE_STOP_STREAM);
        result = kd_mpi_vicap_stop_stream(CAPTURE_DEVICE);
        if (result)
            return record_fault(capture, "stop-stream (pool retained)", result);
        capture->stream_attempted = 0;
    }
    if (capture->vicap_attempted) {
        capture_progress(capture, TDVP_CAPTURE_VICAP_DEINIT);
        result = kd_mpi_vicap_deinit(CAPTURE_DEVICE);
        if (result)
            return record_fault(capture, "deinit (pool retained)", result);
        capture->vicap_attempted = 0;
    }
    if (capture->vb_ready) {
        capture_progress(capture, TDVP_CAPTURE_VB_EXIT);
        result = kd_mpi_vb_exit();
        if (result)
            return record_fault(capture, "vb-exit", result);
        capture->vb_ready = 0;
    }
    if (!capture->fault)
        capture_progress(capture, TDVP_CAPTURE_STOPPED);
    return capture->fault;
}

int tdvp_cpu1_capture_start(struct tdvp_cpu1_capture *capture)
{
    k_vicap_dev_attr device;
    k_vicap_chn_attr channel;
    k_vb_config buffers;
    const char *stage = "sensor-info";
    int result;

    if (!capture || capture->attempted)
        return -EINVAL;
    capture->attempted = 1;
    memset(&device, 0, sizeof(device));
    memset(&channel, 0, sizeof(channel));
    memset(&buffers, 0, sizeof(buffers));
    capture_progress(capture, TDVP_CAPTURE_SENSOR_INFO);
    result = kd_mpi_vicap_get_sensor_info(
        GC2093_MIPI_CSI2_1920X1080_30FPS_10BIT_LINEAR, &device.sensor_info);
    if (result)
        goto failed;
    if (device.sensor_info.width != TDVP_CAPTURE_WIDTH ||
        device.sensor_info.height != TDVP_CAPTURE_HEIGHT ||
        device.sensor_info.csi_num != VICAP_CSI2) {
        result = -EPROTO;
        goto failed;
    }

    buffers.max_pool_cnt = 1;
    buffers.comm_pool[0].blk_size = VICAP_ALIGN_UP(TDVP_CAPTURE_FRAME_BYTES, VICAP_ALIGN_1K);
    buffers.comm_pool[0].blk_cnt = CAPTURE_BUFFERS;
    buffers.comm_pool[0].mode = VB_REMAP_MODE_NOCACHE;
    stage = "vb-config";
    capture_progress(capture, TDVP_CAPTURE_VB_CONFIG);
    result = kd_mpi_vb_set_config(&buffers);
    if (result)
        goto failed;
    stage = "vb-init";
    capture_progress(capture, TDVP_CAPTURE_VB_INIT);
    result = kd_mpi_vb_init();
    if (result)
        goto failed;
    capture->vb_ready = 1;

    device.acq_win.width = TDVP_CAPTURE_WIDTH;
    device.acq_win.height = TDVP_CAPTURE_HEIGHT;
    device.mode = VICAP_WORK_ONLINE_MODE;
    device.input_type = VICAP_INPUT_TYPE_SENSOR;
    device.pipe_ctrl.data = 0x0fffffffU;
    device.pipe_ctrl.bits.af_enable = 0;
    device.pipe_ctrl.bits.ahdr_enable = 0;
    device.pipe_ctrl.bits.dnr3_enable = 0;
    device.dw_enable = K_FALSE;
    stage = "device-attributes";
    capture_progress(capture, TDVP_CAPTURE_DEVICE_ATTRIBUTES);
    result = kd_mpi_vicap_set_dev_attr(CAPTURE_DEVICE, device);
    if (result)
        goto failed;
    /* ROMFS firmware must not depend on an SD card holding ISP XML files. */
    stage = "isp-database-header";
    capture_progress(capture, TDVP_CAPTURE_ISP_DATABASE);
    result = kd_mpi_vicap_set_database_parse_mode(CAPTURE_DEVICE, VICAP_DATABASE_PARSE_HEADER);
    if (result)
        goto failed;
    channel.out_win.width = TDVP_CAPTURE_WIDTH;
    channel.out_win.height = TDVP_CAPTURE_HEIGHT;
    channel.crop_win = channel.out_win;
    channel.scale_win = channel.out_win;
    channel.chn_enable = K_TRUE;
    channel.pix_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    channel.buffer_num = CAPTURE_BUFFERS;
    channel.buffer_size = buffers.comm_pool[0].blk_size;
    channel.fps = 30;
    capture_progress(capture, TDVP_CAPTURE_DUMP_RESERVED);
    kd_mpi_vicap_set_dump_reserved(CAPTURE_DEVICE, CAPTURE_CHANNEL, K_TRUE);
    stage = "channel-attributes";
    capture_progress(capture, TDVP_CAPTURE_CHANNEL_ATTRIBUTES);
    result = kd_mpi_vicap_set_chn_attr(CAPTURE_DEVICE, CAPTURE_CHANNEL, channel);
    if (result)
        goto failed;
    stage = "vicap-init";
    capture_progress(capture, TDVP_CAPTURE_VICAP_INIT);
    capture->vicap_attempted = 1;
    result = kd_mpi_vicap_init(CAPTURE_DEVICE);
    if (result)
        goto failed;
    stage = "start-stream";
    capture_progress(capture, TDVP_CAPTURE_START_STREAM);
    capture->stream_attempted = 1;
    result = kd_mpi_vicap_start_stream(CAPTURE_DEVICE);
    if (result)
        goto failed;
    capture->running = 1;
    capture_progress(capture, TDVP_CAPTURE_RUNNING);
    return 0;

failed:
    record_fault(capture, stage, result);
    (void)tdvp_cpu1_capture_stop(capture);
    return result;
}

static int plane_in_mmz(uint64_t physical, uint32_t stride, uint32_t rows)
{
    uint64_t bytes = (uint64_t)stride * rows;
    uint64_t limit = TDVP_VISION_MMZ_BASE + TDVP_VISION_MMZ_SIZE - 4096UL;

    return stride >= TDVP_CAPTURE_WIDTH && bytes <= TDVP_VISION_MMZ_SIZE &&
           physical >= TDVP_VISION_MMZ_BASE && physical < limit &&
           bytes <= limit - physical;
}

int tdvp_cpu1_capture_next(struct tdvp_cpu1_capture *capture,
                           tdvp_cpu1_frame_visitor visit, void *context)
{
    k_video_frame_info owned;
    struct tdvp_cpu1_frame frame;
    uint32_t lengths[2] = {0, 0};
    unsigned int plane;
    int result, cleanup;

    if (!capture || !capture->running || capture->fault || !visit)
        return -EINVAL;
    memset(&owned, 0, sizeof(owned));
    memset(&frame, 0, sizeof(frame));
    result = kd_mpi_vicap_dump_frame(CAPTURE_DEVICE, CAPTURE_CHANNEL, VICAP_DUMP_YUV,
                                    &owned, CAPTURE_DUMP_TIMEOUT_MS);
    /* No frame acquired on timeout/error; the caller bounds consecutive
     * failures and can service STOP/peer heartbeat between these calls. */
    if (result)
        return result;
    if (owned.v_frame.width != TDVP_CAPTURE_WIDTH ||
        owned.v_frame.height != TDVP_CAPTURE_HEIGHT ||
        owned.v_frame.pixel_format != PIXEL_FORMAT_YUV_SEMIPLANAR_420) {
        result = record_fault(capture, "frame-format", -EPROTO);
        goto release;
    }
    frame.width = owned.v_frame.width;
    frame.height = owned.v_frame.height;
    frame.pts = owned.v_frame.pts;
    for (plane = 0; plane < 2; ++plane) {
        uint32_t rows = frame.height >> plane;
        uint64_t physical = owned.v_frame.phys_addr[plane];
        frame.stride[plane] = owned.v_frame.stride[plane];
        if (!plane_in_mmz(physical, frame.stride[plane], rows)) {
            result = record_fault(capture, "frame-plane-outside-mmz", -ERANGE);
            goto release;
        }
        lengths[plane] = frame.stride[plane] * rows;
        /* Uncached mappings: do not create a cached alias of DMA-written VB. */
        frame.plane[plane] = kd_mpi_sys_mmap(physical, lengths[plane]);
        if (!frame.plane[plane] || frame.plane[plane] == MAP_FAILED) {
            frame.plane[plane] = NULL;
            result = record_fault(capture, "frame-map", -ENOMEM);
            goto release;
        }
    }
    result = visit(&frame, context);

release:
    for (plane = 0; plane < 2; ++plane) {
        if (!frame.plane[plane])
            continue;
        cleanup = kd_mpi_sys_munmap((void *)frame.plane[plane], lengths[plane]);
        if (cleanup) {
            record_fault(capture, "frame-unmap", cleanup);
            if (!result)
                result = cleanup;
        }
    }
    cleanup = kd_mpi_vicap_dump_release(CAPTURE_DEVICE, CAPTURE_CHANNEL, &owned);
    if (cleanup) {
        record_fault(capture, "frame-release", cleanup);
        if (!result)
            result = cleanup;
    }
    return result;
}
