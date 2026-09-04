/*
 * relaydx - combined IPv4 and IPv6 relay daemon
 *
 * Copyright (C) 2010-2013 relayd and 6relayd contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2.
 */
#ifndef RELAYDX_H
#define RELAYDX_H

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netpacket/packet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <syslog.h>

#include <linux/rtnetlink.h>

#include "libubox/list.h"
#include "libubox/uloop.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define RELAYD_BUFFER_SIZE 8192
#define RELAYD_MAX_PREFIXES 8

/* RFC 4191 and RFC 6106 Router Advertisement options. */
#define ND_OPT_ROUTE_INFO 24
#define ND_OPT_RECURSIVE_DNS 25
#define ND_OPT_DNS_SEARCH 31

#define _unused __attribute__((unused))
#define _packed __attribute__((packed))
#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define ALL_IPV6_NODES {{{0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}}}
#define ALL_IPV6_ROUTERS {{{0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}}}

#define __uc(c) ((unsigned char *)(c))
#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_BUF(c) __uc(c)[0], __uc(c)[1], __uc(c)[2], __uc(c)[3], __uc(c)[4], __uc(c)[5]
#define IP_FMT "%d.%d.%d.%d"
#define IP_BUF(c) __uc(c)[0], __uc(c)[1], __uc(c)[2], __uc(c)[3]

#define DUMMY_IP ((uint8_t *)"\x01\x01\x01\x01")
#define DHCP_FLAG_BROADCAST (1U << 15)

struct relayd_interface;

struct relayd_event {
	int socket;
	void (*handle_event)(struct relayd_event *event);
	void (*handle_dgram)(void *addr, void *data, size_t len,
			struct relayd_interface *iface);
	struct uloop_fd uloop;
};

struct relayd_ipaddr {
	struct in6_addr addr;
	uint8_t prefix;
	uint32_t preferred;
	uint32_t valid;
};

struct relayd_interface {
	struct list_head list;
	char ifname[IFNAMSIZ];
	int ifindex;
	uint8_t mac[ETH_ALEN];

	/* IPv4 state */
	struct uloop_fd fd;
	struct uloop_fd bcast_fd;
	struct sockaddr_ll sll;
	struct sockaddr_ll bcast_sll;
	struct list_head hosts;
	uint8_t src_ip[4];
	bool managed;
	bool ipv4_routes_added;
	int rt_table;

	/* IPv6 state */
	bool external;
	struct relayd_event timer_rs;
	struct list_head pd_assignments;
	struct relayd_ipaddr pd_addr[RELAYD_MAX_PREFIXES];
	size_t pd_addr_len;
	bool pd_reconf;
};

struct relayd_host {
	struct list_head list;
	struct list_head routes;
	struct relayd_interface *rif;
	uint8_t lladdr[ETH_ALEN];
	uint8_t ipaddr[4];
	struct uloop_timeout timeout;
	int cleanup_pending;
};

struct relayd_route {
	struct list_head list;
	uint8_t dest[4];
	uint8_t mask;
};

struct arp_packet {
	struct ether_header eth;
	struct ether_arp arp;
} __packed;

#define RELAYD_MANAGED_MFLAG 1
#define RELAYD_MANAGED_NO_AFLAG 2

struct relayd_config {
	bool enable_ipv4;
	bool forward_bcast4;
	bool forward_dhcp4;
	bool parse_dhcp4;
	int host_timeout4;
	int host_ping_tries4;
	int route_table4;
	uint8_t local_addr4[4];
	bool local_addr4_valid;

	bool enable_router_discovery_relay;
	bool enable_router_discovery_server;
	bool enable_dhcpv6_relay;
	bool enable_dhcpv6_server;
	bool enable_ndp_relay;
	bool enable_route_learning;
	bool send_router_solicitation;
	bool always_rewrite_dns;
	bool always_announce_default_router;
	bool deprecate_ula_if_public_avail;
	bool ra_not_onlink;
	int ra_managed_mode;
	int ra_preference;

	struct in6_addr dnsaddr;
	struct relayd_interface master;
	struct relayd_interface *slaves;
	size_t slavecount;

	char *dhcpv6_cb;
	char *dhcpv6_statefile;
	char **dhcpv6_lease;
	size_t dhcpv6_lease_len;
	char **static_ndp;
	size_t static_ndp_len;
};

extern struct list_head interfaces;
extern int debug;
#define DPRINTF(level, ...) \
	do { if (debug >= (level)) fprintf(stderr, __VA_ARGS__); } while (0)

extern int route_table;
extern uint8_t local_addr[4];
extern int local_route_table;

int relayd_core_init(void);
void relayd_core_done(void);
int relayd_open_interface(struct relayd_interface *iface, const char *ifname,
		bool managed, bool external);
void relayd_close_interface(struct relayd_interface *iface);
int relayd_register_event(struct relayd_event *event);
void relayd_unregister_event(struct relayd_event *event);
int relayd_open_rtnl_socket(void);
ssize_t relayd_forward_packet(int socket, struct sockaddr_in6 *dest,
		struct iovec *iov, size_t iov_len,
		const struct relayd_interface *iface);
ssize_t relayd_get_interface_addresses(int ifindex,
		struct relayd_ipaddr *addrs, size_t cnt);
struct relayd_interface *relayd_get_interface_by_name(const char *name);
struct relayd_interface *relayd_get_interface_by_index(int ifindex);
int relayd_get_interface_mtu(const char *ifname);
int relayd_get_interface_mac(const char *ifname, uint8_t mac[6]);
void relayd_urandom(void *data, size_t len);
ssize_t relayd_dns_encode_name(const char *name, uint8_t *output,
		size_t output_size);
ssize_t relayd_dns_encode_search(uint8_t *output, size_t output_size);
ssize_t relayd_dns_decode_name(const uint8_t *message, const uint8_t *end,
		const uint8_t *encoded, char *output, size_t output_size);

typedef void (*relayd_link_event_cb)(bool available, bool address_change,
		void *context);
int relayd_link_watch_init(const char *const *ifnames, size_t count,
		relayd_link_event_cb callback, void *context);
void relayd_link_watch_done(void);
bool relayd_interfaces_ready(const char *const *ifnames, size_t count,
		char *unavailable, size_t unavailable_size);

void relayd_setup_route(const struct in6_addr *addr, int prefixlen,
		const struct relayd_interface *iface, const struct in6_addr *gw,
		bool add);

int relayd_ipv4_init(const struct relayd_config *relayd_config);
void relayd_ipv4_done(void);
void rtnl_route_set(struct relayd_host *host, struct relayd_route *route,
		bool add);
void relayd_add_interface_routes(struct relayd_interface *rif);
void relayd_del_interface_routes(struct relayd_interface *rif);
int relayd_rtnl_init(void);
void relayd_rtnl_done(void);
struct relayd_host *relayd_refresh_host(struct relayd_interface *rif,
		const uint8_t *lladdr, const uint8_t *ipaddr);
void relayd_add_host_route(struct relayd_host *host, const uint8_t *ipaddr,
		uint8_t mask);
void relayd_add_pending_route(const uint8_t *gateway, const uint8_t *dest,
		uint8_t mask, int timeout);
void relayd_forward_bcast_packet(struct relayd_interface *from_rif,
		void *packet, int len);
bool relayd_handle_dhcp_packet(struct relayd_interface *rif, void *data,
		int len, bool forward, bool parse);

static inline void relayd_add_route(struct relayd_host *host,
		struct relayd_route *route)
{
	rtnl_route_set(host, route, true);
}

static inline void relayd_del_route(struct relayd_host *host,
		struct relayd_route *route)
{
	rtnl_route_set(host, route, false);
}

int init_router_discovery_relay(const struct relayd_config *relayd_config);
int init_dhcpv6_relay(const struct relayd_config *relayd_config);
int init_ndp_proxy(const struct relayd_config *relayd_config);
void deinit_router_discovery_relay(void);
void deinit_dhcpv6_relay(void);
void deinit_ndp_proxy(void);

#endif
