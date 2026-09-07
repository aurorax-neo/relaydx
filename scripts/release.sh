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
if [[ ! "$file_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]]; then
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

build_dir="$root_dir/build-release"
stage_dir="$root_dir/.release-stage"
dist_dir="$root_dir/dist"
archive="relaydx-${version}-linux-${archive_arch}.tar.gz"

rm -rf "$build_dir" "$stage_dir"
mkdir -p "$dist_dir"
cmake -S "$root_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DRELAYDX_BUILD_EXECUTABLE=ON \
    -DRELAYDX_NETWORK_TESTING=OFF
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure

DESTDIR="$stage_dir" cmake --install "$build_dir" --prefix /usr
mkdir -p "$stage_dir/usr/share/doc/relaydx" "$stage_dir/etc/systemd/system" 
cp "$root_dir/LICENSE" "$root_dir/README.md" \
    "$root_dir/THIRD_PARTY_NOTICES.md" "$stage_dir/usr/share/doc/relaydx/"
cp "$root_dir/contrib/relaydx.service" "$stage_dir/etc/systemd/system/"
cp "$root_dir/contrib/relaydx.default" "$stage_dir/usr/share/doc/relaydx/"

rm -f "$dist_dir/$archive" "$dist_dir/$archive.sha256"
tar -C "$stage_dir" -czf "$dist_dir/$archive" .
(
    cd "$dist_dir"
    sha256sum "$archive" > "$archive.sha256"
)
printf 'Created %s\n' "$dist_dir/$archive"
printf 'Created %s\n' "$dist_dir/$archive.sha256"
