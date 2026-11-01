"""Pure payload conversions and light config for the CoAP<->MQTT bridge.

Kept free of network dependencies (aiocoap/aiomqtt) so the conversion logic is
unit-testable on its own. The light is tunable white: brightness + color
temperature (mireds). See ../docs/home-assistant.md.
"""
from __future__ import annotations

import json
from dataclasses import dataclass

import cbor2

CT_CBOR = 60  # application/cbor
DISCOVERY_PREFIX = "homeassistant"
BASE_TOPIC = "coapbridge/light"

# Color-temperature range in mireds (matches firmware/common/coap_resources.h).
CT_MIN_MIREDS = 153  # ~6500 K
CT_MAX_MIREDS = 370  # ~2700 K


@dataclass
class LightCfg:
    id: str
    address: str            # IPv6 address or hostname of the light node
    name: str = ""
    brightness: bool = False
    color_temp: bool = False  # tunable white (color temperature) support

    @property
    def uri(self) -> str:
        return f"coap://[{self.address}]"

    def topic(self, leaf: str) -> str:
        return f"{BASE_TOPIC}/{self.id}/{leaf}"

    @property
    def config_topic(self) -> str:
        return f"{DISCOVERY_PREFIX}/light/{self.id}/config"


def coap_state_to_ha(payload: bytes) -> dict:
    """CBOR light state -> HA JSON light state (brightness + color temp)."""
    state = cbor2.loads(payload) if payload else {}
    ha = {"state": "ON" if state.get("on") else "OFF"}
    if "bri" in state:
        ha["brightness"] = int(state["bri"])
    if "ct" in state:
        ha["color_mode"] = "color_temp"
        ha["color_temp"] = int(state["ct"])
    return ha


def ha_command_to_coap(payload: bytes) -> bytes:
    """HA JSON command -> CBOR light state for PUT /light."""
    cmd = json.loads(payload)
    out: dict = {}
    if "state" in cmd:
        out["on"] = str(cmd["state"]).upper() == "ON"
    if "brightness" in cmd:
        out["bri"] = int(cmd["brightness"])
    if "color_temp" in cmd:
        out["ct"] = int(cmd["color_temp"])
    out["src"] = "bridge"
    return cbor2.dumps(out)


def discovery_config(cfg: LightCfg) -> dict:
    name = cfg.name or f"CoAP Light {cfg.id}"
    conf = {
        "name": name,
        "unique_id": f"coap_light_{cfg.id}",
        "schema": "json",
        "state_topic": cfg.topic("state"),
        "command_topic": cfg.topic("set"),
        "availability_topic": cfg.topic("availability"),
        "device": {
            "identifiers": [f"coap_light_{cfg.id}"],
            "manufacturer": "coap-thread-switch-light",
            "model": "Light Node",
        },
    }
    if cfg.color_temp:
        # color_temp mode implies brightness in HA's JSON schema.
        conf["supported_color_modes"] = ["color_temp"]
        conf["brightness_scale"] = 254
        conf["min_mireds"] = CT_MIN_MIREDS
        conf["max_mireds"] = CT_MAX_MIREDS
    elif cfg.brightness:
        conf["brightness"] = True
        conf["brightness_scale"] = 254
    return conf
