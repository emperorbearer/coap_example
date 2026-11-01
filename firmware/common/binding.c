#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zcbor_encode.h>

#include "binding.h"

LOG_MODULE_REGISTER(binding, LOG_LEVEL_INF);

static struct binding_record bindings[SWITCH_MAX_BINDINGS];
static struct coap_client coap_client;

#define BINDING_SETTINGS_ROOT "sw"
#define CMD_MAP_MAX_ENTRIES   5

/* --- persistence --- */

static int binding_settings_set(const char *name, size_t len,
				settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "bind", &next) && next) {
		int idx = atoi(next);

		if (idx < 0 || idx >= SWITCH_MAX_BINDINGS) {
			return -EINVAL;
		}
		if (read_cb(cb_arg, &bindings[idx], sizeof(bindings[idx])) > 0) {
			return 0;
		}
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(binding, BINDING_SETTINGS_ROOT, NULL,
			      binding_settings_set, NULL, NULL);

static void binding_persist(int idx)
{
	char key[32];

	snprintk(key, sizeof(key), BINDING_SETTINGS_ROOT "/bind/%d", idx);
	settings_save_one(key, &bindings[idx], sizeof(bindings[idx]));
}

int binding_init(void)
{
	int err = coap_client_init(&coap_client, NULL);

	if (err) {
		LOG_ERR("coap_client_init: %d", err);
		return err;
	}
	settings_subsys_init();
	settings_load_subtree(BINDING_SETTINGS_ROOT);
	return 0;
}

int binding_add(const struct in6_addr *target, const char *uri,
		uint8_t mode, uint8_t flags)
{
	for (int i = 0; i < SWITCH_MAX_BINDINGS; i++) {
		if (!bindings[i].in_use) {
			bindings[i].in_use = true;
			bindings[i].target = *target;
			strncpy(bindings[i].uri, uri ? uri : COAP_URI_LIGHT,
				sizeof(bindings[i].uri) - 1);
			bindings[i].mode = mode;
			bindings[i].flags = flags;
			binding_persist(i);
			return i;
		}
	}
	return -ENOMEM;
}

void binding_clear(void)
{
	for (int i = 0; i < SWITCH_MAX_BINDINGS; i++) {
		memset(&bindings[i], 0, sizeof(bindings[i]));
		binding_persist(i);
	}
}

/* --- encoding --- */

/* Encode a light_cmd into a CBOR state map. Returns length or negative. */
static int encode_cmd(const struct light_cmd *cmd, uint8_t *buf, size_t buf_len)
{
	ZCBOR_STATE_E(enc, 1, buf, buf_len, 1);
	bool ok = true;

	ok = ok && zcbor_map_start_encode(enc, CMD_MAP_MAX_ENTRIES);

	if (cmd->has_on) {
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_ON);
		ok = ok && zcbor_bool_put(enc, cmd->on);
	}
	if (cmd->has_bri) {
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_BRI);
		ok = ok && zcbor_uint32_put(enc, cmd->bri);
	}
	if (cmd->has_ct) {
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_CT);
		ok = ok && zcbor_uint32_put(enc, cmd->ct);
	}
	ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_SRC);
	ok = ok && zcbor_tstr_put_lit(enc, LIGHT_SRC_SWITCH);

	ok = ok && zcbor_map_end_encode(enc, CMD_MAP_MAX_ENTRIES);

	return ok ? (int)(enc->payload - buf) : -ENOMEM;
}

/* --- sending --- */

static void coap_response_cb(int16_t code, size_t offset, const uint8_t *payload,
			     size_t len, bool last_block, void *user_data)
{
	ARG_UNUSED(offset);
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
	ARG_UNUSED(last_block);
	ARG_UNUSED(user_data);

	if (code < 0) {
		LOG_WRN("CoAP request failed: %d", code);
	} else {
		LOG_INF("CoAP response: %d.%02d", code >> 5, code & 0x1f);
	}
}

static void send_to_binding(struct binding_record *b,
			    const struct light_cmd *cmd)
{
	int sock;
	struct sockaddr_in6 dst = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(COAP_PORT),
		.sin6_addr = b->target,
	};
	uint8_t payload[48];
	int plen;
	bool toggle = cmd->toggle || b->mode == BINDING_MODE_POST_TOGGLE;
	struct coap_client_request req = {
		.confirmable = (b->flags & BINDING_FLAG_CONFIRMABLE) != 0,
		.path = b->uri,
		.cb = coap_response_cb,
	};

	if (toggle) {
		req.method = COAP_METHOD_POST;
	} else {
		plen = encode_cmd(cmd, payload, sizeof(payload));
		if (plen < 0) {
			return;
		}
		req.method = COAP_METHOD_PUT;
		req.fmt = COAP_CONTENT_FORMAT_APP_CBOR;
		req.payload = payload;
		req.len = plen;
	}

	sock = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("socket: %d", errno);
		return;
	}
	if (zsock_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) == 0) {
		(void)coap_client_req(&coap_client, sock,
				      (struct sockaddr *)&dst, &req, NULL);
	} else {
		LOG_ERR("connect: %d", errno);
	}
	zsock_close(sock);
}

void binding_send(const struct light_cmd *cmd)
{
	for (int i = 0; i < SWITCH_MAX_BINDINGS; i++) {
		if (bindings[i].in_use) {
			send_to_binding(&bindings[i], cmd);
		}
	}
}

void binding_dispatch(bool maintained, bool on)
{
	struct light_cmd cmd = {0};

	if (maintained) {
		cmd.has_on = true;
		cmd.on = on;
	} else {
		cmd.toggle = true;
	}
	binding_send(&cmd);
}
