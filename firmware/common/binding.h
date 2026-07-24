/*
 * Binding: which light(s) a switch controls, and how to reach them.
 * Shared by the inner on/off switch and the panel (buttons + encoder) switch.
 * Records persist in NVS. See docs/coap-resource-model.md section 4.
 */
#ifndef BINDING_H_
#define BINDING_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/net/net_ip.h>
#include "coap_resources.h"

struct binding_record {
	bool in_use;
	struct in6_addr target;              /* light unicast or group address */
	char uri[SWITCH_BINDING_URI_MAX];    /* target resource, default "light" */
	uint8_t mode;                        /* enum binding_mode */
	uint8_t flags;                       /* BINDING_FLAG_* */
};

/* A command to send to bound lights. Only fields with their has_* flag set
 * (or 'toggle') are included in the request. */
struct light_cmd {
	bool toggle;             /* POST toggle instead of PUT state */
	bool has_on;  bool on;
	bool has_bri; uint8_t bri;
	bool has_rgb; uint8_t r, g, b;
	bool has_w;   uint8_t w;
	bool has_ct;  uint16_t ct;
};

/* Load persisted bindings and init the CoAP client. */
int binding_init(void);

/* Add/replace a binding at the first free slot. Returns index or negative. */
int binding_add(const struct in6_addr *target, const char *uri,
		uint8_t mode, uint8_t flags);

/* Clear all bindings (factory reset). */
void binding_clear(void);

/* Send a command to every configured binding. */
void binding_send(const struct light_cmd *cmd);

/* Convenience for the inner on/off switch.
 *   maintained==true : send explicit on/off state (PUT).
 *   maintained==false: send a toggle (POST); 'on' ignored. */
void binding_dispatch(bool maintained, bool on);

#endif /* BINDING_H_ */
