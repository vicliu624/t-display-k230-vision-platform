#ifndef TDVP_VVCAM_LEGACY_ADAPTER_H
#define TDVP_VVCAM_LEGACY_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

struct vvcam_sensor_mode;

/* Exact LP64 sensor registration layout of the scalar ISP in upstream
 * k230_linux_sdk 155359af908a5fb38e870e81ee82c91bceb30800 (v0.6.1).
 * Do not pass the current vvcam_sensor directly: four flip callbacks were
 * inserted before gain/exposure without incrementing VVCAM_API_VERSION.
 */
struct tdvp_vvcam_legacy_sensor {
    const char *name;
    struct {
        int (*init)(void **ctx);
        void (*deinit)(void *ctx);
        int (*enum_mode)(void *ctx, uint32_t index, struct vvcam_sensor_mode *mode);
        int (*get_mode)(void *ctx, struct vvcam_sensor_mode *mode);
        int (*set_mode)(void *ctx, uint32_t index);
        int (*set_stream)(void *ctx, bool on);
        int (*set_analog_gain)(void *ctx, float gain);
        int (*set_digital_gain)(void *ctx, float gain);
        int (*set_int_time)(void *ctx, float time);
    } ctrl;
};

/* Distinct C identifier prevents a prototype collision with the modern
 * vendor header; the legacy ISP supplies this exact dynamic symbol.
 */
void tdvp_vvcam_legacy_add(struct tdvp_vvcam_legacy_sensor *sensor)
    __asm__("vvcam_sensor_add");

#endif
