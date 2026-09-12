// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
/*
 * The T-Display K230's dedicated LilyGO key is emitted by the TCA8418 as
 * KEY_F13. KEY_MENU is deliberately not used: it is a desktop-wide context
 * menu key, so Labwc/GTK clients can consume it at the same time as this
 * bridge and produce a competing popup or a full-screen damage flash. F13
 * has no global action in the TDVP XKB policy; this process is its sole
 * product-level consumer. The helper delegates the actual application-menu
 * UI to Raspberry Pi's already-installed wfplug-menu ("smenu") plugin and
 * implements no launcher UI of its own.
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
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#define TDVP_KEYBOARD_NAME "tca8418"
#define EVENT_SCAN_LIMIT 64
#define MENU_DEBOUNCE_MS 450
#define TDVP_LILLYGO_MENU_KEY KEY_F13
#define TDVP_LILLYGO_SCAN_INDEX 7

/*
 * DTS owns the release mapping, but changing from a pre-F13 image must not
 * make the hardware key regress after a routine reboot.  The generic input
 * keymap API updates only the live device and is a no-op once the release DTB
 * already provides KEY_F13.
 */
static void
normalize_lilygo_menu_key(const char *event_path)
{
	struct input_keymap_entry entry = { 0 };
	int fd = open(event_path, O_RDWR | O_CLOEXEC);

	if (fd < 0)
		return;

	entry.flags = INPUT_KEYMAP_BY_INDEX;
	entry.index = TDVP_LILLYGO_SCAN_INDEX;
	if (ioctl(fd, EVIOCGKEYCODE_V2, &entry) < 0) {
		perror("tdvp-key-bridge: read LilyGO keymap");
		close(fd);
		return;
	}
	if (entry.keycode != KEY_MENU) {
		close(fd);
		return;
	}

	entry.keycode = TDVP_LILLYGO_MENU_KEY;
	if (ioctl(fd, EVIOCSKEYCODE_V2, &entry) < 0)
		perror("tdvp-key-bridge: remap LilyGO key");
	close(fd);
}

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
		normalize_lilygo_menu_key(event_path);
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
			if (event.type == EV_KEY && event.code == TDVP_LILLYGO_MENU_KEY && event.value == 1
				&& should_toggle_menu(&last_press)) {
				toggle_panel_menu();
			}
		}
		close(fd);
		sleep(1);
	}
}
