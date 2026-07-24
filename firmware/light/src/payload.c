/*
 * CBOR (de)serialization using zcbor's manual encoding/decoding API.
 * Kept deliberately small: the schema is a flat map of a few keys.
 */
#include <zcbor_encode.h>
#include <zcbor_decode.h>
#include "payload.h"

/* State map: on, bri, r, g, b, w, ct, src, seq. */
#define STATE_MAP_MAX_ENTRIES 9

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

	/* RGB is emitted as a group when any channel is non-zero or the
	 * fixture supports color (bri!=0 alone doesn't imply color). We emit
	 * r/g/b together so consumers get a consistent color triple. */
	if (state->r || state->g || state->b) {
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_R);
		ok = ok && zcbor_uint32_put(enc, state->r);
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_G);
		ok = ok && zcbor_uint32_put(enc, state->g);
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_B);
		ok = ok && zcbor_uint32_put(enc, state->b);
	}

	if (state->w) {
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_W);
		ok = ok && zcbor_uint32_put(enc, state->w);
	}

	if (state->ct) {
		ok = ok && zcbor_tstr_put_lit(enc, LIGHT_KEY_CT);
		ok = ok && zcbor_uint32_put(enc, state->ct);
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

/* Decode one 0..255 channel into *out; returns false on error. */
static bool decode_u8(zcbor_state_t *dec, uint8_t *out)
{
	uint32_t v;

	if (!zcbor_uint32_decode(dec, &v)) {
		return false;
	}
	*out = (v > LIGHT_COL_MAX) ? LIGHT_COL_MAX : (uint8_t)v;
	return true;
}

int payload_decode_update(const uint8_t *buf, size_t buf_len,
			  struct light_update *out)
{
	ZCBOR_STATE_D(dec, 1, buf, buf_len, STATE_MAP_MAX_ENTRIES, 0);

	bool got_r = false, got_g = false, got_b = false;

	*out = (struct light_update){0};

	if (!zcbor_map_start_decode(dec)) {
		return -EINVAL;
	}

	while (!zcbor_list_or_map_end(dec)) {
		struct zcbor_string key;

		if (!zcbor_tstr_decode(dec, &key)) {
			return -EINVAL;
		}

#define KEY_IS(lit) (key.len == strlen(lit) && \
		     memcmp(key.value, lit, key.len) == 0)

		if (KEY_IS(LIGHT_KEY_ON)) {
			if (!zcbor_bool_decode(dec, &out->on)) {
				return -EINVAL;
			}
			out->has_on = true;
		} else if (KEY_IS(LIGHT_KEY_BRI)) {
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
		} else if (KEY_IS(LIGHT_KEY_R)) {
			if (!decode_u8(dec, &out->r)) {
				return -EINVAL;
			}
			got_r = true;
		} else if (KEY_IS(LIGHT_KEY_G)) {
			if (!decode_u8(dec, &out->g)) {
				return -EINVAL;
			}
			got_g = true;
		} else if (KEY_IS(LIGHT_KEY_B)) {
			if (!decode_u8(dec, &out->b)) {
				return -EINVAL;
			}
			got_b = true;
		} else if (KEY_IS(LIGHT_KEY_W)) {
			if (!decode_u8(dec, &out->w)) {
				return -EINVAL;
			}
			out->has_w = true;
		} else if (KEY_IS(LIGHT_KEY_CT)) {
			uint32_t ct;

			if (!zcbor_uint32_decode(dec, &ct)) {
				return -EINVAL;
			}
			out->ct = (uint16_t)ct;
			out->has_ct = true;
		} else {
			/* Unknown key (e.g. "src"/"seq" from a peer): skip value. */
			if (!zcbor_any_skip(dec, NULL)) {
				return -EINVAL;
			}
		}
#undef KEY_IS
	}

	if (!zcbor_map_end_decode(dec)) {
		return -EINVAL;
	}

	/* RGB is only applied when the full triple is present. */
	out->has_rgb = got_r && got_g && got_b;
	return 0;
}
