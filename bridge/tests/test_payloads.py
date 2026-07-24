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


def test_discovery_config_onoff_vs_dimmable():
    onoff = discovery_config(LightCfg(id="a", address="fd00::1"))
    assert "brightness" not in onoff
    assert onoff["state_topic"] == "coapbridge/light/a/state"
    assert onoff["schema"] == "json"

    dim = discovery_config(LightCfg(id="b", address="fd00::2", brightness=True))
    assert dim["brightness"] is True
    assert dim["brightness_scale"] == 254


def _run_all():
    fns = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for fn in fns:
        fn()
        print(f"ok  {fn.__name__}")
    print(f"\n{len(fns)} tests passed")


if __name__ == "__main__":
    _run_all()
