#!/bin/bash
set -euo pipefail
BIN="${RELAYDX_BIN:-/home/relaydx/relaydx/build/relaydx}"
p=""
cleanup() {
  [ -z "$p" ] || kill -TERM "$p" 2>/dev/null || true
  [ -z "$p" ] || wait "$p" 2>/dev/null || true
  for n in nsclient nsremote; do ip netns del "$n" 2>/dev/null || true; done
  ip link del down0 2>/dev/null || true
  ip link del remote0 2>/dev/null || true
}
trap cleanup EXIT
cleanup
ip netns add nsclient
ip netns add nsremote
ip link add down0 type veth peer name client0
ip link add remote0 type veth peer name far0
ip link set client0 netns nsclient
ip link set far0 netns nsremote
ip link set down0 up
ip link set remote0 up
ip netns exec nsclient ip link set lo up
ip netns exec nsclient ip link set client0 up
ip netns exec nsremote ip link set lo up
ip netns exec nsremote ip link set far0 up
for d in down0 remote0; do echo 0 > /proc/sys/net/ipv6/conf/$d/accept_dad; done
ip netns exec nsclient sh -c 'echo 0 > /proc/sys/net/ipv6/conf/client0/accept_dad'
ip netns exec nsremote sh -c 'echo 0 > /proc/sys/net/ipv6/conf/far0/accept_dad'
echo 1 > /proc/sys/net/ipv6/conf/all/forwarding
ip -6 addr add fd10::1/56 dev down0
ip -6 addr add fd30::1/64 dev remote0
ip netns exec nsclient ip -6 addr add fd10::2/56 dev client0
ip netns exec nsremote ip -6 addr add fd30::100/64 dev far0
ip netns exec nsremote ip -6 route add default via fd30::1
$BIN --server6 -M . -i down0 --no-interface-watch --no-forwarding-setup -vv >/tmp/rdx-iana-e2e.log 2>&1 & p=$!
sleep 2
result=$(ip netns exec nsclient python3 /tmp/relaydx-dhcp6-ia-test.py na client0 fd10::2)
echo "$result"
assigned=$(printf '%s\n' "$result" | sed -n 's/.*address=\([^ ]*\).*/\1/p')
[ -n "$assigned" ]
ip netns exec nsclient ip -6 addr del fd10::2/56 dev client0
ip netns exec nsclient ip -6 addr add "$assigned"/64 dev client0
ip netns exec nsclient ip -6 route add default via fd10::1
sleep 1
echo "configured_ia_na=$assigned"
ip netns exec nsclient ping -6 -I "$assigned" -c 5 -W 3 fd30::100
ip netns exec nsremote ping -6 -c 5 -W 3 "$assigned"
kill -TERM "$p"
wait "$p"
p=""
echo daemon_exit=0
grep -E 'Got DHCPv6 request|Learned|Relay active' /tmp/rdx-iana-e2e.log | tail -30
