#!/bin/sh
set -eu

package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=${DESTDIR:-}

if [ -z "$root" ] && [ "$(id -u)" -ne 0 ]; then
    echo "Run this installer as root (for example: sudo ./install.sh)." >&2
    exit 1
fi

install -d "$root/usr/sbin" "$root/usr/share/relaydx" \
    "$root/etc/systemd/system" "$root/etc/default"
install -m 0755 "$package_dir/relaydx" "$root/usr/sbin/relaydx"
install -m 0755 "$package_dir/update.sh" "$root/usr/sbin/relaydx-update"
install -m 0644 "$package_dir/version.txt" "$root/usr/share/relaydx/version.txt"
install -m 0644 "$package_dir/relaydx.service" \
    "$root/etc/systemd/system/relaydx.service"

if [ ! -e "$root/etc/default/relaydx" ]; then
    install -m 0644 "$package_dir/relaydx.default" "$root/etc/default/relaydx"
    echo "Installed default configuration: /etc/default/relaydx"
else
    echo "Preserved existing configuration: /etc/default/relaydx"
fi

if [ -z "$root" ] && command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload
fi

echo "Installed relaydx to /usr/sbin/relaydx"
echo "Installed updater to /usr/sbin/relaydx-update"
echo "Edit /etc/default/relaydx, then run:"
echo "  sudo systemctl enable --now relaydx"
