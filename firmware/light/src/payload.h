/*
 * CBOR (de)serialization of the light state object.
 * Schema: { "on": bool, "bri": uint (optional), "src": tstr (optional),
 *           "seq": uint (optional) }.  See docs/coap-resource-model.md.
 */
#ifndef PAYLOAD_H_
#define PAYLOAD_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "coap_resources.h"

/* Result of decoding a (possibly partial) PUT payload. Flags mark which
 * fields were actually present so the handler can do a partial update.
 */
struct light_update {
	bool has_on;
	bool on;
	bool has_bri;
	uint8_t bri;
	bool has_rgb;        /* r,g,b all present */
	uint8_t r, g, b;
	bool has_w;
	uint8_t w;
	bool has_ct;
	uint16_t ct;
};

/* Encode full state -> CBOR into buf. Returns bytes written or negative errno. */
int payload_encode_state(const struct light_state *state, const char *src,
			 uint8_t *buf, size_t buf_len);

/* Decode a PUT payload into an update descriptor. Returns 0 or negative errno. */
int payload_decode_update(const uint8_t *buf, size_t buf_len,
			  struct light_update *out);

#endif /* PAYLOAD_H_ */
