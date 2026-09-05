#!/usr/bin/env bash
set -euo pipefail

# Restore a boot set written by deploy-k230-kernel-candidate.sh.  This is
# intentionally narrower than a general file copier: the only writable
# destination is the K230 boot partition and the only readable source is an
# already-recorded, hash-verified TDVP boot backup.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REMOTE="${ROOT}/buildroot/tools/tdvp-remote.sh"

usage() {
	cat >&2 <<'EOF'
Usage:
  TDVP_REMOTE_PASSWORD=... rollback-k230-kernel-candidate.sh <boot-backup-id>

Restore Image, k.dtb and k230-canmv-rm69a10.dtb from one backup made by
deploy-k230-kernel-candidate.sh. The backup ID must have the form
boot-YYYYMMDDTHHMMSSZ. This command does not reboot the board; verify the
reported hashes and reboot separately.
EOF
}

if [ "$#" -ne 1 ]; then
	usage
	exit 2
fi

backup_id="$1"
if [[ ! "${backup_id}" =~ ^boot-[0-9]{8}T[0-9]{6}Z$ ]]; then
	printf 'invalid TDVP boot backup id: %s\n' "${backup_id}" >&2
	exit 2
fi

remote() {
	"${REMOTE}" "$1"
}

# backup_id is validated above before it is interpolated into the remote
# command. All other paths are constants owned by this script.
remote "
set -eu
backup=/var/lib/tdvp/boot-backup/${backup_id}
manifest=\"\$backup/SHA256SUMS\"
mount_dir=/mnt/tdvp-kernel-rollback
state_dir=/var/lib/tdvp/boot-rollback-state
mounted=0
cleanup() {
  if [ \"\$mounted\" = 1 ] && mountpoint -q \"\$mount_dir\"; then
    umount \"\$mount_dir\" || true
  fi
}
trap cleanup EXIT HUP INT TERM

test -d \"\$backup\"
test -f \"\$manifest\"
for name in Image k.dtb k230-canmv-rm69a10.dtb; do
  file=\"\$backup/\$name\"
  test -f \"\$file\"
  expected=\$(awk -v file=\"\$file\" '\$2 == file { print \$1 }' \"\$manifest\")
  test \"\$(printf '%s\\n' \"\$expected\" | sed '/^\$/d' | wc -l | tr -d ' ')\" = 1
  case \"\$expected\" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) ;;
    *) echo \"invalid backup hash for \$name\" >&2; exit 1 ;;
  esac
  test \"\$(sha256sum \"\$file\" | awk '{print \$1}')\" = \"\$expected\"
done
cmp -s \"\$backup/k.dtb\" \"\$backup/k230-canmv-rm69a10.dtb\"

mkdir -p \"\$mount_dir\" \"\$state_dir\"
if mountpoint -q \"\$mount_dir\"; then
  umount \"\$mount_dir\"
fi
mount /dev/mmcblk1p1 \"\$mount_dir\"
mounted=1
test -f \"\$mount_dir/Image\"
test -f \"\$mount_dir/k.dtb\"
test -f \"\$mount_dir/k230-canmv-rm69a10.dtb\"

for name in Image k.dtb k230-canmv-rm69a10.dtb; do
  source_file=\"\$backup/\$name\"
  target_file=\"\$mount_dir/\$name\"
  temporary=\"\$target_file.tdvp-rollback.\$\$\"
  cp \"\$source_file\" \"\$temporary\"
  expected=\$(awk -v file=\"\$source_file\" '\$2 == file { print \$1 }' \"\$manifest\")
  test \"\$(sha256sum \"\$temporary\" | awk '{print \$1}')\" = \"\$expected\"
done
sync
for name in Image k.dtb k230-canmv-rm69a10.dtb; do
  mv -f \"\$mount_dir/\$name.tdvp-rollback.\$\$\" \"\$mount_dir/\$name\"
done
sync

for name in Image k.dtb k230-canmv-rm69a10.dtb; do
  source_file=\"\$backup/\$name\"
  expected=\$(awk -v file=\"\$source_file\" '\$2 == file { print \$1 }' \"\$manifest\")
  test \"\$(sha256sum \"\$mount_dir/\$name\" | awk '{print \$1}')\" = \"\$expected\"
done
cmp -s \"\$mount_dir/k.dtb\" \"\$mount_dir/k230-canmv-rm69a10.dtb\"
umount \"\$mount_dir\"
mounted=0
printf 'backup_id=%s restored_epoch=%s\\n' '${backup_id}' \"\$(date +%s)\" > \"\$state_dir/last.new.\$\$\"
mv -f \"\$state_dir/last.new.\$\$\" \"\$state_dir/last\"
printf 'TDVP-boot-rollback-complete backup=%s\\n' \"\$backup\"
"

printf 'TDVP boot rollback: complete. Reboot separately after reviewing the backup path.\n'
