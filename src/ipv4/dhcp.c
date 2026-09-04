/*
 *   Copyright (C) 2010 Felix Fietkau <nbd@openwrt.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License v2 as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "relaydx.h"

struct ip_packet {
	struct ether_header eth;
	struct iphdr iph;
} __packed;


enum {
	DHCP_OPTION_ROUTER = 0x03,
	DHCP_OPTION_ROUTES = 0x79,
	DHCP_OPTION_END	= 0xff,
};

struct dhcp_option {
	uint8_t code;
	uint8_t len;
	uint8_t data[];
};

struct dhcp_header {
	uint8_t op, htype, hlen, hops;
	uint32_t xit;
	uint16_t secs, flags;
	struct in_addr ciaddr, yiaddr, siaddr, giaddr;
	unsigned char chaddr[16];
	unsigned char sname[64];
	unsigned char file[128];
	uint32_t cookie;
	uint8_t option_data[];
} __packed;

static uint16_t
chksum(uint16_t sum, const uint8_t *data, uint16_t len)
{
	const uint8_t *last;
	uint16_t t;

	last = data + len - 1;

	while(data < last) {
		t = (data[0] << 8) + data[1];
		sum += t;
		if(sum < t)
			sum++;
		data += 2;
	}

	if(data == last) {
		t = (data[0] << 8) + 0;
		sum += t;
		if(sum < t)
			sum++;
	}

	return sum;
}

static void
parse_dhcp_options(struct relayd_host *host, struct dhcp_header *dhcp, int len)
{
	uint8_t *cursor = dhcp->option_data;
	uint8_t *end = (uint8_t *)dhcp + len;
	static const uint8_t dest[4] = {0, 0, 0, 0};

	while (cursor < end) {
		uint8_t code = *cursor++;
		uint8_t option_len;
		uint8_t *option_data;

		if (code == 0)
			continue;
		if (code == DHCP_OPTION_END)
			break;
		if (cursor >= end)
			break;
		option_len = *cursor++;
		if ((size_t)(end - cursor) < option_len)
			break;
		option_data = cursor;
		cursor += option_len;

		switch (code) {
		case DHCP_OPTION_ROUTER:
			DPRINTF(2, "Found a DHCP router option, len=%u\n",
					option_len);
			if (option_len < 4)
				break;
			if (!memcmp(option_data, host->ipaddr, 4))
				relayd_add_host_route(host, dest, 0);
			else
				relayd_add_pending_route(option_data, dest, 0, 10000);
			break;
		case DHCP_OPTION_ROUTES:
			DPRINTF(2, "Found a DHCP static routes option, len=%u\n",
					option_len);
			break;
		default:
			DPRINTF(3, "Skipping unknown DHCP option %02x\n", code);
			break;
		}
	}
}

bool relayd_handle_dhcp_packet(struct relayd_interface *rif, void *data, int len, bool forward, bool parse)
{
	struct ip_packet *pkt = data;
	struct udphdr *udp;
	struct dhcp_header *dhcp;
	struct relayd_host *host;
	int udplen;
	int ip_header_len;
	int frame_payload_len;
	uint16_t sum;

	if (len < (int)(sizeof(pkt->eth) + sizeof(pkt->iph)))
		return false;
	if (pkt->eth.ether_type != htons(ETH_P_IP))
		return false;

	if (pkt->iph.version != 4 || pkt->iph.ihl < 5)
		return false;

	ip_header_len = pkt->iph.ihl << 2;
	frame_payload_len = len - (int)sizeof(pkt->eth);
	if (ip_header_len > frame_payload_len ||
			ntohs(pkt->iph.tot_len) < ip_header_len)
		return false;

	if (pkt->iph.protocol != IPPROTO_UDP)
		return false;

	udp = (void *) ((char *) &pkt->iph + (pkt->iph.ihl << 2));
	dhcp = (void *) (udp + 1);

	if ((uint8_t *)udp  + sizeof(*udp)  > (uint8_t *)data + len ||
	    (uint8_t *)dhcp + sizeof(*dhcp) > (uint8_t *)data + len)
		return false;

	udplen = ntohs(udp->len);
	if (udplen < (int)(sizeof(*udp) + sizeof(*dhcp)) ||
			udplen > len - ((char *)udp - (char *)data) ||
			ip_header_len + udplen > ntohs(pkt->iph.tot_len))
		return false;

	if (udp->dest != htons(67) && udp->source != htons(67))
		return false;

	if ((dhcp->op != 1 && dhcp->op != 2) ||
			dhcp->cookie != htonl(0x63825363))
		return false;

	if (!forward)
		return true;

	if (dhcp->op == 2) {
		host = relayd_refresh_host(rif, pkt->eth.ether_shost, (void *) &pkt->iph.saddr);
		if (host && parse)
			parse_dhcp_options(host, dhcp, udplen - sizeof(struct udphdr));
	}

	DPRINTF(2, "%s: handling DHCP %s\n", rif->ifname, (dhcp->op == 1 ? "request" : "response"));

	dhcp->flags |= htons(DHCP_FLAG_BROADCAST);

	udp->check = 0;
	sum = udplen + IPPROTO_UDP;
	sum = chksum(sum, (void *) &pkt->iph.saddr, 8);
	sum = chksum(sum, (void *) udp, udplen);
	if (sum == 0)
		sum = 0xffff;

	udp->check = htons(~sum);

	relayd_forward_bcast_packet(rif, data, len);

	return true;
}


