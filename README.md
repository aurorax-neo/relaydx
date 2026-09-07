# relaydx

[English](README.md) | [中文](README.zh.md)

`relaydx` relays IPv4 ARP/DHCP and IPv6 RA, DHCPv6 and NDP between
interfaces in one process and one event loop.

There is no configuration file: pass everything on the command line.
The systemd unit forwards those flags through `RELAYDX_OPTIONS`.

## Features

- IPv4 ARP proxying and host-route management
- IPv4 DHCP and broadcast forwarding
- IPv6 Router Advertisement relay or server mode
- DHCPv6 relay or server mode, including IA_NA and prefix delegation
- IPv6 Neighbor Discovery proxy and optional route learning
- Built-in interface hotplug, carrier and IPv4 address monitoring

The daemon is Linux-specific and must run as root because it uses raw packet
sockets and rtnetlink. By default it enables kernel forwarding for each active
address family. In IPv6 relay mode it sets the upstream interface's `accept_ra`
to `2` before enabling global IPv6 forwarding, so upstream Router
Advertisements continue to be accepted.

## Build

Linux only. Requires CMake and a C99 compiler; no extra libraries.

```sh
cmake --preset linux
cmake --build --preset linux-release
sudo cmake --install cmake-build-linux --prefix /usr
```

## Release packages

Download the archive for your architecture, verify it, extract it, and run the
included installer. Official release binaries are fully static musl builds, so
they do not require a particular glibc version and run on supported Linux
systems such as Debian 12 and Debian 13:

```sh
sha256sum -c relaydx-0.1.0-linux-amd64.tar.gz.sha256
tar -xzf relaydx-0.1.0-linux-amd64.tar.gz
cd relaydx-0.1.0-linux-amd64
sudo ./install.sh
sudo editor /etc/default/relaydx
sudo systemctl enable --now relaydx
```

Each archive has one top-level directory with a flat, readable layout:

```text
relaydx-0.1.0-linux-amd64/
├── relaydx
├── relaydx.service
├── relaydx.default
├── install.sh
├── uninstall.sh
├── version.txt
├── README.md
├── README.zh.md
├── LICENSE
└── THIRD_PARTY_NOTICES.md
```

`install.sh` installs the binary to `/usr/sbin/relaydx`, the unit to
`/etc/systemd/system/relaydx.service`, and the initial configuration to
`/etc/default/relaydx`. An existing configuration is preserved. Remove the
program with `sudo ./uninstall.sh`; add `--purge` to remove the configuration.

Maintainers can build the package using the version in `version.txt`. Packaging
requires `musl-gcc` (`musl-tools`) and `readelf` (`binutils`) because the script
rejects dynamically linked release binaries:

```sh
sudo apt-get install musl-tools binutils
./scripts/release.sh
```

Pushing the matching `v*` tag starts `.github/workflows/release.yml`, which
builds amd64 and arm64 packages, verifies their checksums, and publishes a
GitHub Release.

## Quick start

Relay both IPv4 and IPv6 between an upstream and a downstream interface:

```sh
relaydx -4 -6 -M wan0 -I wan0 -I lan0 --dhcp4
```

IPv4 relay plus IPv6 RA/DHCPv6 server mode:

```sh
relaydx -4 --server6 -M . -I lan0 -i guest0 --dhcp4
```

IPv4-only two-interface relay with local access to the relayed network:

```sh
relaydx -4 -I wlan0 -I br-lan --broadcast4 --dhcp4 --local4 192.168.1.2
```

IPv6 NDP proxy with an external downstream interface:

```sh
relaydx -6 -M wan0 -i wan0 -i '~lan0'
```

IPv6 server with DNS rewrite, a static IA_NA, and a lease hook:

```sh
relaydx --server6 -M . -i lan0 \
    --rewrite-dns6=fd00:53::1 \
    --lease6 000100012b3c4d5e001122334455:00000001 \
    --state6 /var/lib/relaydx/leases,/usr/local/sbin/relaydx-lease-hook
```

Foreground debug run:

```sh
relaydx -vv -4 -6 -M wan0 -I wan0 -I lan0 --dhcp4
```

## Command line

```text
relaydx [options] {-i|-I} IFACE [{-i|-I} IFACE ...]
```

Must run as root. Enable at least one family (`-4`, `-6`, `--server6`, or an
individual IPv6 feature). At least one interface is required. IPv4 relay needs
two or more interfaces.

```sh
relaydx --help
```

### Invocation rules

- Unknown options, extra non-option arguments, or missing families/interfaces
  print the help text and exit `1`.
- Interface names are Linux device names. Maximum length is 15 characters
  (`IFNAMSIZ - 1`).
- The same interface may be passed more than once; flags are merged.
- GNU getopt permutes options, so flags and interfaces may be mixed.
- Optional arguments must use the `=` form (`--rewrite-dns6=ADDR`). A space
  before the value is treated as a separate argument and is rejected.
- Flags are applied in command-line order. If both `-6` and `--server6` are
  given, the later one wins for overlapping settings.

```sh
# NDP + RA relay, but no DHCPv6
relaydx -6 --dhcp6 off -M wan0 -i wan0 -i lan0

# IPv6 server, then also turn on NDP against a real master
relaydx --server6 --ndp6 -M wan0 -I wan0 -I lan0
```

### Exit status

| Status | Meaning |
|--------|---------|
| `0` | Clean shutdown (`SIGINT` / `SIGTERM`), or `--help` |
| `1` | Invalid options, or the event loop stopped with `--no-interface-watch` |
| `2` | Not root, or uloop / core / link-monitor initialization failed |
| `4` | Interfaces looked ready but the relay modules failed to start |

### Families and interfaces

#### `-4`, `--ipv4`

Enable IPv4 ARP proxying and host tracking. Implied by `--broadcast4`,
`--dhcp4`, `--gateway4`, `--route4`, and `--local4`.

IPv4 treats every listed interface as a peer. `-M` does not change IPv4
topology.

#### `-6`, `--ipv6`

Enable the IPv6 **relay** preset:

| Feature | Setting |
|---------|---------|
| Router Advertisement | relay |
| DHCPv6 | relay |
| NDP proxy | on |
| Initial Router Solicitation | on |
| NDP route learning | on |

Later flags can override individual pieces.

#### `--server6`

Enable the IPv6 **server** preset:

| Feature | Setting |
|---------|---------|
| Router Advertisement | server |
| DHCPv6 | server (IA_NA + prefix delegation) |
| NDP proxy | off |
| Initial Router Solicitation | off |
| NDP route learning | off |

If `-M` is omitted, the IPv6 master defaults to `.` (no real upstream).

Server mode is a small RA/DHCPv6 server. It announces prefixes that are
already assigned on the downstream interfaces. Address assignment on those
interfaces remains the job of NetworkManager, systemd-networkd, or static
configuration.

#### `-i`, `--interface IFACE`

Add a relay interface **without** IPv4 host-route management.

The interface still participates in ARP proxying, DHCP/broadcast forwarding,
and IPv6 services. Learned IPv4 hosts on it expire after `--timeout4` without
ARP probes, and no per-host connected route is installed for them.

Prefix the name with `~` to mark it external for NDP.

#### `-I`, `--managed-interface IFACE`

Add a relay interface **with** IPv4 ARP cache and host-route management.

For each IPv4 host learned on a managed interface, `relaydx` installs a host
route in the other interfaces' policy tables and ARP-pings the host before
expiring it. This is the usual choice for both WAN and LAN in a two-interface
relay. `~` is accepted here as well.

#### `-M`, `--master6 IFACE`

Set the IPv6 upstream (master) interface.

| Value | Meaning |
|-------|---------|
| a real interface | Upstream for RA/DHCPv6 relay and NDP. Added automatically if it was not already listed. |
| `.` | No real master. Required for IPv6 server-only operation. |

Default when `-M` is omitted:

- IPv6 server mode (`--server6`, `--ra6 server`, or `--dhcp6 server`): `.`
- otherwise: the first interface given with `-i` / `-I`

The master is excluded from the IPv6 downstream (slave) list. IPv6 needs at
least one downstream interface. NDP proxy requires a **real** master;
`-M . --ndp6` is rejected at start.

#### External NDP interfaces

Prefix any `-i` / `-I` name with `~` to mark it external for NDP:

```sh
relaydx -6 -M wan0 -i wan0 -i '~lan0'
```

On an external interface, `relaydx` does **not** proxy Neighbor Solicitations
for other hosts. It only answers Duplicate Address Detection and traffic
destined to the router itself. Additional firewall rules are recommended.

The `~` is stripped before the kernel interface name is used.

#### Constraints

- IPv4 (`-4` or any IPv4 option): at least two interfaces.
- IPv6: at least one downstream interface after the master is taken out.
- NDP (`-6`, `--ndp6`, `--learn-routes6`, `--static-ndp6`): master must not be `.`.
- Dummy master `.` is skipped when waiting for carrier.

### IPv4 options

Several of these options imply `-4`.

#### `--broadcast4`

Forward IPv4 broadcasts between the listed interfaces. Implies `-4`.

Without this flag, only ARP (and optionally DHCP) is relayed.

#### `--dhcp4`

Forward DHCPv4 (UDP port 67) between the listed interfaces. Implies `-4`.

Relayed DHCP replies have the broadcast flag set so clients accept them
without a unicast L2 path through the relay.

#### `--no-dhcp4-parse`

Do not learn routes from DHCPv4 options.

By default, DHCP replies are inspected for option 3 (router). If the router
address is the DHCP server host itself, a default route via that host is
added; otherwise a pending default route is queued until that gateway is
seen. Option 121 (classless static routes) is recognized but currently not
installed.

#### `--gateway4 IP`

Queue a client default route `0.0.0.0/0` via `IP`. Implies `-4`. Repeatable.

The route is installed once the gateway host is learned through ARP/DHCP.

#### `--route4 GW:NET/MASK`

Queue a static IPv4 route. Implies `-4`. Repeatable.

```text
--route4 192.0.2.1:10.0.0.0/8
```

`GW` and `NET` are dotted-quad addresses. `MASK` is an integer prefix length
`0..32`. The route is installed once `GW` is learned as a host.

#### `--timeout4 SECONDS`

IPv4 host-entry expiry timeout in seconds. Default: `30`. Minimum: `1`.

On **unmanaged** (`-i`) interfaces the host is dropped when the timer fires.
On **managed** (`-I`) interfaces `relaydx` first sends ARP probes.

#### `--arp-tries4 COUNT`

ARP probes sent from every relay interface before a managed host is dropped.
Default: `5`. Minimum: `1`. One probe per second.

#### `--table4 NUMBER`

First IPv4 policy-routing table number. Default: `16800`. Minimum: `1`.

Each IPv4 interface gets a consecutive table and an `ip rule` that matches
ingress on that interface. Host routes for a learned host are installed in
every **other** interface's table, so traffic that enters from one side is
routed out toward that host.

If `--local4` is set, that local table consumes the first number and
interfaces start at `NUMBER + 1`.

#### `--local4 IP`

Enable access from the relay host itself into the relayed IPv4 networks,
using `IP` as the source address. Implies `-4`.

`relaydx` answers ARP for `IP` on the relay interfaces, allocates one extra
policy table, and installs host routes there with `prefsrc = IP`. `IP` should
be an address you dedicate to this purpose; it does not have to be configured
on an interface.

### IPv6 options

Presets (`-6`, `--server6`) can be refined or replaced by these flags.
`--ra6` and `--dhcp6` accept `relay`, `server`, or `off`.

#### `--ra6 relay|server|off`

Router Advertisement mode.

- `relay` — proxy RS/RA between master and downstream.
- `server` — generate RAs on downstream interfaces from their assigned
  prefixes, MTU, and default-route presence.
- `off` — disable RA.

Server RAs include Prefix Information for each suitable `/64` (or shorter)
address on the downstream interface. Preferred/valid lifetimes are capped at
3600 s / 7200 s. A default-router lifetime is announced only when a default
route exists **and** a non-ULA prefix is present, unless
`--always-default6` is set.

When DHCPv6 server mode is also on, the RA Other-Config flag is set.

#### `--dhcp6 relay|server|off`

DHCPv6 mode.

- `relay` — DHCPv6 relay between downstream clients and the master.
- `server` — mini-server: stateless info, stateful IA_NA, and IA_PD.
- `off` — disable DHCPv6.

In server mode, prefixes already on the downstream interface are offered as
IA_NA. Prefixes longer than `/64` contribute delegated space: all but the
first `/64` can be offered via IA_PD to downstream routers.

#### `--ndp6`

Enable Neighbor Discovery proxy between master and downstream. Implied by
`-6`, `--learn-routes6`, and `--static-ndp6`.

Requires a real IPv6 master.

#### `--rs6`

Send an initial Router Solicitation on the master so an RA is obtained
quickly. Also enables RA relay. Implied by `-6`.

#### `--learn-routes6`

Learn IPv6 routes to neighbors from NDP and insert them into the local
routing table. Also enables NDP proxy. Implied by `-6`.

#### `--rewrite-dns6[=ADDR]`

Always rewrite DNS servers announced in RA (RDNSS) and DHCPv6.

```sh
--rewrite-dns6                 # use a local address of the outgoing interface
--rewrite-dns6=2001:db8::53    # use this address (note the '=')
```

Use the no-argument form together with a local DNS proxy. Authenticated
DHCPv6 replies are not rewritten.

#### `--always-default6`

Announce a default route in server RAs even when only ULA prefixes
(`fc00::/7`) are present on the interface.

Without this flag, a present default route is suppressed if there is no
public prefix, and a warning is logged.

#### `--deprecate-ula6`

When a public prefix is present, advertise ULA prefixes with preferred
lifetime 0 in RAs, and skip ULA in DHCPv6 IA_NA.

#### `--ra-managed6 0|1|2`

SLAAC / Managed-Config mode for server RAs. Default: `0`.

| Value | SLAAC (A flag) | Managed-Config (M flag) |
|-------|----------------|-------------------------|
| `0` | yes | no |
| `1` | yes | yes |
| `2` | no | yes |

#### `--ra-not-onlink6`

Clear the on-link (L) flag on Prefix Information options. Hosts then do not
treat the prefix as on-link and send off-link traffic to the router.

#### `--ra-preference6 LEVEL`

Default-router and route-info preference: `low`, `medium` (default), or
`high`.

#### `--state6 FILE[,CMD]`

DHCPv6 server: lease state file and optional update callback.

```sh
--state6 /var/lib/relaydx/leases
--state6 /var/lib/relaydx/leases,/usr/local/sbin/relaydx-lease-hook
```

Do not put spaces around the comma. `CMD` is executed with `execv`, so it
must be an absolute path; it receives no arguments.

The file contains:

- hosts-style lines `IPv6<TAB>hostname` for IA_NA assignments that have a
  hostname
- comment lines `# iface DUID iaid hostname lifetime assigned length addrs...`

#### `--lease6 DUID:VALUE`

Static DHCPv6 IA_NA assignment. Repeatable.

- `DUID` is the client DUID as an even-length hex string (at most 260 hex
  digits, i.e. 130 bytes).
- `VALUE` is a 32-bit hex host identifier. It becomes the last 32 bits of the
  assigned address (`prefix + VALUE`).

```sh
--lease6 000100012b3c4d5e001122334455:00000001
```

#### `--static-ndp6 PREFIX/LEN:IFACE`

Static NDP prefix-to-interface binding. Repeatable. Enables NDP proxy.

```sh
--static-ndp6 2001:db8:1::/64:lan0
```

Neighbor Solicitations for addresses in that prefix are treated as living on
`IFACE` without waiting to learn a neighbor.

### Process options

#### `-v`, `--verbose`

Increase logging verbosity. Repeatable (`-v`, `-vv`, …).

| Count | syslog mask | `DPRINTF` |
|-------|-------------|-----------|
| none | up to `LOG_WARNING` | off |
| `-v` | up to `LOG_INFO` | level 1 |
| `-vv` and more | all levels | matching debug level |

Logs go to syslog identity `relaydx`, facility `LOG_DAEMON`. In the
foreground, messages are also printed to stderr (`LOG_PERROR`).

#### `-d`, `--daemon`

Daemonize with `daemon(0, 0)` and write the PID file. Do **not** use this
with the shipped systemd unit (`Type=notify`).

#### `-p`, `--pidfile FILE`

PID file path. Default: `/var/run/relaydx.pid`. Only written when `-d` is
used; unlinked on exit.

#### `--no-interface-watch`

One-shot mode. Do not wait for interfaces to appear or come up, and do not
reload on link or IPv4-address changes. If the event loop stops for any
reason other than `SIGINT`/`SIGTERM`, the process exits `1`.

#### `--no-forwarding-setup`

Do not touch kernel forwarding sysctls. Use this when another component owns
`ip_forward` / `forwarding`.

Default sysctl changes, applied only for enabled families:

| Condition | Sysctl | Value |
|-----------|--------|-------|
| IPv4 enabled | `/proc/sys/net/ipv4/ip_forward` | `1` |
| IPv6 relay (RA relay, DHCPv6 relay, or NDP) and a real master | `/proc/sys/net/ipv6/conf/<master>/accept_ra` | `2` |
| any IPv6 feature | `/proc/sys/net/ipv6/conf/all/forwarding` | `1` |

`accept_ra=2` is set **before** global IPv6 forwarding so the upstream
interface still accepts Router Advertisements.

Interface addresses, including static IPv6 addresses, are never configured by
`relaydx`.

#### `-h`, `--help`

Print help to stdout and exit `0`. Invalid usage prints the same text to
stderr and exits `1`.

## Interface monitoring

Interface monitoring is enabled by default. `relaydx` can start before the
configured interfaces exist or have carrier. It waits until every real
interface is both administratively up and running (`IFF_UP | IFF_RUNNING`),
then starts the relay modules. If a watched interface goes down or
disappears, the daemon removes its learned routes and relay state and waits.
It initializes the modules again when all interfaces recover. IPv4 address
changes (`RTM_NEWADDR` / `RTM_DELADDR`) also trigger an in-process reload.

Use `--no-interface-watch` for one-shot behavior and
`--no-forwarding-setup` if another component owns the IPv4/IPv6 forwarding
sysctls. Interface addresses, including static IPv6 addresses, remain the
responsibility of NetworkManager, systemd-networkd, or the distribution
network configuration.

| Signal | Action |
|--------|--------|
| `SIGINT`, `SIGTERM` | Stop the event loop and exit `0` |
| `SIGHUP` | Drop relay state and reload as if interfaces changed |
| `SIGCHLD` | Reap children (DHCPv6 lease-hook) |

## systemd

The release archive includes `install.sh`, `relaydx.service`, and
`relaydx.default`. Install it as described under **Release packages**, then edit
`RELAYDX_OPTIONS` in `/etc/default/relaydx` before starting the service.

When installing from a source checkout instead, build and install the binary,
then install the two integration files:

```sh
sudo cmake --install cmake-build-linux --prefix /usr
sudo install -m 0644 contrib/relaydx.service /etc/systemd/system/relaydx.service
sudo install -m 0644 contrib/relaydx.default /etc/default/relaydx
sudo systemctl daemon-reload
sudo editor /etc/default/relaydx
sudo systemctl enable --now relaydx
sudo systemctl status relaydx
```

Do not add `-d`; the unit is `Type=notify` and supervises the foreground
process. No `BindsTo=`, interface polling `ExecStartPre`, NetworkManager
dispatcher, or `post-up` restart hook is needed. The service uses the systemd
watchdog and `Restart=on-failure` only for actual process failures.

## License

GPL-2.0. See `LICENSE`.

## References

- `relayd`: <https://github.com/openwrt/relayd>
- `6relayd`: <https://github.com/sbyx/6relayd>
- `libubox`: <https://github.com/openwrt/libubox>
