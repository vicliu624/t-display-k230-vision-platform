#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct tdvp_plane_properties {
	bool type_present;
	uint64_t type;
	bool in_formats_present;
	uint64_t in_formats_blob_id;
	bool in_fence_present;
	bool rotation_present;
	bool zpos_present;
	bool alpha_present;
};

static void tdvp_usage(const char *program)
{
	fprintf(stderr,
		"usage: %s [--device PATH]\n"
		"\n"
		"Observe KMS plane capabilities without modesetting, page-flipping, "
		"allocating buffers, or taking DRM master.\n",
		program);
}

static const char *tdvp_plane_type_name(uint64_t type)
{
	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		return "primary";
	case DRM_PLANE_TYPE_OVERLAY:
		return "overlay";
	case DRM_PLANE_TYPE_CURSOR:
		return "cursor";
	default:
		return "unknown";
	}
}

static bool tdvp_plane_has_format(const drmModePlanePtr plane, uint32_t format)
{
	uint32_t i;

	for (i = 0; i < plane->count_formats; ++i) {
		if (plane->formats[i] == format)
			return true;
	}
	return false;
}

static void tdvp_print_errno(const char *operation)
{
	fprintf(stderr, "tdvp-kms-capability-observer: %s: %s\n", operation,
		strerror(errno));
}

/*
 * Plane IN_FENCE_FD and CRTC OUT_FENCE_PTR properties alone are not enough to
 * advertise the linux-drm-syncobj-v1 protocol.  Report the global DRM
 * syncobj/timeline capabilities separately, without creating a syncobj or
 * taking DRM master. A zero value is a valid, observable unsupported result;
 * an ioctl/query failure is an observer failure.
 */
static int tdvp_print_drm_capability(int fd, uint64_t capability,
		const char *name)
{
	uint64_t value = 0;

	if (drmGetCap(fd, capability, &value) < 0) {
		tdvp_print_errno(name);
		return -1;
	}
	printf("tdvp-kms-capability-observer: DRM_CAP name=%s value=%" PRIu64
	       " supported=%s\n", name, value, value != 0 ? "yes" : "no");
	return 0;
}

static int tdvp_read_plane_properties(int fd, uint32_t plane_id,
				      struct tdvp_plane_properties *result)
{
	drmModeObjectPropertiesPtr properties;
	uint32_t i;

	memset(result, 0, sizeof(*result));
	properties = drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
	if (!properties) {
		tdvp_print_errno("drmModeObjectGetProperties(plane)");
		return -1;
	}

	for (i = 0; i < properties->count_props; ++i) {
		drmModePropertyPtr property =
			drmModeGetProperty(fd, properties->props[i]);
		uint64_t value = properties->prop_values[i];

		if (!property) {
			tdvp_print_errno("drmModeGetProperty(plane)");
			drmModeFreeObjectProperties(properties);
			return -1;
		}
		if (strcmp(property->name, "type") == 0) {
			result->type_present = true;
			result->type = value;
		} else if (strcmp(property->name, "IN_FORMATS") == 0) {
			result->in_formats_present = true;
			result->in_formats_blob_id = value;
		} else if (strcmp(property->name, "IN_FENCE_FD") == 0) {
			result->in_fence_present = true;
		} else if (strcmp(property->name, "rotation") == 0) {
			result->rotation_present = true;
		} else if (strcmp(property->name, "zpos") == 0) {
			result->zpos_present = true;
		} else if (strcmp(property->name, "alpha") == 0) {
			result->alpha_present = true;
		}
		drmModeFreeProperty(property);
	}

	drmModeFreeObjectProperties(properties);
	return 0;
}

static bool tdvp_range_valid(size_t blob_length, uint32_t offset, size_t size)
{
	return (size_t)offset <= blob_length && size <= blob_length - (size_t)offset;
}

static void tdvp_print_modifiers_for_format(const drmModePropertyBlobPtr blob,
					    uint32_t format, const char *format_name)
{
	const struct drm_format_modifier_blob *header = blob->data;
	const uint32_t *formats;
	const struct drm_format_modifier *modifiers;
	size_t blob_length = blob->length;
	uint32_t format_index;
	uint32_t i;
	bool printed = false;

	if (blob_length < sizeof(*header) || header->count_formats == 0 ||
	    header->count_modifiers == 0 ||
	    header->count_formats > blob_length / sizeof(*formats) ||
	    header->count_modifiers > blob_length / sizeof(*modifiers) ||
	    !tdvp_range_valid(blob_length, header->formats_offset,
			      (size_t)header->count_formats * sizeof(*formats)) ||
	    !tdvp_range_valid(blob_length, header->modifiers_offset,
			      (size_t)header->count_modifiers * sizeof(*modifiers))) {
		printf("tdvp-kms-capability-observer: MODIFIERS format=%s invalid_blob\n",
		       format_name);
		return;
	}

	formats = (const uint32_t *)((const unsigned char *)blob->data +
				     header->formats_offset);
	modifiers = (const struct drm_format_modifier *)
		((const unsigned char *)blob->data + header->modifiers_offset);
	for (format_index = 0; format_index < header->count_formats; ++format_index) {
		if (formats[format_index] != format)
			continue;

		printf("tdvp-kms-capability-observer: MODIFIERS format=%s values=",
		       format_name);
		for (i = 0; i < header->count_modifiers; ++i) {
			const struct drm_format_modifier *modifier = &modifiers[i];
			uint32_t bit;

			if (format_index < modifier->offset ||
			    format_index - modifier->offset >= 64U)
				continue;
			bit = format_index - modifier->offset;
			if ((modifier->formats & (UINT64_C(1) << bit)) == 0)
				continue;
			printf("%s0x%016llx", printed ? "," : "",
			       (unsigned long long)modifier->modifier);
			printed = true;
		}
		printf("%s\n", printed ? "" : "none");
		return;
	}

	printf("tdvp-kms-capability-observer: MODIFIERS format=%s absent\n",
	       format_name);
}

static int tdvp_print_crtc(int fd, uint32_t crtc_id)
{
	drmModeObjectPropertiesPtr properties;
	bool out_fence_present = false;
	uint32_t i;

	properties = drmModeObjectGetProperties(fd, crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!properties) {
		tdvp_print_errno("drmModeObjectGetProperties(CRTC)");
		return -1;
	}
	for (i = 0; i < properties->count_props; ++i) {
		drmModePropertyPtr property =
			drmModeGetProperty(fd, properties->props[i]);

		if (!property) {
			tdvp_print_errno("drmModeGetProperty(CRTC)");
			drmModeFreeObjectProperties(properties);
			return -1;
		}
		if (strcmp(property->name, "OUT_FENCE_PTR") == 0)
			out_fence_present = true;
		drmModeFreeProperty(property);
	}
	drmModeFreeObjectProperties(properties);
	printf("tdvp-kms-capability-observer: CRTC id=%u out_fence=%s\n",
	       crtc_id, out_fence_present ? "yes" : "no");
	return 0;
}

static int tdvp_print_plane(int fd, uint32_t plane_id)
{
	drmModePlanePtr plane;
	drmModePropertyBlobPtr in_formats = NULL;
	struct tdvp_plane_properties properties;
	const char *type_name;
	int result = -1;

	plane = drmModeGetPlane(fd, plane_id);
	if (!plane) {
		tdvp_print_errno("drmModeGetPlane");
		return -1;
	}
	if (tdvp_read_plane_properties(fd, plane_id, &properties) < 0)
		goto out;

	type_name = properties.type_present ?
		tdvp_plane_type_name(properties.type) : "not-advertised";
	printf("tdvp-kms-capability-observer: PLANE id=%u type=%s "
	       "possible_crtcs=0x%x current_crtc=%u current_fb=%u formats=%u "
	       "XR24=%s AR24=%s in_formats=%s in_fence=%s out_fence_scope=crtc "
	       "rotation=%s zpos=%s alpha=%s\n",
	       plane->plane_id, type_name, plane->possible_crtcs, plane->crtc_id,
	       plane->fb_id, plane->count_formats,
	       tdvp_plane_has_format(plane, DRM_FORMAT_XRGB8888) ? "yes" : "no",
	       tdvp_plane_has_format(plane, DRM_FORMAT_ARGB8888) ? "yes" : "no",
	       properties.in_formats_present ? "yes" : "no",
	       properties.in_fence_present ? "yes" : "no",
	       properties.rotation_present ? "yes" : "no",
	       properties.zpos_present ? "yes" : "no",
	       properties.alpha_present ? "yes" : "no");

	if (!properties.in_formats_present || properties.in_formats_blob_id == 0) {
		printf("tdvp-kms-capability-observer: MODIFIERS plane=%u unavailable "
		       "(legacy format list only)\n", plane->plane_id);
		result = 0;
		goto out;
	}

	in_formats = drmModeGetPropertyBlob(fd,
					   (uint32_t)properties.in_formats_blob_id);
	if (!in_formats) {
		tdvp_print_errno("drmModeGetPropertyBlob(IN_FORMATS)");
		goto out;
	}
	tdvp_print_modifiers_for_format(in_formats, DRM_FORMAT_XRGB8888, "XR24");
	tdvp_print_modifiers_for_format(in_formats, DRM_FORMAT_ARGB8888, "AR24");
	result = 0;

out:
	if (in_formats)
		drmModeFreePropertyBlob(in_formats);
	drmModeFreePlane(plane);
	return result;
}

int main(int argc, char **argv)
{
	const char *device = "/dev/dri/card0";
	drmModeResPtr resources;
	drmModePlaneResPtr plane_resources;
	int crtc_index;
	uint32_t i;
	int fd;
	int result = 1;

	if (argc == 2 && (strcmp(argv[1], "--help") == 0 ||
			  strcmp(argv[1], "-h") == 0)) {
		tdvp_usage(argv[0]);
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "--device") == 0) {
		device = argv[2];
	} else if (argc != 1) {
		tdvp_usage(argv[0]);
		return 1;
	}

	fd = open(device, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		tdvp_print_errno(device);
		return 1;
	}
	if (drmIsMaster(fd)) {
		fprintf(stderr, "tdvp-kms-capability-observer: refusing DRM master on %s\n",
			device);
		goto out;
	}
	if (tdvp_print_drm_capability(fd, DRM_CAP_SYNCOBJ, "syncobj") < 0 ||
			tdvp_print_drm_capability(fd, DRM_CAP_SYNCOBJ_TIMELINE,
				"syncobj_timeline") < 0)
		goto out;
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) < 0) {
		tdvp_print_errno("drmSetClientCap(UNIVERSAL_PLANES)");
		goto out;
	}
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
		tdvp_print_errno("drmSetClientCap(ATOMIC)");
		goto out;
	}

	resources = drmModeGetResources(fd);
	if (!resources) {
		tdvp_print_errno("drmModeGetResources");
		goto out;
	}
	for (crtc_index = 0; crtc_index < resources->count_crtcs; ++crtc_index) {
		if (tdvp_print_crtc(fd, resources->crtcs[crtc_index]) < 0) {
			drmModeFreeResources(resources);
			goto out;
		}
	}
	drmModeFreeResources(resources);

	plane_resources = drmModeGetPlaneResources(fd);
	if (!plane_resources) {
		tdvp_print_errno("drmModeGetPlaneResources");
		goto out;
	}
	printf("tdvp-kms-capability-observer: PASS device=%s planes=%u "
	       "mode=read-only-no-master\n", device, plane_resources->count_planes);
	for (i = 0; i < plane_resources->count_planes; ++i) {
		if (tdvp_print_plane(fd, plane_resources->planes[i]) < 0) {
			drmModeFreePlaneResources(plane_resources);
			goto out;
		}
	}
	drmModeFreePlaneResources(plane_resources);
	result = 0;

out:
	close(fd);
	return result;
}
