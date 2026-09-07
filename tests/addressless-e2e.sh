#!/bin/sh
set -eu

RELAYDX_BIN=${RELAYDX_BIN:-./relaydx}
RELAYDX_BIN=$(readlink -f "$RELAYDX_BIN")
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RELAY_NS=rdx_addr_relay
UP_NS=rdx_addr_up
DOWN_NS=rdx_addr_down
UP_HOST=rdxau0
UP_PEER=rdxau1
DOWN_HOST=rdxad0
DOWN_PEER=rdxad1
PID=

cleanup()
{
    if [ -n "$PID" ]; then
        kill "$PID" 2>/dev/null || true
        wait "$PID" 2>/dev/null || true
    fi
    ip netns del "$RELAY_NS" 2>/dev/null || true
    ip netns del "$UP_NS" 2>/dev/null || true
    ip netns del "$DOWN_NS" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    echo "addressless network test requires root" >&2
    exit 77
fi

cleanup
ip netns add "$RELAY_NS"
ip netns add "$UP_NS"
ip netns add "$DOWN_NS"
ip link add "$UP_HOST" type veth peer name "$UP_PEER"
ip link add "$DOWN_HOST" type veth peer name "$DOWN_PEER"
ip link set "$UP_HOST" netns "$RELAY_NS"
ip link set "$DOWN_HOST" netns "$RELAY_NS"
ip link set "$UP_PEER" netns "$UP_NS"
ip link set "$DOWN_PEER" netns "$DOWN_NS"
ip -n "$RELAY_NS" link set lo up
ip -n "$RELAY_NS" link set "$UP_HOST" up
ip -n "$RELAY_NS" link set "$DOWN_HOST" up
ip -n "$UP_NS" link set lo up
ip -n "$DOWN_NS" link set lo up
ip -n "$UP_NS" link set "$UP_PEER" up
ip -n "$DOWN_NS" link set "$DOWN_PEER" up

# Addresses on relay interfaces must be removed while endpoint addresses remain.
ip -n "$RELAY_NS" addr add 198.51.100.10/24 dev "$UP_HOST"
ip -n "$RELAY_NS" addr add 198.51.100.11/24 dev "$DOWN_HOST"
ip -n "$RELAY_NS" -6 addr add 2001:db8:ffff::10/64 dev "$UP_HOST"
ip -n "$RELAY_NS" -6 addr add 2001:db8:ffff::11/64 dev "$DOWN_HOST"
ip -n "$UP_NS" addr add 192.0.2.1/24 dev "$UP_PEER"
ip -n "$DOWN_NS" addr add 192.0.2.2/24 dev "$DOWN_PEER"
ip -n "$UP_NS" -6 addr add fe80::100/64 dev "$UP_PEER"
ip -n "$UP_NS" -6 addr add 2001:db8:1::1/64 dev "$UP_PEER"
ip -n "$DOWN_NS" -6 addr add 2001:db8:1::2/64 dev "$DOWN_PEER"
sleep 2

ip netns exec "$RELAY_NS" "$RELAYDX_BIN" \
    -4 -6 --dhcp6 off --no-address4 --no-address6 \
    -M "$UP_HOST" -I "$UP_HOST" -I "$DOWN_HOST" &
PID=$!
sleep 2
kill -0 "$PID"
ip netns exec "$UP_NS" python3 "$SCRIPT_DIR/ra-router.py" \
    "$UP_PEER" fe80::100 2001:db8:1::
sleep 2

if ip -n "$RELAY_NS" -4 -o addr show dev "$UP_HOST" | grep -q . ||
        ip -n "$RELAY_NS" -4 -o addr show dev "$DOWN_HOST" | grep -q .; then
    echo "IPv4 address suppression failed" >&2
    exit 1
fi
if ip -n "$RELAY_NS" -6 -o addr show dev "$UP_HOST" scope global | grep -q . ||
        ip -n "$RELAY_NS" -6 -o addr show dev "$DOWN_HOST" scope global | grep -q .; then
    echo "IPv6 global address suppression failed" >&2
    exit 1
fi
ip -n "$RELAY_NS" -6 -o addr show dev "$UP_HOST" scope link | grep -q 'fe80:'
ip -n "$RELAY_NS" -6 -o addr show dev "$DOWN_HOST" scope link | grep -q 'fe80:'

# Verify that addresses added after startup are removed by the link monitor.
ip -n "$RELAY_NS" addr add 203.0.113.20/24 dev "$DOWN_HOST"
ip -n "$RELAY_NS" -6 addr add 2001:db8:ffff::20/64 dev "$DOWN_HOST"
for _ in 1 2 3 4 5; do
    if ! ip -n "$RELAY_NS" -4 -o addr show dev "$DOWN_HOST" | grep -q . &&
            ! ip -n "$RELAY_NS" -6 -o addr show dev "$DOWN_HOST" scope global |
                grep -q .; then
        break
    fi
    sleep 1
done
if ip -n "$RELAY_NS" -4 -o addr show dev "$DOWN_HOST" | grep -q . ||
        ip -n "$RELAY_NS" -6 -o addr show dev "$DOWN_HOST" scope global |
            grep -q .; then
    echo "runtime address suppression failed" >&2
    exit 1
fi
kill -0 "$PID"
ip netns exec "$UP_NS" python3 "$SCRIPT_DIR/ra-router.py" \
    "$UP_PEER" fe80::100 2001:db8:1::
sleep 1

# Prime both host tables by emitting ARP/NS from each endpoint.
ip netns exec "$UP_NS" ping -c 1 -W 1 192.0.2.254 >/dev/null 2>&1 || true
ip netns exec "$DOWN_NS" ping -c 1 -W 1 192.0.2.254 >/dev/null 2>&1 || true
ip netns exec "$UP_NS" ping -6 -c 1 -W 1 2001:db8:1::ffff >/dev/null 2>&1 || true
ip netns exec "$DOWN_NS" ping -6 -c 1 -W 1 2001:db8:1::ffff >/dev/null 2>&1 || true
sleep 1

ip netns exec "$DOWN_NS" ping -c 3 -W 2 192.0.2.1
ip netns exec "$UP_NS" ping -c 3 -W 2 192.0.2.2
ip netns exec "$DOWN_NS" ping -6 -c 3 -W 2 2001:db8:1::1
ip netns exec "$UP_NS" ping -6 -c 3 -W 2 2001:db8:1::2

echo "addressless IPv4 and IPv6 relay passed"
