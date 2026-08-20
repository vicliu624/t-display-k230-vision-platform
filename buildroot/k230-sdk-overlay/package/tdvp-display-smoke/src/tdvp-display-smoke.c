#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define TDVP_MAX_PROPS 64

struct tdvp_props {
	drmModePropertyPtr props[TDVP_MAX_PROPS];
	unsigned int count;
};

struct tdvp_display {
	int fd;
	const char *device;
	drmModeResPtr res;
	drmModeConnectorPtr conn;
	drmModeModeInfo mode;
	uint32_t conn_id;
	uint32_t crtc_id;
	int crtc_idx;
	uint32_t mode_blob_id;
	struct tdvp_props conn_props;
	struct tdvp_props crtc_props;
	struct tdvp_props plane_props;
	uint32_t plane_id;
	uint32_t fourcc;
};

struct tdvp_buffer {
	uint32_t handle;
	uint32_t fb_id;
	uint32_t pitch;
	uint64_t size;
	void *map;
};

struct tdvp_color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

enum tdvp_pattern {
	TDVP_PATTERN_BARS,
	TDVP_PATTERN_BLACK,
	TDVP_PATTERN_WHITE,
	TDVP_PATTERN_RED,
	TDVP_PATTERN_GREEN,
	TDVP_PATTERN_BLUE,
	TDVP_PATTERN_VLINES,
	TDVP_PATTERN_HLINES,
	TDVP_PATTERN_CHECKER,
	TDVP_PATTERN_STRIDE,
	TDVP_PATTERN_GRID,
	TDVP_PATTERN_COUNTER,
};

struct tdvp_options {
	const char *device;
	const char *format_name;
	enum tdvp_pattern pattern;
	unsigned int seconds;
	unsigned int cell;
	unsigned int fps;
};

static const uint32_t tdvp_format_candidates[] = {
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
};

static void tdvp_fourcc_name(uint32_t fourcc, char out[5])
{
	out[0] = (char)((fourcc >> 0) & 0xff);
	out[1] = (char)((fourcc >> 8) & 0xff);
	out[2] = (char)((fourcc >> 16) & 0xff);
	out[3] = (char)((fourcc >> 24) & 0xff);
	out[4] = '\0';
}

static void tdvp_print_errno(const char *what)
{
	fprintf(stderr, "tdvp-display-smoke: %s failed: %s\n", what,
		strerror(errno));
}

static void tdvp_usage(const char *argv0)
{
	printf("usage: %s [options]\n", argv0);
	printf("\n");
	printf("DRM/KMS validation-only scanout test for T-Display K230.\n");
	printf("\n");
	printf("  --device PATH       DRM node (default: /dev/dri/card0)\n");
	printf("  --seconds N         hold/run duration (default: 5)\n");
	printf("  --format NAME       auto or a DRM fourcc such as RG24/XR24\n");
	printf("  --pattern NAME      bars, black, white, red, green, blue,\n");
	printf("                      vlines, hlines, checker, stride, grid, counter\n");
	printf("  --cell N            line/checker/grid cell width in pixels\n");
	printf("  --fps N             counter updates per second, 1..60 (default: 1)\n");
	printf("\n");
	printf("counter uses two dumb buffers: first commit modesets, later commits\n");
	printf("only replace the plane framebuffer. All coordinates are raw DRM mode\n");
	printf("coordinates, before any fbcon or userspace rotation policy.\n");
}

static void tdvp_sleep_seconds(unsigned int seconds)
{
	struct timespec req;

	req.tv_sec = seconds;
	req.tv_nsec = 0;

	while (nanosleep(&req, &req) < 0 && errno == EINTR) {
	}
}

static void tdvp_sleep_milliseconds(unsigned int milliseconds)
{
	struct timespec req;

	req.tv_sec = milliseconds / 1000;
	req.tv_nsec = (long)(milliseconds % 1000) * 1000000L;

	while (nanosleep(&req, &req) < 0 && errno == EINTR) {
	}
}

static int tdvp_parse_uint(const char *value, unsigned int *out)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno || !end || *end || parsed > UINT_MAX)
		return -1;

	*out = (unsigned int)parsed;
	return 0;
}

static int tdvp_load_props(int fd, uint32_t object_id, uint32_t object_type,
			   struct tdvp_props *out)
{
	drmModeObjectPropertiesPtr object_props;
	unsigned int i;

	memset(out, 0, sizeof(*out));

	object_props = drmModeObjectGetProperties(fd, object_id, object_type);
	if (!object_props) {
		tdvp_print_errno("drmModeObjectGetProperties");
		return -1;
	}

	for (i = 0; i < object_props->count_props && i < TDVP_MAX_PROPS; ++i) {
		out->props[i] = drmModeGetProperty(fd, object_props->props[i]);
		if (!out->props[i]) {
			tdvp_print_errno("drmModeGetProperty");
			drmModeFreeObjectProperties(object_props);
			return -1;
		}
		out->count++;
	}

	drmModeFreeObjectProperties(object_props);
	return 0;
}

static void tdvp_free_props(struct tdvp_props *props)
{
	unsigned int i;

	for (i = 0; i < props->count; ++i) {
		if (props->props[i]) {
			drmModeFreeProperty(props->props[i]);
			props->props[i] = NULL;
		}
	}

	props->count = 0;
}

static uint32_t tdvp_find_prop_id(const struct tdvp_props *props,
				  const char *name)
{
	unsigned int i;

	for (i = 0; i < props->count; ++i) {
		if (props->props[i] && strcmp(props->props[i]->name, name) == 0)
			return props->props[i]->prop_id;
	}

	return 0;
}

static int tdvp_add_prop(drmModeAtomicReqPtr req, uint32_t object_id,
			 const struct tdvp_props *props, const char *name,
			 uint64_t value)
{
	uint32_t prop_id = tdvp_find_prop_id(props, name);
	int ret;

	if (!prop_id) {
		fprintf(stderr, "tdvp-display-smoke: missing DRM property %s\n",
			name);
		return -1;
	}

	ret = drmModeAtomicAddProperty(req, object_id, prop_id, value);
	if (ret < 0) {
		tdvp_print_errno("drmModeAtomicAddProperty");
		return -1;
	}

	return 0;
}

static int tdvp_find_crtc_index(const drmModeResPtr res, uint32_t crtc_id)
{
	int i;

	for (i = 0; i < res->count_crtcs; ++i) {
		if (res->crtcs[i] == crtc_id)
			return i;
	}

	return -1;
}

static int tdvp_select_connector(struct tdvp_display *display)
{
	int i;

	for (i = 0; i < display->res->count_connectors; ++i) {
		drmModeConnectorPtr conn;

		conn = drmModeGetConnector(display->fd,
					   display->res->connectors[i]);
		if (!conn)
			continue;

		if (conn->connection == DRM_MODE_CONNECTED &&
		    conn->count_modes > 0) {
			display->conn = conn;
			display->conn_id = conn->connector_id;
			return 0;
		}

		drmModeFreeConnector(conn);
	}

	fprintf(stderr,
		"tdvp-display-smoke: no connected connector with modes found\n");
	return -1;
}

static int tdvp_select_mode(struct tdvp_display *display)
{
	int i;

	for (i = 0; i < display->conn->count_modes; ++i) {
		const drmModeModeInfo *mode = &display->conn->modes[i];

		if (mode->hdisplay <= 1920 && mode->vdisplay <= 1280 &&
		    mode->vrefresh <= 65) {
			display->mode = *mode;
			return 0;
		}
	}

	display->mode = display->conn->modes[0];
	return 0;
}

static int tdvp_select_crtc(struct tdvp_display *display)
{
	drmModeEncoderPtr enc = NULL;
	int i;

	if (display->conn->encoder_id) {
		enc = drmModeGetEncoder(display->fd, display->conn->encoder_id);
		if (enc && enc->crtc_id) {
			display->crtc_id = enc->crtc_id;
			display->crtc_idx =
				tdvp_find_crtc_index(display->res, enc->crtc_id);
			drmModeFreeEncoder(enc);
			return display->crtc_idx >= 0 ? 0 : -1;
		}
		if (enc)
			drmModeFreeEncoder(enc);
	}

	for (i = 0; i < display->conn->count_encoders; ++i) {
		int crtc;

		enc = drmModeGetEncoder(display->fd, display->conn->encoders[i]);
		if (!enc)
			continue;

		for (crtc = 0; crtc < display->res->count_crtcs; ++crtc) {
			if (enc->possible_crtcs & (1U << crtc)) {
				display->crtc_id = display->res->crtcs[crtc];
				display->crtc_idx = crtc;
				drmModeFreeEncoder(enc);
				return 0;
			}
		}

		drmModeFreeEncoder(enc);
	}

	fprintf(stderr, "tdvp-display-smoke: no suitable CRTC found\n");
	return -1;
}

static int tdvp_open_display(struct tdvp_display *display, const char *device)
{
	uint64_t has_dumb = 0;

	memset(display, 0, sizeof(*display));
	display->fd = -1;
	display->device = device;
	display->crtc_idx = -1;

	display->fd = open(device, O_RDWR | O_CLOEXEC);
	if (display->fd < 0) {
		tdvp_print_errno(device);
		return -1;
	}

	if (drmGetCap(display->fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0) {
		tdvp_print_errno("drmGetCap(DRM_CAP_DUMB_BUFFER)");
		return -1;
	}

	if (!has_dumb) {
		fprintf(stderr,
			"tdvp-display-smoke: DRM device does not support dumb buffers\n");
		return -1;
	}

	(void)drmSetClientCap(display->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	if (drmSetClientCap(display->fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
		tdvp_print_errno("drmSetClientCap(DRM_CLIENT_CAP_ATOMIC)");
		return -1;
	}

	display->res = drmModeGetResources(display->fd);
	if (!display->res) {
		tdvp_print_errno("drmModeGetResources");
		return -1;
	}

	if (tdvp_select_connector(display) < 0)
		return -1;

	tdvp_select_mode(display);

	if (tdvp_select_crtc(display) < 0)
		return -1;

	if (drmModeCreatePropertyBlob(display->fd, &display->mode,
				      sizeof(display->mode),
				      &display->mode_blob_id) < 0) {
		tdvp_print_errno("drmModeCreatePropertyBlob");
		return -1;
	}

	if (tdvp_load_props(display->fd, display->conn_id,
			    DRM_MODE_OBJECT_CONNECTOR,
			    &display->conn_props) < 0)
		return -1;

	if (tdvp_load_props(display->fd, display->crtc_id, DRM_MODE_OBJECT_CRTC,
			    &display->crtc_props) < 0)
		return -1;

	printf("tdvp-display-smoke: connector=%u crtc=%u mode=%ux%u@%u\n",
	       display->conn_id, display->crtc_id, display->mode.hdisplay,
	       display->mode.vdisplay, display->mode.vrefresh);

	return 0;
}

static void tdvp_close_display(struct tdvp_display *display)
{
	tdvp_free_props(&display->plane_props);
	tdvp_free_props(&display->conn_props);
	tdvp_free_props(&display->crtc_props);

	if (display->mode_blob_id) {
		drmModeDestroyPropertyBlob(display->fd, display->mode_blob_id);
		display->mode_blob_id = 0;
	}

	if (display->conn) {
		drmModeFreeConnector(display->conn);
		display->conn = NULL;
	}

	if (display->res) {
		drmModeFreeResources(display->res);
		display->res = NULL;
	}

	if (display->fd >= 0) {
		close(display->fd);
		display->fd = -1;
	}
}

static bool tdvp_plane_supports_format(const drmModePlanePtr plane,
				       uint32_t fourcc)
{
	uint32_t i;

	for (i = 0; i < plane->count_formats; ++i) {
		if (plane->formats[i] == fourcc)
			return true;
	}

	return false;
}

static int tdvp_select_plane(struct tdvp_display *display, uint32_t fourcc)
{
	drmModePlaneResPtr plane_res;
	uint32_t i;

	tdvp_free_props(&display->plane_props);
	display->plane_id = 0;
	display->fourcc = 0;

	plane_res = drmModeGetPlaneResources(display->fd);
	if (!plane_res) {
		tdvp_print_errno("drmModeGetPlaneResources");
		return -1;
	}

	for (i = 0; i < plane_res->count_planes; ++i) {
		drmModePlanePtr plane;

		plane = drmModeGetPlane(display->fd, plane_res->planes[i]);
		if (!plane)
			continue;

		if ((plane->possible_crtcs & (1U << display->crtc_idx)) &&
		    tdvp_plane_supports_format(plane, fourcc)) {
			display->plane_id = plane->plane_id;
			display->fourcc = fourcc;
			drmModeFreePlane(plane);
			drmModeFreePlaneResources(plane_res);
			return tdvp_load_props(display->fd, display->plane_id,
					       DRM_MODE_OBJECT_PLANE,
					       &display->plane_props);
		}

		drmModeFreePlane(plane);
	}

	drmModeFreePlaneResources(plane_res);
	return -1;
}

static unsigned int tdvp_format_bpp(uint32_t fourcc)
{
	switch (fourcc) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return 16;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return 24;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
		return 32;
	default:
		return 0;
	}
}

static int tdvp_create_buffer(struct tdvp_display *display,
			      struct tdvp_buffer *buffer)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;
	uint32_t handles[4] = {0};
	uint32_t pitches[4] = {0};
	uint32_t offsets[4] = {0};
	unsigned int bpp = tdvp_format_bpp(display->fourcc);

	memset(buffer, 0, sizeof(*buffer));
	buffer->map = MAP_FAILED;

	if (!bpp) {
		fprintf(stderr, "tdvp-display-smoke: unsupported format\n");
		return -1;
	}

	memset(&creq, 0, sizeof(creq));
	creq.width = display->mode.hdisplay;
	creq.height = display->mode.vdisplay;
	creq.bpp = bpp;

	if (drmIoctl(display->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		tdvp_print_errno("DRM_IOCTL_MODE_CREATE_DUMB");
		return -1;
	}

	buffer->handle = creq.handle;
	buffer->pitch = creq.pitch;
	buffer->size = creq.size;

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = buffer->handle;

	if (drmIoctl(display->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		tdvp_print_errno("DRM_IOCTL_MODE_MAP_DUMB");
		return -1;
	}

	buffer->map = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE,
			   MAP_SHARED, display->fd, mreq.offset);
	if (buffer->map == MAP_FAILED) {
		tdvp_print_errno("mmap");
		return -1;
	}

	handles[0] = buffer->handle;
	pitches[0] = buffer->pitch;
	offsets[0] = 0;

	if (drmModeAddFB2(display->fd, display->mode.hdisplay,
			  display->mode.vdisplay, display->fourcc, handles,
			  pitches, offsets, &buffer->fb_id, 0) < 0) {
		tdvp_print_errno("drmModeAddFB2");
		return -1;
	}

	return 0;
}

static void tdvp_destroy_buffer(struct tdvp_display *display,
				struct tdvp_buffer *buffer)
{
	struct drm_mode_destroy_dumb dreq;

	if (buffer->fb_id) {
		drmModeRmFB(display->fd, buffer->fb_id);
		buffer->fb_id = 0;
	}

	if (buffer->map != MAP_FAILED && buffer->map) {
		munmap(buffer->map, buffer->size);
		buffer->map = MAP_FAILED;
	}

	if (buffer->handle) {
		memset(&dreq, 0, sizeof(dreq));
		dreq.handle = buffer->handle;
		drmIoctl(display->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		buffer->handle = 0;
	}
}

static void tdvp_put_pixel(uint8_t *pixel, uint32_t fourcc,
			   struct tdvp_color color)
{
	uint16_t v16;

	switch (fourcc) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		v16 = (uint16_t)(((color.r >> 3) << 11) |
				 ((color.g >> 2) << 5) | (color.b >> 3));
		pixel[0] = (uint8_t)(v16 & 0xff);
		pixel[1] = (uint8_t)(v16 >> 8);
		break;
	case DRM_FORMAT_RGB888:
		pixel[0] = color.r;
		pixel[1] = color.g;
		pixel[2] = color.b;
		break;
	case DRM_FORMAT_BGR888:
		pixel[0] = color.b;
		pixel[1] = color.g;
		pixel[2] = color.r;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		pixel[0] = color.b;
		pixel[1] = color.g;
		pixel[2] = color.r;
		pixel[3] = color.a;
		break;
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
		pixel[0] = color.r;
		pixel[1] = color.g;
		pixel[2] = color.b;
		pixel[3] = color.a;
		break;
	case DRM_FORMAT_RGBA8888:
		pixel[0] = color.r;
		pixel[1] = color.g;
		pixel[2] = color.b;
		pixel[3] = color.a;
		break;
	case DRM_FORMAT_BGRA8888:
		pixel[0] = color.b;
		pixel[1] = color.g;
		pixel[2] = color.r;
		pixel[3] = color.a;
		break;
	default:
		break;
	}
}

static const char *tdvp_pattern_name(enum tdvp_pattern pattern)
{
	switch (pattern) {
	case TDVP_PATTERN_BARS:
		return "bars";
	case TDVP_PATTERN_BLACK:
		return "black";
	case TDVP_PATTERN_WHITE:
		return "white";
	case TDVP_PATTERN_RED:
		return "red";
	case TDVP_PATTERN_GREEN:
		return "green";
	case TDVP_PATTERN_BLUE:
		return "blue";
	case TDVP_PATTERN_VLINES:
		return "vlines";
	case TDVP_PATTERN_HLINES:
		return "hlines";
	case TDVP_PATTERN_CHECKER:
		return "checker";
	case TDVP_PATTERN_STRIDE:
		return "stride";
	case TDVP_PATTERN_GRID:
		return "grid";
	case TDVP_PATTERN_COUNTER:
		return "counter";
	default:
		return "unknown";
	}
}

static int tdvp_parse_pattern(const char *name, enum tdvp_pattern *pattern)
{
	static const struct {
		const char *name;
		enum tdvp_pattern pattern;
	} patterns[] = {
		{ "bars", TDVP_PATTERN_BARS },
		{ "black", TDVP_PATTERN_BLACK },
		{ "white", TDVP_PATTERN_WHITE },
		{ "red", TDVP_PATTERN_RED },
		{ "green", TDVP_PATTERN_GREEN },
		{ "blue", TDVP_PATTERN_BLUE },
		{ "vlines", TDVP_PATTERN_VLINES },
		{ "hlines", TDVP_PATTERN_HLINES },
		{ "checker", TDVP_PATTERN_CHECKER },
		{ "stride", TDVP_PATTERN_STRIDE },
		{ "grid", TDVP_PATTERN_GRID },
		{ "counter", TDVP_PATTERN_COUNTER },
	};
	unsigned int i;

	for (i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
		if (strcasecmp(name, patterns[i].name) == 0) {
			*pattern = patterns[i].pattern;
			return 0;
		}
	}

	return -1;
}

static unsigned int tdvp_default_cell(enum tdvp_pattern pattern)
{
	switch (pattern) {
	case TDVP_PATTERN_VLINES:
	case TDVP_PATTERN_HLINES:
		return 1;
	case TDVP_PATTERN_CHECKER:
		return 16;
	case TDVP_PATTERN_STRIDE:
		return 8;
	case TDVP_PATTERN_GRID:
	case TDVP_PATTERN_COUNTER:
		return 64;
	default:
		return 1;
	}
}

static bool tdvp_pattern_is_dynamic(enum tdvp_pattern pattern)
{
	return pattern == TDVP_PATTERN_COUNTER;
}

static struct tdvp_color tdvp_color(uint8_t r, uint8_t g, uint8_t b)
{
	struct tdvp_color color = { r, g, b, 255 };

	return color;
}

static struct tdvp_color tdvp_pattern_color(enum tdvp_pattern pattern,
					    uint32_t x, uint32_t y,
					    uint32_t width, uint32_t height,
					    unsigned int cell, uint64_t frame)
{
	const struct tdvp_color bars[] = {
		{ 255, 0, 0, 255 },
		{ 0, 255, 0, 255 },
		{ 0, 0, 255, 255 },
		{ 255, 255, 255, 255 },
	};
	uint32_t marker_x;
	uint32_t marker_y;

	switch (pattern) {
	case TDVP_PATTERN_BARS:
		return bars[(x * (sizeof(bars) / sizeof(bars[0]))) / width];
	case TDVP_PATTERN_BLACK:
		return tdvp_color(0, 0, 0);
	case TDVP_PATTERN_WHITE:
		return tdvp_color(255, 255, 255);
	case TDVP_PATTERN_RED:
		return tdvp_color(255, 0, 0);
	case TDVP_PATTERN_GREEN:
		return tdvp_color(0, 255, 0);
	case TDVP_PATTERN_BLUE:
		return tdvp_color(0, 0, 255);
	case TDVP_PATTERN_VLINES:
		return ((x / cell) & 1) ? tdvp_color(255, 255, 255) :
			tdvp_color(0, 0, 0);
	case TDVP_PATTERN_HLINES:
		return ((y / cell) & 1) ? tdvp_color(255, 255, 255) :
			tdvp_color(0, 0, 0);
	case TDVP_PATTERN_CHECKER:
		return (((x / cell) + (y / cell)) & 1) ?
			tdvp_color(255, 255, 255) : tdvp_color(0, 0, 0);
	case TDVP_PATTERN_STRIDE:
		if ((x % cell) == 0)
			return tdvp_color(255, 255, 255);
		return ((y / cell) & 1) ? tdvp_color(0, 80, 255) :
			tdvp_color(0, 20, 80);
	case TDVP_PATTERN_GRID:
		if (x < 4)
			return tdvp_color(255, 0, 0);
		if (y < 4)
			return tdvp_color(0, 255, 0);
		if (x >= width - 4)
			return tdvp_color(0, 0, 255);
		if (y >= height - 4)
			return tdvp_color(255, 255, 255);
		if ((x % cell) < 2 || (y % cell) < 2)
			return tdvp_color(255, 255, 0);
		return (((x / cell) + (y / cell)) & 1) ?
			tdvp_color(30, 30, 30) : tdvp_color(100, 100, 100);
	case TDVP_PATTERN_COUNTER:
		marker_x = (uint32_t)(frame % width);
		marker_y = (uint32_t)((frame * 3U) % height);
		if ((x >= marker_x && x < marker_x + 8) ||
		    (y >= marker_y && y < marker_y + 8))
			return tdvp_color(255, 255, 0);
		if ((x % cell) < 2 || (y % cell) < 2)
			return tdvp_color(0, 255, 255);
		return (((x / cell) + (y / cell)) & 1) ?
			tdvp_color(0, 18, 60) : tdvp_color(0, 45, 120);
	default:
		return tdvp_color(0, 0, 0);
	}
}

static void tdvp_fill_pattern(const struct tdvp_display *display,
			      const struct tdvp_buffer *buffer,
			      enum tdvp_pattern pattern, unsigned int cell,
			      uint64_t frame)
{
	unsigned int bytes_per_pixel = tdvp_format_bpp(display->fourcc) / 8;
	uint32_t active_bytes = display->mode.hdisplay * bytes_per_pixel;
	uint32_t x;
	uint32_t y;
	uint8_t *map = buffer->map;

	for (y = 0; y < display->mode.vdisplay; ++y) {
		uint8_t *row = map + y * buffer->pitch;

		for (x = 0; x < display->mode.hdisplay; ++x) {
			tdvp_put_pixel(row + x * bytes_per_pixel, display->fourcc,
				       tdvp_pattern_color(pattern, x, y,
						  display->mode.hdisplay,
						  display->mode.vdisplay, cell, frame));
		}

		/* A nonzero, per-row padding value exposes an incorrect DMA pitch. */
		if (buffer->pitch > active_bytes) {
			memset(row + active_bytes, (unsigned char)(0xa5U ^ y),
			       buffer->pitch - active_bytes);
		}
	}
}

static int tdvp_commit_buffer(struct tdvp_display *display,
			      const struct tdvp_buffer *buffer, bool modeset)
{
	drmModeAtomicReqPtr req;
	int ret;

	req = drmModeAtomicAlloc();
	if (!req) {
		fprintf(stderr, "tdvp-display-smoke: drmModeAtomicAlloc failed\n");
		return -1;
	}

	ret = 0;
	if (modeset) {
		if (tdvp_add_prop(req, display->conn_id, &display->conn_props,
				  "CRTC_ID", display->crtc_id) < 0)
			ret = -1;
		if (tdvp_add_prop(req, display->crtc_id, &display->crtc_props,
				  "MODE_ID", display->mode_blob_id) < 0)
			ret = -1;
		if (tdvp_add_prop(req, display->crtc_id, &display->crtc_props,
				  "ACTIVE", 1) < 0)
			ret = -1;
	}
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props,
			  "FB_ID", buffer->fb_id) < 0)
		ret = -1;
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props,
			  "CRTC_ID", display->crtc_id) < 0)
		ret = -1;
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props, "SRC_X",
			  0) < 0)
		ret = -1;
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props, "SRC_Y",
			  0) < 0)
		ret = -1;
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props, "SRC_W",
			  (uint64_t)display->mode.hdisplay << 16) < 0)
		ret = -1;
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props, "SRC_H",
			  (uint64_t)display->mode.vdisplay << 16) < 0)
		ret = -1;
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props, "CRTC_X",
			  0) < 0)
		ret = -1;
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props, "CRTC_Y",
			  0) < 0)
		ret = -1;
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props, "CRTC_W",
			  display->mode.hdisplay) < 0)
		ret = -1;
	if (tdvp_add_prop(req, display->plane_id, &display->plane_props, "CRTC_H",
			  display->mode.vdisplay) < 0)
		ret = -1;

	if (!ret && drmModeAtomicCommit(display->fd, req,
				       modeset ? DRM_MODE_ATOMIC_ALLOW_MODESET : 0,
				       NULL) < 0) {
		tdvp_print_errno("drmModeAtomicCommit");
		ret = -1;
	}

	drmModeAtomicFree(req);
	return ret;
}

static bool tdvp_requested_format_matches(const char *requested,
					  uint32_t fourcc)
{
	char name[5];

	if (strcasecmp(requested, "auto") == 0)
		return true;

	tdvp_fourcc_name(fourcc, name);
	return strcasecmp(requested, name) == 0;
}

static int tdvp_select_test_format(struct tdvp_display *display,
				   const struct tdvp_options *options,
				   struct tdvp_buffer *buffer)
{
	char name[5];
	unsigned int i;

	for (i = 0; i < sizeof(tdvp_format_candidates) /
			    sizeof(tdvp_format_candidates[0]);
	     ++i) {
		uint32_t fourcc = tdvp_format_candidates[i];

		tdvp_fourcc_name(fourcc, name);
		if (!tdvp_requested_format_matches(options->format_name, fourcc))
			continue;
		printf("tdvp-display-smoke: trying format %s\n", name);

		if (tdvp_select_plane(display, fourcc) < 0)
			continue;

		if (tdvp_create_buffer(display, buffer) < 0) {
			tdvp_destroy_buffer(display, buffer);
			continue;
		}

		return 0;
	}

	fprintf(stderr,
		"tdvp-display-smoke: no candidate format/plane could be created\n");
	return -1;
}

static void tdvp_print_test_info(const struct tdvp_display *display,
				 const struct tdvp_buffer *buffer,
				 const struct tdvp_options *options)
{
	char name[5];
	unsigned int bytes_per_pixel = tdvp_format_bpp(display->fourcc) / 8;

	tdvp_fourcc_name(display->fourcc, name);
	printf("tdvp-display-smoke: TEST pattern=%s mode=%ux%u format=%s bpp=%u plane=%u pitch=%u active_bytes=%u size=%llu\n",
	       tdvp_pattern_name(options->pattern), display->mode.hdisplay,
	       display->mode.vdisplay, name, bytes_per_pixel * 8,
	       display->plane_id, buffer->pitch,
	       display->mode.hdisplay * bytes_per_pixel,
	       (unsigned long long)buffer->size);
}

static int tdvp_run_static(struct tdvp_display *display,
			   const struct tdvp_options *options,
			   struct tdvp_buffer *buffer)
{
	tdvp_fill_pattern(display, buffer, options->pattern, options->cell, 0);
	if (tdvp_commit_buffer(display, buffer, true) < 0)
		return -1;

	printf("tdvp-display-smoke: PASS static pattern=%s hold_seconds=%u\n",
	       tdvp_pattern_name(options->pattern), options->seconds);
	tdvp_sleep_seconds(options->seconds);
	return 0;
}

static int tdvp_run_counter(struct tdvp_display *display,
			    const struct tdvp_options *options,
			    struct tdvp_buffer *buffers)
{
	uint64_t frames = (uint64_t)options->seconds * options->fps;
	uint64_t frame;
	unsigned int interval_ms = 1000U / options->fps;

	tdvp_fill_pattern(display, &buffers[0], options->pattern, options->cell, 0);
	tdvp_fill_pattern(display, &buffers[1], options->pattern, options->cell, 0);
	if (tdvp_commit_buffer(display, &buffers[0], true) < 0)
		return -1;

	printf("tdvp-display-smoke: PASS initial modeset pattern=counter frames=%llu fps=%u\n",
	       (unsigned long long)frames, options->fps);
	for (frame = 1; frame <= frames; ++frame) {
		struct tdvp_buffer *next = &buffers[frame & 1U];

		tdvp_fill_pattern(display, next, options->pattern, options->cell,
				  frame);
		if (tdvp_commit_buffer(display, next, false) < 0) {
			fprintf(stderr,
				"tdvp-display-smoke: dynamic commit failed at frame=%llu\n",
				(unsigned long long)frame);
			return -1;
		}

		if ((frame % options->fps) == 0) {
			printf("tdvp-display-smoke: frame=%llu elapsed_seconds=%llu\n",
			       (unsigned long long)frame,
			       (unsigned long long)(frame / options->fps));
		}
		tdvp_sleep_milliseconds(interval_ms);
	}

	printf("tdvp-display-smoke: PASS dynamic pattern=counter frames=%llu\n",
	       (unsigned long long)frames);
	return 0;
}

static int tdvp_run_smoke(struct tdvp_display *display,
			  const struct tdvp_options *options)
{
	struct tdvp_buffer buffers[2];
	bool dynamic = tdvp_pattern_is_dynamic(options->pattern);
	int ret;

	memset(buffers, 0, sizeof(buffers));
	buffers[0].map = MAP_FAILED;
	buffers[1].map = MAP_FAILED;

	if (tdvp_select_test_format(display, options, &buffers[0]) < 0)
		return -1;

	if (dynamic && tdvp_create_buffer(display, &buffers[1]) < 0) {
		tdvp_destroy_buffer(display, &buffers[1]);
		tdvp_destroy_buffer(display, &buffers[0]);
		return -1;
	}

	tdvp_print_test_info(display, &buffers[0], options);
	if (dynamic)
		ret = tdvp_run_counter(display, options, buffers);
	else
		ret = tdvp_run_static(display, options, &buffers[0]);

	tdvp_destroy_buffer(display, &buffers[1]);
	tdvp_destroy_buffer(display, &buffers[0]);
	return ret;
}

int main(int argc, char **argv)
{
	struct tdvp_options options = {
		.device = "/dev/dri/card0",
		.format_name = "auto",
		.pattern = TDVP_PATTERN_BARS,
		.seconds = 5,
		.cell = 0,
		.fps = 1,
	};
	struct tdvp_display display;
	int i;
	int ret;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
			options.device = argv[++i];
		} else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
			if (tdvp_parse_uint(argv[++i], &options.seconds) < 0) {
				fprintf(stderr, "tdvp-display-smoke: invalid --seconds value\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
			options.format_name = argv[++i];
		} else if (strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) {
			if (tdvp_parse_pattern(argv[++i], &options.pattern) < 0) {
				fprintf(stderr, "tdvp-display-smoke: invalid --pattern value\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--cell") == 0 && i + 1 < argc) {
			if (tdvp_parse_uint(argv[++i], &options.cell) < 0 ||
			    options.cell == 0) {
				fprintf(stderr, "tdvp-display-smoke: --cell must be positive\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
			if (tdvp_parse_uint(argv[++i], &options.fps) < 0 ||
			    options.fps == 0 || options.fps > 60) {
				fprintf(stderr, "tdvp-display-smoke: --fps must be 1..60\n");
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

	if (options.cell == 0)
		options.cell = tdvp_default_cell(options.pattern);

	if (tdvp_open_display(&display, options.device) < 0) {
		tdvp_close_display(&display);
		return 1;
	}

	ret = tdvp_run_smoke(&display, &options);
	tdvp_close_display(&display);

	return ret == 0 ? 0 : 1;
}
