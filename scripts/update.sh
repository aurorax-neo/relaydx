#!/bin/sh
set -eu

REPOSITORY=${RELAYDX_REPOSITORY:-aurorax-neo/relaydx}
ROOT=${DESTDIR:-}
MIRROR=${RELAYDX_GITHUB_MIRROR:-}
REQUESTED_VERSION=latest
VERSION_FILE="$ROOT/usr/share/relaydx/version.txt"
DEFAULT_FILE="$ROOT/etc/default/relaydx"

usage()
{
    echo "Usage: relaydx-update [--mirror URL] [VERSION]"
    echo "       RELAYDX_GITHUB_MIRROR=https://ghfast.top relaydx-update"
    echo "If VERSION is omitted, the latest GitHub Release is installed."
    echo "A GitHub mirror prefixes download URLs, for example:"
    echo "  https://ghfast.top/https://github.com/..."
}

quote_strip()
{
    printf '%s' "$1" | sed 's/^["'\'']//; s/["'\'']$//'
}

if [ -z "$MIRROR" ] && [ -f "$DEFAULT_FILE" ]; then
    file_mirror=$(sed -n 's/^[[:space:]]*RELAYDX_GITHUB_MIRROR=//p' \
        "$DEFAULT_FILE" | tail -n 1)
    if [ -n "$file_mirror" ]; then
        MIRROR=$(quote_strip "$file_mirror")
    fi
fi

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --mirror)
            if [ $# -lt 2 ]; then
                echo "relaydx-update: --mirror requires a URL" >&2
                exit 1
            fi
            MIRROR=$2
            shift 2
            ;;
        --mirror=*)
            MIRROR=${1#--mirror=}
            shift
            ;;
        -*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
        *)
            REQUESTED_VERSION=$1
            shift
            ;;
    esac
done

if [ -n "${VERSION:-}" ] && [ "$REQUESTED_VERSION" = latest ]; then
    REQUESTED_VERSION=$VERSION
fi

case "$(uname -m)" in
    x86_64|amd64) TARGET=x86_64-unknown-linux-musl ;;
    aarch64|arm64) TARGET=aarch64-unknown-linux-musl ;;
    *)
        echo "Unsupported architecture: $(uname -m)" >&2
        exit 1
        ;;
esac

github_url()
{
    url=$1
    case "$url" in
        file:*|http://127.*|http://localhost*|https://127.*|https://localhost*)
            printf '%s\n' "$url"
            return
            ;;
    esac
    if [ -n "${RELAYDX_BASE_URL:-}" ]; then
        printf '%s\n' "$url"
        return
    fi
    if [ -z "$MIRROR" ]; then
        printf '%s\n' "$url"
        return
    fi
    printf '%s/%s\n' "${MIRROR%/}" "$url"
}

if [ "$REQUESTED_VERSION" = latest ]; then
    command -v curl >/dev/null 2>&1 || {
        echo "curl is required to find the latest release" >&2
        exit 1
    }
    latest_url=$(curl -fsSL -o /dev/null -w '%{url_effective}' \
        "$(github_url "https://github.com/$REPOSITORY/releases/latest")")
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
if [ -n "${RELAYDX_BASE_URL:-}" ]; then
    ARCHIVE_URL="$RELAYDX_BASE_URL/$PACKAGE.tar.gz"
    CHECKSUM_URL="$RELAYDX_BASE_URL/$PACKAGE.tar.gz.sha256"
else
    GITHUB_ASSET="https://github.com/$REPOSITORY/releases/download/$VERSION"
    ARCHIVE_URL=$(github_url "$GITHUB_ASSET/$PACKAGE.tar.gz")
    CHECKSUM_URL=$(github_url "$GITHUB_ASSET/$PACKAGE.tar.gz.sha256")
fi
TEMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/relaydx-update.XXXXXX")
trap 'rm -rf "$TEMP_DIR"' EXIT INT TERM

cd "$TEMP_DIR"
echo "Downloading relaydx $VERSION for $TARGET..."
if [ -n "$MIRROR" ] && [ -z "${RELAYDX_BASE_URL:-}" ]; then
    echo "Using GitHub mirror: $MIRROR"
fi
curl -fL -o "$PACKAGE.tar.gz" "$ARCHIVE_URL"
curl -fL -o "$PACKAGE.tar.gz.sha256" "$CHECKSUM_URL"
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
