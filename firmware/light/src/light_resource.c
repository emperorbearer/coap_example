/*
 * CoAP resources for the light node, built on Zephyr's coap_service subsystem.
 *
 *   GET  /light        -> current state
 *   PUT  /light        -> set (partial) state
 *   POST /light        -> toggle
 *   GET  /light/state  -> current state, observable (RFC 7641)
 *
 * The observe/notify flow mirrors samples/net/sockets/coap_server. Exact helper
 * names can vary slightly between Zephyr versions; adjust to your SDK if needed.
 */
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_service.h>
#include <zephyr/logging/log.h>

#include "light_state.h"
#include "payload.h"

LOG_MODULE_REGISTER(light_resource, LOG_LEVEL_INF);

#define MAX_COAP_MSG_LEN 128
#define STATE_PAYLOAD_MAX 64

/* One CoAP service listening on the default CoAP UDP port on all interfaces. */
COAP_SERVICE_DEFINE(light_service, "0.0.0.0", &(uint16_t){COAP_PORT},
		    COAP_SERVICE_AUTOSTART);

/* Observer pool for the observable /light/state resource. */
static struct coap_observer observers[CONFIG_COAP_OBSERVER_MAX_PER_RESOURCE];

/* Forward decl of the observable resource so the change callback can notify. */
static struct coap_resource *state_resource;

/* --- helpers --- */

static int build_state_response(struct coap_packet *resp, uint8_t *buf,
				size_t buf_len, const struct coap_packet *req,
				uint8_t code, int observe_seq)
{
	uint16_t id = coap_header_get_id(req);
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint8_t tkl = coap_header_get_token(req, token);
	uint8_t payload[STATE_PAYLOAD_MAX];
	struct light_state st;
	int plen;
	int r;

	r = coap_packet_init(resp, buf, buf_len, COAP_VERSION_1,
			     COAP_TYPE_ACK, tkl, token, code, id);
	if (r < 0) {
		return r;
	}

	if (observe_seq >= 0) {
		r = coap_append_option_int(resp, COAP_OPTION_OBSERVE,
					   observe_seq);
		if (r < 0) {
			return r;
		}
	}

	r = coap_append_option_int(resp, COAP_OPTION_CONTENT_FORMAT,
				   COAP_CT_CBOR);
	if (r < 0) {
		return r;
	}

	light_state_get(&st);
	plen = payload_encode_state(&st, NULL, payload, sizeof(payload));
	if (plen < 0) {
		return plen;
	}

	r = coap_packet_append_payload_marker(resp);
	if (r < 0) {
		return r;
	}
	return coap_packet_append_payload(resp, payload, plen);
}

/* --- GET /light and GET /light/state --- */

static int light_get(struct coap_resource *resource,
		     struct coap_packet *request,
		     struct sockaddr *addr, socklen_t addr_len)
{
	uint8_t buf[MAX_COAP_MSG_LEN];
	struct coap_packet response;
	int observe = -1;
	int r;

	/* Handle observe registration/deregistration on the state resource. */
	if (resource == state_resource) {
		int opt = coap_resource_parse_observe(resource, request, addr);

		if (opt == 0) {
			struct coap_observer *o =
				coap_observer_next_unused(observers,
							  ARRAY_SIZE(observers));
			if (o) {
				coap_observer_init(o, request, addr);
				coap_register_observer(resource, o);
				observe = resource->age;
				LOG_INF("observer registered");
			}
		} else if (opt == 1) {
			struct coap_observer *o =
				coap_find_observer(observers,
						   ARRAY_SIZE(observers),
						   addr, NULL, 0);
			if (o) {
				coap_remove_observer(resource, o);
				LOG_INF("observer removed");
			}
		}
	}

	r = build_state_response(&response, buf, sizeof(buf), request,
				 COAP_RESPONSE_CODE_CONTENT, observe);
	if (r < 0) {
		return r;
	}
	return coap_resource_send(resource, &response, addr, addr_len, NULL);
}

/* --- PUT /light --- */

static int light_put(struct coap_resource *resource,
		     struct coap_packet *request,
		     struct sockaddr *addr, socklen_t addr_len)
{
	uint8_t buf[MAX_COAP_MSG_LEN];
	struct coap_packet response;
	const uint8_t *payload;
	uint16_t payload_len;
	struct light_update upd;
	uint8_t code = COAP_RESPONSE_CODE_CHANGED;

	payload = coap_packet_get_payload(request, &payload_len);
	if (payload == NULL || payload_len == 0 ||
	    payload_decode_update(payload, payload_len, &upd) != 0) {
		code = COAP_RESPONSE_CODE_BAD_REQUEST;
	} else {
		light_state_apply(&upd, LIGHT_SRC_BRIDGE);
	}

	if (build_state_response(&response, buf, sizeof(buf), request, code,
				 -1) < 0) {
		return -EINVAL;
	}
	return coap_resource_send(resource, &response, addr, addr_len, NULL);
}

/* --- POST /light (toggle) --- */

static int light_post(struct coap_resource *resource,
		      struct coap_packet *request,
		      struct sockaddr *addr, socklen_t addr_len)
{
	uint8_t buf[MAX_COAP_MSG_LEN];
	struct coap_packet response;

	light_state_toggle(LIGHT_SRC_SWITCH);

	if (build_state_response(&response, buf, sizeof(buf), request,
				 COAP_RESPONSE_CODE_CHANGED, -1) < 0) {
		return -EINVAL;
	}
	return coap_resource_send(resource, &response, addr, addr_len, NULL);
}

/* --- resource definitions --- */

static const char * const light_path[] = { COAP_URI_LIGHT, NULL };
static const char * const light_state_path[] = {
	COAP_URI_LIGHT_STATE_0, COAP_URI_LIGHT_STATE_1, NULL
};

COAP_RESOURCE_DEFINE(light, light_service, {
	.path = light_path,
	.get = light_get,
	.put = light_put,
	.post = light_post,
});

COAP_RESOURCE_DEFINE(light_state, light_service, {
	.path = light_state_path,
	.get = light_get,
});

/* --- notify observers on state change (called from light_state) --- */

static void notify_observers(const struct light_state *state, const char *src)
{
	struct coap_resource *resource = state_resource;
	struct coap_observer *o;

	if (resource == NULL) {
		return;
	}

	resource->age++;

	SYS_SLIST_FOR_EACH_CONTAINER(&resource->observers, o, list) {
		uint8_t buf[MAX_COAP_MSG_LEN];
		uint8_t payload[STATE_PAYLOAD_MAX];
		struct coap_packet notif;
		int plen;

		if (coap_packet_init(&notif, buf, sizeof(buf), COAP_VERSION_1,
				     COAP_TYPE_NON_CON, o->tkl, o->token,
				     COAP_RESPONSE_CODE_CONTENT,
				     coap_next_id()) < 0) {
			continue;
		}
		if (coap_append_option_int(&notif, COAP_OPTION_OBSERVE,
					   resource->age) < 0) {
			continue;
		}
		if (coap_append_option_int(&notif, COAP_OPTION_CONTENT_FORMAT,
					   COAP_CT_CBOR) < 0) {
			continue;
		}
		plen = payload_encode_state(state, src, payload,
					    sizeof(payload));
		if (plen < 0) {
			continue;
		}
		if (coap_packet_append_payload_marker(&notif) < 0) {
			continue;
		}
		if (coap_packet_append_payload(&notif, payload, plen) < 0) {
			continue;
		}
		coap_resource_send(resource, &notif, &o->addr,
				   sizeof(o->addr), NULL);
	}
}

void light_resource_init(void)
{
	state_resource = &light_state;
	light_state_init(notify_observers);
}
