/*
 * Light state: single source of truth for on/off (+ optional brightness),
 * drives the relay/PWM output, and persists across reboots via settings.
 */
#ifndef LIGHT_STATE_H_
#define LIGHT_STATE_H_

#include "coap_resources.h"
#include "payload.h"

/* Callback invoked whenever the state actually changes, so the CoAP layer
 * can notify observers. Registered by the resource module.
 */
typedef void (*light_state_changed_cb)(const struct light_state *state,
				       const char *src);

/* Initialize GPIO/PWM output and restore persisted state. */
int light_state_init(light_state_changed_cb cb);

/* Apply a (partial) update from a given source. No-op notify if unchanged. */
void light_state_apply(const struct light_update *upd, const char *src);

/* Toggle on/off from a given source. */
void light_state_toggle(const char *src);

/* Read current state (copy). */
void light_state_get(struct light_state *out);

#endif /* LIGHT_STATE_H_ */
