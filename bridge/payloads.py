"""Pure payload conversions and light config for the CoAP<->MQTT bridge.

Kept free of network dependencies (aiocoap/aiomqtt) so the conversion logic is
unit-testable on its own. See ../docs/home-assistant.md.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field

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
    # HA color modes this light supports, e.g. ["rgbw"], ["rgb"],
    # ["color_temp"], or a combination. Empty => no color.
    color_modes: list = field(default_factory=list)

    @property
    def uri(self) -> str:
        return f"coap://[{self.address}]"

    def topic(self, leaf: str) -> str:
        return f"{BASE_TOPIC}/{self.id}/{leaf}"

    @property
    def config_topic(self) -> str:
        return f"{DISCOVERY_PREFIX}/light/{self.id}/config"


def coap_state_to_ha(payload: bytes) -> dict:
    """CBOR light state -> HA JSON light state (with color/brightness)."""
    state = cbor2.loads(payload) if payload else {}
    ha = {"state": "ON" if state.get("on") else "OFF"}
    if "bri" in state:
        ha["brightness"] = int(state["bri"])

    if "ct" in state:
        ha["color_mode"] = "color_temp"
        ha["color_temp"] = int(state["ct"])
    elif all(k in state for k in ("r", "g", "b")):
        color = {"r": int(state["r"]), "g": int(state["g"]),
                 "b": int(state["b"])}
        if "w" in state:
            color["w"] = int(state["w"])
            ha["color_mode"] = "rgbw"
        else:
            ha["color_mode"] = "rgb"
        ha["color"] = color
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
    if "color" in cmd and isinstance(cmd["color"], dict):
        color = cmd["color"]
        for ch in ("r", "g", "b", "w"):
            if ch in color:
                out[ch] = int(color[ch])
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
    if cfg.color_modes:
        # With color modes, brightness is implied by HA; don't also set the
        # brightness flag (they are mutually exclusive in the JSON schema).
        conf["supported_color_modes"] = cfg.color_modes
        conf["brightness_scale"] = 254
    elif cfg.brightness:
        conf["brightness"] = True
        conf["brightness_scale"] = 254
    return conf
