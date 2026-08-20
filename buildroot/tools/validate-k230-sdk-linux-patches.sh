#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
	printf 'Usage: %s <kernel-patch-directory>\n' "$0" >&2
	exit 2
fi

PATCH_DIR="$1"
[ -d "$PATCH_DIR" ] || {
	printf 'Kernel patch validation: directory not found: %s\n' "$PATCH_DIR" >&2
	exit 1
}

python3 - "$PATCH_DIR" <<'PY'
from pathlib import Path
import re
import sys

patch_dir = Path(sys.argv[1])
header = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")
errors = []

for path in sorted(patch_dir.glob("*.patch")):
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    index = 0
    while index < len(lines):
        match = header.match(lines[index])
        if not match:
            index += 1
            continue

        expected_old = int(match.group(2) or 1)
        expected_new = int(match.group(4) or 1)
        actual_old = 0
        actual_new = 0
        line_number = index + 1
        index += 1

        while index < len(lines):
            line = lines[index]
            if line.startswith("diff --git ") or line == "-- ":
                break
            if line.startswith("@@ "):
                break
            if line.startswith("\\ No newline at end of file"):
                index += 1
                continue
            if line.startswith(" "):
                actual_old += 1
                actual_new += 1
            elif line.startswith("-"):
                actual_old += 1
            elif line.startswith("+"):
                actual_new += 1
            else:
                errors.append(f"{path.name}:{index + 1}: missing unified-diff prefix")
            index += 1

        if (actual_old, actual_new) != (expected_old, expected_new):
            errors.append(
                f"{path.name}:{line_number}: hunk declares -{expected_old}/+{expected_new}, "
                f"contains -{actual_old}/+{actual_new}"
            )

if errors:
    print("Kernel patch validation failed:", file=sys.stderr)
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)

print(f"Kernel patch validation: {len(list(patch_dir.glob('*.patch')))} patches have valid unified-diff structure")
PY
