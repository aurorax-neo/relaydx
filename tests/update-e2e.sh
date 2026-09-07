#!/bin/sh
set -eu

RELAYDX_BIN=${RELAYDX_BIN:-./relaydx}
ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(tr -d '[:space:]' < "$ROOT_DIR/version.txt")
TEMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/relaydx-update-test.XXXXXX")
trap 'rm -rf "$TEMP_DIR"' EXIT INT TERM

case "$(uname -m)" in
    x86_64|amd64) TARGET=x86_64-unknown-linux-musl ;;
    aarch64|arm64) TARGET=aarch64-unknown-linux-musl ;;
    *) exit 77 ;;
esac

PACKAGE="relaydx-v${VERSION}-${TARGET}"
SERVER="$TEMP_DIR/server"
INSTALL_ROOT="$TEMP_DIR/root"
PACKAGE_DIR="$TEMP_DIR/$PACKAGE"
mkdir -p "$SERVER" "$INSTALL_ROOT/usr/share/relaydx" "$PACKAGE_DIR"
printf '0.0.0\n' > "$INSTALL_ROOT/usr/share/relaydx/version.txt"

install -m 0755 "$RELAYDX_BIN" "$PACKAGE_DIR/relaydx"
install -m 0755 "$ROOT_DIR/scripts/install.sh" "$PACKAGE_DIR/install.sh"
install -m 0755 "$ROOT_DIR/scripts/uninstall.sh" "$PACKAGE_DIR/uninstall.sh"
install -m 0755 "$ROOT_DIR/scripts/update.sh" "$PACKAGE_DIR/update.sh"
install -m 0644 "$ROOT_DIR/contrib/relaydx.service" "$PACKAGE_DIR/relaydx.service"
install -m 0644 "$ROOT_DIR/contrib/relaydx.default" "$PACKAGE_DIR/relaydx.default"
install -m 0644 "$ROOT_DIR/version.txt" "$PACKAGE_DIR/version.txt"
tar -C "$TEMP_DIR" -czf "$SERVER/$PACKAGE.tar.gz" "$PACKAGE"
(
    cd "$SERVER"
    sha256sum "$PACKAGE.tar.gz" > "$PACKAGE.tar.gz.sha256"
)

DESTDIR="$INSTALL_ROOT" RELAYDX_BASE_URL="file://$SERVER" \
    "$ROOT_DIR/scripts/update.sh" "v$VERSION"
test -x "$INSTALL_ROOT/usr/sbin/relaydx"
test -x "$INSTALL_ROOT/usr/sbin/relaydx-update"
test "$(tr -d '[:space:]' < "$INSTALL_ROOT/usr/share/relaydx/version.txt")" = \
    "$VERSION"

output=$(DESTDIR="$INSTALL_ROOT" "$INSTALL_ROOT/usr/sbin/relaydx-update" \
    "v$VERSION")
printf '%s\n' "$output" | grep -q 'already installed; skipping update'

DESTDIR="$INSTALL_ROOT" "$PACKAGE_DIR/uninstall.sh" --purge
test ! -e "$INSTALL_ROOT/usr/sbin/relaydx"
test ! -e "$INSTALL_ROOT/usr/sbin/relaydx-update"
test ! -e "$INSTALL_ROOT/usr/share/relaydx/version.txt"
echo "relaydx update test passed"
