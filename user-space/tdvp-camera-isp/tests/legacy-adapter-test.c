#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vvcam_sensor.h>

#include "tdvp-vvcam-legacy-adapter.h"

static struct tdvp_vvcam_legacy_sensor *registered;
static unsigned registrations, calls;
static int context;
static struct vvcam_sensor_mode expected_mode;

void tdvp_vvcam_legacy_add(struct tdvp_vvcam_legacy_sensor *sensor)
{
    registrations++;
    registered = sensor;
}

static int sensor_init(void **ctx)
{
    calls++;
    *ctx = &context;
    return 11;
}

static void sensor_deinit(void *ctx)
{
    assert(ctx == &context);
    calls++;
}

static int enum_mode(void *ctx, uint32_t index, struct vvcam_sensor_mode *mode)
{
    assert(ctx == &context && index == 0x12345678 && mode == &expected_mode);
    calls++;
    return 12;
}

static int get_mode(void *ctx, struct vvcam_sensor_mode *mode)
{
    assert(ctx == &context && mode == &expected_mode);
    calls++;
    return 13;
}

static int set_mode(void *ctx, uint32_t index)
{
    assert(ctx == &context && index == 0x87654321);
    calls++;
    return 14;
}

static int set_stream(void *ctx, bool on)
{
    assert(ctx == &context && on);
    calls++;
    return 15;
}

static int set_flip(void *ctx, bool on)
{
    (void)ctx;
    (void)on;
    assert(!"legacy callback reached a modern flip slot");
    return -1;
}

static int get_flip(void *ctx, bool *on)
{
    (void)ctx;
    (void)on;
    assert(!"legacy callback reached a modern flip slot");
    return -1;
}

static int analog_gain(void *ctx, float gain)
{
    assert(ctx == &context && gain == 2.5f);
    calls++;
    return 16;
}

static int digital_gain(void *ctx, float gain)
{
    assert(ctx == &context && gain == 1.25f);
    calls++;
    return 17;
}

static int exposure(void *ctx, float time)
{
    assert(ctx == &context && time == 0.125f);
    calls++;
    return 18;
}

struct vvcam_sensor vvcam_gc2093 = {
    .name = "gc2093",
    .ctrl = {
        .init = sensor_init, .deinit = sensor_deinit,
        .enum_mode = enum_mode, .get_mode = get_mode,
        .set_mode = set_mode, .set_stream = set_stream,
        .set_hflip = set_flip, .get_hflip = get_flip,
        .set_vflip = set_flip, .get_vflip = get_flip,
        .set_analog_gain = analog_gain, .set_digital_gain = digital_gain,
        .set_int_time = exposure,
    },
};

int main(void)
{
    void *ctx = NULL;
    vvcam_sensor_init();
    assert(registrations == 1 && registered && calls == 0);
    assert(strcmp(registered->name, "gc2093") == 0);
    assert(offsetof(struct tdvp_vvcam_legacy_sensor, ctrl.set_digital_gain) == 64);
    assert(registered->ctrl.init(&ctx) == 11 && ctx == &context);
    assert(registered->ctrl.enum_mode(ctx, 0x12345678, &expected_mode) == 12);
    assert(registered->ctrl.get_mode(ctx, &expected_mode) == 13);
    assert(registered->ctrl.set_mode(ctx, 0x87654321) == 14);
    assert(registered->ctrl.set_stream(ctx, true) == 15);
    assert(registered->ctrl.set_analog_gain(ctx, 2.5f) == 16);
    assert(registered->ctrl.set_digital_gain(ctx, 1.25f) == 17);
    assert(registered->ctrl.set_int_time(ctx, 0.125f) == 18);
    registered->ctrl.deinit(ctx);
    assert(calls == 9);
    puts("PASS: legacy ISP registration and all 9 callback slots (mock sensor, no hardware I/O)");
    return 0;
}
