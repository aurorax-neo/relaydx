#!/bin/sh
set -eu

REPOSITORY=${RELAYDX_REPOSITORY:-aurorax-neo/relaydx}
ROOT=${DESTDIR:-}
REQUESTED_VERSION=${1:-${VERSION:-latest}}
VERSION_FILE="$ROOT/usr/share/relaydx/version.txt"

usage()
{
    echo "Usage: relaydx-update [VERSION]"
    echo "       VERSION=v0.1.3 relaydx-update"
    echo "If VERSION is omitted, the latest GitHub Release is installed."
}

case "$REQUESTED_VERSION" in
    -h|--help)
        usage
        exit 0
        ;;
esac

case "$(uname -m)" in
    x86_64|amd64) TARGET=x86_64-unknown-linux-musl ;;
    aarch64|arm64) TARGET=aarch64-unknown-linux-musl ;;
    *)
        echo "Unsupported architecture: $(uname -m)" >&2
        exit 1
        ;;
esac

if [ "$REQUESTED_VERSION" = latest ]; then
    command -v curl >/dev/null 2>&1 || {
        echo "curl is required to find the latest release" >&2
        exit 1
    }
    latest_url=$(curl -fsSL -o /dev/null -w '%{url_effective}' \
        "https://github.com/$REPOSITORY/releases/latest")
    REQUESTED_VERSION=${latest_url##*/}
fi
case "$REQUESTED_VERSION" in
    v*) VERSION=$REQUESTED_VERSION ;;
    *) VERSION="v$REQUESTED_VERSION" ;;
esac
if ! printf '%s\n' "$VERSION" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Invalid version: $VERSION" >&2
    exit 1
fi

CURRENT_VERSION=unknown
if [ -f "$VERSION_FILE" ]; then
    CURRENT_VERSION="v$(tr -d '[:space:]' < "$VERSION_FILE")"
fi
if [ "$CURRENT_VERSION" = "$VERSION" ] &&
        [ -x "$ROOT/usr/sbin/relaydx" ]; then
    echo "relaydx $VERSION is already installed; skipping update."
    exit 0
fi

if [ -z "$ROOT" ] && [ "$(id -u)" -ne 0 ]; then
    echo "Run relaydx-update as root (for example: sudo relaydx-update)." >&2
    exit 1
fi
for command_name in curl sha256sum tar mktemp; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "$command_name is required for updates" >&2
        exit 1
    }
done

PACKAGE="relaydx-${VERSION}-${TARGET}"
BASE_URL=${RELAYDX_BASE_URL:-https://github.com/$REPOSITORY/releases/download/$VERSION}
TEMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/relaydx-update.XXXXXX")
trap 'rm -rf "$TEMP_DIR"' EXIT INT TERM

cd "$TEMP_DIR"
echo "Downloading relaydx $VERSION for $TARGET..."
curl -fLO "$BASE_URL/$PACKAGE.tar.gz"
curl -fLO "$BASE_URL/$PACKAGE.tar.gz.sha256"
sha256sum -c "$PACKAGE.tar.gz.sha256"
tar -xzf "$PACKAGE.tar.gz"

if [ ! -x "$PACKAGE/install.sh" ] || [ ! -x "$PACKAGE/relaydx" ] ||
        [ "$(tr -d '[:space:]' < "$PACKAGE/version.txt")" != "${VERSION#v}" ]; then
    echo "Downloaded release package is incomplete or has the wrong version" >&2
    exit 1
fi

WAS_ACTIVE=false
if [ -z "$ROOT" ] && command -v systemctl >/dev/null 2>&1 &&
        systemctl is-active --quiet relaydx.service; then
    WAS_ACTIVE=true
fi

DESTDIR="$ROOT" "$TEMP_DIR/$PACKAGE/install.sh"
if $WAS_ACTIVE; then
    echo "Restarting relaydx.service..."
    systemctl restart relaydx.service
fi

echo "Updated relaydx from $CURRENT_VERSION to $VERSION"
