#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then
    echo 'Usage: test-tdvp-opkg-image-native.sh <built-opkg-0.7.0-source>' >&2
    exit 2
fi
source_dir="$(realpath "$1")"
project="$(cd "$(dirname "$0")/../.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
grep -Fq "PACKAGE_VERSION='0.7.0'" "$source_dir/configure"
cp -a "$source_dir" "$test_dir/opkg"
cd "$test_dir/opkg"
if [[ -f Makefile ]]; then
    make distclean > "$test_dir/clean.log" 2>&1
fi
# CI starts at opkg-extract; developers may provide already-patched sources.
# Normalize recognized patches in reverse order in this disposable copy.
# Later patches can change earlier patch context, so checking each earlier
# patch in isolation cannot establish whether the complete queue is present.
solver_patches=("$project"/buildroot/k230-sdk-overlay/package/opkg/*.patch)
for ((patch_index=${#solver_patches[@]}-1; patch_index>=0; patch_index--)); do
    solver_patch="${solver_patches[patch_index]}"
    if patch --batch --forward --fuzz=0 --reverse --dry-run -p1 < "$solver_patch" > "$test_dir/patch.log" 2>&1; then
        patch --batch --forward --fuzz=0 --reverse -p1 < "$solver_patch"
    fi
done
for solver_patch in "${solver_patches[@]}"; do
    patch --batch --forward --fuzz=0 --dry-run -p1 < "$solver_patch" > "$test_dir/patch.log" 2>&1 || {
        cat "$test_dir/patch.log" >&2; exit 1;
    }
    patch --batch --forward --fuzz=0 -p1 < "$solver_patch"
done
# This native binary tests opkg's installed-file ownership and resolver using
# inert local fixtures. Production signature checks stay enabled in the image.
./configure CC=/usr/bin/cc CXX=/usr/bin/c++ AR=/usr/bin/ar RANLIB=/usr/bin/ranlib \
    --disable-curl --disable-ssl-curl --disable-gpg --disable-sha256 \
    --without-libsolv --with-static-libopkg > "$test_dir/configure.log" 2>&1 || {
    cat "$test_dir/configure.log" >&2; exit 1;
}
make -j4 > "$test_dir/make.log" 2>&1 || { cat "$test_dir/make.log" >&2; exit 1; }
TDVP_TEST_OPKG="$test_dir/opkg/src/opkg" python3 "$project/buildroot/tools/test-tdvp-opkg-image-seed.py"
python3 "$project/buildroot/tools/test-opkg-alternative-solver.py" "$test_dir/opkg/src/opkg"
python3 "$project/buildroot/tools/test-opkg-held-file-owner.py" "$test_dir/opkg/src/opkg"
