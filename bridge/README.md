# CoAP → MQTT bridge

Bridges the Thread light nodes to Home Assistant. It CoAP-Observes each light's
`/light/state` and republishes to MQTT, and forwards HA commands back to the
light via CoAP `PUT /light`. Lights auto-register through HA MQTT Discovery.

Design details: `../docs/home-assistant.md`.

## Where it runs

On a host that can reach the Thread nodes over IPv6 — typically the same machine
as your **OpenThread Border Router** (e.g. a Raspberry Pi), alongside an MQTT
broker (Mosquitto) and Home Assistant.

## Setup

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
cp config.example.yaml config.yaml   # edit addresses + MQTT
python coap_mqtt_bridge.py -c config.yaml
```

## MQTT topics

| Purpose | Topic |
|---------|-------|
| Discovery | `homeassistant/light/<id>/config` (retained) |
| State | `coapbridge/light/<id>/state` |
| Command | `coapbridge/light/<id>/set` |
| Availability | `coapbridge/light/<id>/availability` |

State/command use the HA JSON light schema (`{"state":"ON","brightness":254}`);
the bridge converts to/from the light's CBOR state object.

## Notes

- Light addresses are listed in `config.yaml`. Multicast auto-discovery of
  lights is a planned enhancement (see the design doc).
- For security, put the MQTT broker behind auth/TLS; OSCORE/DTLS on the CoAP
  hop is a future hardening step.
