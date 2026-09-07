# Network Tests

These tests require Linux root privileges, `ip`, `python3`, and `ping -6`.
They create temporary network namespaces and veth pairs, so they are disabled by
default.

Enable them with:

```sh
cmake -S . -B build -DRELAYDX_NETWORK_TESTING=ON
sudo ctest --test-dir build --output-on-failure
```

The scripts cover RA relay, DHCPv6 IA_NA, DHCPv6 IA_PD, and addressless
IPv4/IPv6 relay end-to-end paths. The addressless test verifies that relay
interfaces have no IPv4 or global IPv6 addresses, retain IPv6 link-local
addresses, and still forward traffic in both directions.
They use `RELAYDX_BIN` when set; otherwise they default to the Debian VM path
used by the project integration environment. The DHCPv6 helper programs used
by the VM suite are kept in the integration harness because they are test
clients rather than daemon sources.
