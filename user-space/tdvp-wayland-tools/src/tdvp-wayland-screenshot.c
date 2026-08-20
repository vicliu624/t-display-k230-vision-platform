/*
 * Minimal wlroots screencopy client used for on-device desktop acceptance.
 * It records the compositor's final output, not the legacy fbcon buffer.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"

struct screenshot {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct wl_output *output;
	struct zwlr_screencopy_manager_v1 *manager;
	struct zwlr_screencopy_frame_v1 *frame;
	struct wl_buffer *buffer;
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	void *pixels;
	size_t bytes;
	bool complete;
	bool failed;
};

static void output_geometry(void *data, struct wl_output *output,
		int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
		int32_t subpixel, const char *make, const char *model, int32_t transform)
{
	(void)data;
	(void)output;
	(void)x;
	(void)y;
	(void)physical_width;
	(void)physical_height;
	(void)subpixel;
	(void)make;
	(void)model;
	(void)transform;
}

static void output_mode(void *data, struct wl_output *output, uint32_t flags,
		int32_t width, int32_t height, int32_t refresh)
{
	(void)data;
	(void)output;
	(void)flags;
	(void)width;
	(void)height;
	(void)refresh;
}

static void output_done(void *data, struct wl_output *output)
{
	(void)data;
	(void)output;
}

static void output_scale(void *data, struct wl_output *output, int32_t factor)
{
	(void)data;
	(void)output;
	(void)factor;
}

static void output_name(void *data, struct wl_output *output, const char *name)
{
	(void)data;
	(void)output;
	(void)name;
}

static void output_description(void *data, struct wl_output *output,
		const char *description)
{
	(void)data;
	(void)output;
	(void)description;
}

static const struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
	.name = output_name,
	.description = output_description,
};

static int create_shm_file(size_t bytes)
{
	char path[] = "/tmp/tdvp-screencopy-XXXXXX";
	int fd = mkstemp(path);

	if (fd < 0)
		return -1;

	unlink(path);
	if (ftruncate(fd, (off_t)bytes) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void frame_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame,
		uint32_t format, uint32_t width, uint32_t height, uint32_t stride)
{
	struct screenshot *shot = data;
	struct wl_shm_pool *pool;
	int fd;

	(void)frame;
	if (width == 0 || height == 0 || stride < width * 4 ||
		SIZE_MAX / height < stride) {
		fprintf(stderr, "invalid screencopy geometry %ux%u stride=%u\n",
			width, height, stride);
		shot->failed = true;
		return;
	}

	shot->format = format;
	shot->width = width;
	shot->height = height;
	shot->stride = stride;
	shot->bytes = (size_t)stride * height;
	fd = create_shm_file(shot->bytes);
	if (fd < 0) {
		perror("create screencopy buffer");
		shot->failed = true;
		return;
	}

	shot->pixels = mmap(NULL, shot->bytes, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (shot->pixels == MAP_FAILED) {
		perror("map screencopy buffer");
		shot->pixels = NULL;
		close(fd);
		shot->failed = true;
		return;
	}

	pool = wl_shm_create_pool(shot->shm, fd, (int)shot->bytes);
	shot->buffer = wl_shm_pool_create_buffer(pool, 0, (int)width, (int)height,
		(int)stride, format);
	wl_shm_pool_destroy(pool);
	close(fd);
	zwlr_screencopy_frame_v1_copy(shot->frame, shot->buffer);
}

static void frame_flags(void *data, struct zwlr_screencopy_frame_v1 *frame,
		uint32_t flags)
{
	(void)data;
	(void)frame;
	(void)flags;
}

static void frame_ready(void *data, struct zwlr_screencopy_frame_v1 *frame,
		uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
	struct screenshot *shot = data;

	(void)frame;
	(void)tv_sec_hi;
	(void)tv_sec_lo;
	(void)tv_nsec;
	shot->complete = true;
}

static void frame_failed(void *data, struct zwlr_screencopy_frame_v1 *frame)
{
	struct screenshot *shot = data;

	(void)frame;
	shot->failed = true;
}

static void frame_damage(void *data, struct zwlr_screencopy_frame_v1 *frame,
		uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	(void)data;
	(void)frame;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
	.buffer = frame_buffer,
	.flags = frame_flags,
	.ready = frame_ready,
	.failed = frame_failed,
	.damage = frame_damage,
};

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	struct screenshot *shot = data;

	if (strcmp(interface, wl_shm_interface.name) == 0) {
		shot->shm = wl_registry_bind(registry, name, &wl_shm_interface,
			version < 1 ? version : 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0 && !shot->output) {
		uint32_t bind_version = version < 4 ? version : 4;

		shot->output = wl_registry_bind(registry, name, &wl_output_interface,
			bind_version);
		wl_output_add_listener(shot->output, &output_listener, shot);
	} else if (strcmp(interface,
			zwlr_screencopy_manager_v1_interface.name) == 0) {
		shot->manager = wl_registry_bind(registry, name,
			&zwlr_screencopy_manager_v1_interface, version < 2 ? version : 2);
	}
}

static void registry_remove(void *data, struct wl_registry *registry,
		uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_remove,
};

static int write_ppm(const struct screenshot *shot, const char *path)
{
	FILE *file;
	uint32_t y;

	if (shot->format != WL_SHM_FORMAT_XRGB8888 &&
		shot->format != WL_SHM_FORMAT_ARGB8888) {
		fprintf(stderr, "unsupported screencopy format 0x%08x\n", shot->format);
		return -1;
	}

	file = fopen(path, "wb");
	if (!file) {
		perror(path);
		return -1;
	}
	if (fprintf(file, "P6\n%u %u\n255\n", shot->width, shot->height) < 0) {
		fclose(file);
		return -1;
	}

	for (y = 0; y < shot->height; ++y) {
		const uint8_t *row = (const uint8_t *)shot->pixels + (size_t)y * shot->stride;
		uint32_t x;

		for (x = 0; x < shot->width; ++x) {
			const uint8_t *pixel = row + x * 4;
			const uint8_t rgb[3] = { pixel[2], pixel[1], pixel[0] };

			if (fwrite(rgb, sizeof(rgb), 1, file) != 1) {
				fclose(file);
				return -1;
			}
		}
	}

	if (fclose(file) != 0)
		return -1;
	return 0;
}

int main(int argc, char **argv)
{
	const char *path = argc == 2 ? argv[1] : "/tmp/tdvp-wayland-screenshot.ppm";
	struct screenshot shot = { 0 };
	int status = EXIT_FAILURE;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [output.ppm]\n", argv[0]);
		return EXIT_FAILURE;
	}

	shot.display = wl_display_connect(NULL);
	if (!shot.display) {
		fprintf(stderr, "cannot connect to Wayland display %s\n",
			getenv("WAYLAND_DISPLAY") ?: "wayland-0");
		return EXIT_FAILURE;
	}

	shot.registry = wl_display_get_registry(shot.display);
	wl_registry_add_listener(shot.registry, &registry_listener, &shot);
	if (wl_display_roundtrip(shot.display) < 0 || !shot.shm || !shot.output ||
		!shot.manager) {
		fprintf(stderr, "Wayland compositor does not expose required screencopy globals\n");
		goto out;
	}

	shot.frame = zwlr_screencopy_manager_v1_capture_output(shot.manager, 0, shot.output);
	zwlr_screencopy_frame_v1_add_listener(shot.frame, &frame_listener, &shot);
	while (!shot.complete && !shot.failed && wl_display_dispatch(shot.display) >= 0)
		;

	if (shot.failed || !shot.complete) {
		fprintf(stderr, "Wayland screencopy failed\n");
		goto out;
	}
	if (write_ppm(&shot, path) != 0)
		goto out;

	fprintf(stdout, "captured %ux%u %s\n", shot.width, shot.height, path);
	status = EXIT_SUCCESS;

out:
	if (shot.buffer)
		wl_buffer_destroy(shot.buffer);
	if (shot.frame)
		zwlr_screencopy_frame_v1_destroy(shot.frame);
	if (shot.manager)
		zwlr_screencopy_manager_v1_destroy(shot.manager);
	if (shot.output)
		wl_output_destroy(shot.output);
	if (shot.shm)
		wl_shm_destroy(shot.shm);
	if (shot.registry)
		wl_registry_destroy(shot.registry);
	if (shot.pixels)
		munmap(shot.pixels, shot.bytes);
	if (shot.display)
		wl_display_disconnect(shot.display);
	return status;
}
