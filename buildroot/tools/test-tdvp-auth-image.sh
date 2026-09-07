#!/usr/bin/env bash
set -euo pipefail
export PATH="$PATH:/usr/sbin:/sbin"
project="$(cd "$(dirname "$0")/../.." && pwd)"
verify="$project/buildroot/k230-sdk-overlay/board/tdvp/verify-auth-rootfs.sh"
recipe="$project/buildroot/k230-sdk-overlay/package/swaylock/swaylock.mk"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
root="$test_dir/root"
mkdir -p "$root/usr/sbin" "$root/usr/bin" "$root/etc/pam.d" "$root/etc/greetd"
# Evaluate the real Buildroot recipe's permission definition. The tiny files
# below are inert fixtures, never privileged host executables.
make --no-print-directory -s -f - auth-permissions <<EOF > "$test_dir/permissions"
include $recipe
.PHONY: auth-permissions
auth-permissions:
	@printf '%s\n' '\$(SWAYLOCK_PERMISSIONS)'
EOF
read -r helper kind mode uid gid rest < "$test_dir/permissions"
[[ "$helper $kind $mode $uid $gid $rest" == '/usr/sbin/unix_chkpwd f 4755 0 0 - - - - -' ]]
[[ "$(wc -l < "$test_dir/permissions")" -eq 1 ]]
printf 'inert PAM helper fixture\n' > "$root/usr/sbin/unix_chkpwd"
printf 'inert Wayland locker fixture\n' > "$root/usr/bin/swaylock"
printf 'tdvp:!:1:0:99999:7:::\n' > "$root/etc/shadow"
sed 's/\r$//' "$project/buildroot/k230-sdk-overlay/package/swaylock/src/swaylock" > "$root/etc/pam.d/swaylock"
sed 's/\r$//' "$project/user-space/tdvp-greeter/src/config.toml" > "$root/etc/greetd/config.toml"
truncate -s 8M "$test_dir/good.ext4"
mkfs.ext4 -q -F -d "$root" "$test_dir/good.ext4"
# Set ext4 metadata directly: this test requires neither root nor sudo/fakeroot
# on the CI runner and never changes host file ownership/setuid bits.
for path in /usr/sbin/unix_chkpwd /usr/bin/swaylock /etc/shadow /etc/pam.d/swaylock /etc/greetd/config.toml; do
    debugfs -w -R "set_inode_field $path uid 0" "$test_dir/good.ext4" >/dev/null 2>&1
    debugfs -w -R "set_inode_field $path gid 0" "$test_dir/good.ext4" >/dev/null 2>&1
done
debugfs -w -R "set_inode_field $helper mode 010$mode" "$test_dir/good.ext4" >/dev/null 2>&1
debugfs -w -R 'set_inode_field /usr/bin/swaylock mode 0100755' "$test_dir/good.ext4" >/dev/null 2>&1
debugfs -w -R 'set_inode_field /etc/shadow mode 0100600' "$test_dir/good.ext4" >/dev/null 2>&1
debugfs -w -R 'set_inode_field /etc/pam.d/swaylock mode 0100644' "$test_dir/good.ext4" >/dev/null 2>&1
debugfs -w -R 'set_inode_field /etc/greetd/config.toml mode 0100644' "$test_dir/good.ext4" >/dev/null 2>&1
bash "$verify" "$test_dir/good.ext4"
reject_metadata() {
    cp "$test_dir/good.ext4" "$test_dir/bad.ext4"
    debugfs -w -R "$1" "$test_dir/bad.ext4" >/dev/null 2>&1
    if bash "$verify" "$test_dir/bad.ext4" > "$test_dir/log" 2>&1; then
        echo "FAIL authentication image guard accepted: $1" >&2
        exit 1
    fi
}
reject_metadata 'set_inode_field /usr/sbin/unix_chkpwd mode 0100755'
reject_metadata 'set_inode_field /usr/sbin/unix_chkpwd mode 0104777'
reject_metadata 'set_inode_field /usr/sbin/unix_chkpwd mode 0102755'
reject_metadata 'set_inode_field /usr/sbin/unix_chkpwd uid 1000'
reject_metadata 'set_inode_field /usr/sbin/unix_chkpwd gid 1000'
reject_metadata 'set_inode_field /usr/bin/swaylock mode 0104755'
reject_metadata 'set_inode_field /etc/shadow mode 0100644'
reject_metadata 'rm /usr/sbin/unix_chkpwd'
for failure in pam greeter; do
    cp "$test_dir/good.ext4" "$test_dir/bad.ext4"
    if [[ "$failure" == pam ]]; then
        destination=/etc/pam.d/swaylock
        printf 'auth sufficient pam_permit.so\n' > "$test_dir/replacement"
        sed 's/\r$//' "$root/etc/pam.d/swaylock" >> "$test_dir/replacement"
    else
        destination=/etc/greetd/config.toml
        sed -e 's/tdvp-greeter-session/tdvp-labwc-session/' -e 's/user = "greeter"/user = "tdvp"/' \
            "$root/etc/greetd/config.toml" > "$test_dir/replacement"
    fi
    debugfs -w -R "rm $destination" "$test_dir/bad.ext4" >/dev/null 2>&1
    debugfs -w -R "write $test_dir/replacement $destination" "$test_dir/bad.ext4" >/dev/null 2>&1
    if bash "$verify" "$test_dir/bad.ext4" > "$test_dir/log" 2>&1; then
        echo "FAIL authentication image guard accepted $failure bypass" >&2
        exit 1
    fi
    if [[ "$failure" == pam ]]; then
        grep -Fq 'unexpected swaylock PAM policy' "$test_dir/log"
    else
        grep -Fq 'expected authenticated greeter default' "$test_dir/log"
    fi
done
echo 'TDVP auth image: PASS production permission rule, real ext4 metadata and 10 rejected regressions'
