#!/bin/bash
set -euo pipefail
BIN="${RELAYDX_BIN:-/home/relaydx/relaydx/build/relaydx}"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
p=""
cleanup() {
  [ -z "$p" ] || kill -TERM "$p" 2>/dev/null || true
  [ -z "$p" ] || wait "$p" 2>/dev/null || true
  for n in nsup nsdown; do ip netns del "$n" 2>/dev/null || true; done
  ip link del rdx0 2>/dev/null || true
  ip link del rdx1 2>/dev/null || true
}
trap cleanup EXIT
cleanup
ip netns add nsup
ip netns add nsdown
ip link add rdx0 type veth peer name up0
ip link add rdx1 type veth peer name down0
ip link set up0 netns nsup
ip link set down0 netns nsdown
ip link set rdx0 up
ip link set rdx1 up
for n in nsup nsdown; do ip netns exec "$n" ip link set lo up; done
ip netns exec nsup ip link set up0 up
ip netns exec nsdown ip link set down0 up
for d in rdx0 rdx1; do echo 0 > /proc/sys/net/ipv6/conf/$d/accept_dad; done
echo 2 > /proc/sys/net/ipv6/conf/rdx0/accept_ra
echo 1 > /proc/sys/net/ipv6/conf/all/forwarding
ip netns exec nsup sh -c 'echo 0 > /proc/sys/net/ipv6/conf/up0/accept_dad'
ip netns exec nsdown sh -c 'echo 0 > /proc/sys/net/ipv6/conf/down0/accept_dad; echo 0 > /proc/sys/net/ipv6/conf/all/forwarding; echo 2 > /proc/sys/net/ipv6/conf/down0/accept_ra; echo 1 > /proc/sys/net/ipv6/conf/down0/autoconf'
ip -6 addr add fd10::1/64 dev rdx0
ip netns exec nsup ip -6 addr add fe80::100/64 dev up0
ip netns exec nsup ip -6 addr add fd10::100/64 dev up0
$BIN -6 -M rdx0 -i rdx0 -i rdx1 --no-forwarding-setup -vv >/tmp/rdx-ipv6-e2e.log 2>&1 & p=$!
sleep 3
kill -0 "$p"
ip netns exec nsup python3 "$SCRIPT_DIR/ra-router.py" up0 fe80::100 fd10::
addr=""
for _ in $(seq 1 15); do
  addr=$(ip netns exec nsdown ip -o -6 addr show dev down0 scope global | sed -n '1s/.* inet6 \([^/]*\).*/\1/p')
  [ -n "$addr" ] && break
  sleep 1
done
[ -n "$addr" ]
echo "downstream_address=$addr"
ip netns exec nsdown ip -6 route
sleep 2
ip netns exec nsdown ping -6 -c 5 -W 3 fd10::100
ip netns exec nsup ping -6 -c 5 -W 3 "$addr"
echo ---host-routes---
ip -6 route show | grep -E "fd10::100|$addr"
kill -TERM "$p"
wait "$p"
p=""
echo daemon_exit=0
grep -E 'Got a RA|Learned|Relay active' /tmp/rdx-ipv6-e2e.log | tail -30
