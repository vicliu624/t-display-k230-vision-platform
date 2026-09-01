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
#include <fcntl.h>
#include <inttypes.h>
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
	minimum_dimension = 64,
	maximum_dimension = 4096,
};

struct tdvp_dumb_buffer {
	int drm_fd;
	int prime_fd;
	uint32_t handle;
	uint32_t pitch;
	size_t size;
	void *memory;
};

static void usage(const char *program)
{
	printf("Usage: %s [--width PIXELS] [--height PIXELS]\n", program);
	puts("\n"
	     "Runs an off-screen VGLite + DRM PRIME DMA-BUF acceptance test.\n"
	     "It never takes DRM master, creates a framebuffer, mode-sets, commits, or flips.\n"
	     "Run it as the graphical user (tdvp), not as root.");
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

static size_t count_distinct_pixels(const struct tdvp_dumb_buffer *buffer, int width,
				    int height)
{
	uint32_t seen[3] = {0};
	size_t count = 0;
	int x;
	int y;

	/*
	 * Walk only real pixels, never the pitch padding.  Three distinct values
	 * prove that clear, blit and the anti-aliased path all reached the target;
	 * the count is intentionally capped because this is an acceptance test,
	 * not a slow image-analysis utility.
	 */
	for (y = 0; y < height && count < 3; ++y) {
		for (x = 0; x < width && count < 3; ++x) {
			uint32_t pixel = pixel_at(buffer, x, y);
			size_t index;

			for (index = 0; index < count; ++index) {
				if (seen[index] == pixel) {
					break;
				}
			}
			if (index == count) {
				seen[count++] = pixel;
			}
		}
	}

	return count;
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
	vg_lite_info_t info = {0};
	vg_lite_matrix_t matrix;
	vg_lite_error_t error;
	struct timespec started = {0};
	struct timespec completed = {0};
	uint64_t empty_checksum;
	uint64_t final_checksum;
	uint32_t background_pixel;
	uint32_t blit_pixel;
	uint32_t path_pixel;
	size_t distinct_pixels;
	char product_name[128] = {0};
	vg_lite_uint32_t chip_id = 0;
	vg_lite_uint32_t chip_revision = 0;
	int vg_device_fd = -1;
	int width = default_width;
	int height = default_height;
	int target_mapped = 0;
	int source_allocated = 0;
	int vglite_initialized = 0;
	int index;
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
		fprintf(stderr, "invalid argument: %s\n", argv[index]);
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	vg_device_fd = open_vglite_device();
	if (vg_device_fd < 0) {
		goto cleanup;
	}
	(void)close(vg_device_fd);
	vg_device_fd = -1;

	if (create_dumb_buffer(&dumb, width, height) != 0) {
		goto cleanup;
	}
	empty_checksum = fnv1a64(dumb.memory, dumb.size);
	report_memory("before-init");

	if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) {
		perror("clock_gettime");
		goto cleanup;
	}
	error = vg_lite_init(width, height);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_init(%d, %d) failed: %d\n", width, height, error);
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
	target.format = VG_LITE_BGRA8888;
	target.memory = dumb.memory;
	error = vg_lite_map(&target, VG_LITE_MAP_DMABUF, dumb.prime_fd);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_map(DMA-BUF) failed: %d\n", error);
		goto cleanup;
	}
	target_mapped = 1;

	memset(&source, 0, sizeof(source));
	source.width = 64;
	source.height = 48;
	source.format = VG_LITE_BGRA8888;
	error = vg_lite_allocate(&source);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_allocate(source) failed: %d\n", error);
		goto cleanup;
	}
	source_allocated = 1;

	error = vg_lite_clear(&source, NULL, blit_color);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_clear(source) failed: %d\n", error);
		goto cleanup;
	}
	error = vg_lite_clear(&target, NULL, background);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_clear(target) failed: %d\n", error);
		goto cleanup;
	}
	error = vg_lite_identity(&matrix);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_identity failed: %d\n", error);
		goto cleanup;
	}
	error = vg_lite_translate(16.0f, 16.0f, &matrix);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_translate(blit) failed: %d\n", error);
		goto cleanup;
	}
	error = vg_lite_blit(&target, &source, &matrix, VG_LITE_BLEND_NONE, 0U,
			     VG_LITE_FILTER_POINT);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_blit failed: %d\n", error);
		goto cleanup;
	}
	error = draw_probe_path(&target, width, height);
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_draw(path) failed: %d\n", error);
		goto cleanup;
	}
	error = vg_lite_finish();
	if (error != VG_LITE_SUCCESS) {
		fprintf(stderr, "vg_lite_finish failed: %d\n", error);
		goto cleanup;
	}
	if (clock_gettime(CLOCK_MONOTONIC, &completed) != 0) {
		perror("clock_gettime");
		goto cleanup;
	}

	final_checksum = fnv1a64(dumb.memory, dumb.size);
	background_pixel = pixel_at(&dumb, 2, height - 2);
	blit_pixel = pixel_at(&dumb, 20, 20);
	path_pixel = pixel_at(&dumb, width / 2 + 8, height / 4 + 8);
	distinct_pixels = count_distinct_pixels(&dumb, width, height);
	printf("render: target=VG_LITE_BGRA8888 clear=ok blit=ok path=ok finish=ok "
	       "checksum-empty=%016" PRIx64 " checksum-final=%016" PRIx64
	       " distinct-pixels>=%zu\n", empty_checksum, final_checksum, distinct_pixels);
	printf("samples: background=0x%08" PRIx32 " blit=0x%08" PRIx32
	       " path=0x%08" PRIx32 "\n", background_pixel, blit_pixel, path_pixel);

	if (final_checksum == empty_checksum || background_pixel == blit_pixel ||
		distinct_pixels < 3U) {
		fprintf(stderr, "render result did not contain the expected distinct GPU output\n");
		goto cleanup;
	}

	printf("timing: vg_lite_finish=%.3f ms\n",
	       ((double)(completed.tv_sec - started.tv_sec) * 1000.0) +
	       ((double)(completed.tv_nsec - started.tv_nsec) / 1000000.0));
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
