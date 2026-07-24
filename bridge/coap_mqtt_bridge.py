#!/usr/bin/env python3
"""CoAP <-> MQTT bridge for the Thread light nodes.

For each configured light the bridge:
  * CoAP-Observes  coap://[addr]/light/state   -> publishes HA state (MQTT)
  * subscribes     coapbridge/light/<id>/set   -> CoAP PUT coap://[addr]/light

Lights are exposed to Home Assistant via MQTT Discovery (JSON schema light).
See ../docs/home-assistant.md for the design.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import logging

import yaml
from aiocoap import Context, Message, GET, PUT
import aiomqtt

from payloads import (
    BASE_TOPIC,
    CT_CBOR,
    LightCfg,
    coap_state_to_ha,
    discovery_config,
    ha_command_to_coap,
)

LOG = logging.getLogger("coap_mqtt_bridge")


# ---- bridge --------------------------------------------------------------

class Bridge:
    def __init__(self, cfg: dict):
        self.mqtt_cfg = cfg.get("mqtt", {})
        self.lights = [LightCfg(**light) for light in cfg["lights"]]
        self.coap: Context | None = None
        self.mqtt: aiomqtt.Client | None = None

    async def run(self) -> None:
        self.coap = await Context.create_client_context()
        async with aiomqtt.Client(
            hostname=self.mqtt_cfg.get("host", "localhost"),
            port=self.mqtt_cfg.get("port", 1883),
            username=self.mqtt_cfg.get("username"),
            password=self.mqtt_cfg.get("password"),
            will=aiomqtt.Will(f"{BASE_TOPIC}/bridge/availability", "offline",
                              retain=True),
        ) as mqtt:
            self.mqtt = mqtt
            await mqtt.publish(f"{BASE_TOPIC}/bridge/availability", "online",
                               retain=True)

            for light in self.lights:
                await self._announce(light)

            await mqtt.subscribe(f"{BASE_TOPIC}/+/set")

            async with asyncio.TaskGroup() as tg:
                for light in self.lights:
                    tg.create_task(self._observe(light))
                tg.create_task(self._command_loop())

    async def _announce(self, light: LightCfg) -> None:
        assert self.mqtt is not None
        await self.mqtt.publish(light.config_topic,
                                json.dumps(discovery_config(light)),
                                retain=True)
        LOG.info("announced light %s (%s)", light.id, light.address)

    async def _observe(self, light: LightCfg) -> None:
        """Long-lived CoAP Observe; republish state, track availability."""
        assert self.coap is not None and self.mqtt is not None
        while True:
            try:
                request = Message(code=GET, uri=f"{light.uri}/light/state",
                                  observe=0)
                req = self.coap.request(request)

                first = await req.response
                await self._publish_state(light, first.payload)
                await self.mqtt.publish(light.topic("availability"), "online",
                                        retain=True)

                async for resp in req.observation:
                    await self._publish_state(light, resp.payload)
            except Exception as exc:  # noqa: BLE001 - keep bridge alive
                LOG.warning("observe %s failed: %s; retry in 5s", light.id, exc)
                await self.mqtt.publish(light.topic("availability"), "offline",
                                        retain=True)
                await asyncio.sleep(5)

    async def _publish_state(self, light: LightCfg, payload: bytes) -> None:
        assert self.mqtt is not None
        try:
            ha = coap_state_to_ha(payload)
        except Exception as exc:  # noqa: BLE001
            LOG.warning("bad state payload from %s: %s", light.id, exc)
            return
        await self.mqtt.publish(light.topic("state"), json.dumps(ha),
                                retain=True)
        LOG.debug("state %s -> %s", light.id, ha)

    async def _command_loop(self) -> None:
        assert self.mqtt is not None and self.coap is not None
        by_id = {light.id: light for light in self.lights}
        async for message in self.mqtt.messages:
            topic = message.topic.value
            # coapbridge/light/<id>/set
            parts = topic.split("/")
            if len(parts) != 4 or parts[-1] != "set":
                continue
            light = by_id.get(parts[2])
            if light is None:
                continue
            try:
                body = ha_command_to_coap(message.payload)
                request = Message(code=PUT, uri=f"{light.uri}/light",
                                  payload=body)
                request.opt.content_format = CT_CBOR
                await self.coap.request(request).response
                LOG.info("command -> %s applied", light.id)
            except Exception as exc:  # noqa: BLE001
                LOG.warning("command to %s failed: %s", light.id, exc)


def load_config(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fh:
        return yaml.safe_load(fh)


def main() -> None:
    ap = argparse.ArgumentParser(description="CoAP<->MQTT bridge")
    ap.add_argument("-c", "--config", default="config.yaml")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s %(message)s",
    )

    cfg = load_config(args.config)
    try:
        asyncio.run(Bridge(cfg).run())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
