#!/usr/bin/env bash
# Isolated diagnosis of the platform libsndfile external-codec build.
# Never installs into the input SDK, feed, image root or device system root.
set -Eeuo pipefail
IFS=$'\n\t'
[[ $# == 4 ]] || { echo 'usage: build-audio-codec-proof.sh SDK CODEC_STAGE BUILT_PACKAGE_METADATA NEW_OUTPUT' >&2; exit 64; }
sdk=$(realpath -e -- "$1")
stage=$(realpath -e -- "$2")
metadata=$(realpath -e -- "$3")
output=$4
[[ -f "$sdk/tdvp-sdk-manifest.json" && -f "$metadata" && -d "$stage/usr" ]]
[[ ! -e "$output" && ! -L "$output" ]] || { echo 'proof output must be a new directory' >&2; exit 65; }
mkdir -p "$output"
output=$(realpath -e -- "$output")
mkdir "$output/source" "$output/patches" "$output/sysroot" "$output/install"
cp -a --reflink=auto "$sdk/sysroot/." "$output/sysroot/"
cp -a --reflink=auto "$stage/usr/." "$output/sysroot/usr/"
exporter=$(realpath -e -- "$(dirname -- "${BASH_SOURCE[0]}")/../export-tdvp-sdk.py")
python3 - "$exporter" "$output/sysroot" "$sdk/sysroot" <<'PY'
import importlib.util,pathlib,sys
spec=importlib.util.spec_from_file_location('sdk_export',sys.argv[1])
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
module.relocate_development_files(pathlib.Path(sys.argv[2]),pathlib.Path(sys.argv[3]))
PY
# Fail before downloading or compiling if the complete codec development set
# is absent. The image recipe requires all three external codec providers.
for header in FLAC/stream_decoder.h vorbis/codec.h opus/opus.h; do
  [[ -f "$output/sysroot/usr/include/$header" ]] || { echo "missing codec development header: $header" >&2; exit 67; }
done
source_url=https://github.com/libsndfile/libsndfile/releases/download/1.2.2/libsndfile-1.2.2.tar.xz
source_sha=3799ca9924d3125038880367bf1468e53a1b7e3686a934f098b7e1d286cdb80e
buildroot_commit=3815d578c5759fa824322ea3d95ad51b55ab888e
curl --fail --location --retry 3 "$source_url" -o "$output/source/libsndfile-1.2.2.tar.xz"
[[ $(sha256sum "$output/source/libsndfile-1.2.2.tar.xz" | cut -d' ' -f1) == "$source_sha" ]]
tar -xf "$output/source/libsndfile-1.2.2.tar.xz" -C "$output/source"
mapfile -t patches < <(python3 - "$metadata" <<'PY'
import json,re,sys
record=json.load(open(sys.argv[1]))['libsndfile']
assert record['version']=='1.2.2'
patches=record['patches']
assert len(patches)==14 and len(set(patches))==14
for path in patches:
    assert re.fullmatch(r'package/libsndfile/[0-9]{4}-[A-Za-z0-9_.-]+\.patch',path)
    print(path)
PY
)
[[ ${#patches[@]} == 14 ]] || exit 66
for patch_path in "${patches[@]}"; do
  local_patch="$output/patches/${patch_path##*/}"
  curl --fail --location --retry 3 "https://raw.githubusercontent.com/buildroot/buildroot/$buildroot_commit/$patch_path" -o "$local_patch"
  patch -d "$output/source/libsndfile-1.2.2" -p1 --fuzz=0 --forward --dry-run < "$local_patch"
  patch -d "$output/source/libsndfile-1.2.2" -p1 --fuzz=0 --forward < "$local_patch"
done
sha256sum "$output/source/libsndfile-1.2.2.tar.xz" "$output/patches/"*.patch "$sdk/tdvp-sdk-manifest.json" "$metadata" > "$output/inputs.sha256"
sysroot="$output/sysroot"
export CC="$sdk/bin/riscv64-unknown-linux-gnu-gcc --sysroot=$sysroot"
export CXX="$sdk/bin/riscv64-unknown-linux-gnu-g++ --sysroot=$sysroot"
export NM="$sdk/bin/riscv64-unknown-linux-gnu-nm" STRIP="$sdk/bin/riscv64-unknown-linux-gnu-strip"
export AR="$sdk/bin/riscv64-unknown-linux-gnu-gcc-ar" RANLIB="$sdk/bin/riscv64-unknown-linux-gnu-gcc-ranlib"
export CFLAGS='-O1 -fPIC -march=rv64imafdc_zicsr_zifencei -mabi=lp64d'
export CXXFLAGS="$CFLAGS"
export CPPFLAGS="-I$sysroot/usr/include" LDFLAGS="-L$sysroot/usr/lib -Wl,-rpath-link,$sysroot/usr/lib"
export PKG_CONFIG=/usr/bin/pkg-config PKG_CONFIG_SYSROOT_DIR="$sysroot" PKG_CONFIG_LIBDIR="$sysroot/usr/lib/pkgconfig:$sysroot/usr/share/pkgconfig" PKG_CONFIG_PATH=''
export ac_cv_prog_cc_c99='-std=gnu99'
export lt_cv_sys_lib_dlsearch_path_spec="/lib /usr/lib $sysroot/lib $sysroot/usr/lib"
cd "$output/source/libsndfile-1.2.2"
./configure --build="$(gcc -dumpmachine)" --host=riscv64-unknown-linux-gnu --prefix=/usr --with-sysroot="$sysroot" \
  --enable-shared --disable-static --disable-sqlite --disable-alsa --disable-full-suite --enable-external-libs
grep -Eq '^#define HAVE_EXTERNAL_XIPH_LIBS 1$' src/config.h
make -j"${TDVP_JOBS:-4}"
make DESTDIR="$output/install" install
printf 'Audio codec proof built; isolated install root: %s\n' "$output/install"
