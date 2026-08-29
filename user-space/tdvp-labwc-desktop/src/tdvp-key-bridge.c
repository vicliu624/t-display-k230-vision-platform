// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
/*
 * The T-Display K230's dedicated LilyGO key is emitted by the TCA8418 as
 * KEY_MENU. Labwc 0.8.4 receives that evdev event but does not dispatch the
 * Menu keysym through its configured global binding on this board. Keep the
 * board-specific translation at the input boundary and delegate the actual
 * application-menu UI to Raspberry Pi's already-installed wfplug-menu
 * ("smenu") plugin.  This helper has no launcher UI of its own.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#define TDVP_KEYBOARD_NAME "tca8418"
#define EVENT_SCAN_LIMIT 64
#define MENU_DEBOUNCE_MS 450

static int
open_lilygo_keyboard(void)
{
	char name_path[96];
	char event_path[32];
	char name[128];

	for (int index = 0; index < EVENT_SCAN_LIMIT; ++index) {
		snprintf(name_path, sizeof(name_path),
			"/sys/class/input/event%d/device/name", index);
		FILE *file = fopen(name_path, "r");
		if (!file) {
			continue;
		}

		if (!fgets(name, sizeof(name), file)) {
			fclose(file);
			continue;
		}
		fclose(file);
		name[strcspn(name, "\r\n")] = '\0';
		if (strcmp(name, TDVP_KEYBOARD_NAME) != 0) {
			continue;
		}

		snprintf(event_path, sizeof(event_path), "/dev/input/event%d", index);
		return open(event_path, O_RDONLY | O_CLOEXEC);
	}

	errno = ENODEV;
	return -1;
}

static void
toggle_panel_menu(void)
{
	pid_t child = fork();
	if (child == 0) {
		/* This wrapper sends the command to the existing smenu panel plugin. */
		execl("/usr/local/bin/tdvp-panel-menu", "tdvp-panel-menu", (char *)NULL);
		_exit(127);
	}
}

static int
should_toggle_menu(struct timespec *last_press)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (last_press->tv_sec != 0 || last_press->tv_nsec != 0) {
		int64_t elapsed_ms = (int64_t)(now.tv_sec - last_press->tv_sec) * 1000
			+ (now.tv_nsec - last_press->tv_nsec) / 1000000;
		if (elapsed_ms >= 0 && elapsed_ms < MENU_DEBOUNCE_MS) {
			return 0;
		}
	}

	*last_press = now;
	return 1;
}

int
main(void)
{
	/* The bridge spawns a tiny one-shot command sender on each press. */
	signal(SIGCHLD, SIG_IGN);
	struct timespec last_press = {0};

	for (;;) {
		int fd = open_lilygo_keyboard();
		if (fd < 0) {
			perror("tdvp-key-bridge: open tca8418");
			sleep(2);
			continue;
		}

		struct input_event event;
		while (read(fd, &event, sizeof(event)) == sizeof(event)) {
			/*
			 * Ignore auto-repeat and the TCA8418's occasional repeated press
			 * reports. The panel-menu command is a toggle, so issuing it twice would
			 * make a perfectly valid key press appear to do nothing.
			 */
			if (event.type == EV_KEY && event.code == KEY_MENU && event.value == 1
				&& should_toggle_menu(&last_press)) {
				toggle_panel_menu();
			}
		}
		close(fd);
		sleep(1);
	}
}
