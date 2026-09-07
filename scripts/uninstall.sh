#!/bin/sh
set -eu

root=${DESTDIR:-}
purge=false

if [ "${1:-}" = "--purge" ]; then
    purge=true
elif [ "$#" -ne 0 ]; then
    echo "Usage: $0 [--purge]" >&2
    exit 1
fi

if [ -z "$root" ] && [ "$(id -u)" -ne 0 ]; then
    echo "Run this uninstaller as root (for example: sudo ./uninstall.sh)." >&2
    exit 1
fi

if [ -z "$root" ] && command -v systemctl >/dev/null 2>&1; then
    systemctl disable --now relaydx.service 2>/dev/null || true
fi

rm -f "$root/usr/sbin/relaydx" "$root/etc/systemd/system/relaydx.service"
if $purge; then
    rm -f "$root/etc/default/relaydx"
    echo "Removed /etc/default/relaydx"
else
    echo "Preserved /etc/default/relaydx (use --purge to remove it)"
fi

if [ -z "$root" ] && command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload
    systemctl reset-failed relaydx.service 2>/dev/null || true
fi

echo "Uninstalled relaydx"
