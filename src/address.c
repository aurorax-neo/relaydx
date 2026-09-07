/*
 * Local address suppression for relay interfaces.
 *
 * Copyright (C) 2026 relaydx contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>

#include <linux/if_addr.h>

#include "relaydx.h"

static unsigned int address_seq;

static int write_sysctl(const char *ifname, const char *setting, char value)
{
	char path[160];
	char data = value;
	int fd;
	ssize_t written;

	if (snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/%s/%s",
			ifname, setting) >= (int)sizeof(path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	written = write(fd, &data, sizeof(data));
	close(fd);
	if (written == (ssize_t)sizeof(data))
		return 0;
	if (written >= 0)
		errno = EIO;
	return -1;
}

static bool watched_index(unsigned int ifindex, const char *const *ifnames,
		size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		if (!strcmp(ifnames[i], "."))
			continue;
		if (if_nametoindex(ifnames[i]) == ifindex)
			return true;
	}
	return false;
}

static int add_attribute(struct nlmsghdr *header, size_t capacity, int type,
		const void *data, size_t length)
{
	size_t offset = NLMSG_ALIGN(header->nlmsg_len);
	size_t total = RTA_LENGTH(length);
	struct rtattr *attribute;

	if (offset + RTA_ALIGN(total) > capacity) {
		errno = EMSGSIZE;
		return -1;
	}
	attribute = (struct rtattr *)((uint8_t *)header + offset);
	attribute->rta_type = type;
	attribute->rta_len = total;
	memcpy(RTA_DATA(attribute), data, length);
	header->nlmsg_len = offset + RTA_ALIGN(total);
	return 0;
}

static int delete_address(int socket_fd, const struct ifaddrmsg *source,
		const void *local, const void *address, size_t address_length)
{
	uint8_t request_buffer[128] = {0};
	struct nlmsghdr *header = (struct nlmsghdr *)request_buffer;
	struct ifaddrmsg *message = NLMSG_DATA(header);
	uint8_t response[512];
	unsigned int sequence = ++address_seq;

	header->nlmsg_len = NLMSG_LENGTH(sizeof(*message));
	header->nlmsg_type = RTM_DELADDR;
	header->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	header->nlmsg_seq = sequence;
	*message = *source;
	message->ifa_flags = 0;

	if (local && add_attribute(header, sizeof(request_buffer), IFA_LOCAL,
			local, address_length) < 0)
		return -1;
	if (address && add_attribute(header, sizeof(request_buffer), IFA_ADDRESS,
			address, address_length) < 0)
		return -1;
	if (send(socket_fd, header, header->nlmsg_len, 0) !=
			(ssize_t)header->nlmsg_len)
		return -1;

	for (;;) {
		ssize_t length = recv(socket_fd, response, sizeof(response), 0);
		struct nlmsghdr *reply;

		if (length < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		for (reply = (struct nlmsghdr *)response; NLMSG_OK(reply, length);
				reply = NLMSG_NEXT(reply, length)) {
			struct nlmsgerr *error;

			if (reply->nlmsg_seq != sequence || reply->nlmsg_type != NLMSG_ERROR)
				continue;
			if (NLMSG_PAYLOAD(reply, 0) < sizeof(*error)) {
				errno = EPROTO;
				return -1;
			}
			error = NLMSG_DATA(reply);
			if (!error->error || error->error == -EADDRNOTAVAIL)
				return 0;
			errno = -error->error;
			return -1;
		}
	}
}

static int remove_addresses(const char *const *ifnames, size_t count,
		bool suppress4, bool suppress6)
{
	struct sockaddr_nl address = {.nl_family = AF_NETLINK};
	struct {
		struct nlmsghdr header;
		struct ifaddrmsg message;
	} request = {
		.header = {
			.nlmsg_len = sizeof(request),
			.nlmsg_type = RTM_GETADDR,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
			.nlmsg_seq = ++address_seq,
		},
		.message = {.ifa_family = AF_UNSPEC},
	};
	uint8_t buffer[8192];
	int dump_socket = -1;
	int delete_socket = -1;
	int removed = 0;
	int result = -1;

	dump_socket = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	delete_socket = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (dump_socket < 0 || delete_socket < 0)
		goto out;
	if (connect(dump_socket, (struct sockaddr *)&address, sizeof(address)) < 0 ||
			connect(delete_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
		goto out;
	if (send(dump_socket, &request, sizeof(request), 0) != sizeof(request))
		goto out;

	for (;;) {
		ssize_t length = recv(dump_socket, buffer, sizeof(buffer), 0);
		struct nlmsghdr *header;

		if (length < 0) {
			if (errno == EINTR)
				continue;
			goto out;
		}
		for (header = (struct nlmsghdr *)buffer; NLMSG_OK(header, length);
				header = NLMSG_NEXT(header, length)) {
			struct ifaddrmsg *message;
			struct rtattr *attribute;
			const void *local = NULL;
			const void *assigned = NULL;
			size_t address_length;
			int attributes_length;
			char printable[INET6_ADDRSTRLEN];
			char interface_name[IFNAMSIZ];

			if (header->nlmsg_seq != request.header.nlmsg_seq)
				continue;
			if (header->nlmsg_type == NLMSG_DONE) {
				result = removed;
				goto out;
			}
			if (header->nlmsg_type == NLMSG_ERROR)
				goto out;
			if (header->nlmsg_type != RTM_NEWADDR ||
					NLMSG_PAYLOAD(header, 0) < sizeof(*message))
				continue;
			message = NLMSG_DATA(header);
			if (!watched_index(message->ifa_index, ifnames, count))
				continue;
			if (message->ifa_family == AF_INET) {
				if (!suppress4)
					continue;
				address_length = sizeof(struct in_addr);
			} else if (message->ifa_family == AF_INET6) {
				if (!suppress6)
					continue;
				address_length = sizeof(struct in6_addr);
			} else {
				continue;
			}

			attributes_length = IFA_PAYLOAD(header);
			for (attribute = IFA_RTA(message); RTA_OK(attribute, attributes_length);
					attribute = RTA_NEXT(attribute, attributes_length)) {
				if (RTA_PAYLOAD(attribute) < address_length)
					continue;
				if (attribute->rta_type == IFA_LOCAL)
					local = RTA_DATA(attribute);
				else if (attribute->rta_type == IFA_ADDRESS)
					assigned = RTA_DATA(attribute);
			}
			if (!assigned)
				assigned = local;
			if (!assigned)
				continue;
			if (message->ifa_family == AF_INET6 &&
					IN6_IS_ADDR_LINKLOCAL((const struct in6_addr *)assigned))
				continue;
			if (delete_address(delete_socket, message, local, assigned,
					address_length) < 0)
				goto out;
			if (inet_ntop(message->ifa_family, assigned, printable,
					sizeof(printable))) {
				const char *name = if_indextoname(message->ifa_index,
						interface_name);

				syslog(LOG_NOTICE, "Removed local address %s from %s",
						printable, name ? name : "unknown interface");
			}
			removed++;
		}
	}

out:
	if (dump_socket >= 0)
		close(dump_socket);
	if (delete_socket >= 0)
		close(delete_socket);
	return result;
}

int relaydx_enforce_address_policy(const char *const *ifnames, size_t count,
		bool suppress4, bool suppress6)
{
	if (!suppress4 && !suppress6)
		return 0;
	if (suppress6) {
		for (size_t i = 0; i < count; ++i) {
			if (!strcmp(ifnames[i], "."))
				continue;
			if (write_sysctl(ifnames[i], "autoconf", '0') < 0 ||
					write_sysctl(ifnames[i], "use_tempaddr", '0') < 0)
				return -1;
		}
	}
	return remove_addresses(ifnames, count, suppress4, suppress6) < 0 ? -1 : 0;
}
