"""Pure payload conversions and light config for the CoAP<->MQTT bridge.

Kept free of network dependencies (aiocoap/aiomqtt) so the conversion logic is
unit-testable on its own. See ../docs/home-assistant.md.
"""
from __future__ import annotations

import json
from dataclasses import dataclass

import cbor2

CT_CBOR = 60  # application/cbor
DISCOVERY_PREFIX = "homeassistant"
BASE_TOPIC = "coapbridge/light"


@dataclass
class LightCfg:
    id: str
    address: str            # IPv6 address or hostname of the light node
    name: str = ""
    brightness: bool = False

    @property
    def uri(self) -> str:
        return f"coap://[{self.address}]"

    def topic(self, leaf: str) -> str:
        return f"{BASE_TOPIC}/{self.id}/{leaf}"

    @property
    def config_topic(self) -> str:
        return f"{DISCOVERY_PREFIX}/light/{self.id}/config"


def coap_state_to_ha(payload: bytes) -> dict:
    """CBOR light state {"on":bool,"bri":int?} -> HA JSON light state."""
    state = cbor2.loads(payload) if payload else {}
    ha = {"state": "ON" if state.get("on") else "OFF"}
    if "bri" in state:
        ha["brightness"] = int(state["bri"])
    return ha


def ha_command_to_coap(payload: bytes) -> bytes:
    """HA JSON command -> CBOR light state for PUT /light."""
    cmd = json.loads(payload)
    out: dict = {}
    if "state" in cmd:
        out["on"] = str(cmd["state"]).upper() == "ON"
    if "brightness" in cmd:
        out["bri"] = int(cmd["brightness"])
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
    if cfg.brightness:
        conf["brightness"] = True
        conf["brightness_scale"] = 254
    return conf
