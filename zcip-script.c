#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>

#include <libubus.h>
#include <libubox/blobmsg.h>

int
main(int argc, char **argv)
{
    if (argc != 2 || !getenv("NETIFD_INTERFACE")) {
        fprintf(stderr, "This program should only be called by zcip\n");
        return -1;
    }
    const char *reason = argv[1];

    struct blob_buf b = {};
    blobmsg_buf_init(&b);
    blobmsg_add_string(&b, "reason", reason);

    if (getenv("ip")) {
        blobmsg_add_string(&b, "ip", getenv("ip"));
    }

    struct ubus_context *ctx = ubus_connect(NULL);
	if (!ctx) {
		fprintf(stderr, "Failed to connect to ubus\n");
		return -1;
	}

    uint32_t id;
    char name[256];
    snprintf(name, sizeof(name), "network.interface.%s", getenv("NETIFD_INTERFACE"));
    int ret = ubus_lookup_id(ctx, name, &id);
	if (ret)
		return ret;

	ret = ubus_invoke(ctx, id, "notify_proto", b.head, NULL, NULL, 1000);
    if (ret != 0) {
        fprintf(stderr, "Ubus call failed: %s\n", ubus_strerror(ret));
    }

    return 0;
}

