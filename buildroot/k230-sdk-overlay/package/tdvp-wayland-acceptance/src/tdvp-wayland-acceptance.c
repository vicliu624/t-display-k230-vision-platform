#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

struct tdvp_registry {
	bool compositor;
	bool shm;
	bool xdg_wm_base;
	bool seat;
	bool keyboard;
	bool wayland_keymap;
	bool output;
	bool output_mode;
	int32_t output_width;
	int32_t output_height;
	int32_t output_refresh_mhz;
	int32_t output_transform;
	int32_t output_scale;
	struct wl_seat *seat_proxy;
	struct wl_keyboard *keyboard_proxy;
	struct wl_output *output_proxy;
};

static const char *tdvp_env_or(const char *name, const char *fallback)
{
	const char *value = getenv(name);

	return value && *value ? value : fallback;
}

static int tdvp_check_xkb(void)
{
	struct xkb_context *context;
	struct xkb_keymap *keymap;
	struct xkb_rule_names names = {
		.rules = tdvp_env_or("XKB_DEFAULT_RULES", "evdev"),
		.model = tdvp_env_or("XKB_DEFAULT_MODEL", "pc105"),
		.layout = tdvp_env_or("XKB_DEFAULT_LAYOUT", "us"),
		.variant = tdvp_env_or("XKB_DEFAULT_VARIANT", ""),
		.options = tdvp_env_or("XKB_DEFAULT_OPTIONS", ""),
	};

	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context) {
		fprintf(stderr, "tdvp-wayland-acceptance: cannot create XKB context\n");
		return -1;
	}

	keymap = xkb_keymap_new_from_names(context, &names,
					    XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap) {
		fprintf(stderr,
			"tdvp-wayland-acceptance: cannot compile XKB %s/%s/%s\n",
			names.rules, names.model, names.layout);
		xkb_context_unref(context);
		return -1;
	}

	printf("tdvp-wayland-acceptance: xkb rules=%s model=%s layout=%s\n",
	       names.rules, names.model, names.layout);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	return 0;
}

static void tdvp_keyboard_keymap(void *data, struct wl_keyboard *keyboard,
				 uint32_t format, int fd, uint32_t size)
{
	struct tdvp_registry *state = data;

	(void)keyboard;
	(void)size;
	state->wayland_keymap = format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1;
	if (fd >= 0)
		close(fd);
}

static void tdvp_keyboard_enter(void *data, struct wl_keyboard *keyboard,
				uint32_t serial, struct wl_surface *surface,
				struct wl_array *keys)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void tdvp_keyboard_leave(void *data, struct wl_keyboard *keyboard,
				uint32_t serial, struct wl_surface *surface)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
}

static void tdvp_keyboard_key(void *data, struct wl_keyboard *keyboard,
				uint32_t serial, uint32_t time, uint32_t key,
				uint32_t state)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)time;
	(void)key;
	(void)state;
}

static void tdvp_keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
				      uint32_t serial, uint32_t depressed,
				      uint32_t latched, uint32_t locked,
				      uint32_t group)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)depressed;
	(void)latched;
	(void)locked;
	(void)group;
}

static void tdvp_keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
				       int32_t rate, int32_t delay)
{
	(void)data;
	(void)keyboard;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener tdvp_keyboard_listener = {
	.keymap = tdvp_keyboard_keymap,
	.enter = tdvp_keyboard_enter,
	.leave = tdvp_keyboard_leave,
	.key = tdvp_keyboard_key,
	.modifiers = tdvp_keyboard_modifiers,
	.repeat_info = tdvp_keyboard_repeat_info,
};

static void tdvp_seat_capabilities(void *data, struct wl_seat *seat,
				   uint32_t capabilities)
{
	struct tdvp_registry *state = data;

	if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) ||
	    state->keyboard_proxy)
		return;

	state->keyboard_proxy = wl_seat_get_keyboard(seat);
	if (state->keyboard_proxy) {
		state->keyboard = true;
		wl_keyboard_add_listener(state->keyboard_proxy,
					 &tdvp_keyboard_listener, state);
	}
}

static void tdvp_seat_name(void *data, struct wl_seat *seat,
			   const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener tdvp_seat_listener = {
	.capabilities = tdvp_seat_capabilities,
	.name = tdvp_seat_name,
};

static const char *tdvp_output_transform_name(int32_t transform)
{
	switch (transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		return "normal";
	case WL_OUTPUT_TRANSFORM_90:
		return "90";
	case WL_OUTPUT_TRANSFORM_180:
		return "180";
	case WL_OUTPUT_TRANSFORM_270:
		return "270";
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		return "flipped";
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		return "flipped-90";
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		return "flipped-180";
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		return "flipped-270";
	default:
		return "unknown";
	}
}

static void tdvp_output_geometry(void *data, struct wl_output *output,
				 int32_t x, int32_t y, int32_t physical_width,
				 int32_t physical_height, int32_t subpixel,
				 const char *make, const char *model,
				 int32_t transform)
{
	struct tdvp_registry *state = data;

	(void)output;
	(void)x;
	(void)y;
	(void)physical_width;
	(void)physical_height;
	(void)subpixel;
	(void)make;
	(void)model;
	state->output_transform = transform;
}

static void tdvp_output_mode(void *data, struct wl_output *output,
			     uint32_t flags, int32_t width, int32_t height,
			     int32_t refresh)
{
	struct tdvp_registry *state = data;

	(void)output;
	if (!(flags & WL_OUTPUT_MODE_CURRENT))
		return;

	state->output_mode = true;
	state->output_width = width;
	state->output_height = height;
	state->output_refresh_mhz = refresh;
}

static void tdvp_output_done(void *data, struct wl_output *output)
{
	(void)data;
	(void)output;
}

static void tdvp_output_scale(void *data, struct wl_output *output, int32_t factor)
{
	struct tdvp_registry *state = data;

	(void)output;
	state->output_scale = factor;
}

static const struct wl_output_listener tdvp_output_listener = {
	.geometry = tdvp_output_geometry,
	.mode = tdvp_output_mode,
	.done = tdvp_output_done,
	.scale = tdvp_output_scale,
};

static void tdvp_registry_global(void *data, struct wl_registry *registry,
				 uint32_t name, const char *interface,
				 uint32_t version)
{
	struct tdvp_registry *state = data;

	if (strcmp(interface, "wl_compositor") == 0)
		state->compositor = true;
	else if (strcmp(interface, "wl_shm") == 0)
		state->shm = true;
	else if (strcmp(interface, "xdg_wm_base") == 0)
		state->xdg_wm_base = true;
	else if (strcmp(interface, "wl_seat") == 0) {
		uint32_t bind_version = version < 5 ? version : 5;

		state->seat = true;
		state->seat_proxy = wl_registry_bind(registry, name,
						  &wl_seat_interface, bind_version);
		if (state->seat_proxy)
			wl_seat_add_listener(state->seat_proxy,
					     &tdvp_seat_listener, state);
	} else if (strcmp(interface, "wl_output") == 0 && !state->output_proxy) {
		uint32_t bind_version = version < 2 ? version : 2;

		state->output = true;
		state->output_scale = 1;
		state->output_proxy = wl_registry_bind(registry, name,
						    &wl_output_interface, bind_version);
		if (state->output_proxy)
			wl_output_add_listener(state->output_proxy,
					       &tdvp_output_listener, state);
	}
}

static void tdvp_registry_global_remove(void *data, struct wl_registry *registry,
					uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener tdvp_registry_listener = {
	.global = tdvp_registry_global,
	.global_remove = tdvp_registry_global_remove,
};

int main(void)
{
	struct tdvp_registry state = { 0 };
	struct wl_display *display;
	struct wl_registry *registry;
	const char *socket = tdvp_env_or("WAYLAND_DISPLAY", "wayland-0");
	int xkb_ok;
	int wayland_ok;

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "tdvp-wayland-acceptance: cannot connect to %s\n", socket);
		return 1;
	}

	registry = wl_display_get_registry(display);
	if (!registry) {
		fprintf(stderr, "tdvp-wayland-acceptance: cannot acquire registry\n");
		wl_display_disconnect(display);
		return 1;
	}

	wl_registry_add_listener(registry, &tdvp_registry_listener, &state);
	/*
	 * The second roundtrip delivers wl_seat capabilities and creates the
	 * wl_keyboard proxy. Its keymap event is delivered afterwards, so the
	 * third roundtrip is part of the keyboard acceptance contract.
	 */
	if (wl_display_roundtrip(display) < 0 ||
	    wl_display_roundtrip(display) < 0 ||
	    wl_display_roundtrip(display) < 0) {
		fprintf(stderr, "tdvp-wayland-acceptance: registry roundtrip failed\n");
		wl_registry_destroy(registry);
		wl_display_disconnect(display);
		return 1;
	}

	xkb_ok = tdvp_check_xkb() == 0;
	wayland_ok = state.compositor && state.shm && state.xdg_wm_base &&
		state.seat && state.keyboard && state.wayland_keymap &&
		state.output && state.output_mode;
	printf("tdvp-wayland-acceptance: socket=%s wl_compositor=%s wl_shm=%s xdg_wm_base=%s wl_seat=%s wl_keyboard=%s keymap=%s wl_output=%s\n",
	       socket, state.compositor ? "yes" : "no", state.shm ? "yes" : "no",
	       state.xdg_wm_base ? "yes" : "no", state.seat ? "yes" : "no",
	       state.keyboard ? "yes" : "no", state.wayland_keymap ? "yes" : "no",
	       state.output && state.output_mode ? "yes" : "no");
	if (state.output_mode) {
		printf("tdvp-wayland-acceptance: output=%dx%d refresh=%.3fHz transform=%s scale=%d\n",
		       state.output_width, state.output_height,
		       state.output_refresh_mhz / 1000.0,
		       tdvp_output_transform_name(state.output_transform),
		       state.output_scale);
	}

	if (state.keyboard_proxy)
		wl_keyboard_destroy(state.keyboard_proxy);
	if (state.seat_proxy)
		wl_seat_destroy(state.seat_proxy);
	if (state.output_proxy)
		wl_output_destroy(state.output_proxy);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return wayland_ok && xkb_ok ? 0 : 1;
}
