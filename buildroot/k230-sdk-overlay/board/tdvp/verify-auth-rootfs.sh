#!/usr/bin/env bash
set -euo pipefail
rootfs="${1:?usage: verify-auth-rootfs.sh <rootfs.ext4> [debugfs]}"
debugfs="${2:-debugfs}"

auth_fail() { printf 'TDVP authentication image guard: %s\n' "$*" >&2; exit 1; }

auth_inode() {
    local path="$1" expected_mode="$2" inode mode owner
    inode="$("$debugfs" -R "stat $path" "$rootfs" 2>/dev/null)"
    grep -q 'Type: regular' <<<"$inode" || auth_fail "missing regular file: $path"
    mode="$(sed -n 's/.*Mode:[[:space:]]*\([0-7]*\).*/\1/p' <<<"$inode")"
    owner="$(sed -n 's/^User:[[:space:]]*\([0-9]*\)[[:space:]]*Group:[[:space:]]*\([0-9]*\).*/\1:\2/p' <<<"$inode")"
    [[ "$mode" == "$expected_mode" && "$owner" == 0:0 ]] || \
        auth_fail "$path must be root:root mode $expected_mode; found $owner mode $mode"
}

# Grant privilege only to PAM's purpose-built password helper. World-readable
# shadow, a writable helper or a setuid Wayland client must never pass.
auth_inode /usr/sbin/unix_chkpwd 04755
auth_inode /usr/bin/swaylock 0755
auth_inode /etc/shadow 0600
auth_inode /etc/pam.d/swaylock 0644
auth_inode /etc/greetd/config.toml 0644
actual_pam="$("$debugfs" -R 'cat /etc/pam.d/swaylock' "$rootfs" 2>/dev/null)"
# Compare every non-comment rule: accepting one pam_unix substring would miss
# a preceding sufficient pam_permit rule or a weakened account policy.
actual_rules="$(sed -e '/^[[:space:]]*#/d' -e '/^[[:space:]]*$/d' <<<"$actual_pam" | awk '{$1=$1; print}')"
expected_rules=$'auth required pam_unix.so\naccount required pam_unix.so\nsession required pam_unix.so'
[[ "$actual_rules" == "$expected_rules" ]] || auth_fail 'unexpected swaylock PAM policy'
actual_config="$("$debugfs" -R 'cat /etc/greetd/config.toml' "$rootfs" 2>/dev/null)"
expected_config=$'[terminal]\nvt = 1\n\n[default_session]\ncommand = "/usr/local/bin/tdvp-greeter-session"\nuser = "greeter"'
[[ "$actual_config" == "$expected_config" ]] || auth_fail 'expected authenticated greeter default; no autologin session'
echo 'TDVP authentication image guard: PASS helper privilege, private shadow, unprivileged swaylock, PAM policy and greeter default'
