#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
source_dir="${1:?usage: test-tdvp-gtklock-ui-lifecycle.sh <patched-gtklock-source>}"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
python3 - "$source_dir" "$test_dir/callbacks.h" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
parts = []
for path, name in [('src/window.c', 'window_pw_result'),
                   ('src/window.c', 'window_pw_wait'),
                   ('src/gtklock.c', 'gtklock_shutdown')]:
    source = (root / path).read_text()
    match = re.search(r'^.*\b' + name + r'\([^;\n]*\) \{', source, re.M)
    if not match:
        raise SystemExit('missing production callback: ' + name)
    # These three C callbacks have no braces inside string/comment literals.
    depth = 1
    end = match.end()
    while depth and end < len(source):
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if depth:
        raise SystemExit('unbalanced production callback: ' + name)
    parts.append(source[match.start():end])
pathlib.Path(sys.argv[2]).write_text('\n\n'.join(parts) + '\n')
PY
printf '/* Upstream auth.h uses no GLib types. */\n' > "$test_dir/glib.h"
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -pthread \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$test_dir" -I"$source_dir/include" -DTDVP_UI_CALLBACKS="\"$test_dir/callbacks.h\"" \
    "$project/buildroot/tools/tests/tdvp-gtklock-ui-lifecycle-test.c" -o "$test_dir/test-ui"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$test_dir/test-ui"
