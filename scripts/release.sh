#!/usr/bin/env bash
set -Eeuo pipefail

root_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=${1:-}
arch=${2:-$(uname -m)}
version_file="$root_dir/version.txt"

if [[ ! -f "$version_file" ]]; then
    printf 'Missing version file: %s\n' "$version_file" >&2
    exit 1
fi
file_version=$(head -n 1 "$version_file" | tr -d '[:space:]')
if [[ ! "$file_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'Invalid version.txt value: %s\n' "$file_version" >&2
    exit 1
fi
if [[ -z "$version" ]]; then
    version="$file_version"
fi
version=${version#v}
version=${version//\//-}
if [[ "$version" != "$file_version" ]]; then
    printf 'Release version %s does not match version.txt (%s)\n' \
        "$version" "$file_version" >&2
    exit 1
fi

case "$arch" in
    aarch64|arm64) archive_arch=arm64 ;;
    x86_64|amd64) archive_arch=amd64 ;;
    *) archive_arch=$arch ;;
esac

compiler=${RELAYDX_CC:-musl-gcc}
if ! command -v "$compiler" >/dev/null 2>&1; then
    printf 'Static release compiler not found: %s\n' "$compiler" >&2
    printf 'Install musl-tools or set RELAYDX_CC to a musl compiler.\n' >&2
    exit 1
fi
if ! command -v readelf >/dev/null 2>&1 || \
        ! command -v strip >/dev/null 2>&1; then
    printf 'readelf and strip are required from binutils.\n' >&2
    exit 1
fi

multiarch=$("$compiler" -print-multiarch)
kernel_asm_dir="/usr/include/$multiarch/asm"
if [[ ! -d /usr/include/linux || ! -d /usr/include/asm-generic || \
        ! -d "$kernel_asm_dir" ]]; then
    printf 'Linux UAPI headers are missing for %s.\n' "$multiarch" >&2
    printf 'Install the Linux libc development headers for this architecture.\n' >&2
    exit 1
fi

build_dir="$root_dir/build-release"
stage_dir="$root_dir/.release-stage"
dist_dir="$root_dir/dist"
uapi_dir="$stage_dir/linux-uapi"
package_name="relaydx-${version}-linux-${archive_arch}"
package_dir="$stage_dir/$package_name"
archive="$package_name.tar.gz"

rm -rf "$build_dir" "$stage_dir"
mkdir -p "$dist_dir" "$package_dir" "$uapi_dir"
ln -s /usr/include/linux "$uapi_dir/linux"
ln -s /usr/include/asm-generic "$uapi_dir/asm-generic"
ln -s "$kernel_asm_dir" "$uapi_dir/asm"
cmake -S "$root_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$compiler" \
    -DCMAKE_C_FLAGS="-isystem $uapi_dir" \
    -DCMAKE_EXE_LINKER_FLAGS=-static \
    -DRELAYDX_BUILD_EXECUTABLE=ON \
    -DRELAYDX_NETWORK_TESTING=OFF
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
strip --strip-unneeded "$build_dir/relaydx"

if readelf -l "$build_dir/relaydx" | grep -q ' INTERP '; then
    printf 'Release binary contains a dynamic program interpreter.\n' >&2
    exit 1
fi
if readelf -d "$build_dir/relaydx" | grep -q '(NEEDED)'; then
    printf 'Release binary contains dynamic library dependencies.\n' >&2
    exit 1
fi

install -m 0755 "$build_dir/relaydx" "$package_dir/relaydx"
install -m 0755 "$root_dir/scripts/install.sh" "$package_dir/install.sh"
install -m 0755 "$root_dir/scripts/uninstall.sh" "$package_dir/uninstall.sh"
install -m 0644 "$root_dir/contrib/relaydx.service" \
    "$package_dir/relaydx.service"
install -m 0644 "$root_dir/contrib/relaydx.default" \
    "$package_dir/relaydx.default"
install -m 0644 "$root_dir/version.txt" "$package_dir/version.txt"
install -m 0644 "$root_dir/LICENSE" "$root_dir/README.md" \
    "$root_dir/README.zh.md" "$root_dir/THIRD_PARTY_NOTICES.md" "$package_dir/"

rm -f "$dist_dir/$archive" "$dist_dir/$archive.sha256"
tar -C "$stage_dir" -czf "$dist_dir/$archive" "$package_name"
(
    cd "$dist_dir"
    sha256sum "$archive" > "$archive.sha256"
)
printf 'Created %s\n' "$dist_dir/$archive"
printf 'Created %s\n' "$dist_dir/$archive.sha256"
