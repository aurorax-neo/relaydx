/*
 * Shared relaydx runtime: interfaces, uloop dispatch and IPv6 helpers.
 * Derived from 6relayd by Steven Barth.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include "relaydx.h"

LIST_HEAD(interfaces);
int debug;

static int ioctl_sock = -1;
static int rtnl_socket = -1;
static uint32_t rtnl_seq;
static int urandom_fd = -1;

static void relayd_receive_packets(struct relayd_event *event);

static void relayd_event_cb(struct uloop_fd *fd, unsigned int events)
{
	struct relayd_event *event = container_of(fd, struct relayd_event, uloop);

	(void)events;
	if (event->handle_event)
		event->handle_event(event);
	else if (event->handle_dgram)
		relayd_receive_packets(event);
}

void relaydx_log_ratelimited(int priority, const char *key, const char *format,
		...)
{
	static struct {
		const char *key;
		time_t last;
		unsigned int suppressed;
	} entries[16];
	struct timespec now;
	unsigned int slot = 0;
	va_list args;

	clock_gettime(CLOCK_MONOTONIC, &now);
	for (slot = 0; slot < ARRAY_SIZE(entries); ++slot) {
		if (!entries[slot].key || !strcmp(entries[slot].key, key))
			break;
	}
	if (slot == ARRAY_SIZE(entries))
		slot = 0;
	if (!entries[slot].key)
		entries[slot].key = key;
	if (entries[slot].last && now.tv_sec - entries[slot].last < 5) {
		entries[slot].suppressed++;
		return;
	}
	if (entries[slot].suppressed) {
		syslog(LOG_WARNING, "%s: suppressed %u repeated messages", key,
				entries[slot].suppressed);
		entries[slot].suppressed = 0;
	}
	entries[slot].last = now.tv_sec;
	va_start(args, format);
	vsyslog(priority, format, args);
	va_end(args);
}

int relaydx_notify(const char *message)
{
	const char *path = getenv("NOTIFY_SOCKET");
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	int fd;
	ssize_t sent;

	if (!path || !path[0] || strlen(path) >= sizeof(address.sun_path))
		return 0;
	if (path[0] == '@')
		address.sun_path[0] = '\0';
	memcpy(address.sun_path + (path[0] == '@'), path + (path[0] == '@'),
			strlen(path) - (path[0] == '@'));
	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	sent = sendto(fd, message, strlen(message), MSG_DONTWAIT,
			(struct sockaddr *)&address,
			offsetof(struct sockaddr_un, sun_path) + strlen(path));
	close(fd);
	return sent == (ssize_t)strlen(message) ? 0 : -1;
}

int relayd_core_init(void)
{
	ioctl_sock = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (ioctl_sock < 0)
		return -1;

	rtnl_socket = relayd_open_rtnl_socket();
	if (rtnl_socket < 0)
		goto error;

	urandom_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (urandom_fd < 0)
		goto error;

	return 0;

error:
	relayd_core_done();
	return -1;
}

void relayd_core_done(void)
{
	if (urandom_fd >= 0) {
		close(urandom_fd);
		urandom_fd = -1;
	}
	if (rtnl_socket >= 0) {
		close(rtnl_socket);
		rtnl_socket = -1;
	}
	if (ioctl_sock >= 0) {
		close(ioctl_sock);
		ioctl_sock = -1;
	}
}

int relayd_open_interface(struct relayd_interface *iface, const char *ifname,
		bool managed, bool external)
{
	struct ifreq ifr = {0};
	size_t ifname_len;

	memset(iface, 0, sizeof(*iface));
	INIT_LIST_HEAD(&iface->list);
	INIT_LIST_HEAD(&iface->hosts);
	INIT_LIST_HEAD(&iface->pd_assignments);
	iface->fd.fd = -1;
	iface->bcast_fd.fd = -1;
	iface->timer_rs.socket = -1;
	iface->managed = managed;
	iface->external = external;

	if (!strcmp(ifname, ".")) {
		strcpy(iface->ifname, ".");
		return 0;
	}

	ifname_len = strlen(ifname);
	if (ifname_len == 0 || ifname_len >= IFNAMSIZ) {
		errno = EINVAL;
		return -1;
	}
	memcpy(ifr.ifr_name, ifname, ifname_len + 1);

	if (ioctl(ioctl_sock, SIOCGIFINDEX, &ifr) < 0)
		goto error;
	iface->ifindex = ifr.ifr_ifindex;

	if (ioctl(ioctl_sock, SIOCGIFHWADDR, &ifr) < 0)
		goto error;
	memcpy(iface->mac, ifr.ifr_hwaddr.sa_data, sizeof(iface->mac));
	memcpy(iface->ifname, ifname, ifname_len + 1);
	list_add_tail(&iface->list, &interfaces);
	return 0;

error:
	syslog(LOG_ERR, "Unable to open interface %s: %s", ifname,
			strerror(errno));
	return -1;
}

void relayd_close_interface(struct relayd_interface *iface)
{
	if (iface->list.next && iface->list.prev && !list_empty(&iface->list))
		list_del_init(&iface->list);
}

int relayd_register_event(struct relayd_event *event)
{
	if (event->socket < 0) {
		errno = EBADF;
		return -1;
	}

	event->uloop.fd = event->socket;
	event->uloop.cb = relayd_event_cb;
	return uloop_fd_add(&event->uloop, ULOOP_READ | ULOOP_EDGE_TRIGGER);
}

void relayd_unregister_event(struct relayd_event *event)
{
	if (event->uloop.registered)
		uloop_fd_delete(&event->uloop);
	if (event->socket >= 0) {
		close(event->socket);
		event->socket = -1;
	}
}

int relayd_open_rtnl_socket(void)
{
	struct sockaddr_nl nl = {.nl_family = AF_NETLINK};
	int sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);

	if (sock < 0)
		return -1;
	if (connect(sock, (struct sockaddr *)&nl, sizeof(nl)) < 0) {
		close(sock);
		return -1;
	}
	return sock;
}

int relayd_get_interface_mtu(const char *ifname)
{
	char buf[64];
	ssize_t len;
	int fd;

	snprintf(buf, sizeof(buf), "/proc/sys/net/ipv6/conf/%s/mtu", ifname);
	fd = open(buf, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	len = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (len < 0)
		return -1;
	buf[len] = '\0';
	return atoi(buf);
}

int relayd_get_interface_mac(const char *ifname, uint8_t mac[6])
{
	struct ifreq ifr = {0};

	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
	if (ioctl(ioctl_sock, SIOCGIFHWADDR, &ifr) < 0)
		return -1;
	memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
	return 0;
}

ssize_t relayd_forward_packet(int socket_fd, struct sockaddr_in6 *dest,
		struct iovec *iov, size_t iov_len,
		const struct relayd_interface *iface)
{
	uint8_t cmsg_buf[CMSG_SPACE(sizeof(struct in6_pktinfo))] = {0};
	struct msghdr msg = {
		.msg_name = dest,
		.msg_namelen = sizeof(*dest),
		.msg_iov = iov,
		.msg_iovlen = iov_len,
		.msg_control = cmsg_buf,
		.msg_controllen = sizeof(cmsg_buf),
	};
	struct cmsghdr *chdr = CMSG_FIRSTHDR(&msg);
	struct in6_pktinfo *pktinfo;
	ssize_t sent;

	chdr->cmsg_level = IPPROTO_IPV6;
	chdr->cmsg_type = IPV6_PKTINFO;
	chdr->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	pktinfo = (struct in6_pktinfo *)CMSG_DATA(chdr);
	pktinfo->ipi6_ifindex = iface->ifindex;

	if (IN6_IS_ADDR_LINKLOCAL(&dest->sin6_addr) ||
			IN6_IS_ADDR_MC_LINKLOCAL(&dest->sin6_addr))
		dest->sin6_scope_id = iface->ifindex;

	if (dest->sin6_port == 0) {
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
	}

	sent = sendmsg(socket_fd, &msg, MSG_DONTWAIT);
	if (sent < 0)
		relaydx_log_ratelimited(LOG_WARNING, "relay-send",
				"Failed to relay on %s: %s", iface->ifname, strerror(errno));
	return sent;
}

ssize_t relayd_get_interface_addresses(int ifindex,
		struct relayd_ipaddr *addrs, size_t cnt)
{
	struct {
		struct nlmsghdr nh;
		struct ifaddrmsg ifa;
	} req = {
		.nh = {
			.nlmsg_len = sizeof(req),
			.nlmsg_type = RTM_GETADDR,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
			.nlmsg_seq = ++rtnl_seq,
		},
		.ifa = {.ifa_family = AF_INET6, .ifa_index = ifindex},
	};
	uint8_t buf[8192];
	ssize_t count = 0;

	if (send(rtnl_socket, &req, sizeof(req), 0) != (ssize_t)sizeof(req))
		return -1;

	for (;;) {
		ssize_t len = recv(rtnl_socket, buf, sizeof(buf), 0);
		struct nlmsghdr *nh;

		if (len < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}

		for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len);
				nh = NLMSG_NEXT(nh, len)) {
			struct ifaddrmsg *ifa;
			struct rtattr *rta;
			int alen;

			if (nh->nlmsg_seq != req.nh.nlmsg_seq)
				continue;
			if (nh->nlmsg_type == NLMSG_DONE)
				return count;
			if (nh->nlmsg_type == NLMSG_ERROR)
				return -1;
			if (nh->nlmsg_type != RTM_NEWADDR || count >= (ssize_t)cnt)
				continue;

			ifa = NLMSG_DATA(nh);
			if (ifa->ifa_family != AF_INET6 ||
					ifa->ifa_scope != RT_SCOPE_UNIVERSE ||
					ifa->ifa_index != (unsigned int)ifindex)
				continue;

			memset(&addrs[count], 0, sizeof(addrs[count]));
			addrs[count].prefix = ifa->ifa_prefixlen;
			alen = IFA_PAYLOAD(nh);
			for (rta = IFA_RTA(ifa); RTA_OK(rta, alen);
					rta = RTA_NEXT(rta, alen)) {
				if (rta->rta_type == IFA_ADDRESS &&
						RTA_PAYLOAD(rta) >= sizeof(struct in6_addr)) {
					memcpy(&addrs[count].addr, RTA_DATA(rta),
							sizeof(struct in6_addr));
				} else if (rta->rta_type == IFA_CACHEINFO &&
						RTA_PAYLOAD(rta) >= sizeof(struct ifa_cacheinfo)) {
					struct ifa_cacheinfo *info = RTA_DATA(rta);
					addrs[count].preferred = info->ifa_prefered;
					addrs[count].valid = info->ifa_valid;
				}
			}
			if (ifa->ifa_flags & IFA_F_DEPRECATED)
				addrs[count].preferred = 0;
			count++;
		}
	}
}

struct relayd_interface *relayd_get_interface_by_index(int ifindex)
{
	struct relayd_interface *iface;

	list_for_each_entry(iface, &interfaces, list)
		if (iface->ifindex == ifindex)
			return iface;
	return NULL;
}

struct relayd_interface *relayd_get_interface_by_name(const char *name)
{
	struct relayd_interface *iface;

	list_for_each_entry(iface, &interfaces, list)
		if (!strcmp(iface->ifname, name))
			return iface;
	return NULL;
}

static void relayd_receive_packets(struct relayd_event *event)
{
	uint8_t data_buf[RELAYD_BUFFER_SIZE];
	uint8_t cmsg_buf[128];
	union {
		struct sockaddr_in6 in6;
		struct sockaddr_ll ll;
		struct sockaddr_nl nl;
	} addr;

	for (;;) {
		struct iovec iov = {.iov_base = data_buf, .iov_len = sizeof(data_buf)};
		struct msghdr msg;
		int destiface = 0;
		ssize_t len;
		struct relayd_interface *iface;
		struct cmsghdr *ch;

		memset(&addr, 0, sizeof(addr));
		memset(&msg, 0, sizeof(msg));
		msg.msg_name = &addr;
		msg.msg_namelen = sizeof(addr);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = cmsg_buf;
		msg.msg_controllen = sizeof(cmsg_buf);
		len = recvmsg(event->socket, &msg, MSG_DONTWAIT);

		if (len < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			if (errno == EINTR)
				continue;
			break;
		}

		for (ch = CMSG_FIRSTHDR(&msg); ch; ch = CMSG_NXTHDR(&msg, ch)) {
			if (ch->cmsg_level == IPPROTO_IPV6 &&
					ch->cmsg_type == IPV6_PKTINFO) {
				struct in6_pktinfo *pktinfo =
						(struct in6_pktinfo *)CMSG_DATA(ch);
				destiface = pktinfo->ipi6_ifindex;
			}
		}
		if (addr.ll.sll_family == AF_PACKET)
			destiface = addr.ll.sll_ifindex;

		iface = relayd_get_interface_by_index(destiface);
		if (!iface && addr.nl.nl_family != AF_NETLINK)
			continue;
		event->handle_dgram(&addr, data_buf, (size_t)len, iface);
	}
}

void relayd_urandom(void *data, size_t len)
{
	uint8_t *out = data;

	while (len > 0) {
		ssize_t count = read(urandom_fd, out, len);
		if (count < 0) {
			if (errno == EINTR)
				continue;
			abort();
		}
		out += count;
		len -= (size_t)count;
	}
}
