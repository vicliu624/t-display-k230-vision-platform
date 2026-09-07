#!/usr/bin/env bash
set -euo pipefail

# Debian's non-root SSH PATH omits the filesystem utilities in sbin.
export PATH="${PATH}:/usr/sbin:/sbin"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TEMP_DIR}"' EXIT

# Stage the real, directly installed desktop/greeter source files using their
# package recipes as the path map. Compiled helpers and generated rootfs files
# are intentionally outside this fast test; the complete image guard still
# verifies those after the real build. No expected configuration is mocked.
python3 - "${PROJECT_DIR}" "${TEMP_DIR}" <<'PY'
from pathlib import Path
import re
import shlex
import subprocess
import sys

project, temporary = map(Path, sys.argv[1:])
root = temporary / 'root'
installed = set()
recipe_pattern = re.compile(
    r'\$\(INSTALL\)\s+-D\s+-m\s+([0-7]+)\s+'
    r'\$\(@D\)/(\S+)\s+\$\(TARGET_DIR\)(/\S+)')
for package in ('tdvp-greeter', 'tdvp-labwc-desktop'):
    recipe = (project / 'buildroot/k230-sdk-overlay/package' / package / (package + '.mk')).read_text()
    for mode, source, destination in recipe_pattern.findall(recipe.replace('\\\n', ' ')):
        source = project / 'user-space' / package / 'src' / source
        if not source.is_file():
            # These two outputs are produced by the real target compiler.
            if source.name in ('tdvp-key-bridge', 'tdvp-gdk-committed-compat.so'):
                continue
            raise SystemExit('missing installed source: ' + str(source))
        target = root / destination.lstrip('/')
        target.parent.mkdir(parents=True, exist_ok=True)
        data = source.read_bytes()
        if b'\0' not in data:
            data = data.replace(b'\r\n', b'\n')
        target.write_bytes(data)
        target.chmod(int(mode, 8))
        installed.add(destination)

verify = (project / 'buildroot/k230-sdk-overlay/board/tdvp/verify-sdcard-image.sh').read_text()
helpers = ('require_rootfs_line', 'require_rootfs_fixed_line', 'require_rootfs_content',
           'reject_rootfs_line', 'reject_rootfs_content')
functions = []
for helper in helpers:
    match = re.search(r'^' + helper + r'\(\) \{\n.*?^\}', verify, re.M | re.S)
    if not match:
        raise SystemExit('missing production helper: ' + helper)
    functions.append(match.group())
(temporary / 'helpers.sh').write_text('\n\n'.join(functions) + '\n')
checks = []
checked = set()
for line in verify.splitlines():
    if not re.match(r'^\s*(?:' + '|'.join(helpers) + r')\s', line):
        continue
    fields = shlex.split(line)
    if len(fields) == 3 and fields[1] in installed:
        checks.append(line.strip())
        checked.add(fields[1])
for required in ('/etc/greetd/config.toml', '/usr/local/bin/tdvp-labwc-session',
                 '/etc/tdvp/labwc/environment', '/etc/xdg/labwc/autostart'):
    if required not in checked:
        raise SystemExit('missing image assertions for installed source: ' + required)
(temporary / 'checks.sh').write_text('\n'.join(checks) + '\n')
print('Image source contract: %d production assertions across %d installed files' % (len(checks), len(checked)))

# Exercise the real post-image literal metadata writer and compare it with all
# literal manifest requirements in the release baseline. Artifact hashes and
# variable-valued identifiers remain covered by the complete image build.
post_image = (project / 'buildroot/k230-sdk-overlay/board/tdvp/post-image.sh').read_text()
manifest_block = re.search(
    r"^\{\n\s*printf 'tdvp_image_manifest_version=.*?^\} > \"\$\{IMAGE_MANIFEST\}\"",
    post_image, re.M | re.S)
if not manifest_block:
    raise SystemExit('missing production image manifest writer')
literal_writes = []
for line in manifest_block.group().splitlines():
    if re.match(r"^\s*printf '[a-z0-9_]+=.*\\n'\s*$", line):
        literal_writes.append(line)
metadata = subprocess.run(['bash', '-e'], input='\n'.join(literal_writes),
                          text=True, capture_output=True, check=True).stdout.splitlines()
baseline = (project / 'buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh').read_text()
required_metadata = set()
for line in baseline.splitlines():
    if not re.match(r'^require_content "\$\{IMAGES\}/tdvp-image-manifest" ', line):
        continue
    fields = shlex.split(line)
    if len(fields) != 3:
        raise SystemExit('unsupported image manifest assertion: ' + line)
    if '$' not in fields[2]:
        required_metadata.add(fields[2])

# Independently bind the newly required fields to the real package revisions
# and installed desktop policy, so changing both writer and assertion to the
# same incorrect value cannot hide drift from what is actually delivered.
for package in ('wlroots', 'labwc'):
    recipe = (project / 'buildroot/k230-sdk-overlay/package' / package / (package + '.mk')).read_text()
    revision = re.search(r'^' + package.upper() + r'_VERSION = ([0-9a-f]{40})$', recipe, re.M)
    if not revision:
        raise SystemExit('missing pinned package revision: ' + package)
    required_metadata.add(package + '_commit=' + revision.group(1))
environment = (project / 'user-space/tdvp-labwc-desktop/src/environment').read_text()
for source_key, manifest_key in (('WLR_RENDERER', 'renderer_default'),
                                 ('LABWC_UPDATE_ACTIVATION_ENV', 'labwc_update_activation_env')):
    value = re.search(r'^' + source_key + r'=([^\n]+)$', environment, re.M)
    if not value:
        raise SystemExit('missing desktop policy: ' + source_key)
    required_metadata.add(manifest_key + '=' + value.group(1))
missing_metadata = sorted(required_metadata.difference(metadata))
if missing_metadata:
    raise SystemExit('Image source contract: missing/incorrect production manifest metadata:\n  ' +
                     '\n  '.join(missing_metadata))
print('Image manifest source contract: %d release requirements match production metadata' % len(required_metadata))
PY

# Exercise the production debugfs extraction and grep semantics, not a second
# implementation of the image rules. A small real ext4 fixture needs no mount
# or root privileges and also catches extraction/pipefail mistakes.
ROOTFS="${TEMP_DIR}/rootfs.ext4"
truncate -s 32M "${ROOTFS}"
mke2fs -q -t ext4 -d "${TEMP_DIR}/root" "${ROOTFS}"
source "${TEMP_DIR}/helpers.sh"
failures=0
while IFS= read -r assertion; do
	if ! (eval "${assertion}"); then
		failures=$((failures + 1))
	fi
done < "${TEMP_DIR}/checks.sh"
if [ "${failures}" -ne 0 ]; then
	printf 'Image source contract: FAIL %s mismatches (all checked, not just the first)\n' "${failures}" >&2
	exit 1
fi

# Negative controls must be rejected by those same image assertions. Keep
# mutations in the temporary fixture; production desktop policy is untouched.
for mutation in login-command login-user session; do
	case "${mutation}" in
		login-command)
			path=/etc/greetd/config.toml
			sed 's|tdvp-greeter-session|tdvp-labwc-session|' "${TEMP_DIR}/root${path}" > "${TEMP_DIR}/mutant" ;;
		login-user)
			path=/etc/greetd/config.toml
			sed 's|user = "greeter"|user = "root"|' "${TEMP_DIR}/root${path}" > "${TEMP_DIR}/mutant" ;;
		session)
			path=/usr/local/bin/tdvp-labwc-session
			printf '#!/bin/sh\nexec /usr/bin/dbus-run-session -- /usr/bin/labwc\n' > "${TEMP_DIR}/mutant" ;;
	esac
	debugfs -w -R "rm ${path}" "${ROOTFS}" >/dev/null 2>&1
	debugfs -w -R "write ${TEMP_DIR}/mutant ${path}" "${ROOTFS}" >/dev/null 2>&1
	debugfs -R "dump ${path} ${TEMP_DIR}/readback" "${ROOTFS}" >/dev/null 2>&1
	cmp "${TEMP_DIR}/mutant" "${TEMP_DIR}/readback"
	rejected=0
	while IFS= read -r assertion; do
		if [[ "${assertion}" != *"'${path}'"* ]]; then continue; fi
		if ! (eval "${assertion}") > "${TEMP_DIR}/mutation.log" 2>&1; then rejected=1; fi
	done < "${TEMP_DIR}/checks.sh"
	[ "${rejected}" -eq 1 ] || { printf 'Image source contract: accepted %s regression\n' "${mutation}" >&2; exit 1; }
	debugfs -w -R "rm ${path}" "${ROOTFS}" >/dev/null 2>&1
	debugfs -w -R "write ${TEMP_DIR}/root${path} ${path}" "${ROOTFS}" >/dev/null 2>&1
	debugfs -R "dump ${path} ${TEMP_DIR}/readback" "${ROOTFS}" >/dev/null 2>&1
	cmp "${TEMP_DIR}/root${path}" "${TEMP_DIR}/readback"
done
printf '%s\n' 'test-tdvp-image-source-contract: PASS actual delivery files match image rules; three regressions rejected'
