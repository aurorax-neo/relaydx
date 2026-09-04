/*
 * relaydx unified command-line entry point.
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "relaydx.h"

struct relayd_config relaydx_config;

struct interface_spec {
	char *name;
	bool managed;
	bool external;
};

struct string_list {
	char **items;
	size_t count;
};

enum {
	OPT_SERVER6 = 1000,
	OPT_BROADCAST4,
	OPT_DHCP4,
	OPT_NO_DHCP4_PARSE,
	OPT_GATEWAY4,
	OPT_ROUTE4,
	OPT_TIMEOUT4,
	OPT_ARP_TRIES4,
	OPT_TABLE4,
	OPT_LOCAL4,
	OPT_RA6,
	OPT_DHCP6,
	OPT_NDP6,
	OPT_RS6,
	OPT_LEARN_ROUTES6,
	OPT_REWRITE_DNS6,
	OPT_ALWAYS_DEFAULT6,
	OPT_DEPRECATE_ULA6,
	OPT_RA_MANAGED6,
	OPT_RA_NOT_ONLINK6,
	OPT_RA_PREFERENCE6,
	OPT_STATE6,
	OPT_LEASE6,
	OPT_STATIC_NDP6,
	OPT_NO_INTERFACE_WATCH,
	OPT_NO_FORWARDING_SETUP,
};

static struct interface_spec *specs;
static size_t spec_count;
static char *master_name;
static char *state6_storage;
static volatile sig_atomic_t terminate_requested;
static bool runtime_active;
static bool runtime_ipv4_started;
static bool runtime_ipv6_attempted;
static bool reload_requested;
static struct uloop_timeout retry_timer;

static const struct option long_options[] = {
	{"ipv4", no_argument, NULL, '4'},
	{"ipv6", no_argument, NULL, '6'},
	{"interface", required_argument, NULL, 'i'},
	{"managed-interface", required_argument, NULL, 'I'},
	{"master6", required_argument, NULL, 'M'},
	{"verbose", no_argument, NULL, 'v'},
	{"daemon", no_argument, NULL, 'd'},
	{"pidfile", required_argument, NULL, 'p'},
	{"help", no_argument, NULL, 'h'},
	{"server6", no_argument, NULL, OPT_SERVER6},
	{"broadcast4", no_argument, NULL, OPT_BROADCAST4},
	{"dhcp4", no_argument, NULL, OPT_DHCP4},
	{"no-dhcp4-parse", no_argument, NULL, OPT_NO_DHCP4_PARSE},
	{"gateway4", required_argument, NULL, OPT_GATEWAY4},
	{"route4", required_argument, NULL, OPT_ROUTE4},
	{"timeout4", required_argument, NULL, OPT_TIMEOUT4},
	{"arp-tries4", required_argument, NULL, OPT_ARP_TRIES4},
	{"table4", required_argument, NULL, OPT_TABLE4},
	{"local4", required_argument, NULL, OPT_LOCAL4},
	{"ra6", required_argument, NULL, OPT_RA6},
	{"dhcp6", required_argument, NULL, OPT_DHCP6},
	{"ndp6", no_argument, NULL, OPT_NDP6},
	{"rs6", no_argument, NULL, OPT_RS6},
	{"learn-routes6", no_argument, NULL, OPT_LEARN_ROUTES6},
	{"rewrite-dns6", optional_argument, NULL, OPT_REWRITE_DNS6},
	{"always-default6", no_argument, NULL, OPT_ALWAYS_DEFAULT6},
	{"deprecate-ula6", no_argument, NULL, OPT_DEPRECATE_ULA6},
	{"ra-managed6", required_argument, NULL, OPT_RA_MANAGED6},
	{"ra-not-onlink6", no_argument, NULL, OPT_RA_NOT_ONLINK6},
	{"ra-preference6", required_argument, NULL, OPT_RA_PREFERENCE6},
	{"state6", required_argument, NULL, OPT_STATE6},
	{"lease6", required_argument, NULL, OPT_LEASE6},
	{"static-ndp6", required_argument, NULL, OPT_STATIC_NDP6},
	{"no-interface-watch", no_argument, NULL, OPT_NO_INTERFACE_WATCH},
	{"no-forwarding-setup", no_argument, NULL, OPT_NO_FORWARDING_SETUP},
	{NULL, 0, NULL, 0},
};

static int usage(const char *name, int status)
{
	FILE *out = status ? stderr : stdout;

	fprintf(out,
		"Usage: %s [options] -i IFACE -i IFACE ...\n\n"
		"Families and interfaces:\n"
		"  -4, --ipv4                 Enable IPv4 ARP relay\n"
		"  -6, --ipv6                 Enable automatic IPv6 relay\n"
		"      --server6              Enable automatic IPv6 server\n"
		"  -i, --interface IFACE      Add a relay interface\n"
		"  -I, --managed-interface IFACE\n"
		"                             Add an IPv4 managed interface\n"
		"  -M, --master6 IFACE        IPv6 upstream interface (or '.')\n"
		"                             Prefix an interface with '~' to mark\n"
		"                             it external for NDP\n\n"
		"IPv4 options:\n"
		"      --broadcast4           Forward IPv4 broadcasts\n"
		"      --dhcp4                Forward DHCPv4\n"
		"      --no-dhcp4-parse       Do not learn routes from DHCPv4\n"
		"      --gateway4 IP          Add a client default gateway\n"
		"      --route4 GW:NET/MASK   Add a static route\n"
		"      --timeout4 SECONDS     Host expiry timeout (default: 30)\n"
		"      --arp-tries4 COUNT     ARP probes before expiry (default: 5)\n"
		"      --table4 NUMBER        First policy table (default: 16800)\n"
		"      --local4 IP            Enable local access using this source\n\n"
		"IPv6 feature options:\n"
		"      --ra6 relay|server     Router Advertisement mode\n"
		"      --dhcp6 relay|server   DHCPv6 mode\n"
		"      --ndp6                 Enable NDP proxy\n"
		"      --rs6                  Send initial Router Solicitation\n"
		"      --learn-routes6        Learn routes from NDP\n"
		"      --rewrite-dns6[=ADDR]  Rewrite announced DNS servers\n"
		"      --always-default6      Announce default route with ULA only\n"
		"      --deprecate-ula6       Deprecate ULA when public prefix exists\n"
		"      --ra-managed6 0|1|2    SLAAC/managed-address mode\n"
		"      --ra-not-onlink6       Clear RA on-link prefix flag\n"
		"      --ra-preference6 LEVEL low, medium, or high\n"
		"      --state6 FILE[,CMD]    DHCPv6 lease state and callback\n"
		"      --lease6 DUID:VALUE    Static DHCPv6 IA_NA assignment\n"
		"      --static-ndp6 P/L:IF   Static NDP prefix\n\n"
		"Process options:\n"
		"  -v, --verbose              Increase logging verbosity\n"
		"  -d, --daemon               Run in the background\n"
		"  -p, --pidfile FILE         PID file (default: /var/run/relaydx.pid)\n"
		"      --no-interface-watch   Exit instead of waiting/reloading on link changes\n"
		"      --no-forwarding-setup  Do not enable kernel IP forwarding\n"
		"  -h, --help                 Show this help\n",
		name);
	return status;
}

static int append_string(struct string_list *list, const char *value)
{
	char **items = realloc(list->items, sizeof(*items) * (list->count + 1));

	if (!items)
		return -1;
	list->items = items;
	list->items[list->count++] = strdup(value);
	return list->items[list->count - 1] ? 0 : -1;
}

static int append_config_string(char ***items, size_t *count, const char *value)
{
	char **new_items = realloc(*items, sizeof(**items) * (*count + 1));

	if (!new_items)
		return -1;
	*items = new_items;
	(*items)[*count] = strdup(value);
	if (!(*items)[*count])
		return -1;
	(*count)++;
	return 0;
}

static int add_interface_spec(const char *arg, bool managed)
{
	bool external = arg[0] == '~';
	const char *name = external ? arg + 1 : arg;
	struct interface_spec *new_specs;

	if (!name[0] || strlen(name) >= IFNAMSIZ)
		return -1;
	for (size_t i = 0; i < spec_count; i++) {
		if (!strcmp(specs[i].name, name)) {
			specs[i].managed |= managed;
			specs[i].external |= external;
			return 0;
		}
	}

	new_specs = realloc(specs, sizeof(*specs) * (spec_count + 1));
	if (!new_specs)
		return -1;
	specs = new_specs;
	specs[spec_count].name = strdup(name);
	if (!specs[spec_count].name)
		return -1;
	specs[spec_count].managed = managed;
	specs[spec_count].external = external;
	spec_count++;
	return 0;
}

static void enable_ipv6_relay(void)
{
	relaydx_config.enable_router_discovery_relay = true;
	relaydx_config.enable_router_discovery_server = false;
	relaydx_config.enable_dhcpv6_relay = true;
	relaydx_config.enable_dhcpv6_server = false;
	relaydx_config.enable_ndp_relay = true;
	relaydx_config.send_router_solicitation = true;
	relaydx_config.enable_route_learning = true;
}

static void enable_ipv6_server(void)
{
	relaydx_config.enable_router_discovery_relay = true;
	relaydx_config.enable_router_discovery_server = true;
	relaydx_config.enable_dhcpv6_relay = true;
	relaydx_config.enable_dhcpv6_server = true;
	relaydx_config.enable_ndp_relay = false;
	relaydx_config.send_router_solicitation = false;
	relaydx_config.enable_route_learning = false;
}

static int set_mode(bool *enabled, bool *server, const char *mode)
{
	if (!strcmp(mode, "relay")) {
		*enabled = true;
		*server = false;
	} else if (!strcmp(mode, "server")) {
		*enabled = true;
		*server = true;
	} else if (!strcmp(mode, "off")) {
		*enabled = false;
		*server = false;
	} else {
		return -1;
	}
	return 0;
}

static void stop_handler(_unused int signal_number)
{
	terminate_requested = 1;
	uloop_end();
}

static void reload_handler(_unused int signal_number)
{
	reload_requested = true;
	uloop_end();
}

static void retry_handler(_unused struct uloop_timeout *timeout)
{
	uloop_end();
}

static void child_handler(_unused int signal_number)
{
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

static void link_event_handler(bool available, bool address_change,
		_unused void *context)
{
	if (!runtime_active || !available || address_change) {
		reload_requested = true;
		uloop_end();
	}
}

static int parse_int_option(const char *value, int minimum, int maximum,
		int *result)
{
	char *end;
	long parsed;

	errno = 0;
	parsed = strtol(value, &end, 10);
	if (errno || !value[0] || *end || parsed < minimum || parsed > maximum)
		return -1;
	*result = (int)parsed;
	return 0;
}

static int decode_route4(const char *value, struct in_addr *gateway,
		struct in_addr *destination, uint8_t *prefix)
{
	char *copy = strdup(value);
	char *colon;
	char *slash;
	char *end;
	long mask;
	int result = -1;

	if (!copy)
		return -1;
	colon = strchr(copy, ':');
	if (!colon)
		goto out;
	*colon++ = '\0';
	slash = strchr(colon, '/');
	if (!slash)
		goto out;
	*slash++ = '\0';
	mask = strtol(slash, &end, 10);
	if (*end || mask < 0 || mask > 32 ||
			inet_aton(copy, gateway) == 0 ||
			inet_aton(colon, destination) == 0)
		goto out;
	*prefix = (uint8_t)mask;
	result = 0;

out:
	free(copy);
	return result;
}

static int parse_route4(const char *value)
{
	struct in_addr gateway;
	struct in_addr destination;
	uint8_t prefix;

	if (decode_route4(value, &gateway, &destination, &prefix) < 0)
		return -1;
	relayd_add_pending_route((uint8_t *)&gateway.s_addr,
			(uint8_t *)&destination.s_addr, prefix, 0);
	return 0;
}

static int normalize_interface_specs(void)
{
	bool ipv6_enabled = relaydx_config.enable_router_discovery_relay ||
			relaydx_config.enable_dhcpv6_relay || relaydx_config.enable_ndp_relay;

	if (spec_count == 0)
		return -1;
	if (!master_name) {
		if (ipv6_enabled && (relaydx_config.enable_router_discovery_server ||
				relaydx_config.enable_dhcpv6_server))
			master_name = strdup(".");
		else
			master_name = strdup(specs[0].name);
	}
	if (!master_name)
		return -1;
	if (!strcmp(master_name, "."))
		return 0;

	for (size_t i = 0; i < spec_count; ++i) {
		if (!strcmp(specs[i].name, master_name))
			return 0;
	}
	return add_interface_spec(master_name, false);
}

static int prepare_interfaces(void)
{
	ssize_t master_index = -1;
	size_t slave_index = 0;

	if (spec_count == 0 || !master_name)
		return -1;

	if (strcmp(master_name, ".")) {
		for (size_t i = 0; i < spec_count; i++) {
			if (!strcmp(specs[i].name, master_name)) {
				master_index = (ssize_t)i;
				break;
			}
		}
		if (master_index < 0)
			return -1;
	}

	relaydx_config.slavecount = spec_count - (master_index >= 0 ? 1 : 0);
	relaydx_config.slaves = calloc(relaydx_config.slavecount, sizeof(*relaydx_config.slaves));
	if (relaydx_config.slavecount && !relaydx_config.slaves)
		return -1;

	if (master_index >= 0) {
		struct interface_spec *spec = &specs[master_index];
		if (relayd_open_interface(&relaydx_config.master, spec->name, spec->managed,
				false) < 0)
			return -1;
	} else if (relayd_open_interface(&relaydx_config.master, ".", false, false) < 0) {
		return -1;
	}

	for (size_t i = 0; i < spec_count; i++) {
		struct interface_spec *spec = &specs[i];
		if ((ssize_t)i == master_index)
			continue;
		if (relayd_open_interface(&relaydx_config.slaves[slave_index], spec->name,
				spec->managed, spec->external) < 0)
			return -1;
		slave_index++;
	}
	return 0;
}

static void cleanup_interfaces(void)
{
	for (size_t i = 0; i < relaydx_config.slavecount; i++)
		relayd_close_interface(&relaydx_config.slaves[i]);
	relayd_close_interface(&relaydx_config.master);
	free(relaydx_config.slaves);
	relaydx_config.slaves = NULL;
	relaydx_config.slavecount = 0;
}

static int write_sysctl_value(const char *path, char desired)
{
	char value[2] = {desired, '\n'};
	char current = '\0';
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t written;

	if (fd >= 0) {
		ssize_t length = read(fd, &current, 1);
		close(fd);
		if (length == 1 && current == desired)
			return 0;
	}

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	written = write(fd, value, sizeof(value));
	close(fd);
	if (written == (ssize_t)sizeof(value))
		return 0;
	if (written >= 0)
		errno = EIO;
	return -1;
}

static int enable_kernel_forwarding(void)
{
	bool ipv6_enabled = relaydx_config.enable_router_discovery_relay ||
			relaydx_config.enable_dhcpv6_relay || relaydx_config.enable_ndp_relay;
	bool ipv6_relay = relaydx_config.enable_ndp_relay ||
			(relaydx_config.enable_router_discovery_relay &&
			 !relaydx_config.enable_router_discovery_server) ||
			(relaydx_config.enable_dhcpv6_relay &&
			 !relaydx_config.enable_dhcpv6_server);

	if (relaydx_config.enable_ipv4 &&
			write_sysctl_value("/proc/sys/net/ipv4/ip_forward", '1') < 0)
		return -1;
	if (!ipv6_enabled)
		return 0;

	if (ipv6_relay && relaydx_config.master.ifindex > 0) {
		char path[128];

		snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/%s/accept_ra",
				relaydx_config.master.ifname);
		if (write_sysctl_value(path, '2') < 0)
			return -1;
	}
	return write_sysctl_value("/proc/sys/net/ipv6/conf/all/forwarding", '1');
}

static void stop_relay_runtime(void)
{
	runtime_active = false;
	if (runtime_ipv6_attempted) {
		deinit_dhcpv6_relay();
		deinit_ndp_proxy();
		deinit_router_discovery_relay();
		runtime_ipv6_attempted = false;
	}
	if (runtime_ipv4_started) {
		relayd_ipv4_done();
		runtime_ipv4_started = false;
	}
	cleanup_interfaces();
}

static int start_relay_runtime(const struct string_list *gateways,
		const struct string_list *routes, bool setup_forwarding)
{
	if (prepare_interfaces() < 0)
		return -1;
	if (setup_forwarding && enable_kernel_forwarding() < 0) {
		syslog(LOG_ERR, "Unable to configure kernel IP forwarding: %s",
				strerror(errno));
		goto error;
	}

	for (size_t i = 0; i < gateways->count; ++i) {
		struct in_addr gateway;

		if (!inet_aton(gateways->items[i], &gateway))
			goto error;
		relayd_add_pending_route((uint8_t *)&gateway.s_addr,
				(const uint8_t *)"\0\0\0\0", 0, 0);
	}
	for (size_t i = 0; i < routes->count; ++i) {
		if (parse_route4(routes->items[i]) < 0)
			goto error;
	}

	if (relaydx_config.enable_ipv4) {
		if (relayd_ipv4_init(&relaydx_config) < 0)
			goto error;
		runtime_ipv4_started = true;
	}
	if (relaydx_config.enable_router_discovery_relay ||
			relaydx_config.enable_dhcpv6_relay ||
			relaydx_config.enable_ndp_relay) {
		runtime_ipv6_attempted = true;
		if (relaydx_config.slavecount < 1) {
			syslog(LOG_ERR, "IPv6 requires at least one downstream interface");
			goto error;
		}
		if (relaydx_config.enable_ndp_relay &&
				relaydx_config.master.ifindex == 0) {
			syslog(LOG_ERR, "NDP relay requires a real IPv6 master interface");
			goto error;
		}
		if (init_router_discovery_relay(&relaydx_config) ||
				init_dhcpv6_relay(&relaydx_config) ||
				init_ndp_proxy(&relaydx_config))
			goto error;
	}

	runtime_active = true;
	syslog(LOG_NOTICE, "Relay active on %zu interfaces", spec_count);
	return 0;

error:
	stop_relay_runtime();
	return -1;
}

static void wait_for_interface_event(void)
{
	retry_timer.cb = retry_handler;
	uloop_timeout_set(&retry_timer, 1000);
	uloop_run();
	uloop_timeout_cancel(&retry_timer);
}

int main(int argc, char **argv)
{
	const char *pidfile = "/var/run/relaydx.pid";
	struct string_list gateways = {0};
	struct string_list routes = {0};
	const char **watched_names = NULL;
	bool daemonize = false;
	bool daemon_started = false;
	bool uloop_started = false;
	bool link_watch_started = false;
	bool watch_interfaces = true;
	bool setup_forwarding = true;
	int verbosity = 0;
	int status = 1;
	int option;

	memset(&relaydx_config, 0, sizeof(relaydx_config));
	relaydx_config.parse_dhcp4 = true;
	relaydx_config.host_timeout4 = 30;
	relaydx_config.host_ping_tries4 = 5;
	relaydx_config.route_table4 = 16800;

	while ((option = getopt_long(argc, argv, "46i:I:M:vdp:h", long_options,
			NULL)) != -1) {
		switch (option) {
		case '4': relaydx_config.enable_ipv4 = true; break;
		case '6': enable_ipv6_relay(); break;
		case 'i':
			if (add_interface_spec(optarg, false) < 0)
				goto invalid;
			break;
		case 'I':
			if (add_interface_spec(optarg, true) < 0)
				goto invalid;
			break;
		case 'M':
			free(master_name);
			master_name = strdup(optarg);
			if (!master_name)
				goto out;
			break;
		case 'v': verbosity++; break;
		case 'd': daemonize = true; break;
		case 'p': pidfile = optarg; break;
		case 'h': status = usage(argv[0], 0); goto out;
		case OPT_SERVER6: enable_ipv6_server(); break;
		case OPT_BROADCAST4:
			relaydx_config.enable_ipv4 = true;
			relaydx_config.forward_bcast4 = true;
			break;
		case OPT_DHCP4:
			relaydx_config.enable_ipv4 = true;
			relaydx_config.forward_dhcp4 = true;
			break;
		case OPT_NO_DHCP4_PARSE: relaydx_config.parse_dhcp4 = false; break;
		case OPT_GATEWAY4:
			relaydx_config.enable_ipv4 = true;
			if (append_string(&gateways, optarg) < 0)
				goto out;
			break;
		case OPT_ROUTE4:
			relaydx_config.enable_ipv4 = true;
			if (append_string(&routes, optarg) < 0)
				goto out;
			break;
		case OPT_TIMEOUT4:
			if (parse_int_option(optarg, 1, INT_MAX,
					&relaydx_config.host_timeout4) < 0)
				goto invalid;
			break;
		case OPT_ARP_TRIES4:
			if (parse_int_option(optarg, 1, INT_MAX,
					&relaydx_config.host_ping_tries4) < 0)
				goto invalid;
			break;
		case OPT_TABLE4:
			if (parse_int_option(optarg, 1, INT_MAX,
					&relaydx_config.route_table4) < 0)
				goto invalid;
			break;
		case OPT_LOCAL4: {
			struct in_addr addr;
			if (!inet_aton(optarg, &addr))
				goto invalid;
			memcpy(relaydx_config.local_addr4, &addr.s_addr, 4);
			relaydx_config.local_addr4_valid = true;
			relaydx_config.enable_ipv4 = true;
			break;
		}
		case OPT_RA6:
			if (set_mode(&relaydx_config.enable_router_discovery_relay,
					&relaydx_config.enable_router_discovery_server, optarg) < 0)
				goto invalid;
			break;
		case OPT_DHCP6:
			if (set_mode(&relaydx_config.enable_dhcpv6_relay,
					&relaydx_config.enable_dhcpv6_server, optarg) < 0)
				goto invalid;
			break;
		case OPT_NDP6: relaydx_config.enable_ndp_relay = true; break;
		case OPT_RS6:
			relaydx_config.enable_router_discovery_relay = true;
			relaydx_config.send_router_solicitation = true;
			break;
		case OPT_LEARN_ROUTES6:
			relaydx_config.enable_ndp_relay = true;
			relaydx_config.enable_route_learning = true;
			break;
		case OPT_REWRITE_DNS6:
			relaydx_config.always_rewrite_dns = true;
			if (optarg && inet_pton(AF_INET6, optarg, &relaydx_config.dnsaddr) != 1)
				goto invalid;
			break;
		case OPT_ALWAYS_DEFAULT6: relaydx_config.always_announce_default_router = true; break;
		case OPT_DEPRECATE_ULA6: relaydx_config.deprecate_ula_if_public_avail = true; break;
		case OPT_RA_MANAGED6:
			if (parse_int_option(optarg, 0, 2,
					&relaydx_config.ra_managed_mode) < 0)
				goto invalid;
			break;
		case OPT_RA_NOT_ONLINK6: relaydx_config.ra_not_onlink = true; break;
		case OPT_RA_PREFERENCE6:
			if (!strcmp(optarg, "low")) relaydx_config.ra_preference = -1;
			else if (!strcmp(optarg, "medium")) relaydx_config.ra_preference = 0;
			else if (!strcmp(optarg, "high")) relaydx_config.ra_preference = 1;
			else goto invalid;
			break;
		case OPT_STATE6:
			free(state6_storage);
			state6_storage = strdup(optarg);
			if (!state6_storage)
				goto out;
			relaydx_config.dhcpv6_statefile = strtok(state6_storage, ",");
			relaydx_config.dhcpv6_cb = strtok(NULL, ",");
			if (!relaydx_config.dhcpv6_statefile)
				goto invalid;
			break;
		case OPT_LEASE6:
			if (append_config_string(&relaydx_config.dhcpv6_lease,
					&relaydx_config.dhcpv6_lease_len, optarg) < 0)
				goto out;
			break;
		case OPT_STATIC_NDP6:
			relaydx_config.enable_ndp_relay = true;
			if (append_config_string(&relaydx_config.static_ndp,
					&relaydx_config.static_ndp_len, optarg) < 0)
				goto out;
			break;
		case OPT_NO_INTERFACE_WATCH: watch_interfaces = false; break;
		case OPT_NO_FORWARDING_SETUP: setup_forwarding = false; break;
		default: goto invalid;
		}
	}

	if (optind != argc || (!relaydx_config.enable_ipv4 &&
			!relaydx_config.enable_router_discovery_relay &&
			!relaydx_config.enable_dhcpv6_relay && !relaydx_config.enable_ndp_relay) ||
			relaydx_config.host_timeout4 <= 0 || relaydx_config.host_ping_tries4 <= 0 ||
			relaydx_config.route_table4 <= 0)
		goto invalid;
	if (normalize_interface_specs() < 0)
		goto invalid;
	if (relaydx_config.route_table4 > INT_MAX - (int)spec_count - 1)
		goto invalid;
	if (relaydx_config.enable_ipv4 && spec_count < 2) {
		fprintf(stderr, "relaydx: IPv4 relay needs at least two interfaces\n");
		goto out;
	}
	for (size_t i = 0; i < gateways.count; ++i) {
		struct in_addr gateway;
		if (!inet_aton(gateways.items[i], &gateway))
			goto invalid;
	}
	for (size_t i = 0; i < routes.count; ++i) {
		struct in_addr gateway;
		struct in_addr destination;
		uint8_t prefix;
		if (decode_route4(routes.items[i], &gateway, &destination,
				&prefix) < 0)
			goto invalid;
	}

	watched_names = calloc(spec_count, sizeof(*watched_names));
	if (!watched_names)
		goto out;
	for (size_t i = 0; i < spec_count; ++i)
		watched_names[i] = specs[i].name;

	if (getuid() != 0) {
		fprintf(stderr, "relaydx: must be run as root\n");
		status = 2;
		goto out;
	}

	openlog("relaydx", LOG_PERROR | LOG_PID, LOG_DAEMON);
	if (verbosity == 0)
		setlogmask(LOG_UPTO(LOG_WARNING));
	else if (verbosity == 1)
		setlogmask(LOG_UPTO(LOG_INFO));
	debug = verbosity;

	if (uloop_init() < 0) {
		perror("relaydx: uloop initialization failed");
		status = 2;
		goto out;
	}
	uloop_started = true;
	if (relayd_core_init() < 0) {
		perror("relaydx: initialization failed");
		status = 2;
		goto runtime_out;
	}
	if (watch_interfaces) {
		if (relayd_link_watch_init(watched_names, spec_count,
				link_event_handler, NULL) < 0) {
			syslog(LOG_ERR, "Unable to initialize interface monitor: %s",
					strerror(errno));
			status = 2;
			goto runtime_out;
		}
		link_watch_started = true;
	}

	if (daemonize) {
		FILE *pid;
		if (daemon(0, 0) < 0)
			goto runtime_out;
		openlog("relaydx", LOG_PID, LOG_DAEMON);
		daemon_started = true;
		pid = fopen(pidfile, "w");
		if (pid) {
			fprintf(pid, "%ld\n", (long)getpid());
			fclose(pid);
		}
	}

	signal(SIGTERM, stop_handler);
	signal(SIGHUP, reload_handler);
	signal(SIGINT, stop_handler);
	signal(SIGCHLD, child_handler);

	{
		char unavailable[IFNAMSIZ] = "";
		char reported_unavailable[IFNAMSIZ] = "";

		while (!terminate_requested) {
			if (watch_interfaces && !relayd_interfaces_ready(watched_names,
					spec_count, unavailable, sizeof(unavailable))) {
				if (strcmp(unavailable, reported_unavailable)) {
					syslog(LOG_WARNING, "Waiting for interface %s",
							unavailable);
					strncpy(reported_unavailable, unavailable,
							sizeof(reported_unavailable) - 1);
				}
				reload_requested = false;
				wait_for_interface_event();
				continue;
			}

			if (reported_unavailable[0]) {
				syslog(LOG_NOTICE, "All relay interfaces are available");
				reported_unavailable[0] = '\0';
			}
			if (start_relay_runtime(&gateways, &routes,
					setup_forwarding) < 0) {
				stop_relay_runtime();
				if (watch_interfaces && !relayd_interfaces_ready(watched_names,
						spec_count, unavailable, sizeof(unavailable)))
					continue;
				status = 4;
				goto runtime_out;
			}

			reload_requested = false;
			uloop_run();
			stop_relay_runtime();
			if (terminate_requested)
				break;
			if (!watch_interfaces) {
				status = 1;
				goto runtime_out;
			}
			if (!reload_requested) {
				syslog(LOG_WARNING, "Relay loop stopped; retrying interfaces");
				wait_for_interface_event();
			}
		}
	}
	status = 0;
	goto runtime_out;

invalid:
	status = usage(argv[0], 1);
	goto out;

runtime_out:
	stop_relay_runtime();
	if (link_watch_started)
		relayd_link_watch_done();
	relayd_core_done();
	if (uloop_started)
		uloop_done();
	if (daemon_started)
		unlink(pidfile);
out:
	free(watched_names);
	for (size_t i = 0; i < spec_count; i++)
		free(specs[i].name);
	free(specs);
	free(master_name);
	for (size_t i = 0; i < gateways.count; i++)
		free(gateways.items[i]);
	free(gateways.items);
	for (size_t i = 0; i < routes.count; i++)
		free(routes.items[i]);
	free(routes.items);
	for (size_t i = 0; i < relaydx_config.dhcpv6_lease_len; i++)
		free(relaydx_config.dhcpv6_lease[i]);
	free(relaydx_config.dhcpv6_lease);
	for (size_t i = 0; i < relaydx_config.static_ndp_len; i++)
		free(relaydx_config.static_ndp[i]);
	free(relaydx_config.static_ndp);
	free(state6_storage);
	return status;
}
