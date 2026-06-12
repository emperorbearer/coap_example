#!/usr/bin/env python3
"""Bridge the switch device to the light device through the Leshan server.

Observes the switch endpoint's IPSO On/Off Switch state (3342/0/5500) via
the Leshan server-demo REST API and mirrors every change to the light
endpoint's IPSO Light Control On/Off resource (3311/0/5850).

Both devices only ever talk to Leshan; this script is the server-side glue
that makes the switch control the light.

Usage:
    python3 leshan_bridge.py --leshan http://localhost:8080 \
        --switch mgm240-switch --light mgm240-light

Requires only the Python standard library. Tested against the Leshan
server-demo 2.x REST API.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

SWITCH_PATH = "/3342/0/5500"   # IPSO On/Off Switch - Digital Input State
LIGHT_PATH = "/3311/0/5850"    # IPSO Light Control - On/Off


def api(leshan: str, method: str, path: str, body: dict | None = None):
    """Send one request to the Leshan REST API and return the parsed JSON."""
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(
        f"{leshan}/api{path}", data=data, method=method,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        payload = resp.read()
    return json.loads(payload) if payload else None


def write_light(leshan: str, light_ep: str, on: bool) -> None:
    result = api(leshan, "PUT", f"/clients/{light_ep}{LIGHT_PATH}", {
        "id": 5850,
        "kind": "singleResource",
        "type": "boolean",
        "value": on,
    })
    status = (result or {}).get("status", "unknown")
    print(f"[bridge] light '{light_ep}' <- {'ON' if on else 'OFF'} ({status})")


def extract_bool(node) -> bool | None:
    """Pull a boolean value out of the various JSON shapes Leshan emits."""
    if isinstance(node, bool):
        return node
    if isinstance(node, str):
        if node.lower() in ("true", "false"):
            return node.lower() == "true"
        return None
    if isinstance(node, dict):
        for key in ("value", "val", "content"):
            if key in node:
                found = extract_bool(node[key])
                if found is not None:
                    return found
    return None


def start_observe(leshan: str, switch_ep: str) -> None:
    api(leshan, "POST", f"/clients/{switch_ep}{SWITCH_PATH}/observe")
    print(f"[bridge] observing {switch_ep}{SWITCH_PATH}")


def listen(leshan: str, switch_ep: str, light_ep: str) -> None:
    """Consume Leshan's SSE stream and react to switch notifications."""
    req = urllib.request.Request(
        f"{leshan}/api/event?ep={switch_ep}",
        headers={"Accept": "text/event-stream"},
    )
    last_state = None

    with urllib.request.urlopen(req) as stream:
        event_name = None
        for raw in stream:
            line = raw.decode("utf-8", errors="replace").strip()
            if line.startswith("event:"):
                event_name = line.split(":", 1)[1].strip()
            elif line.startswith("data:") and event_name == "NOTIFICATION":
                data = json.loads(line.split(":", 1)[1].strip())
                if data.get("res") != SWITCH_PATH:
                    continue
                state = extract_bool(data.get("val"))
                if state is None or state == last_state:
                    continue
                last_state = state
                print(f"[bridge] switch '{switch_ep}' -> {'ON' if state else 'OFF'}")
                write_light(leshan, light_ep, state)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leshan", default="http://localhost:8080",
                        help="Leshan server-demo web URL")
    parser.add_argument("--switch", default="mgm240-switch",
                        help="endpoint name of the switch device")
    parser.add_argument("--light", default="mgm240-light",
                        help="endpoint name of the light device")
    args = parser.parse_args()

    while True:
        try:
            start_observe(args.leshan, args.switch)
            listen(args.leshan, args.switch, args.light)
        except KeyboardInterrupt:
            return 0
        except (urllib.error.URLError, urllib.error.HTTPError, OSError) as err:
            print(f"[bridge] connection problem: {err}; retrying in 5 s",
                  file=sys.stderr)
            time.sleep(5)


if __name__ == "__main__":
    sys.exit(main())
