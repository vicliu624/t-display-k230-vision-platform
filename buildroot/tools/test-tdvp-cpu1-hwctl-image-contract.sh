#!/usr/bin/env bash
set -euo pipefail

# No mount, root privileges, target execution or hardware access is needed.
# An optional real (including cross-compiled/stripped) vpl-hwctl can be checked
# with exactly the same rules. The early CI default uses only the production
# status object, not the unit-test executable containing expected strings.
# Keep that native object unoptimized: x86 GCC can split string literals into
# vector-store fragments, unlike the target ELF inspected by the image guard.
if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [compiled-vpl-hwctl]\n' "$0" >&2
    exit 2
fi
export PATH="${PATH}:/usr/sbin:/sbin"
project="$(cd "$(dirname "$0")/../.." && pwd)"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
hardware="$project/user-space/vicliu-pocket-linux-hardware/src/hardware"
if [ "$#" -eq 1 ]; then
    payload="$1"
else
    payload="$temporary/cpu1-status.o"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O0 -I"$hardware" \
        -c "$hardware/cpu1_vision_status.cpp" -o "$payload"
fi
mkdir -p "$temporary/root/usr/local/bin"
cp "$payload" "$temporary/root/usr/local/bin/vpl-hwctl"

python3 - "$project" "$temporary" <<'PY'
from pathlib import Path
import re
import shlex
import sys

project, temporary = map(Path, sys.argv[1:])
verify = (project / 'buildroot/k230-sdk-overlay/board/tdvp/verify-sdcard-image.sh').read_text()
payload = (temporary / 'root/usr/local/bin/vpl-hwctl').read_bytes()
if not payload.startswith(b'\x7fELF'):
    raise SystemExit('CPU1 hwctl image contract: expected a compiled ELF payload')
helpers = ('require_rootfs_content', 'reject_rootfs_content')
functions = []
for helper in helpers:
    match = re.search(r'^' + helper + r'\(\) \{\n.*?^\}', verify, re.M | re.S)
    if not match:
        raise SystemExit('missing production helper: ' + helper)
    functions.append(match.group())
(temporary / 'helpers.sh').write_text('\n\n'.join(functions) + '\n')
checks = []
rules = []
for line in verify.splitlines():
    if not re.match(r'^\s*(?:' + '|'.join(helpers) + r')\s', line):
        continue
    fields = shlex.split(line)
    if len(fields) == 3 and fields[1] == '/usr/local/bin/vpl-hwctl':
        checks.append(line.strip())
        rules.append((fields[0], fields[2]))
if not checks:
    raise SystemExit('missing production vpl-hwctl assertions')
(temporary / 'checks.sh').write_text('\n'.join(checks) + '\n')

# Mutate every production requirement, rather than testing a second copy of
# its grep semantics. These files are never executed. Same-sized replacement
# removes all instances of a required literal without shifting ELF offsets.
for index, (helper, text) in enumerate(rules):
    token = text.encode()
    if helper == 'require_rootfs_content':
        mutant = payload.replace(token, b'~' * len(token))
    else:
        mutant = payload + b'\0' + token + b'\0'
    (temporary / ('mutant-%03d' % index)).write_bytes(mutant)
print('CPU1 hwctl image contract: %d production assertions and negative controls' % len(rules))
PY

export ROOTFS="$temporary/rootfs.ext4"
truncate -s 32M "$ROOTFS"
mke2fs -q -t ext4 -d "$temporary/root" "$ROOTFS"
bash -euo pipefail -c 'source "$1"; source "$2"' _ \
    "$temporary/helpers.sh" "$temporary/checks.sh"

# Independently require the current interface and retirement rules, so deleting
# a production assertion cannot silently shrink this regression's coverage.
python3 - "$temporary/checks.sh" <<'PY'
from pathlib import Path
import shlex
import sys

rules = {tuple(shlex.split(line)[::2]) for line in Path(sys.argv[1]).read_text().splitlines()}
required = {
    '/sys/class/misc/tdvp-vision/status', '/sys/class/misc/tdvp-ai/status',
    'cpu1_ai_status_valid', 'cpu1_ai_available', 'cpu1_ai_state',
    'cpu1-rtsmart-kws-reference', 'cpu1-reference-unverified',
}
forbidden = {
    'cpu1-model-unconfigured', 'cpu1-rtsmart-no-model',
    '/root/app/ai2d_kpu', 'tdvp-kpu-acceptance.service',
}
expected = {('require_rootfs_content', text) for text in required}
expected.update(('reject_rootfs_content', text) for text in forbidden)
missing = expected - rules
if missing:
    raise SystemExit('CPU1 hwctl image contract: missing production rules: ' + repr(sorted(missing)))
PY

for mutant in "$temporary"/mutant-*; do
    debugfs -w -R 'rm /usr/local/bin/vpl-hwctl' "$ROOTFS" >/dev/null 2>&1
    debugfs -w -R "write $mutant /usr/local/bin/vpl-hwctl" "$ROOTFS" >/dev/null 2>&1
    debugfs -R "dump /usr/local/bin/vpl-hwctl $temporary/readback" "$ROOTFS" >/dev/null 2>&1
    cmp "$mutant" "$temporary/readback"
    if bash -euo pipefail -c 'source "$1"; source "$2"' _ \
        "$temporary/helpers.sh" "$temporary/checks.sh" >"$temporary/mutation.log" 2>&1; then
        printf 'CPU1 hwctl image contract: accepted regression %s\n' "${mutant##*/}" >&2
        exit 1
    fi
done
printf '%s\n' 'CPU1 hwctl image contract: PASS compiled payload accepted; every required/retired marker regression rejected (not hardware acceptance)'
