#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
OBSERVER="$PROJECT_DIR/buildroot/k230-sdk-overlay/package/tdvp-display-smoke/src/tdvp-vblank-observer.c"
ACCEPTANCE="$PROJECT_DIR/buildroot/k230-sdk-overlay/package/tdvp-display-smoke/src/tdvp-kms-acceptance"

require() {
	grep -F -- "$2" "$1" >/dev/null || {
		printf 'test-tdvp-vblank-observer-maintenance-master: missing %s in %s\n' \
			"$2" "$1" >&2
		exit 1
	}
}

[ -f "$OBSERVER" ]
[ -f "$ACCEPTANCE" ]
require "$OBSERVER" 'bool allow_maintenance_master;'
require "$OBSERVER" 'if (!options.allow_maintenance_master)'
require "$OBSERVER" 'refusing to run as DRM master'
require "$OBSERVER" 'mode=isolated-maintenance-master'
require "$ACCEPTANCE" 'tdvp-display-smoke'
require "$ACCEPTANCE" '--allow-maintenance-master'

# The observer is event-only.  Its maintenance opt-in must not grow a KMS
# mutation path.
if grep -E 'drmMode(SetCrtc|AddFB|AtomicCommit|PageFlip)|drmSetMaster' \
	"$OBSERVER" >/dev/null; then
	printf '%s\n' 'test-tdvp-vblank-observer-maintenance-master: observer contains a KMS mutation' >&2
	exit 1
fi

# The direct page-flip client must exit before the observer runs.
smoke_line=$(grep -n '"\$display_smoke_program"' "$ACCEPTANCE" | head -n 1 | cut -d: -f1)
vblank_line=$(grep -n '"\$vblank_observer_program"' "$ACCEPTANCE" | head -n 1 | cut -d: -f1)
[ -n "$smoke_line" ] && [ -n "$vblank_line" ] && [ "$smoke_line" -lt "$vblank_line" ] || {
	printf '%s\n' 'test-tdvp-vblank-observer-maintenance-master: observer is not ordered after display smoke' >&2
	exit 1
}

printf '%s\n' 'test-tdvp-vblank-observer-maintenance-master: PASS'
