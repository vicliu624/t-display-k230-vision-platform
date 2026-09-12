/*
 * TDVP K230 VGLite acceptance probe.
 *
 * This program is intentionally an off-screen probe.  It validates the exact
 * allocation path that the future wlroots renderer must use:
 *
 *     DRM dumb buffer -> PRIME DMA-BUF fd -> VGLite map -> finish -> CPU read
 *
 * It never creates a DRM framebuffer, obtains DRM master, changes a mode,
 * performs an atomic commit, or page-flips.  It is consequently safe to run
 * while the tdvp user's Labwc session owns card0.
 */
#define _GNU_SOURCE

/* Buildroot's libdrm exposes these through usr/include/libdrm. */
#include <drm.h>
#include <drm_mode.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/dma-buf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <vg_lite.h>

enum {
	default_width = 256,
	default_height = 128,
	default_source_width = 64,
	default_source_height = 48,
	minimum_dimension = 64,
	maximum_dimension = 4096,
	minimum_frames = 1,
	maximum_frames = 600,
	maximum_wait_watchdog_ms = 5000,
};

struct tdvp_dumb_buffer {
	int drm_fd;
	int prime_fd;
	uint32_t handle;
	uint32_t pitch;
	size_t size;
	void *memory;
};

struct tdvp_target_format {
	const char *name;
	vg_lite_buffer_format_t vglite_format;
};

struct tdvp_timing_stats {
	double total_ms;
	double min_ms;
	double max_ms;
	unsigned int samples;
};

enum tdvp_blit_api {
	TDVP_BLIT_API_NORMAL,
	TDVP_BLIT_API_RECT,
};

/*
 * DRM_FORMAT_ARGB8888 has BGRA bytes on this little-endian target, while
 * DRM_FORMAT_XRGB8888 has BGRX bytes. Keep both acceptance cases explicit:
 * treating the X byte as alpha changes source-over compositing semantics.
 */
static const struct tdvp_target_format target_format_bgra = {
	.name = "VG_LITE_BGRA8888",
	.vglite_format = VG_LITE_BGRA8888,
};

static const struct tdvp_target_format target_format_bgrx = {
	.name = "VG_LITE_BGRX8888",
	.vglite_format = VG_LITE_BGRX8888,
};

static void usage(const char *program)
{
	printf("Usage: %s [--width PIXELS] [--height PIXELS] "
	       "[--source-width PIXELS] [--source-height PIXELS] [--frames COUNT] "
	       "[--target-format bgra|bgrx] [--blit-api normal|rect] "
	       "[--inject-inflight-close] [--trace]\n",
	       program);
	puts("\n"
	     "Runs an off-screen VGLite + DRM PRIME DMA-BUF acceptance test.\n"
	     "It never takes DRM master, creates a framebuffer, mode-sets, commits, or flips.\n"
	     "--frames defaults to 1; use 120 for the bounded repeated-submit gate.\n"
	     "--source-width/--source-height default to 64x48; set them to the output "
	     "size to measure full-texture cache-maintenance cost.\n"
	     "--target-format defaults to bgra; bgrx validates the DRM XRGB target path.\n"
	     "--blit-api defaults to normal. rect uses vg_lite_blit_rect() with a full "
	     "source rectangle so its result can be compared with the ordinary API.\n"
	     "--inject-inflight-close is a single-frame fault injection: after a successful "
	     "VG_LITE_SUBMIT it exits without WAIT or VGLite cleanup, exercising the kernel "
	     "in-flight-close recovery path. Use it only through the explicit recovery gate.\n"
	     "--trace writes per-frame VGLite stage boundaries to stderr for failure diagnosis.\n"
	     "Run it as the graphical user (tdvp), not as root.");
}

/*
 * The vendor's historical VG_LITE_INFINITE ioctl wait can leave both an
 * off-screen test client and the compositor behind it indefinitely blocked if
 * a completion IRQ is lost.  Opening the node once is harmless and allows a
 * modular driver to load, but no GPU command may be issued until the 0059/0060
 * bounded-wait parameter is visible and within the platform's five-second
 * recovery budget.
 */
static int require_finite_wait_watchdog(void)
{
	static const char *const candidates[] = {
		"/sys/module/vglite/parameters/infinite_wait_watchdog_ms",
		"/sys/module/vg_lite/parameters/infinite_wait_watchdog_ms",
	};
	char value[64];
	size_t index;

	for (index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
		FILE *stream = fopen(candidates[index], "r");
		char *end = NULL;
		unsigned long milliseconds;

		if (stream == NULL) {
			continue;
		}
		if (fgets(value, sizeof(value), stream) == NULL) {
			fprintf(stderr, "tdvp-vglite-probe: cannot read VGLite wait watchdog %s\n",
				candidates[index]);
			(void)fclose(stream);
			return -1;
		}
		(void)fclose(stream);

		milliseconds = strtoul(value, &end, 10);
		while (end != NULL && isspace((unsigned char)*end)) {
			++end;
		}
		if (value[0] == '\0' || end == value || end == NULL || *end != '\0' ||
			milliseconds == 0 || milliseconds > maximum_wait_watchdog_ms) {
			fprintf(stderr,
				"tdvp-vglite-probe: refusing GPU workload: VGLite wait watchdog %s "
				"must be an integer in 1..%d ms (got %s)",
				candidates[index], maximum_wait_watchdog_ms, value);
			return -1;
		}

		printf("kernel: vglite_wait_watchdog parameter=%s milliseconds=%lu\n",
			candidates[index], milliseconds);
		return 0;
	}

	fputs("tdvp-vglite-probe: refusing GPU workload without a finite VGLite "
		"wait watchdog; deploy the 0059/0060 kernel candidate first\n", stderr);
	return -1;
}

static int parse_dimension(const char *argument, int *value)
{
	char *end = NULL;
	unsigned long parsed;

	if (argument == NULL || value == NULL) {
		return -1;
	}

	parsed = strtoul(argument, &end, 10);
	if (*argument == '\0' || end == NULL || *end != '\0' ||
		parsed < minimum_dimension || parsed > maximum_dimension) {
		return -1;
	}

	*value = (int)parsed;
	return 0;
}

static int parse_frame_count(const char *argument, int *value)
{
	char *end = NULL;
	unsigned long parsed;

	if (argument == NULL || value == NULL) {
		return -1;
	}

	parsed = strtoul(argument, &end, 10);
	if (*argument == '\0' || end == NULL || *end != '\0' ||
		parsed < minimum_frames || parsed > maximum_frames) {
		return -1;
	}

	*value = (int)parsed;
	return 0;
}

static const struct tdvp_target_format *parse_target_format(const char *argument)
{
	if (argument == NULL) {
		return NULL;
	}
	if (strcmp(argument, "bgra") == 0) {
		return &target_format_bgra;
	}
	if (strcmp(argument, "bgrx") == 0) {
		return &target_format_bgrx;
	}
	return NULL;
}

static int parse_blit_api(const char *argument, enum tdvp_blit_api *api)
{
	if (argument == NULL || api == NULL) {
		return -1;
	}
	if (strcmp(argument, "normal") == 0) {
		*api = TDVP_BLIT_API_NORMAL;
		return 0;
	}
	if (strcmp(argument, "rect") == 0) {
		*api = TDVP_BLIT_API_RECT;
		return 0;
	}
	return -1;
}

static const char *blit_api_name(enum tdvp_blit_api api)
{
	return api == TDVP_BLIT_API_RECT ? "vg_lite_blit_rect" : "vg_lite_blit";
}

static double elapsed_milliseconds(const struct timespec *started,
		const struct timespec *finished)
{
	return ((double)(finished->tv_sec - started->tv_sec) * 1000.0) +
		((double)(finished->tv_nsec - started->tv_nsec) / 1000000.0);
}

static void record_timing(struct tdvp_timing_stats *stats, double milliseconds)
{
	if (stats->samples == 0U || milliseconds < stats->min_ms) {
		stats->min_ms = milliseconds;
	}
	if (milliseconds > stats->max_ms) {
		stats->max_ms = milliseconds;
	}
	stats->total_ms += milliseconds;
	stats->samples += 1U;
}

static uint64_t fnv1a64(const void *memory, size_t length)
{
	const uint8_t *bytes = memory;
	uint64_t hash = UINT64_C(14695981039346656037);
	size_t index;

	for (index = 0; index < length; ++index) {
		hash ^= bytes[index];
		hash *= UINT64_C(1099511628211);
	}

	return hash;
}

static uint32_t pixel_at(const struct tdvp_dumb_buffer *buffer, int x, int y)
{
	uint32_t pixel = 0;
	const uint8_t *address = buffer->memory;

	memcpy(&pixel, address + (size_t)y * buffer->pitch + (size_t)x * sizeof(pixel),
	       sizeof(pixel));
	return pixel;
}

static uint64_t sample_signature(uint32_t background_pixel, uint32_t blit_pixel,
		uint32_t path_pixel)
{
	const uint32_t samples[] = {
		background_pixel,
		blit_pixel,
		path_pixel,
	};

	return fnv1a64(samples, sizeof(samples));
}

/*
 * vg_lite_finish() makes the VGLite command stream complete, but it does not
 * by itself establish CPU cache ownership of this separately mmap'd PRIME
 * DMA-BUF.  A stale CPU cache line would make this acceptance probe report a
 * false missing GPU update when a compositor is also using VGLite.  Use the
 * exporter-defined DMA-BUF synchronization protocol around only the CPU
 * verification reads; VGLite remains the owner of all rendering commands.
 */
static int dma_buf_cpu_read_sync(int fd, uint64_t flags, const char *phase)
{
	struct dma_buf_sync sync = {
		.flags = flags,
	};

	if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) == 0) {
		return 0;
	}
	perror(phase);
	return -1;
}

static int read_target_samples(const struct tdvp_dumb_buffer *buffer,
		uint32_t *background_pixel, uint32_t *blit_pixel, uint32_t *path_pixel,
		int width, int height)
{
	if (dma_buf_cpu_read_sync(buffer->prime_fd,
			DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ,
			"DMA_BUF_IOCTL_SYNC(start CPU read)") != 0) {
		return -1;
	}

	*background_pixel = pixel_at(buffer, 2, height - 2);
	*blit_pixel = pixel_at(buffer, 20, 20);
	*path_pixel = pixel_at(buffer, width / 2 + 8, height / 4 + 8);

	if (dma_buf_cpu_read_sync(buffer->prime_fd,
			DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ,
			"DMA_BUF_IOCTL_SYNC(end CPU read)") != 0) {
		return -1;
	}
	return 0;
}

static void report_memory(const char *phase)
{
	FILE *status = fopen("/proc/self/status", "re");
	char line[256];

	if (status == NULL) {
		perror("open /proc/self/status");
		return;
	}

	printf("memory[%s]:", phase);
	while (fgets(line, sizeof(line), status) != NULL) {
		if (strncmp(line, "VmPeak:", 7) == 0 ||
			strncmp(line, "VmSize:", 7) == 0 ||
			strncmp(line, "VmHWM:", 6) == 0 ||
			strncmp(line, "VmRSS:", 6) == 0) {
			char *newline = strchr(line, '\n');
			if (newline != NULL) {
				*newline = '\0';
			}
			printf(" %s", line);
		}
	}
	putchar('\n');
	(void)fclose(status);
}

static int open_vglite_device(void)
{
	int fd = open("/dev/vg_lite", O_RDWR | O_CLOEXEC);

	if (fd < 0) {
		perror("open /dev/vg_lite");
		return -1;
	}

	printf("device: /dev/vg_lite direct-open=ok uid=%ld gid=%ld\n",
	       (long)geteuid(), (long)getegid());
	return fd;
}

static int create_dumb_buffer(struct tdvp_dumb_buffer *buffer, int width, int height)
{
	struct drm_mode_create_dumb create = {0};
	struct drm_mode_map_dumb map = {0};
	struct drm_prime_handle prime = {0};

	memset(buffer, 0, sizeof(*buffer));
	buffer->drm_fd = -1;
	buffer->prime_fd = -1;

	buffer->drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (buffer->drm_fd < 0) {
		perror("open /dev/dri/card0");
		return -1;
	}

	/* This allocates memory only.  No fb object is created and no KMS state is touched. */
	create.width = (uint32_t)width;
	create.height = (uint32_t)height;
	create.bpp = 32;
	if (ioctl(buffer->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
		perror("DRM_IOCTL_MODE_CREATE_DUMB");
		return -1;
	}

	buffer->handle = create.handle;
	buffer->pitch = create.pitch;
	buffer->size = (size_t)create.size;
	map.handle = create.handle;
	if (ioctl(buffer->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
		perror("DRM_IOCTL_MODE_MAP_DUMB");
		return -1;
	}

	buffer->memory = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED,
				      buffer->drm_fd, (off_t)map.offset);
	if (buffer->memory == MAP_FAILED) {
		buffer->memory = NULL;
		perror("mmap DRM dumb buffer");
		return -1;
	}
	memset(buffer->memory, 0, buffer->size);

	prime.handle = buffer->handle;
	prime.flags = DRM_CLOEXEC | DRM_RDWR;
	if (ioctl(buffer->drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) != 0) {
		perror("DRM_IOCTL_PRIME_HANDLE_TO_FD");
		return -1;
	}
	buffer->prime_fd = prime.fd;

	printf("dma-buf: card0 dumb=%dx%d stride=%" PRIu32 " bytes=%zu prime-fd=%d\n",
	       width, height, buffer->pitch, buffer->size, buffer->prime_fd);
	return 0;
}

static void destroy_dumb_buffer(struct tdvp_dumb_buffer *buffer)
{
	if (buffer->prime_fd >= 0) {
		(void)close(buffer->prime_fd);
	}
	if (buffer->memory != NULL) {
		(void)munmap(buffer->memory, buffer->size);
	}
	if (buffer->drm_fd >= 0 && buffer->handle != 0U) {
		struct drm_mode_destroy_dumb destroy = { .handle = buffer->handle };
		(void)ioctl(buffer->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	}
	if (buffer->drm_fd >= 0) {
		(void)close(buffer->drm_fd);
	}
	memset(buffer, 0, sizeof(*buffer));
	buffer->drm_fd = -1;
	buffer->prime_fd = -1;
}

static vg_lite_error_t draw_probe_path(vg_lite_buffer_t *target, int width, int height)
{
	/* This tiny closed path is the vendor triangle sample's S8 path. */
	uint8_t path_data[] = {
		2, 0, 0,
		4, 0, 1,
		6, 1, 1, 1, 0,
		4, 0, 0,
		0,
	};
	vg_lite_path_t path;
	vg_lite_matrix_t matrix;
	int edge = width < height ? width : height;
	vg_lite_error_t error;

	memset(&path, 0, sizeof(path));
	path.bounding_box[0] = 0.0f;
	path.bounding_box[1] = 1.0f;
	path.bounding_box[2] = 1.0f;
	path.bounding_box[3] = 0.0f;
	path.quality = VG_LITE_HIGH;
	path.format = VG_LITE_S8;
	path.path_length = (vg_lite_uint32_t)sizeof(path_data);
	path.path = path_data;
	path.path_changed = 1;

	error = vg_lite_identity(&matrix);
	if (error != VG_LITE_SUCCESS) {
		return error;
	}
	error = vg_lite_translate((vg_lite_float_t)width / 2.0f,
				  (vg_lite_float_t)height / 4.0f, &matrix);
	if (error != VG_LITE_SUCCESS) {
		return error;
	}
	error = vg_lite_scale((vg_lite_float_t)edge / 4.0f,
			      (vg_lite_float_t)edge / 4.0f, &matrix);
	if (error != VG_LITE_SUCCESS) {
		return error;
	}

	return vg_lite_draw(target, &path, VG_LITE_FILL_NON_ZERO, &matrix,
			    VG_LITE_BLEND_NONE, 0xff512da8U);
}

int main(int argc, char **argv)
{
	const vg_lite_color_t background = 0xff18233aU;
	const vg_lite_color_t blit_color = 0xff20b4e5U;
	struct tdvp_dumb_buffer dumb = { .drm_fd = -1, .prime_fd = -1 };
	vg_lite_buffer_t target;
	vg_lite_buffer_t source;
	vg_lite_rectangle_t source_rect;
	vg_lite_info_t info = {0};
	vg_lite_matrix_t matrix;
	vg_lite_error_t error;
	struct timespec init_started = {0};
	struct timespec init_completed = {0};
	struct timespec upload_started = {0};
	struct timespec upload_completed = {0};
	struct timespec submit_started = {0};
	struct timespec finish_started = {0};
	struct timespec finish_completed = {0};
	uint64_t empty_checksum;
	uint64_t final_checksum;
	uint64_t previous_checksum;
	uint32_t background_pixel;
	uint32_t blit_pixel;
	uint32_t path_pixel;
	struct tdvp_timing_stats upload_stats = {0};
	struct tdvp_timing_stats submit_stats = {0};
	struct tdvp_timing_stats finish_stats = {0};
	struct tdvp_timing_stats frame_stats = {0};
	char product_name[128] = {0};
	vg_lite_uint32_t chip_id = 0;
	vg_lite_uint32_t chip_revision = 0;
	int vg_device_fd = -1;
	int width = default_width;
	int height = default_height;
	int source_width = default_source_width;
	int source_height = default_source_height;
	int frames = minimum_frames;
	int inject_inflight_close = 0;
	int trace = 0;
	enum tdvp_blit_api blit_api = TDVP_BLIT_API_NORMAL;
	const struct tdvp_target_format *target_format = &target_format_bgra;
	int target_mapped = 0;
	int source_allocated = 0;
	size_t source_index;
	int vglite_initialized = 0;
	int index;
	int frame;
	int result = EXIT_FAILURE;

	for (index = 1; index < argc; ++index) {
		if (strcmp(argv[index], "--help") == 0) {
			usage(argv[0]);
			return EXIT_SUCCESS;
		}
		if (strcmp(argv[index], "--width") == 0 && index + 1 < argc) {
			if (parse_dimension(argv[++index], &width) == 0) {
				continue;
			}
		}
		if (strcmp(argv[index], "--height") == 0 && index + 1 < argc) {
			if (parse_dimension(argv[++index], &height) == 0) {
				continue;
			}
		}
		if (strcmp(argv[index], "--source-width") == 0 && index + 1 < argc) {
			if (parse_dimension(argv[++index], &source_width) == 0) {
				continue;
			}
		}
		if (strcmp(argv[index], "--source-height") == 0 && index + 1 < argc) {
			if (parse_dimension(argv[++index], &source_height) == 0) {
				continue;
			}
		}
		if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
			if (parse_frame_count(argv[++index], &frames) == 0) {
				continue;
			}
		}
		if (strcmp(argv[index], "--target-format") == 0 && index + 1 < argc) {
			target_format = parse_target_format(argv[++index]);
			if (target_format != NULL) {
				continue;
			}
		}
		if (strcmp(argv[index], "--blit-api") == 0 && index + 1 < argc) {
			if (parse_blit_api(argv[++index], &blit_api) == 0) {
				continue;
			}
		}
		if (strcmp(argv[index], "--inject-inflight-close") == 0) {
			inject_inflight_close = 1;
			continue;
		}
		if (strcmp(argv[index], "--trace") == 0) {
			trace = 1;
			continue;
		}
		fprintf(stderr, "invalid argument: %s\n", argv[index]);
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if (inject_inflight_close != 0 && frames != minimum_frames) {
		fputs("--inject-inflight-close requires --frames 1\n", stderr);
		return EXIT_FAILURE;
	}

	vg_device_fd = open_vglite_device();
	if (vg_device_fd < 0) {
		goto cleanup;
	}
	(void)close(vg_device_fd);
	vg_device_fd = -1;
	if (require_finite_wait_watchdog() != 0) {
		goto cleanup;
	}

	if (create_dumb_buffer(&dumb, width, height) != 0) {
		goto cleanup;
	}
	/*
	 * Keep the validation cost independent of the output size. Full-frame FNV
	 * hashing made a 1232x568 repeated-submit test CPU-bound on K230 and could
	 * falsely look like a VGLite queue stall. These three fixed samples cover
	 * the clear-only region, CPU-upload blit, and anti-aliased path.
	 */
	empty_checksum = sample_signature(
		pixel_at(&dumb, 2, height - 2),
		pixel_at(&dumb, 20, 20),
		pixel_at(&dumb, width / 2 + 8, height / 4 + 8));
	report_memory("before-init");

	if (clock_gettime(CLOCK_MONOTONIC, &init_started) != 0) {
		perror("clock_gettime");
		goto cleanup;
	}
	error = vg_lite_init(width, height);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_init(%d, %d) failed: %d\n", width, height, error);
		goto cleanup;
	}
	if (clock_gettime(CLOCK_MONOTONIC, &init_completed) != 0) {
		perror("clock_gettime");
		goto cleanup;
	}
	vglite_initialized = 1;

	error = vg_lite_get_info(&info);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_get_info failed: %d\n", error);
		goto cleanup;
	}
	(void)vg_lite_get_product_info(product_name, &chip_id, &chip_revision);
	printf("vglite: product=%s chip=0x%08" PRIx32 " revision=0x%08" PRIx32
	       " api=0x%08" PRIx32 " header=0x%08" PRIx32 " release=0x%08" PRIx32 "\n",
	       product_name[0] == '\0' ? "unknown" : product_name, chip_id, chip_revision,
	       info.api_version, info.header_version, info.release_version);

	memset(&target, 0, sizeof(target));
	target.width = width;
	target.height = height;
	target.stride = (vg_lite_int32_t)dumb.pitch;
	target.format = target_format->vglite_format;
	target.memory = dumb.memory;
	error = vg_lite_map(&target, VG_LITE_MAP_DMABUF, dumb.prime_fd);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_map(DMA-BUF) failed: %d\n", error);
		goto cleanup;
	}
	target_mapped = 1;

	memset(&source, 0, sizeof(source));
	source.width = source_width;
	source.height = source_height;
	source.stride = source.width * (vg_lite_int32_t)sizeof(uint32_t);
	source.format = VG_LITE_BGRA8888;
	/*
	 * The K230 kernel rejects VG_LITE_MAP_USER_MEMORY for arbitrary userspace
	 * mappings.  wlroots must consequently upload Wayland SHM damage into a
	 * VGLite-owned texture.  Verify that the allocation is CPU writable before
	 * it becomes a GPU source, rather than only exercising GPU-generated source
	 * pixels.
	 */
	error = vg_lite_allocate(&source);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_allocate(cpu-upload source) failed: %d\n", error);
		goto cleanup;
	}
	source_allocated = 1;
	if (source.memory == NULL) {
		fputs("vg_lite_allocate returned no CPU mapping for source\n", stderr);
		goto cleanup;
	}
	/* Rect mode deliberately describes the entire source. It isolates the
	 * vendor API's source-cache path without changing the sampled output. */
	source_rect.x = 0;
	source_rect.y = 0;
	source_rect.width = source.width;
	source_rect.height = source.height;

	/*
	 * Exercise the same buffer for a bounded series of submissions.  Every
	 * frame changes both a CPU-uploaded source and the target background, then
	 * verifies from the CPU mapping that VGLite's finish made that exact frame
	 * visible.  This catches the otherwise invisible cache/synchronization
	 * failure where one isolated GPU command succeeds but later frames retain
	 * stale contents.  It remains entirely off-screen and does not create a
	 * framebuffer or interact with the active KMS state.
	 */
	previous_checksum = empty_checksum;
	for (frame = 0; frame < frames; ++frame) {
		const vg_lite_color_t frame_background =
			background ^ ((vg_lite_color_t)(frame + 1) * 0x00010101U);
		const vg_lite_color_t frame_blit_color =
			blit_color ^ ((vg_lite_color_t)(frame + 1) * 0x00000101U);
		double upload_ms;
		double submit_ms;
		double finish_ms;
		double frame_ms;

		/* Keep every CPU-written source pixel in the same BGRA byte order as
		 * the vendor VGLite target used below. */
		if (clock_gettime(CLOCK_MONOTONIC, &upload_started) != 0) {
			perror("clock_gettime");
			goto cleanup;
		}
		for (source_index = 0;
		     source_index < (size_t)source.width * (size_t)source.height;
		     ++source_index) {
			((uint32_t *)source.memory)[source_index] = frame_blit_color;
		}
		if (clock_gettime(CLOCK_MONOTONIC, &upload_completed) != 0) {
			perror("clock_gettime");
			goto cleanup;
		}
		if (clock_gettime(CLOCK_MONOTONIC, &submit_started) != 0) {
			perror("clock_gettime");
			goto cleanup;
		}
		if (trace != 0) {
			fprintf(stderr, "trace: frame=%d/%d stage=clear\n", frame + 1, frames);
			(void)fflush(stderr);
		}
		error = vg_lite_clear(&target, NULL, frame_background);
		if (error != VG_LITE_SUCCESS) {
			fprintf(stderr, "vg_lite_clear(target) failed at frame %d: %d\n", frame, error);
			goto cleanup;
		}
		error = vg_lite_identity(&matrix);
		if (error != VG_LITE_SUCCESS) {
			fprintf(stderr, "vg_lite_identity failed at frame %d: %d\n", frame, error);
			goto cleanup;
		}
		error = vg_lite_translate(16.0f, 16.0f, &matrix);
		if (error != VG_LITE_SUCCESS) {
			fprintf(stderr, "vg_lite_translate(blit) failed at frame %d: %d\n", frame, error);
			goto cleanup;
		}
		if (trace != 0) {
			fprintf(stderr, "trace: frame=%d/%d stage=blit api=%s\n",
				frame + 1, frames, blit_api_name(blit_api));
			(void)fflush(stderr);
		}
		if (blit_api == TDVP_BLIT_API_RECT) {
			error = vg_lite_blit_rect(&target, &source, &source_rect, &matrix,
				VG_LITE_BLEND_NONE, 0U, VG_LITE_FILTER_POINT);
		} else {
			error = vg_lite_blit(&target, &source, &matrix, VG_LITE_BLEND_NONE,
				0U, VG_LITE_FILTER_POINT);
		}
		if (error != VG_LITE_SUCCESS) {
			fprintf(stderr, "%s failed at frame %d: %d\n", blit_api_name(blit_api),
				frame, error);
			goto cleanup;
		}
		if (trace != 0) {
			fprintf(stderr, "trace: frame=%d/%d stage=path\n", frame + 1, frames);
			(void)fflush(stderr);
		}
		error = draw_probe_path(&target, width, height);
		if (error != VG_LITE_SUCCESS) {
			fprintf(stderr, "vg_lite_draw(path) failed at frame %d: %d\n", frame, error);
			goto cleanup;
		}
		if (inject_inflight_close != 0) {
			/*
			 * vg_lite_flush() submits this command buffer without waiting for it.
			 * _exit() deliberately bypasses vg_lite_free(), vg_lite_unmap(), and
			 * vg_lite_close(), so the kernel's file-release path must recover the
			 * active command before its file-owned resources can be reclaimed.
			 */
			error = vg_lite_flush();
			if (error != VG_LITE_SUCCESS) {
				fprintf(stderr, "vg_lite_flush(inflight-close) failed: %d\n", error);
				goto cleanup;
			}
			printf("fault-injection: submitted=yes wait=skipped cleanup=skipped "
			       "action=inflight-close\n");
			(void)fflush(NULL);
			_exit(EXIT_SUCCESS);
		}
		if (clock_gettime(CLOCK_MONOTONIC, &finish_started) != 0) {
			perror("clock_gettime");
			goto cleanup;
		}
		if (trace != 0) {
			fprintf(stderr, "trace: frame=%d/%d stage=finish\n", frame + 1, frames);
			(void)fflush(stderr);
		}
		error = vg_lite_finish();
		if (error != VG_LITE_SUCCESS) {
			fprintf(stderr, "vg_lite_finish failed at frame %d: %d\n", frame, error);
			goto cleanup;
		}
		if (clock_gettime(CLOCK_MONOTONIC, &finish_completed) != 0) {
			perror("clock_gettime");
			goto cleanup;
		}

		upload_ms = elapsed_milliseconds(&upload_started, &upload_completed);
		submit_ms = elapsed_milliseconds(&submit_started, &finish_started);
		finish_ms = elapsed_milliseconds(&finish_started, &finish_completed);
		frame_ms = elapsed_milliseconds(&upload_started, &finish_completed);
		if (trace != 0) {
			fprintf(stderr, "trace: frame=%d/%d stage=complete upload_ms=%.3f "
				"submit_ms=%.3f finish_ms=%.3f frame_ms=%.3f\n",
				frame + 1, frames, upload_ms, submit_ms, finish_ms, frame_ms);
			(void)fflush(stderr);
		}
		record_timing(&upload_stats, upload_ms);
		record_timing(&submit_stats, submit_ms);
		record_timing(&finish_stats, finish_ms);
		record_timing(&frame_stats, frame_ms);

		if (read_target_samples(&dumb, &background_pixel, &blit_pixel,
				&path_pixel, width, height) != 0) {
			goto cleanup;
		}
		final_checksum = sample_signature(background_pixel, blit_pixel, path_pixel);
		if (trace != 0) {
			fprintf(stderr, "trace: frame=%d/%d stage=samples signature=%016" PRIx64 "\n",
				frame + 1, frames, final_checksum);
			(void)fflush(stderr);
		}
		if (final_checksum == empty_checksum || final_checksum == previous_checksum ||
			background_pixel == blit_pixel || background_pixel == path_pixel ||
			blit_pixel == path_pixel) {
			fprintf(stderr, "frame %d did not expose distinct, fresh GPU output\n", frame);
			goto cleanup;
		}
		previous_checksum = final_checksum;
	}

	printf("render: frames=%d target=%s source=vg-allocated-cpu-upload:%dx%d "
	       "blit-api=%s clear=ok blit=ok path=ok finish=ok checksum-empty=%016" PRIx64
	       " checksum-final=%016" PRIx64 " sampled-pixels=3\n",
	       frames, target_format->name, source_width, source_height, blit_api_name(blit_api),
	       empty_checksum,
	       final_checksum);
	printf("samples: background=0x%08" PRIx32 " blit=0x%08" PRIx32
	       " path=0x%08" PRIx32 "\n", background_pixel, blit_pixel, path_pixel);

	printf("timing: vg_lite_init=%.3f ms frames=%d\n",
	       elapsed_milliseconds(&init_started, &init_completed), frames);
	printf("timing-upload: min=%.3f ms avg=%.3f ms max=%.3f ms total=%.3f ms\n",
	       upload_stats.min_ms, upload_stats.total_ms / (double)frames,
	       upload_stats.max_ms, upload_stats.total_ms);
	printf("timing-submit: min=%.3f ms avg=%.3f ms max=%.3f ms total=%.3f ms\n",
	       submit_stats.min_ms, submit_stats.total_ms / (double)frames,
	       submit_stats.max_ms, submit_stats.total_ms);
	printf("timing-finish: min=%.3f ms avg=%.3f ms max=%.3f ms total=%.3f ms\n",
	       finish_stats.min_ms, finish_stats.total_ms / (double)frames,
	       finish_stats.max_ms, finish_stats.total_ms);
	printf("timing-frame: min=%.3f ms avg=%.3f ms max=%.3f ms total=%.3f ms\n",
	       frame_stats.min_ms, frame_stats.total_ms / (double)frames,
	       frame_stats.max_ms, frame_stats.total_ms);
	report_memory("after-render");
	puts("tdvp-vglite-probe: PASS (off-screen only; no KMS state was modified)");
	result = EXIT_SUCCESS;

cleanup:
	if (source_allocated != 0) {
		(void)vg_lite_free(&source);
	}
	if (target_mapped != 0) {
		(void)vg_lite_unmap(&target);
	}
	if (vglite_initialized != 0) {
		(void)vg_lite_close();
	}
	destroy_dumb_buffer(&dumb);
	if (vg_device_fd >= 0) {
		(void)close(vg_device_fd);
	}
	return result;
}
