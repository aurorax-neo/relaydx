#!/bin/bash
set -euo pipefail
BIN="${RELAYDX_BIN:-/home/relaydx/relaydx/build/relaydx}"
p=""
cleanup() {
  [ -z "$p" ] || kill -TERM "$p" 2>/dev/null || true
  [ -z "$p" ] || wait "$p" 2>/dev/null || true
  for n in nsrouter nsleaf nsremote; do ip netns del "$n" 2>/dev/null || true; done
  ip link del down0 2>/dev/null || true
  ip link del remote0 2>/dev/null || true
  ip link del lan0 2>/dev/null || true
}
trap cleanup EXIT
cleanup
for n in nsrouter nsleaf nsremote; do ip netns add "$n"; ip netns exec "$n" ip link set lo up; done
ip link add down0 type veth peer name wan0
ip link add remote0 type veth peer name far0
ip link add lan0 type veth peer name leaf0
ip link set wan0 netns nsrouter
ip link set lan0 netns nsrouter
ip link set leaf0 netns nsleaf
ip link set far0 netns nsremote
ip link set down0 up
ip link set remote0 up
ip netns exec nsrouter ip link set wan0 up
ip netns exec nsrouter ip link set lan0 up
ip netns exec nsleaf ip link set leaf0 up
ip netns exec nsremote ip link set far0 up
for d in down0 remote0; do echo 0 > /proc/sys/net/ipv6/conf/$d/accept_dad; done
for spec in 'nsrouter wan0' 'nsrouter lan0' 'nsleaf leaf0' 'nsremote far0'; do set -- $spec; ip netns exec "$1" sh -c "echo 0 > /proc/sys/net/ipv6/conf/$2/accept_dad"; done
echo 1 > /proc/sys/net/ipv6/conf/all/forwarding
ip netns exec nsrouter sh -c 'echo 1 > /proc/sys/net/ipv6/conf/all/forwarding'
ip -6 addr add fd10::1/56 dev down0
ip -6 addr add fd30::1/64 dev remote0
ip netns exec nsrouter ip -6 addr add fd10::2/56 dev wan0
ip netns exec nsrouter ip -6 route add default via fd10::1
ip netns exec nsremote ip -6 addr add fd30::100/64 dev far0
ip netns exec nsremote ip -6 route add default via fd30::1
$BIN --server6 -M . -i down0 --no-interface-watch --no-forwarding-setup -vv >/tmp/rdx-pd-e2e.log 2>&1 & p=$!
sleep 2
result=$(ip netns exec nsrouter python3 /tmp/relaydx-dhcp6-ia-test.py pd wan0 fd10::2)
echo "$result"
echo "$result" | grep -q 'prefix=fd10:0:0:4::/62'
ip -6 route show dev down0 | grep -q 'fd10:0:0:4::/62 via fd10::2'
ip netns exec nsrouter ip -6 addr add fd10:0:0:4::1/64 dev lan0
ip netns exec nsleaf ip -6 addr add fd10:0:0:4::2/64 dev leaf0
ip netns exec nsleaf ip -6 route add default via fd10:0:0:4::1
sleep 1
echo ---router-routes---; ip netns exec nsrouter ip -6 route
echo ---host-pd-route---; ip -6 route show dev down0 | grep 'fd10:0:0:4::/62'
ip netns exec nsleaf ping -6 -c 5 -W 3 fd30::100
ip netns exec nsremote ping -6 -c 5 -W 3 fd10:0:0:4::2
ip netns exec nsrouter python3 /tmp/relaydx-dhcp6-release.py pd wan0 fd10::2
if ip -6 route show dev down0 | grep -q 'fd10:0:0:4::/62 via fd10::2'; then
  echo stale_pd_route_after_release
  exit 1
fi
echo pd_route_after_release=removed
kill -TERM "$p"
wait "$p"
p=""
echo daemon_exit=0
