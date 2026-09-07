/*
 * Interface readiness and rtnetlink monitoring for relaydx.
 */
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>

#include <linux/if_link.h>

#include "relaydx.h"

static struct uloop_fd watch_fd = {.fd = -1};
static int status_socket = -1;
static const char *const *watched_names;
static size_t watched_count;
static bool monitor_ipv6_addresses;
static relayd_link_event_cb event_callback;
static void *event_context;

static bool interface_is_watched(int ifindex, const char *ifname)
{
	if (ifindex > 0 && relayd_get_interface_by_index(ifindex))
		return true;
	for (size_t i = 0; i < watched_count; ++i) {
		if (!strcmp(watched_names[i], "."))
			continue;
		if (ifname && !strcmp(watched_names[i], ifname))
			return true;
		if (ifindex > 0 && if_nametoindex(watched_names[i]) == (unsigned int)ifindex)
			return true;
	}
	return false;
}

static const char *link_message_name(struct nlmsghdr *header,
		struct ifinfomsg *info)
{
	int length = IFLA_PAYLOAD(header);
	struct rtattr *attribute;

	for (attribute = IFLA_RTA(info); RTA_OK(attribute, length);
			attribute = RTA_NEXT(attribute, length)) {
		if (attribute->rta_type == IFLA_IFNAME && RTA_PAYLOAD(attribute) > 0)
			return RTA_DATA(attribute);
	}
	return NULL;
}

static void handle_link_messages(struct uloop_fd *fd, unsigned int events)
{
	uint8_t buffer[8192];

	(void)events;
	for (;;) {
		ssize_t length = recv(fd->fd, buffer, sizeof(buffer), MSG_DONTWAIT);
		struct nlmsghdr *header;

		if (length < 0) {
			if (errno == EINTR)
				continue;
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				syslog(LOG_ERR, "Interface monitor receive failed: %s",
						strerror(errno));
			break;
		}
		if (length == 0)
			break;

		for (header = (struct nlmsghdr *)buffer; NLMSG_OK(header, length);
				header = NLMSG_NEXT(header, length)) {
			if (header->nlmsg_type == RTM_NEWLINK ||
					header->nlmsg_type == RTM_DELLINK) {
				struct ifinfomsg *info = NLMSG_DATA(header);
				const char *name;
				bool available;

				if (NLMSG_PAYLOAD(header, 0) < sizeof(*info))
					continue;
				name = link_message_name(header, info);
				if (!interface_is_watched(info->ifi_index, name))
					continue;
				available = header->nlmsg_type == RTM_NEWLINK &&
						(info->ifi_flags & IFF_UP) &&
						(info->ifi_flags & IFF_RUNNING);
				if (event_callback)
					event_callback(available, false, event_context);
			} else if (header->nlmsg_type == RTM_NEWADDR ||
					header->nlmsg_type == RTM_DELADDR) {
				struct ifaddrmsg *info = NLMSG_DATA(header);

				if (NLMSG_PAYLOAD(header, 0) < sizeof(*info) ||
						(info->ifa_family != AF_INET &&
						 !(monitor_ipv6_addresses && info->ifa_family == AF_INET6)) ||
						!interface_is_watched(info->ifa_index, NULL))
					continue;
				if (event_callback)
					event_callback(true, true, event_context);
			}
		}
	}
}

int relayd_link_watch_init(const char *const *ifnames, size_t count,
		bool watch_ipv6_addresses, relayd_link_event_cb callback, void *context)
{
	struct sockaddr_nl address = {
		.nl_family = AF_NETLINK,
		.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR |
				(watch_ipv6_addresses ? RTMGRP_IPV6_IFADDR : 0),
	};

	watched_names = ifnames;
	watched_count = count;
	event_callback = callback;
	event_context = context;

	monitor_ipv6_addresses = watch_ipv6_addresses;
	status_socket = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (status_socket < 0)
		goto error;
	watch_fd.fd = socket(AF_NETLINK,
			SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
	if (watch_fd.fd < 0)
		goto error;
	if (bind(watch_fd.fd, (struct sockaddr *)&address, sizeof(address)) < 0)
		goto error;

	watch_fd.cb = handle_link_messages;
	if (uloop_fd_add(&watch_fd, ULOOP_READ | ULOOP_EDGE_TRIGGER) < 0)
		goto error;
	return 0;

error:
	relayd_link_watch_done();
	return -1;
}

void relayd_link_watch_done(void)
{
	if (watch_fd.registered)
		uloop_fd_delete(&watch_fd);
	if (watch_fd.fd >= 0) {
		close(watch_fd.fd);
		watch_fd.fd = -1;
	}
	if (status_socket >= 0) {
		close(status_socket);
		status_socket = -1;
	}
	watched_names = NULL;
	watched_count = 0;
	monitor_ipv6_addresses = false;
	event_callback = NULL;
	event_context = NULL;
}

bool relayd_interfaces_ready(const char *const *ifnames, size_t count,
		char *unavailable, size_t unavailable_size)
{
	for (size_t i = 0; i < count; ++i) {
		struct ifreq request = {0};
		bool ready;

		if (!strcmp(ifnames[i], "."))
			continue;
		strncpy(request.ifr_name, ifnames[i], sizeof(request.ifr_name) - 1);
		ready = status_socket >= 0 &&
				ioctl(status_socket, SIOCGIFFLAGS, &request) == 0 &&
				(request.ifr_flags & IFF_UP) &&
				(request.ifr_flags & IFF_RUNNING);
		if (ready)
			continue;
		if (unavailable && unavailable_size > 0) {
			strncpy(unavailable, ifnames[i], unavailable_size - 1);
			unavailable[unavailable_size - 1] = '\0';
		}
		return false;
	}
	if (unavailable && unavailable_size > 0)
		unavailable[0] = '\0';
	return true;
}
