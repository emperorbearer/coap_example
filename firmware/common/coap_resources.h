/*
 * Shared CoAP application protocol definitions for the switch and light nodes.
 * See docs/coap-resource-model.md for the full specification.
 */
#ifndef COAP_RESOURCES_H_
#define COAP_RESOURCES_H_

/* Resource URIs (no leading slash; matches Zephyr coap path arrays). */
#define COAP_URI_LIGHT          "light"
#define COAP_URI_LIGHT_STATE_0  "light"
#define COAP_URI_LIGHT_STATE_1  "state"

/* CoRE Link Format resource type used for discovery/binding. */
#define COAP_RT_LIGHT           "light.switch"

/* Content-Formats (RFC 7252 / CoAP registry). */
#define COAP_CT_JSON            50
#define COAP_CT_CBOR            60
#define COAP_CT_LINK_FORMAT     40

/* State object keys (used as CBOR map text keys and JSON keys alike). */
#define LIGHT_KEY_ON            "on"
#define LIGHT_KEY_BRI           "bri"   /* master brightness 1..254 */
#define LIGHT_KEY_R             "r"     /* red   0..255 */
#define LIGHT_KEY_G             "g"     /* green 0..255 */
#define LIGHT_KEY_B             "b"     /* blue  0..255 */
#define LIGHT_KEY_W             "w"     /* white 0..255 (RGBW fixtures) */
#define LIGHT_KEY_CT            "ct"    /* color temperature, mireds (optional) */
#define LIGHT_KEY_SRC           "src"
#define LIGHT_KEY_SEQ           "seq"

/* Change-source values for LIGHT_KEY_SRC. */
#define LIGHT_SRC_SWITCH        "switch"
#define LIGHT_SRC_BRIDGE        "bridge"
#define LIGHT_SRC_LOCAL         "local"
#define LIGHT_SRC_BOOT          "boot"

/* Brightness range (0 is unused; off is expressed via "on":false). */
#define LIGHT_BRI_MIN           1
#define LIGHT_BRI_MAX           254

/* Color channel range. */
#define LIGHT_COL_MAX           255

/* Feature flags a light may advertise (for HA color_mode mapping etc.). */
#define LIGHT_FEAT_DIM          (1u << 0) /* brightness */
#define LIGHT_FEAT_RGB          (1u << 1) /* r/g/b color */
#define LIGHT_FEAT_WHITE        (1u << 2) /* dedicated white channel (RGBW) */
#define LIGHT_FEAT_CT           (1u << 3) /* tunable white / color temp */

/* Binding limits for the switch node. */
#define SWITCH_MAX_BINDINGS     4
#define SWITCH_BINDING_URI_MAX  24

/* Binding transport mode. */
enum binding_mode {
	BINDING_MODE_PUT_STATE = 0, /* PUT /light with explicit state */
	BINDING_MODE_POST_TOGGLE = 1, /* POST /light to toggle */
};

/* Binding flags bitfield. */
#define BINDING_FLAG_CONFIRMABLE (1u << 0)

/* Logical light state shared between modules. */
struct light_state {
	bool on;
	uint8_t bri;      /* master brightness, 0 => dimming unsupported/unknown */
	uint8_t r, g, b;  /* RGB color, 0..255 */
	uint8_t w;        /* white channel, 0..255 (RGBW) */
	uint16_t ct;      /* color temperature in mireds, 0 => unused */
	uint32_t seq;     /* monotonically increasing */
};

#endif /* COAP_RESOURCES_H_ */
