/* SPDX-License-Identifier: MIT */
#ifndef TDVP_CPU1_CAPTURE_TRACE_H
#define TDVP_CPU1_CAPTURE_TRACE_H

/* Optional diagnostic extension in producer.reserved[0..3]: version, current
 * stage, first failing stage, raw first error (signed 32-bit MPI/errno code).
 * One CPU1 capture thread writes it, Linux never writes it. Each stage names
 * the call about to execute; RUNNING/STOPPED mean the whole operation returned
 * successfully. No pointers, timestamps or hardware addresses on the wire.
 * The raw error is published last and zero means no fault recorded. Cleanup
 * progress never overwrites the first failure. Readers not knowing version 2
 * continue to ignore the reserved words. No frame/control ABI size change.
 */
#define TDVP_CAPTURE_TRACE_VERSION 2U
#define TDVP_CAPTURE_TRACE_WORDS 4U
enum tdvp_capture_stage {
    TDVP_CAPTURE_SENSOR_INFO = 1,
    TDVP_CAPTURE_VB_CONFIG,
    TDVP_CAPTURE_VB_INIT,
    TDVP_CAPTURE_DEVICE_ATTRIBUTES,
    TDVP_CAPTURE_ISP_DATABASE,
    TDVP_CAPTURE_DUMP_RESERVED,
    TDVP_CAPTURE_CHANNEL_ATTRIBUTES,
    TDVP_CAPTURE_VICAP_INIT,
    TDVP_CAPTURE_START_STREAM,
    TDVP_CAPTURE_RUNNING,
    TDVP_CAPTURE_STOP_STREAM,
    TDVP_CAPTURE_VICAP_DEINIT,
    TDVP_CAPTURE_VB_EXIT,
    TDVP_CAPTURE_STOPPED
};
#endif
