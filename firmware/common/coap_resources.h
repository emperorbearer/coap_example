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
#define LIGHT_KEY_BRI           "bri"
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
	uint8_t bri;      /* 0 => brightness not supported/unknown */
	uint32_t seq;     /* monotonically increasing */
};

#endif /* COAP_RESOURCES_H_ */
