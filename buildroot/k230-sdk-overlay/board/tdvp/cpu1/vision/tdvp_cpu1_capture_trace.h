/* SPDX-License-Identifier: MIT */
#ifndef TDVP_CPU1_CAPTURE_TRACE_H
#define TDVP_CPU1_CAPTURE_TRACE_H

/* Optional diagnostic extension in producer.reserved[0..1]: version, stage.
 * One CPU1 capture thread writes it, Linux never writes it. Each stage names
 * the call about to execute; RUNNING/STOPPED mean the whole operation returned
 * successfully. No pointers, timestamps or hardware addresses on the wire.
 * Readers which do not know version 1 continue to ignore these reserved words.
 */
#define TDVP_CAPTURE_TRACE_VERSION 1U
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
