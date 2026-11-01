"""Unit tests for the pure payload conversions (no network deps needed).

Run directly:  python3 tests/test_payloads.py
Or with pytest: pytest bridge/tests
"""
import json
import os
import sys

import cbor2

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from payloads import (  # noqa: E402
    LightCfg,
    coap_state_to_ha,
    discovery_config,
    ha_command_to_coap,
)


def test_coap_state_to_ha_on_off():
    assert coap_state_to_ha(cbor2.dumps({"on": True})) == {"state": "ON"}
    assert coap_state_to_ha(cbor2.dumps({"on": False})) == {"state": "OFF"}


def test_coap_state_to_ha_brightness():
    ha = coap_state_to_ha(cbor2.dumps({"on": True, "bri": 200}))
    assert ha == {"state": "ON", "brightness": 200}


def test_coap_state_empty_payload():
    assert coap_state_to_ha(b"") == {"state": "OFF"}


def test_ha_command_to_coap_on():
    out = cbor2.loads(ha_command_to_coap(json.dumps({"state": "ON"}).encode()))
    assert out["on"] is True
    assert out["src"] == "bridge"


def test_ha_command_to_coap_brightness():
    out = cbor2.loads(
        ha_command_to_coap(json.dumps({"state": "ON", "brightness": 128}).encode())
    )
    assert out["on"] is True
    assert out["bri"] == 128


def test_roundtrip_state_command():
    # HA turns light on at brightness 254 -> device CBOR -> back to HA state.
    coap = ha_command_to_coap(json.dumps({"state": "ON", "brightness": 254}).encode())
    ha = coap_state_to_ha(coap)
    assert ha == {"state": "ON", "brightness": 254}


def test_coap_state_to_ha_color_temp():
    ha = coap_state_to_ha(cbor2.dumps({"on": True, "bri": 200, "ct": 300}))
    assert ha["state"] == "ON"
    assert ha["brightness"] == 200
    assert ha["color_mode"] == "color_temp"
    assert ha["color_temp"] == 300


def test_ha_command_color_temp_to_coap():
    body = json.dumps({"state": "ON", "brightness": 128, "color_temp": 250})
    out = cbor2.loads(ha_command_to_coap(body.encode()))
    assert out["on"] is True
    assert out["bri"] == 128
    assert out["ct"] == 250


def test_roundtrip_tunable_white():
    cmd = json.dumps({"state": "ON", "brightness": 200, "color_temp": 320})
    ha = coap_state_to_ha(ha_command_to_coap(cmd.encode()))
    assert ha["state"] == "ON"
    assert ha["brightness"] == 200
    assert ha["color_mode"] == "color_temp"
    assert ha["color_temp"] == 320


def test_discovery_config_onoff_dimmable_ct():
    onoff = discovery_config(LightCfg(id="a", address="fd00::1"))
    assert "brightness" not in onoff
    assert "supported_color_modes" not in onoff
    assert onoff["state_topic"] == "coapbridge/light/a/state"
    assert onoff["schema"] == "json"

    dim = discovery_config(LightCfg(id="b", address="fd00::2", brightness=True))
    assert dim["brightness"] is True
    assert dim["brightness_scale"] == 254

    ct = discovery_config(
        LightCfg(id="c", address="fd00::3", color_temp=True))
    assert ct["supported_color_modes"] == ["color_temp"]
    assert "brightness" not in ct  # implied by color_temp mode
    assert ct["brightness_scale"] == 254
    assert ct["min_mireds"] == 153
    assert ct["max_mireds"] == 370


def _run_all():
    fns = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for fn in fns:
        fn()
        print(f"ok  {fn.__name__}")
    print(f"\n{len(fns)} tests passed")


if __name__ == "__main__":
    _run_all()
