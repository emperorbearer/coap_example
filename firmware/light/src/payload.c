/*
 * CBOR (de)serialization using zcbor's manual encoding/decoding API.
 * Kept deliberately small: the schema is a flat map of a few keys.
 */
#include <zcbor_encode.h>
#include <zcbor_decode.h>
#include "payload.h"

/* A state map has at most 4 entries (on, bri, src, seq). */
#define STATE_MAP_MAX_ENTRIES 4

int payload_encode_state(const struct light_state *state, const char *src,
			 uint8_t *buf, size_t buf_len)
{
	ZCBOR_STATE_E(enc, 1, buf, buf_len, 1);
	bool ok = true;

	ok = ok && zcbor_map_start_encode(enc, STATE_MAP_MAX_ENTRIES);

	ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_ON);
	ok = ok && zcbor_bool_put(enc, state->on);

	if (state->bri != 0) {
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_BRI);
		ok = ok && zcbor_uint32_put(enc, state->bri);
	}

	if (src != NULL) {
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_SRC);
		ok = ok && zcbor_tstr_put_term(enc, (char *)src, 16);
	}

	ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_SEQ);
	ok = ok && zcbor_uint32_put(enc, state->seq);

	ok = ok && zcbor_map_end_encode(enc, STATE_MAP_MAX_ENTRIES);

	if (!ok) {
		return -ENOMEM;
	}
	return (int)(enc->payload - buf);
}

int payload_decode_update(const uint8_t *buf, size_t buf_len,
			  struct light_update *out)
{
	ZCBOR_STATE_D(dec, 1, buf, buf_len, STATE_MAP_MAX_ENTRIES, 0);

	*out = (struct light_update){0};

	if (!zcbor_map_start_decode(dec)) {
		return -EINVAL;
	}

	while (!zcbor_list_or_map_end(dec)) {
		struct zcbor_string key;

		if (!zcbor_tstr_decode(dec, &key)) {
			return -EINVAL;
		}

		if (key.len == strlen(LIGHT_KEY_ON) &&
		    memcmp(key.value, LIGHT_KEY_ON, key.len) == 0) {
			if (!zcbor_bool_decode(dec, &out->on)) {
				return -EINVAL;
			}
			out->has_on = true;
		} else if (key.len == strlen(LIGHT_KEY_BRI) &&
			   memcmp(key.value, LIGHT_KEY_BRI, key.len) == 0) {
			uint32_t bri;

			if (!zcbor_uint32_decode(dec, &bri)) {
				return -EINVAL;
			}
			if (bri < LIGHT_BRI_MIN) {
				bri = LIGHT_BRI_MIN;
			}
			if (bri > LIGHT_BRI_MAX) {
				bri = LIGHT_BRI_MAX;
			}
			out->bri = (uint8_t)bri;
			out->has_bri = true;
		} else {
			/* Unknown key (e.g. "src"/"seq" from a peer): skip value. */
			if (!zcbor_any_skip(dec, NULL)) {
				return -EINVAL;
			}
		}
	}

	if (!zcbor_map_end_decode(dec)) {
		return -EINVAL;
	}
	return 0;
}
