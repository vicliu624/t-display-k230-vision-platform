// SPDX-License-Identifier: MIT
/*
 * GTK3's K230 Wayland backend does not expose the Raspberry Pi-specific
 * GdkWindow "committed" signal used by wf-panel-pi 1b6010c. The upstream
 * panel uses that notification solely to defer a GtkMenu popup until after a
 * gtk-layer-shell state change has been submitted. On ordinary GTK3 builds
 * g_signal_connect_data() rejects the signal, so the popup callback is never
 * run and the panel menu appears inert.
 *
 * This small preload translates that unavailable notification into a GLib idle
 * callback. The callback runs after the current GTK dispatch returns, which
 * preserves the ordering the panel needs without inventing another launcher.
 * It is intentionally process-scoped: TDVP loads it only for wf-panel-pi.
 */

#define _GNU_SOURCE

typedef void *gpointer;
typedef unsigned int guint;
typedef unsigned long gulong;
typedef int gboolean;
typedef void (*GCallback)(void);
typedef void (*GClosureNotify)(gpointer, gpointer);
typedef int GConnectFlags;

/* Keep this preload buildable with the target toolchain alone. */
#define RTLD_NEXT ((void *)-1L)
#define NULL ((void *)0)
extern void *dlsym(void *handle, const char *symbol);
extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern int strcmp(const char *left, const char *right);

extern guint g_idle_add_full(int priority, gboolean (*function)(gpointer),
	gpointer data, GClosureNotify notify);

struct deferred_callback {
	GCallback callback;
	gpointer instance;
	gpointer user_data;
};

static gboolean
run_deferred_callback(gpointer opaque)
{
	struct deferred_callback *deferred = opaque;

	((void (*)(gpointer, gpointer))deferred->callback)(
		deferred->instance, deferred->user_data);
	return 0;
}

static void
release_deferred_callback(gpointer opaque, gpointer unused)
{
	(void)unused;
	free(opaque);
}

/*
 * wf-panel-pi stores the returned handler id and later disconnects it.  The
 * replacement callback is a GLib idle source rather than a GObject signal, so
 * return a clearly private non-zero id and consume its matching disconnect.
 * Returning zero made GLib print a critical warning for every menu close.
 */
#define TDVP_COMMITTED_HANDLER_BASE 0x7f000000UL
static gulong next_committed_handler = TDVP_COMMITTED_HANDLER_BASE;

gulong
g_signal_connect_data(gpointer instance, const char *detailed_signal,
	GCallback callback, gpointer user_data, GClosureNotify destroy_data,
	GConnectFlags connect_flags)
{
	typedef gulong (*connect_fn)(gpointer, const char *, GCallback, gpointer,
		GClosureNotify, GConnectFlags);
	static connect_fn real_connect;

	if (detailed_signal && strcmp(detailed_signal, "committed") == 0) {
		struct deferred_callback *deferred = malloc(sizeof(*deferred));
		if (deferred) {
			deferred->callback = callback;
			deferred->instance = instance;
			deferred->user_data = user_data;
			/* G_PRIORITY_DEFAULT_IDLE; avoid a GLib header dependency here. */
			g_idle_add_full(200, run_deferred_callback, deferred,
				release_deferred_callback);
		}
		return next_committed_handler++;
	}

	if (!real_connect)
		real_connect = (connect_fn)dlsym(RTLD_NEXT, "g_signal_connect_data");
	return real_connect(instance, detailed_signal, callback, user_data,
		destroy_data, connect_flags);
}

void
g_signal_handler_disconnect(gpointer instance, gulong handler_id)
{
	typedef void (*disconnect_fn)(gpointer, gulong);
	static disconnect_fn real_disconnect;

	if (handler_id >= TDVP_COMMITTED_HANDLER_BASE)
		return;
	if (!real_disconnect)
		real_disconnect = (disconnect_fn)dlsym(RTLD_NEXT,
			"g_signal_handler_disconnect");
	real_disconnect(instance, handler_id);
}
