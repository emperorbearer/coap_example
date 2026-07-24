/*
 * Binding: which light(s) this switch controls, and how to reach them.
 * Records persist in NVS so bindings survive reboot / battery change.
 * See docs/coap-resource-model.md section 4.
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

/* Load persisted bindings from NVS. */
int binding_init(void);

/* Add/replace a binding at the first free slot. Returns index or negative. */
int binding_add(const struct in6_addr *target, const char *uri,
		uint8_t mode, uint8_t flags);

/* Clear all bindings (e.g. factory reset). */
void binding_clear(void);

/* Deliver a switch event to all bindings.
 *   maintained==true : send the explicit on/off state (PUT).
 *   maintained==false: send a toggle (POST), 'on' ignored.
 */
void binding_dispatch(bool maintained, bool on);

#endif /* BINDING_H_ */
