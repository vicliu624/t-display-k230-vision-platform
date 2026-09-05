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
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>

#define TDVP_DEFAULT_FRAMES 120U
#define TDVP_DEFAULT_MAX_INTERVAL_MS 250U

struct tdvp_options {
	const char *device;
	unsigned int frames;
	unsigned int max_interval_ms;
	/*
	 * A DRM primary-node open becomes master when no other master exists.  The
	 * ordinary desktop probe must reject that case, but the managed KMS
	 * transaction deliberately runs after it has stopped every graphical DRM
	 * client.  This explicit opt-in keeps those two safety policies distinct.
	 */
	bool allow_maintenance_master;
};

struct tdvp_observer {
	bool event_received;
	unsigned int sequence;
	/*
	 * `event_ns` comes from the DRM vblank event itself.  `received_ns` is
	 * deliberately kept separately: on this single-core target a late
	 * userspace dispatch must not be misreported as an irregular hardware
	 * vblank cadence.
	 */
	uint64_t event_ns;
	uint64_t received_ns;
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
		"usage: %s [--device PATH] [--frames N] [--max-interval-ms N] "
		"[--allow-maintenance-master]\n"
		"\n"
		"Observe DRM vblank events without modesetting or page-flipping. "
		"By default, refuse DRM master; --allow-maintenance-master is only "
		"for an isolated managed maintenance transaction.\n",
		program);
}

static int tdvp_parse_uint(const char *value, unsigned int *result)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno != 0 || value[0] == '\0' || *end != '\0' ||
	    parsed > UINT_MAX)
		return -1;
	*result = (unsigned int)parsed;
	return 0;
}

static void tdvp_vblank_handler(int fd, unsigned int sequence,
				unsigned int tv_sec, unsigned int tv_usec,
				void *user_data)
{
	struct tdvp_observer *observer = user_data;

	(void)fd;
	observer->sequence = sequence;
	observer->event_ns = (uint64_t)tv_sec * 1000000000ULL +
		(uint64_t)tv_usec * 1000ULL;
	observer->received_ns = tdvp_now_ns();
	observer->event_received = true;
}

static int tdvp_wait_for_vblank(int fd, struct tdvp_observer *observer,
				unsigned int max_interval_ms)
{
	drmVBlank vblank = { 0 };
	drmEventContext event_context = { 0 };
	struct pollfd pollfd = {
		.fd = fd,
		.events = POLLIN,
	};
	int poll_result;

	observer->event_received = false;
	observer->event_ns = 0;
	observer->received_ns = 0;
	vblank.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
	vblank.request.sequence = 1;
	vblank.request.signal = (unsigned long)(uintptr_t)observer;
	if (drmWaitVBlank(fd, &vblank) < 0) {
		fprintf(stderr, "tdvp-vblank-observer: DRM_VBLANK_EVENT request: %s\n",
			strerror(errno));
		return -1;
	}

	do {
		poll_result = poll(&pollfd, 1, (int)max_interval_ms);
	} while (poll_result < 0 && errno == EINTR);
	if (poll_result == 0) {
		fprintf(stderr,
			"tdvp-vblank-observer: timed out after %u ms waiting for vblank\n",
			max_interval_ms);
		return -1;
	}
	if (poll_result < 0) {
		fprintf(stderr, "tdvp-vblank-observer: poll: %s\n", strerror(errno));
		return -1;
	}
	if ((pollfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
	    (pollfd.revents & POLLIN) == 0) {
		fprintf(stderr,
			"tdvp-vblank-observer: unexpected DRM poll events 0x%x\n",
			(unsigned int)pollfd.revents);
		return -1;
	}

	event_context.version = DRM_EVENT_CONTEXT_VERSION;
	event_context.vblank_handler = tdvp_vblank_handler;
	if (drmHandleEvent(fd, &event_context) < 0) {
		fprintf(stderr, "tdvp-vblank-observer: drmHandleEvent: %s\n",
			strerror(errno));
		return -1;
	}
	if (!observer->event_received) {
		fprintf(stderr,
			"tdvp-vblank-observer: DRM fd became readable without a complete vblank event\n");
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct tdvp_options options = {
		.device = "/dev/dri/card0",
		.frames = TDVP_DEFAULT_FRAMES,
		.max_interval_ms = TDVP_DEFAULT_MAX_INTERVAL_MS,
	};
	struct tdvp_observer observer = { 0 };
	uint64_t previous_event_ns = 0;
	uint64_t previous_received_ns = 0;
	uint64_t event_interval_total_us = 0;
	uint64_t event_interval_min_us = UINT64_MAX;
	uint64_t event_interval_max_us = 0;
	uint64_t delivery_interval_total_us = 0;
	uint64_t delivery_interval_min_us = UINT64_MAX;
	uint64_t delivery_interval_max_us = 0;
	unsigned int previous_sequence = 0;
	unsigned int intervals = 0;
	unsigned int i;
	bool have_previous = false;
	int fd = -1;
	int ret = 1;

	for (i = 1; i < (unsigned int)argc; ++i) {
		if (strcmp(argv[i], "--device") == 0 &&
		    i + 1 < (unsigned int)argc) {
			options.device = argv[++i];
		} else if (strcmp(argv[i], "--frames") == 0 &&
			   i + 1 < (unsigned int)argc) {
			if (tdvp_parse_uint(argv[++i], &options.frames) < 0 ||
			    options.frames == 0 || options.frames > 10000) {
				fprintf(stderr,
					"tdvp-vblank-observer: --frames must be 1..10000\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--max-interval-ms") == 0 &&
			   i + 1 < (unsigned int)argc) {
			if (tdvp_parse_uint(argv[++i], &options.max_interval_ms) < 0 ||
			    options.max_interval_ms == 0 ||
			    options.max_interval_ms > 10000) {
				fprintf(stderr,
					"tdvp-vblank-observer: --max-interval-ms must be 1..10000\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--allow-maintenance-master") == 0) {
			options.allow_maintenance_master = true;
		} else if (strcmp(argv[i], "--help") == 0 ||
			   strcmp(argv[i], "-h") == 0) {
			tdvp_usage(argv[0]);
			return 0;
		} else {
			tdvp_usage(argv[0]);
			return 1;
		}
	}

	fd = open(options.device, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "tdvp-vblank-observer: open %s: %s\n", options.device,
			strerror(errno));
		goto out;
	}
	if (drmIsMaster(fd)) {
		if (!options.allow_maintenance_master) {
			fprintf(stderr,
				"tdvp-vblank-observer: refusing to run as DRM master on %s\n",
				options.device);
			goto out;
		}
		printf("tdvp-vblank-observer: mode=isolated-maintenance-master "
		       "device=%s\n", options.device);
	} else {
		printf("tdvp-vblank-observer: mode=read-only-no-master device=%s\n",
		       options.device);
	}

	for (i = 0; i < options.frames; ++i) {
		if (tdvp_wait_for_vblank(fd, &observer,
					 options.max_interval_ms) < 0)
			goto out;
		if (have_previous) {
			uint32_t sequence_step;
			uint64_t event_interval_us;
			uint64_t delivery_interval_us;

			sequence_step = observer.sequence - previous_sequence;
			if (sequence_step == 0 ||
			    observer.event_ns <= previous_event_ns ||
			    observer.received_ns <= previous_received_ns) {
				fprintf(stderr,
					"tdvp-vblank-observer: non-monotonic vblank event at frame %u\n",
					i + 1U);
				goto out;
			}
			event_interval_us =
				(observer.event_ns - previous_event_ns) / 1000ULL;
			delivery_interval_us =
				(observer.received_ns - previous_received_ns) / 1000ULL;
			if (event_interval_us >
			    (uint64_t)options.max_interval_ms * 1000ULL) {
				fprintf(stderr,
					"tdvp-vblank-observer: DRM vblank interval exceeded %u ms at frame %u\n",
					options.max_interval_ms, i + 1U);
				goto out;
			}
			if (event_interval_us < event_interval_min_us)
				event_interval_min_us = event_interval_us;
			if (event_interval_us > event_interval_max_us)
				event_interval_max_us = event_interval_us;
			if (delivery_interval_us < delivery_interval_min_us)
				delivery_interval_min_us = delivery_interval_us;
			if (delivery_interval_us > delivery_interval_max_us)
				delivery_interval_max_us = delivery_interval_us;
			event_interval_total_us += event_interval_us;
			delivery_interval_total_us += delivery_interval_us;
			++intervals;
		}
		previous_sequence = observer.sequence;
		previous_event_ns = observer.event_ns;
		previous_received_ns = observer.received_ns;
		have_previous = true;
	}

	printf("tdvp-vblank-observer: PASS device=%s frames=%u sequence=%u "
	       "event_interval_us min=%llu avg=%.1f max=%llu "
	       "delivery_interval_us min=%llu avg=%.1f max=%llu\n",
	       options.device, options.frames, previous_sequence,
	       (unsigned long long)(intervals == 0 ? 0 : event_interval_min_us),
	       intervals == 0 ? 0.0 :
		(double)event_interval_total_us / (double)intervals,
	       (unsigned long long)event_interval_max_us,
	       (unsigned long long)(intervals == 0 ? 0 : delivery_interval_min_us),
	       intervals == 0 ? 0.0 :
		(double)delivery_interval_total_us / (double)intervals,
	       (unsigned long long)delivery_interval_max_us);
	ret = 0;

out:
	if (fd >= 0)
		close(fd);
	return ret;
}
