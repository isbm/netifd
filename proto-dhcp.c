#include <string.h>

#include "netifd.h"
#include "proto.h"
#include "interface.h"
#include "interface-ip.h"
#include "device.h"
#include "system.h"

struct dhcp_proto_state {
	struct interface_proto_state proto;

	struct blob_attr *config;

	bool dhcpv6;
	int dad_wait_time;

	enum {
		DHCP_IDLE,
		DHCP_SETTING_UP,
		DHCP_DONE
	} state;

	struct netifd_process client;
};

enum {
	NOTIFY_REASON,
	NOTIFY_IP,
	NOTIFY_SUBNET,
	NOTIFY_ROUTER,
	NOTIFY_DNS,
	NOTIFY_MTU,
	NOTIFY_IP6,
	NOTIFY_IP6_PREFIXLEN,
	NOTIFY_IP6_PREFERRED,
	NOTIFY_IP6_VALID,
	NOTIFY_IP6_DNS,
	__NOTIFY_LAST
};

static const struct blobmsg_policy notify_attr[__NOTIFY_LAST] = {
	[NOTIFY_REASON] = { .name = "reason", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_IP] = { .name = "new_ip_address", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_SUBNET] = { .name = "new_subnet_mask", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_ROUTER] = { .name = "new_routers", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_DNS] = { .name = "new_domain_name_servers", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_MTU] = { .name = "new_interface_mtu", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_IP6] = { .name = "new_ip6_address", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_IP6_PREFIXLEN] = { .name = "new_ip6_prefixlen", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_IP6_PREFERRED] = { .name = "new_preferred_life", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_IP6_VALID] = { .name = "new_max_life", .type = BLOBMSG_TYPE_STRING },
	[NOTIFY_IP6_DNS] = { .name = "new_dhcp6_name_servers", .type = BLOBMSG_TYPE_STRING },
};

enum {
	OPT_DAD_WAIT_TIME,
	__OPT_MAX,
};

static const struct blobmsg_policy proto_dhcp_attributes[__OPT_MAX] = {
	[OPT_DAD_WAIT_TIME] = { .name = "dad_wait_time", .type = BLOBMSG_TYPE_INT32},
};


const struct uci_blob_param_list proto_dhcp_attr = {
	.n_params = __OPT_MAX,
	.params = proto_dhcp_attributes
};

static void
dhcp_process_callback(struct netifd_process *proc, int ret)
{

}

static void
start_dhcp_client(struct dhcp_proto_state *state)
{
	char leasefile[128];
	char conffile[128];

	snprintf(leasefile, sizeof(leasefile), "/var/run/udhcp-%s.lease", state->proto.iface->name);
	snprintf(conffile, sizeof(conffile), "/var/run/udhcp-%s.conf", state->proto.iface->name);

	FILE *f = fopen(conffile, "w");
	if (state->dhcpv6)  {
		fprintf(f, "request dhcp6.name-servers;\n");
	} else {
		fprintf(f, "request subnet-mask, broadcast-address, routers, domain-name-servers, interface-mtu;\n");
	}
	fclose(f);

	char iface[256];
	snprintf(iface, sizeof(iface), "NETIFD_INTERFACE=%s", state->proto.iface->name);
	char dad_wait[10];
	snprintf(dad_wait, sizeof(dad_wait), "%d", state->dad_wait_time);

	const char *argv[] = {
		"/sbin/dhclient",
		state->dhcpv6 ? "-6" : "-4",
		"-d",
		"-q",
		"-pf", "/dev/null",
		"-lf", leasefile,
		"-cf", conffile,
		"-sf", "/usr/libexec/netifd/dhcp-script",
		"--dad-wait-time", dad_wait,
		"-e",
		iface,
		state->proto.iface->main_dev.dev->ifname,
		NULL
	};

	netifd_start_process(argv, NULL, &state->client);
}

static int
dhcp_proto_setup(struct dhcp_proto_state *state)
{
	struct interface *iface = state->proto.iface;
	struct device *dev = state->proto.iface->main_dev.dev;
	interface_set_l3_dev(iface, dev);

	struct blob_attr *tb[__OPT_MAX];
	blobmsg_parse(proto_dhcp_attributes, __OPT_MAX, tb, blob_data(state->config), blob_len(state->config));

	state->dad_wait_time = tb[OPT_DAD_WAIT_TIME] ? blobmsg_get_u32(tb[OPT_DAD_WAIT_TIME]) : 5;

	state->state = DHCP_SETTING_UP;

	start_dhcp_client(state);

	return 0;
}

static int
dhcp_handler(struct interface_proto_state *proto,
	       enum interface_proto_cmd cmd, bool force)
{
	int ret = 0;

	struct dhcp_proto_state *state = container_of(proto, struct dhcp_proto_state, proto);

	switch (cmd) {
	case PROTO_CMD_SETUP:
		ret = dhcp_proto_setup(state);
		break;
	case PROTO_CMD_TEARDOWN:
	case PROTO_CMD_RENEW:
		break;
	}

	return ret;
}

static void
dhcp_set_dns_servers(struct interface *iface, struct blob_attr *attr)
{
	if (attr == NULL)
		return;

	struct blob_buf b = {};
	blobmsg_buf_init(&b);

	char *dnss = blobmsg_get_string(attr);
	char *saveptr = NULL;
	for (int i = 0; ; i++) {
		char *dns = strtok_r(i == 0 ? dnss : NULL, " ", &saveptr);
		if (dns == NULL)
			break;

		blobmsg_add_string(&b, "dns", dns);
	}
	interface_add_dns_server_list(&iface->proto_ip, b.head);
	blob_buf_free(&b);
}

static int
dhcp4_configure(struct dhcp_proto_state *state, const char *action, struct blob_attr **tb)
{
	struct interface *iface = state->proto.iface;
	if (strcmp(action, "EXPIRE") == 0) {
		interface_update_start(iface, false);
		interface_update_complete(iface);
	} else if (strcmp(action, "BOUND") == 0 || strcmp(action, "RENEW") == 0 || strcmp(action, "REBIND") == 0 || strcmp(action, "REBOOT") == 0) {
		interface_update_start(iface, false);

		struct blob_buf b = {};
		blobmsg_buf_init(&b);
		void *cookie = blobmsg_open_array(&b, "ipaddr");
		void *cookie2 = blobmsg_open_table(&b, "ipaddr");
		if (tb[NOTIFY_IP])
			blobmsg_add_string(&b, "ipaddr", blobmsg_get_string(tb[NOTIFY_IP]));
		if (tb[NOTIFY_SUBNET])
			blobmsg_add_string(&b, "mask", blobmsg_get_string(tb[NOTIFY_SUBNET]));
		blobmsg_close_table(&b, cookie2);
		blobmsg_close_array(&b, cookie);
		proto_apply_ip_settings(iface, b.head, false);
		blob_buf_free(&b);

		if (tb[NOTIFY_ROUTER]) {
			char *routers = blobmsg_get_string(tb[NOTIFY_ROUTER]);
			char *saveptr = NULL;
			for (int i = 0; ; i++) {
				char *router = strtok_r(i == 0 ? routers : NULL, " ", &saveptr);
				if (router == NULL) break;

				blobmsg_buf_init(&b);
				blobmsg_add_string(&b, "target", "0.0.0.0");
				blobmsg_add_string(&b, "netmask", "0.0.0.0");
				blobmsg_add_string(&b, "gateway", router);
				blobmsg_add_string(&b, "source", blobmsg_get_string(tb[NOTIFY_IP]));
				blobmsg_add_u32(&b, "metric", i + 1);

				interface_ip_add_route(iface, b.head, false);
				blob_buf_free(&b);
			}
		}

		dhcp_set_dns_servers(iface, tb[NOTIFY_DNS]);

		if (tb[NOTIFY_MTU]) {
			char * end;
			const char *smtu = blobmsg_get_string(tb[NOTIFY_MTU]);
			int mtu = strtol(smtu, &end, 10);
			if (end[0] == '\0' && mtu > 58 && mtu <= 65535) {
				iface->l3_dev.dev->settings.mtu = mtu;
				iface->l3_dev.dev->settings.flags |= DEV_OPT_MTU;
				system_if_apply_settings(iface->l3_dev.dev, &iface->l3_dev.dev->settings, DEV_OPT_MTU);
			}
		}

		interface_update_complete(iface);

		state->state = DHCP_DONE;
		state->proto.proto_event(&state->proto, IFPEV_UP);
	}
	return 0;
}

static int
dhcp6_configure(struct dhcp_proto_state *state, const char *action, struct blob_attr **tb)
{
	struct interface *iface = state->proto.iface;
	if (strcmp(action, "DEREF6") == 0 || strcmp(action, "EXPIRE6") == 0 || strcmp(action, "RELEASE6") == 0 || strcmp(action, "STOP6") == 0) {
		interface_update_start(iface, false);
		interface_update_complete(iface);
	} else if (strcmp(action, "BOUND6") == 0 || strcmp(action, "RENEW6") == 0 || strcmp(action, "REBIND6") == 0) {
		interface_update_start(iface, false);

		struct blob_buf b = {};
		blobmsg_buf_init(&b);
		void *cookie = blobmsg_open_array(&b, "ip6addr");
		void *cookie2 = blobmsg_open_table(&b, "ip6addr");
		if (tb[NOTIFY_IP6])
			blobmsg_add_string(&b, "ipaddr", blobmsg_get_string(tb[NOTIFY_IP6]));
		if (tb[NOTIFY_IP6_PREFIXLEN])
			blobmsg_add_string(&b, "mask", blobmsg_get_string(tb[NOTIFY_IP6_PREFIXLEN]));
		if (tb[NOTIFY_IP6_PREFERRED]) {
			char *end;
			int preferred = strtol(blobmsg_get_string(tb[NOTIFY_IP6_PREFERRED]), &end, 10);
			if (end && *end == '\0') {
				blobmsg_add_u32(&b, "preferred", preferred);
			}
		}
		if (tb[NOTIFY_IP6_VALID]) {
			char *end;
			int valid = strtol(blobmsg_get_string(tb[NOTIFY_IP6_VALID]), &end, 10);
			if (end && *end == '\0') {
				blobmsg_add_u32(&b, "valid", valid);
			}
		}
		blobmsg_close_table(&b, cookie2);
		blobmsg_close_array(&b, cookie);
		proto_apply_ip_settings(iface, b.head, false);
		blob_buf_free(&b);

		dhcp_set_dns_servers(iface, tb[NOTIFY_IP6_DNS]);

		interface_update_complete(iface);

		state->state = DHCP_DONE;
		state->proto.proto_event(&state->proto, IFPEV_UP);
	}
	return 0;
}

static int
dhcp_notify(struct interface_proto_state *proto, struct blob_attr *attr)
{
	struct dhcp_proto_state *state = container_of(proto, struct dhcp_proto_state, proto);

	struct blob_attr *tb[__NOTIFY_LAST];
	blobmsg_parse(notify_attr, __NOTIFY_LAST, tb, blob_data(attr), blob_len(attr));

	if (!tb[NOTIFY_REASON]) {
		return UBUS_STATUS_INVALID_ARGUMENT;
	}
	char *action = blobmsg_get_string(tb[NOTIFY_REASON]);

	if (state->dhcpv6) {
		return dhcp6_configure(state, action, tb);
	} else {
		return dhcp4_configure(state, action, tb);
	}
}

static void
dhcp_free(struct interface_proto_state *proto)
{
	struct dhcp_proto_state *state;

	state = container_of(proto, struct dhcp_proto_state, proto);
	free(state);
}

static struct proto_handler dhcp_proto6;

static struct interface_proto_state *
dhcp_attach(const struct proto_handler *h, struct interface *iface,
	      struct blob_attr *attr)
{
	struct dhcp_proto_state *state;

	state = calloc(1, sizeof(*state));
	if (!state)
		return NULL;

	state->config = malloc(blob_pad_len(attr));
	if (!state->config) {
		free(state);
		return NULL;
	}
	memcpy(state->config, attr, blob_pad_len(attr));

	state->proto.free = dhcp_free;
	state->proto.cb = dhcp_handler;
	state->proto.notify = dhcp_notify;

	state->dhcpv6 = h == &dhcp_proto6;

	state->client.cb = dhcp_process_callback;
	state->client.log_prefix = iface->name;
	state->state = DHCP_IDLE;

	return &state->proto;
}

static struct proto_handler dhcp_proto = {
	.name = "dhcp",
	.flags = PROTO_FLAG_RENEW_AVAILABLE,
	.config_params = &proto_dhcp_attr,
	.attach = dhcp_attach,
};

static struct proto_handler dhcp_proto6 = {
	.name = "dhcp6",
	.flags = 0,
	.config_params = &proto_dhcp_attr,
	.attach = dhcp_attach,
};

static void __init
dhcp_proto_init(void)
{
	add_proto_handler(&dhcp_proto);
	add_proto_handler(&dhcp_proto6);
}
