#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#define TDVP_DEFAULT_WIDTH 320
#define TDVP_DEFAULT_HEIGHT 240
#define TDVP_MAX_DIMENSION 4096
#define TDVP_MAX_PIXELS (16U * 1024U * 1024U)
#define TDVP_BENCH_BUFFER_COUNT 2
#define TDVP_DEFAULT_FRAMES 120
#define TDVP_DEFAULT_MAX_FRAME_MS 1000

struct tdvp_bench;

enum tdvp_shm_format {
	TDVP_SHM_FORMAT_XR24,
	TDVP_SHM_FORMAT_AR24,
};

struct tdvp_buffer {
	struct tdvp_bench *bench;
	struct wl_buffer *buffer;
	void *data;
	size_t size;
	int fd;
	uint64_t submitted_ns;
	unsigned int submitted_callback_count;
	bool initialized;
	bool busy;
};

struct tdvp_options {
	unsigned int width;
	unsigned int height;
	unsigned int damage_size;
	unsigned int frames;
	unsigned int max_frame_ms;
	enum tdvp_shm_format shm_format;
};

struct tdvp_damage {
	int x;
	int y;
	int width;
	int height;
};

struct tdvp_bench {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_callback *frame_callback;
	struct tdvp_buffer buffers[TDVP_BENCH_BUFFER_COUNT];
	struct tdvp_options options;
	uint64_t submitted_ns;
	uint64_t last_activity_ns;
	uint64_t callback_total_us;
	uint64_t callback_min_us;
	uint64_t callback_max_us;
	uint64_t release_total_us;
	uint64_t release_min_us;
	uint64_t release_max_us;
	unsigned int submitted_frames;
	unsigned int completed_frames;
	unsigned int released_frames;
	unsigned int releases_before_callback;
	unsigned int releases_after_callback;
	bool configured;
	bool advertised_xr24;
	bool advertised_ar24;
	bool waiting_for_buffer;
	bool draining_releases;
	bool failed;
	bool done;
};

static uint64_t tdvp_now_ns(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return 0;
	return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static void tdvp_usage(const char *program)
{
	fprintf(stderr,
		"usage: %s [--width N] [--height N] [--damage-size N] [--frames N] "
		"[--max-frame-ms N] [--format xr24|ar24]\n"
		"\n"
		"Create one XDG toplevel using two linear wl_shm XR24 or AR24 buffers,\n"
		"measure bounded frame-callback and wl_buffer.release latency, then\n"
		"unmap the surface and verify both buffers are released. This never\n"
		"opens DRM.  The default size is 320x240; use 1232x568 for the K230\n"
		"panel-sized SHM path. --damage-size 0 (the default) changes the full "
		"surface every frame; a non-zero N changes only a fixed NxN square after "
		"each buffer's initial full upload. --format defaults to xr24; ar24 "
		"uses varying non-opaque alpha to exercise the composited alpha path.\n",
		program);
}

static const char *tdvp_shm_format_name(enum tdvp_shm_format format)
{
	return format == TDVP_SHM_FORMAT_AR24 ? "AR24" : "XR24";
}

static uint32_t tdvp_shm_wayland_format(enum tdvp_shm_format format)
{
	return format == TDVP_SHM_FORMAT_AR24 ? WL_SHM_FORMAT_ARGB8888 :
		WL_SHM_FORMAT_XRGB8888;
}

static int tdvp_parse_shm_format(const char *value,
				 enum tdvp_shm_format *format)
{
	if (strcasecmp(value, "xr24") == 0 ||
	    strcasecmp(value, "xrgb8888") == 0) {
		*format = TDVP_SHM_FORMAT_XR24;
		return 0;
	}
	if (strcasecmp(value, "ar24") == 0 ||
	    strcasecmp(value, "argb8888") == 0) {
		*format = TDVP_SHM_FORMAT_AR24;
		return 0;
	}
	return -1;
}

static int tdvp_parse_uint(const char *value, unsigned int *result)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno || !value[0] || *end || parsed > UINT_MAX)
		return -1;
	*result = (unsigned int)parsed;
	return 0;
}

static void tdvp_fail(struct tdvp_bench *bench, const char *message)
{
	if (!bench->failed)
		fprintf(stderr, "tdvp-wayland-shm-bench: %s\n", message);
	bench->failed = true;
}

static void tdvp_print_scheduler_context(void)
{
	char loadavg[128] = "unavailable";
	FILE *file;
	long online_cpus;

	online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	file = fopen("/proc/loadavg", "r");
	if (file) {
		if (fgets(loadavg, sizeof(loadavg), file))
			loadavg[strcspn(loadavg, "\n")] = '\0';
		fclose(file);
	}
	printf("tdvp-wayland-shm-bench: SCHED online_cpus=%ld loadavg=%s\n",
	       online_cpus, loadavg);
}

static void tdvp_fail_idle_timeout(struct tdvp_bench *bench,
				   uint64_t idle_ms)
{
	unsigned int busy_buffers = 0;
	unsigned int i;
	char message[256];

	for (i = 0; i < TDVP_BENCH_BUFFER_COUNT; ++i) {
		if (bench->buffers[i].busy)
			++busy_buffers;
	}

	/*
	 * A timeout is useful only if it says which protocol resource stopped
	 * making progress.  A pending frame callback points at the compositor's
	 * repaint/commit path; two busy buffers without a callback points at
	 * wl_buffer.release.  Both differ materially from a client-side write or
	 * flush failure, and neither requires opening DRM from this SHM client.
	 */
	(void)snprintf(message, sizeof(message),
		"no Wayland progress before the callback timeout "
		"idle_ms=%llu submitted=%u completed=%u released=%u/%u "
		"frame_callback=%s waiting_for_buffer=%s draining_releases=%s "
		"busy_buffers=%u/%u",
		(unsigned long long)idle_ms, bench->submitted_frames,
		bench->completed_frames, bench->released_frames,
		bench->submitted_frames, bench->frame_callback ? "pending" : "none",
		bench->waiting_for_buffer ? "yes" : "no",
		bench->draining_releases ? "yes" : "no", busy_buffers,
		TDVP_BENCH_BUFFER_COUNT);
	tdvp_fail(bench, message);
}

static struct tdvp_buffer *tdvp_find_free_buffer(struct tdvp_bench *bench)
{
	unsigned int i;

	for (i = 0; i < TDVP_BENCH_BUFFER_COUNT; ++i) {
		if (!bench->buffers[i].busy)
			return &bench->buffers[i];
	}
	return NULL;
}

/*
 * wl_shm defines alpha-bearing formats as pre-multiplied.  Keep the generated
 * AR24 workload within that contract: an alpha of 0x70, for example, must not
 * carry an R, G, or B component greater than 0x70.  The +127 term rounds the
 * 8-bit product to its nearest representable pre-multiplied component.
 */
static uint32_t tdvp_premultiply_channel(uint32_t channel, uint32_t alpha)
{
	return (channel * alpha + 127U) / 255U;
}

static void tdvp_fill_buffer(struct tdvp_buffer *buffer, unsigned int frame,
		struct tdvp_damage *damage)
{
	const struct tdvp_bench *bench = buffer->bench;
	uint32_t *pixels = buffer->data;
	unsigned int x;
	unsigned int y;
	unsigned int first_upload = !buffer->initialized;
	unsigned int damage_size = bench->options.damage_size;
	uint32_t phase;

	if (first_upload || damage_size == 0) {
		/*
		 * In small-damage mode both buffers receive the same static base image.
		 * Reusing the other buffer must not make unchanged pixels differ from the
		 * last frame, otherwise wl_surface_damage() would under-report damage.
		 */
		phase = damage_size == 0 ? frame * 13U : 0;
		for (y = 0; y < bench->options.height; ++y) {
			for (x = 0; x < bench->options.width; ++x) {
				unsigned int checker = ((x / 24U) + (y / 24U) +
					(damage_size == 0 ? frame : 0)) & 1U;
				uint32_t red = (x + phase) & 0xffU;
				uint32_t green = (y * 2U + phase) & 0xffU;
				uint32_t blue = checker ? 0x44U : 0xb8U;
				uint32_t alpha = bench->options.shm_format ==
					TDVP_SHM_FORMAT_AR24 ? (checker ? 0x90U : 0xd0U) : 0U;

				if (bench->options.shm_format == TDVP_SHM_FORMAT_AR24) {
					red = tdvp_premultiply_channel(red, alpha);
					green = tdvp_premultiply_channel(green, alpha);
					blue = tdvp_premultiply_channel(blue, alpha);
				}
				pixels[y * bench->options.width + x] =
					(alpha << 24) | (red << 16) | (green << 8) | blue;
			}
		}
		buffer->initialized = true;
		*damage = (struct tdvp_damage){
			.x = 0,
			.y = 0,
			.width = (int)bench->options.width,
			.height = (int)bench->options.height,
		};
		return;
	}

	phase = frame * 13U;
	damage->x = (int)((bench->options.width - damage_size) / 2U);
	damage->y = (int)((bench->options.height - damage_size) / 2U);
	damage->width = (int)damage_size;
	damage->height = (int)damage_size;
	for (y = (unsigned int)damage->y;
			y < (unsigned int)(damage->y + damage->height); ++y) {
		for (x = (unsigned int)damage->x;
				x < (unsigned int)(damage->x + damage->width); ++x) {
			unsigned int checker = ((x / 8U) + (y / 8U) + frame) & 1U;
			uint32_t red = (x + phase) & 0xffU;
			uint32_t green = (y * 2U + phase) & 0xffU;
			uint32_t blue = checker ? 0x18U : 0xf0U;
			uint32_t alpha = bench->options.shm_format ==
				TDVP_SHM_FORMAT_AR24 ? (checker ? 0x70U : 0xb0U) : 0U;

			if (bench->options.shm_format == TDVP_SHM_FORMAT_AR24) {
				red = tdvp_premultiply_channel(red, alpha);
				green = tdvp_premultiply_channel(green, alpha);
				blue = tdvp_premultiply_channel(blue, alpha);
			}
			pixels[y * bench->options.width + x] =
				(alpha << 24) | (red << 16) | (green << 8) | blue;
		}
	}
}

static int tdvp_submit_next_frame(struct tdvp_bench *bench);

static bool tdvp_all_buffers_released(const struct tdvp_bench *bench)
{
	unsigned int i;

	for (i = 0; i < TDVP_BENCH_BUFFER_COUNT; ++i) {
		if (bench->buffers[i].busy)
			return false;
	}
	return bench->released_frames == bench->submitted_frames;
}

static int tdvp_begin_release_drain(struct tdvp_bench *bench)
{
	int flush_result;

	if (bench->draining_releases)
		return 0;

	/*
	 * The last attached wl_buffer can remain current indefinitely.  Detach it
	 * before declaring the benchmark complete so a PASS proves every SHM
	 * buffer was released by the compositor rather than merely frame-callback
	 * throttled.
	 */
	bench->draining_releases = true;
	wl_surface_attach(bench->surface, NULL, 0, 0);
	wl_surface_commit(bench->surface);
	flush_result = wl_display_flush(bench->display);
	if (flush_result < 0 && errno != EAGAIN) {
		fprintf(stderr,
			"tdvp-wayland-shm-bench: wl_display_flush while draining: %s\n",
			strerror(errno));
		return -1;
	}

	if (tdvp_all_buffers_released(bench))
		bench->done = true;
	return 0;
}

static void tdvp_buffer_release(void *data, struct wl_buffer *buffer)
{
	struct tdvp_buffer *tdvp_buffer = data;
	struct tdvp_bench *bench = tdvp_buffer->bench;
	uint64_t now_ns = tdvp_now_ns();
	uint64_t release_us;

	(void)buffer;
	if (!tdvp_buffer->busy || !tdvp_buffer->submitted_ns || !now_ns ||
	    now_ns < tdvp_buffer->submitted_ns) {
		tdvp_fail(bench, "received an invalid wl_buffer.release event");
		return;
	}

	release_us = (now_ns - tdvp_buffer->submitted_ns) / 1000ULL;
	if (release_us < bench->release_min_us)
		bench->release_min_us = release_us;
	if (release_us > bench->release_max_us)
		bench->release_max_us = release_us;
	bench->release_total_us += release_us;
	if (bench->completed_frames > tdvp_buffer->submitted_callback_count)
		++bench->releases_after_callback;
	else
		++bench->releases_before_callback;
	tdvp_buffer->busy = false;
	tdvp_buffer->submitted_ns = 0;
	++bench->released_frames;
	bench->last_activity_ns = now_ns;
	if (bench->draining_releases && tdvp_all_buffers_released(bench)) {
		bench->done = true;
		return;
	}
	if (bench->waiting_for_buffer && !bench->frame_callback && !bench->done &&
	    !bench->failed && !bench->draining_releases &&
	    tdvp_submit_next_frame(bench) < 0)
		tdvp_fail(bench, "could not submit a frame after wl_buffer.release");
}

static const struct wl_buffer_listener tdvp_buffer_listener = {
	.release = tdvp_buffer_release,
};

static void tdvp_frame_done(void *data, struct wl_callback *callback,
			    uint32_t callback_time)
{
	struct tdvp_bench *bench = data;
	uint64_t now_ns = tdvp_now_ns();
	uint64_t callback_us;

	(void)callback_time;
	if (callback != bench->frame_callback) {
		wl_callback_destroy(callback);
		tdvp_fail(bench, "received an unexpected frame callback");
		return;
	}

	bench->frame_callback = NULL;
	wl_callback_destroy(callback);
	if (!bench->submitted_ns || !now_ns || now_ns < bench->submitted_ns) {
		tdvp_fail(bench, "could not measure frame callback time");
		return;
	}

	callback_us = (now_ns - bench->submitted_ns) / 1000ULL;
	bench->last_activity_ns = now_ns;
	if (callback_us > (uint64_t)bench->options.max_frame_ms * 1000ULL) {
		char message[192];

		(void)snprintf(message, sizeof(message),
			"frame callback exceeded the configured timeout "
			"callback_us=%llu limit_ms=%u completed=%u submitted=%u",
			(unsigned long long)callback_us, bench->options.max_frame_ms,
			bench->completed_frames, bench->submitted_frames);
		tdvp_fail(bench, message);
		return;
	}

	if (callback_us < bench->callback_min_us)
		bench->callback_min_us = callback_us;
	if (callback_us > bench->callback_max_us)
		bench->callback_max_us = callback_us;
	bench->callback_total_us += callback_us;
	++bench->completed_frames;

	if (bench->completed_frames >= bench->options.frames) {
		if (tdvp_begin_release_drain(bench) < 0)
			tdvp_fail(bench, "could not detach the final SHM buffer");
		return;
	}

	if (tdvp_submit_next_frame(bench) < 0)
		tdvp_fail(bench, "could not submit the next frame");
}

static const struct wl_callback_listener tdvp_frame_listener = {
	.done = tdvp_frame_done,
};

static int tdvp_submit_next_frame(struct tdvp_bench *bench)
{
	struct tdvp_buffer *buffer;
	struct tdvp_damage damage;
	int flush_result;

	if (!bench->configured || bench->failed || bench->done ||
	    bench->draining_releases ||
	    bench->frame_callback)
		return 0;

	buffer = tdvp_find_free_buffer(bench);
	if (!buffer) {
		bench->waiting_for_buffer = true;
		return 0;
	}

	bench->waiting_for_buffer = false;
	tdvp_fill_buffer(buffer, bench->submitted_frames + 1U, &damage);
	bench->frame_callback = wl_surface_frame(bench->surface);
	if (!bench->frame_callback) {
		fprintf(stderr,
			"tdvp-wayland-shm-bench: cannot create wl_surface frame callback\n");
		return -1;
	}
	wl_callback_add_listener(bench->frame_callback, &tdvp_frame_listener,
				 bench);
	buffer->submitted_ns = tdvp_now_ns();
	if (!buffer->submitted_ns) {
		fprintf(stderr,
			"tdvp-wayland-shm-bench: cannot timestamp SHM submission\n");
		return -1;
	}
	buffer->submitted_callback_count = bench->completed_frames;
	wl_surface_attach(bench->surface, buffer->buffer, 0, 0);
	wl_surface_damage(bench->surface, damage.x, damage.y, damage.width,
			  damage.height);
	buffer->busy = true;
	bench->submitted_ns = buffer->submitted_ns;
	++bench->submitted_frames;
	wl_surface_commit(bench->surface);
	flush_result = wl_display_flush(bench->display);
	if (flush_result < 0 && errno != EAGAIN) {
		fprintf(stderr, "tdvp-wayland-shm-bench: wl_display_flush: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

static void tdvp_wm_base_ping(void *data, struct xdg_wm_base *wm_base,
			      uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener tdvp_wm_base_listener = {
	.ping = tdvp_wm_base_ping,
};

static void tdvp_xdg_surface_configure(void *data,
				       struct xdg_surface *xdg_surface,
				       uint32_t serial)
{
	struct tdvp_bench *bench = data;

	xdg_surface_ack_configure(xdg_surface, serial);
	bench->configured = true;
	bench->last_activity_ns = tdvp_now_ns();
	if (tdvp_submit_next_frame(bench) < 0)
		tdvp_fail(bench, "could not submit the first SHM frame");
}

static const struct xdg_surface_listener tdvp_xdg_surface_listener = {
	.configure = tdvp_xdg_surface_configure,
};

static void tdvp_toplevel_configure(void *data,
				    struct xdg_toplevel *toplevel,
				    int32_t width, int32_t height,
				    struct wl_array *states)
{
	(void)data;
	(void)toplevel;
	(void)width;
	(void)height;
	(void)states;
}

static void tdvp_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
	struct tdvp_bench *bench = data;

	(void)toplevel;
	tdvp_fail(bench, "the compositor closed the SHM benchmark surface");
}

static const struct xdg_toplevel_listener tdvp_toplevel_listener = {
	.configure = tdvp_toplevel_configure,
	.close = tdvp_toplevel_close,
};

static const struct wl_shm_listener tdvp_shm_listener;

static void tdvp_registry_global(void *data, struct wl_registry *registry,
				 uint32_t name, const char *interface,
				 uint32_t version)
{
	struct tdvp_bench *bench = data;

	if (strcmp(interface, "wl_compositor") == 0 && !bench->compositor) {
		uint32_t bind_version = version < 4 ? version : 4;

		bench->compositor = wl_registry_bind(registry, name,
						    &wl_compositor_interface,
						    bind_version);
	} else if (strcmp(interface, "wl_shm") == 0 && !bench->shm) {
		bench->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		if (bench->shm)
			wl_shm_add_listener(bench->shm, &tdvp_shm_listener, bench);
	} else if (strcmp(interface, "xdg_wm_base") == 0 && !bench->wm_base) {
		bench->wm_base = wl_registry_bind(registry, name,
						 &xdg_wm_base_interface, 1);
		if (bench->wm_base)
			xdg_wm_base_add_listener(bench->wm_base,
						 &tdvp_wm_base_listener, bench);
	}
}

static void tdvp_registry_global_remove(void *data,
					struct wl_registry *registry,
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

static void tdvp_shm_format(void *data, struct wl_shm *shm, uint32_t format)
{
	struct tdvp_bench *bench = data;

	(void)shm;
	if (format == WL_SHM_FORMAT_XRGB8888)
		bench->advertised_xr24 = true;
	else if (format == WL_SHM_FORMAT_ARGB8888)
		bench->advertised_ar24 = true;
}

static const struct wl_shm_listener tdvp_shm_listener = {
	.format = tdvp_shm_format,
};

static int tdvp_allocate_buffer(struct tdvp_bench *bench,
				struct tdvp_buffer *buffer)
{
	char path[] = "/tmp/tdvp-wayland-shm-XXXXXX";
	struct wl_shm_pool *pool;
	void *data;
	size_t size = (size_t)bench->options.width *
		(size_t)bench->options.height * 4U;
	int fd;

	fd = mkstemp(path);
	if (fd < 0) {
		fprintf(stderr, "tdvp-wayland-shm-bench: mkstemp: %s\n",
			strerror(errno));
		return -1;
	}
	if (unlink(path) < 0 || ftruncate(fd, (off_t)size) < 0) {
		fprintf(stderr, "tdvp-wayland-shm-bench: initialise SHM buffer: %s\n",
			strerror(errno));
		close(fd);
		return -1;
	}

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "tdvp-wayland-shm-bench: mmap: %s\n",
			strerror(errno));
		close(fd);
		return -1;
	}

	if ((bench->options.shm_format == TDVP_SHM_FORMAT_XR24 &&
	     !bench->advertised_xr24) ||
	    (bench->options.shm_format == TDVP_SHM_FORMAT_AR24 &&
	     !bench->advertised_ar24)) {
		fprintf(stderr,
			"tdvp-wayland-shm-bench: wl_shm did not advertise required %s format\n",
			tdvp_shm_format_name(bench->options.shm_format));
		munmap(data, size);
		close(fd);
		return -1;
	}

	pool = wl_shm_create_pool(bench->shm, fd, (int32_t)size);
	if (!pool) {
		fprintf(stderr, "tdvp-wayland-shm-bench: cannot create wl_shm pool\n");
		munmap(data, size);
		close(fd);
		return -1;
	}
	buffer->buffer = wl_shm_pool_create_buffer(pool, 0,
						    (int)bench->options.width,
						    (int)bench->options.height,
						    (int)(bench->options.width * 4U),
						    tdvp_shm_wayland_format(bench->options.shm_format));
	wl_shm_pool_destroy(pool);
	if (!buffer->buffer) {
		fprintf(stderr, "tdvp-wayland-shm-bench: cannot create wl_shm buffer\n");
		munmap(data, size);
		close(fd);
		return -1;
	}

	buffer->bench = bench;
	buffer->data = data;
	buffer->size = size;
	buffer->fd = fd;
	buffer->busy = false;
	wl_buffer_add_listener(buffer->buffer, &tdvp_buffer_listener, buffer);
	return 0;
}

static void tdvp_destroy_buffer(struct tdvp_buffer *buffer)
{
	if (buffer->buffer)
		wl_buffer_destroy(buffer->buffer);
	if (buffer->data && buffer->data != MAP_FAILED)
		munmap(buffer->data, buffer->size);
	if (buffer->fd >= 0)
		close(buffer->fd);
	memset(buffer, 0, sizeof(*buffer));
	buffer->fd = -1;
}

static void tdvp_destroy_bench(struct tdvp_bench *bench)
{
	unsigned int i;

	if (bench->frame_callback)
		wl_callback_destroy(bench->frame_callback);
	for (i = 0; i < TDVP_BENCH_BUFFER_COUNT; ++i)
		tdvp_destroy_buffer(&bench->buffers[i]);
	if (bench->toplevel)
		xdg_toplevel_destroy(bench->toplevel);
	if (bench->xdg_surface)
		xdg_surface_destroy(bench->xdg_surface);
	if (bench->surface)
		wl_surface_destroy(bench->surface);
	if (bench->wm_base)
		xdg_wm_base_destroy(bench->wm_base);
	if (bench->shm)
		wl_shm_destroy(bench->shm);
	if (bench->compositor)
		wl_compositor_destroy(bench->compositor);
	if (bench->registry)
		wl_registry_destroy(bench->registry);
	if (bench->display)
		wl_display_disconnect(bench->display);
}

static int tdvp_dispatch_until_done(struct tdvp_bench *bench)
{
	struct pollfd pollfd = {
		.fd = wl_display_get_fd(bench->display),
		.events = POLLIN,
	};

	while (!bench->done && !bench->failed) {
		uint64_t now_ns;
		uint64_t idle_ms;
		int poll_result;

		if (wl_display_dispatch_pending(bench->display) < 0) {
			tdvp_fail(bench, "wl_display_dispatch_pending failed");
			break;
		}
		if (bench->done || bench->failed)
			break;

		while (wl_display_prepare_read(bench->display) != 0) {
			if (wl_display_dispatch_pending(bench->display) < 0) {
				tdvp_fail(bench, "cannot drain pending Wayland events");
				break;
			}
		}
		if (bench->failed)
			break;

		if (wl_display_flush(bench->display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(bench->display);
			tdvp_fail(bench, "wl_display_flush failed while waiting");
			break;
		}

		poll_result = poll(&pollfd, 1, 50);
		if (poll_result > 0) {
			if (wl_display_read_events(bench->display) < 0) {
				tdvp_fail(bench, "wl_display_read_events failed");
				break;
			}
		} else {
			wl_display_cancel_read(bench->display);
			if (poll_result < 0 && errno != EINTR) {
				tdvp_fail(bench, "poll on the Wayland socket failed");
				break;
			}
		}

		now_ns = tdvp_now_ns();
		if (!now_ns || now_ns < bench->last_activity_ns) {
			tdvp_fail(bench, "could not measure Wayland activity timeout");
			break;
		}
		idle_ms = (now_ns - bench->last_activity_ns) / 1000000ULL;
		if (idle_ms > bench->options.max_frame_ms)
			tdvp_fail_idle_timeout(bench, idle_ms);
	}

	return bench->done && !bench->failed ? 0 : -1;
}

int main(int argc, char **argv)
{
	struct tdvp_options options = {
		.width = TDVP_DEFAULT_WIDTH,
		.height = TDVP_DEFAULT_HEIGHT,
		.damage_size = 0,
		.frames = TDVP_DEFAULT_FRAMES,
		.max_frame_ms = TDVP_DEFAULT_MAX_FRAME_MS,
		.shm_format = TDVP_SHM_FORMAT_XR24,
	};
	struct tdvp_bench bench;
	unsigned int i;
	int ret = 1;

	for (i = 1; i < (unsigned int)argc; ++i) {
		if (strcmp(argv[i], "--width") == 0 && i + 1 < (unsigned int)argc) {
			if (tdvp_parse_uint(argv[++i], &options.width) < 0 ||
			    options.width == 0 || options.width > TDVP_MAX_DIMENSION) {
				fprintf(stderr,
					"tdvp-wayland-shm-bench: --width must be 1..%u\n",
					TDVP_MAX_DIMENSION);
				return 1;
			}
		} else if (strcmp(argv[i], "--height") == 0 &&
			   i + 1 < (unsigned int)argc) {
			if (tdvp_parse_uint(argv[++i], &options.height) < 0 ||
			    options.height == 0 || options.height > TDVP_MAX_DIMENSION) {
				fprintf(stderr,
					"tdvp-wayland-shm-bench: --height must be 1..%u\n",
					TDVP_MAX_DIMENSION);
				return 1;
			}
		} else if (strcmp(argv[i], "--damage-size") == 0 &&
			   i + 1 < (unsigned int)argc) {
			if (tdvp_parse_uint(argv[++i], &options.damage_size) < 0 ||
			    options.damage_size > TDVP_MAX_DIMENSION) {
				fprintf(stderr,
					"tdvp-wayland-shm-bench: --damage-size must be 0..%u\n",
					TDVP_MAX_DIMENSION);
				return 1;
			}
		} else if (strcmp(argv[i], "--frames") == 0 && i + 1 < (unsigned int)argc) {
			if (tdvp_parse_uint(argv[++i], &options.frames) < 0 ||
			    options.frames == 0 || options.frames > 10000) {
				fprintf(stderr,
					"tdvp-wayland-shm-bench: --frames must be 1..10000\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--max-frame-ms") == 0 &&
			   i + 1 < (unsigned int)argc) {
			if (tdvp_parse_uint(argv[++i], &options.max_frame_ms) < 0 ||
			    options.max_frame_ms == 0 ||
			    options.max_frame_ms > 10000) {
				fprintf(stderr,
					"tdvp-wayland-shm-bench: --max-frame-ms must be 1..10000\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--format") == 0 &&
			   i + 1 < (unsigned int)argc) {
			if (tdvp_parse_shm_format(argv[++i], &options.shm_format) < 0) {
				fprintf(stderr,
					"tdvp-wayland-shm-bench: --format must be xr24 or ar24\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--help") == 0 ||
			   strcmp(argv[i], "-h") == 0) {
			tdvp_usage(argv[0]);
			return 0;
		} else {
			tdvp_usage(argv[0]);
			return 1;
		}
	}
	if ((uint64_t)options.width * (uint64_t)options.height >
	    TDVP_MAX_PIXELS) {
		fprintf(stderr,
			"tdvp-wayland-shm-bench: --width * --height must be at most %u pixels\n",
			TDVP_MAX_PIXELS);
		return 1;
	}
	if (options.damage_size > options.width ||
	    options.damage_size > options.height) {
		fprintf(stderr,
			"tdvp-wayland-shm-bench: --damage-size must not exceed width or height\n");
		return 1;
	}

	memset(&bench, 0, sizeof(bench));
	for (i = 0; i < TDVP_BENCH_BUFFER_COUNT; ++i)
		bench.buffers[i].fd = -1;
	bench.options = options;
	bench.callback_min_us = UINT64_MAX;
	bench.release_min_us = UINT64_MAX;
	bench.last_activity_ns = tdvp_now_ns();
	tdvp_print_scheduler_context();
	bench.display = wl_display_connect(NULL);
	if (!bench.display) {
		fprintf(stderr,
			"tdvp-wayland-shm-bench: cannot connect to WAYLAND_DISPLAY=%s\n",
			getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "wayland-0");
		goto out;
	}

	bench.registry = wl_display_get_registry(bench.display);
	if (!bench.registry) {
		tdvp_fail(&bench, "cannot acquire the Wayland registry");
		goto out;
	}
	wl_registry_add_listener(bench.registry, &tdvp_registry_listener, &bench);
	if (wl_display_roundtrip(bench.display) < 0 || !bench.compositor ||
	    !bench.shm || !bench.wm_base) {
		tdvp_fail(&bench,
			  "requires wl_compositor, wl_shm and xdg_wm_base");
		goto out;
	}
	/*
	 * The registry callback above sends wl_registry.bind(wl_shm).  That bind is
	 * queued only while dispatching the first sync callback, so its initial
	 * wl_shm.format events arrive after a second roundtrip.  Checking the
	 * advertised format bits before that point turns every valid compositor
	 * into a false negative.
	 */
	if (wl_display_roundtrip(bench.display) < 0) {
		tdvp_fail(&bench, "could not receive initial wl_shm format events");
		goto out;
	}

	for (i = 0; i < TDVP_BENCH_BUFFER_COUNT; ++i) {
		if (tdvp_allocate_buffer(&bench, &bench.buffers[i]) < 0)
			goto out;
	}
	bench.surface = wl_compositor_create_surface(bench.compositor);
	if (!bench.surface) {
		tdvp_fail(&bench, "cannot create a Wayland surface");
		goto out;
	}
	bench.xdg_surface = xdg_wm_base_get_xdg_surface(bench.wm_base,
							 bench.surface);
	if (!bench.xdg_surface) {
		tdvp_fail(&bench, "cannot create an xdg_surface");
		goto out;
	}
	xdg_surface_add_listener(bench.xdg_surface, &tdvp_xdg_surface_listener,
				 &bench);
	bench.toplevel = xdg_surface_get_toplevel(bench.xdg_surface);
	if (!bench.toplevel) {
		tdvp_fail(&bench, "cannot create an xdg_toplevel");
		goto out;
	}
	xdg_toplevel_add_listener(bench.toplevel, &tdvp_toplevel_listener, &bench);
	xdg_toplevel_set_title(bench.toplevel, "TDVP Wayland SHM benchmark");
	xdg_toplevel_set_app_id(bench.toplevel, "vicliu624.tdvp-wayland-shm-bench");
	wl_surface_commit(bench.surface);

	if (tdvp_dispatch_until_done(&bench) < 0)
		goto out;

	printf("tdvp-wayland-shm-bench: PASS buffers=%u format=%s-linear-shm "
	       "size=%ux%u damage=%s damage_size=%u submitted=%u callbacks=%u released=%u "
	       "callback_us min=%llu avg=%.1f max=%llu "
	       "release_us min=%llu avg=%.1f max=%llu "
	       "release_before_callback=%u release_after_callback=%u\n",
	       TDVP_BENCH_BUFFER_COUNT, tdvp_shm_format_name(options.shm_format),
	       options.width, options.height,
	       options.damage_size == 0 ? "full" : "fixed-square", options.damage_size,
	       bench.submitted_frames, bench.completed_frames, bench.released_frames,
	       (unsigned long long)bench.callback_min_us,
	       (double)bench.callback_total_us / (double)bench.completed_frames,
	       (unsigned long long)bench.callback_max_us,
	       (unsigned long long)bench.release_min_us,
	       (double)bench.release_total_us / (double)bench.released_frames,
	       (unsigned long long)bench.release_max_us,
	       bench.releases_before_callback, bench.releases_after_callback);
	ret = 0;

out:
	tdvp_destroy_bench(&bench);
	return ret;
}
