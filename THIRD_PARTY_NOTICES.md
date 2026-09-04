# Third-party notices

## libubox subset

`src/libubox/list.h`, `src/libubox/uloop.h`, `src/libubox/uloop.c`,
`src/libubox/uloop-epoll.c`, and `src/libubox/utils.h` are derived from
OpenWrt libubox:

- Source: https://github.com/openwrt/libubox
- Copyright: Felix Fietkau and the libubox contributors
- License: ISC for libubox files; `list.h` also contains the BSD-licensed
  list implementation notices preserved in the source file.

The embedded files retain their original copyright and license notices. The
ISC/BSD terms are compatible with and do not replace the GPL-2.0 terms of the
original relaydx code.

## Other imported code

The IPv4 and IPv6 relay modules retain the copyright and license notices from
their respective source files. See `LICENSE` and the file headers for details.
