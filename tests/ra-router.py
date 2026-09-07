#!/usr/bin/env python3
"""Send a minimal IPv6 Router Advertisement for namespace integration tests."""
import socket
import struct
import sys
import time

if len(sys.argv) != 4:
    raise SystemExit(f"usage: {sys.argv[0]} IFACE SOURCE PREFIX")

iface, source, prefix = sys.argv[1:]
ifindex = socket.if_nametoindex(iface)
ra = struct.pack("!BBHBBHII", 134, 0, 0, 64, 0, 1800, 0, 0)
ra += struct.pack(
    "!BBBBIII16s",
    3,
    4,
    64,
    0xC0,
    7200,
    3600,
    0,
    socket.inet_pton(socket.AF_INET6, prefix),
)
sock = socket.socket(socket.AF_INET6, socket.SOCK_RAW, socket.IPPROTO_ICMPV6)
sock.setsockopt(socket.IPPROTO_RAW, socket.IPV6_CHECKSUM, 2)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 255)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, iface.encode() + b"\0")
sock.bind((source, 0, 0, ifindex))
for _ in range(3):
    sock.sendto(ra, ("ff02::1", 0, 0, ifindex))
    time.sleep(0.2)
