# Light node firmware

Mains-powered Thread **FTD/Router** that exposes a CoAP-controlled light and
notifies observers (switch node + Home Assistant bridge) on every state change.

## CoAP resources

| Resource | Methods | Notes |
|----------|---------|-------|
| `/light` | GET / PUT / POST | GET state, PUT partial state, POST toggle |
| `/light/state` | GET + **Observe** | Observable state (RFC 7641) |

Payload is CBOR (`application/cbor`, ct=60): `{"on":bool,"bri":uint?,"seq":uint}`.
See `../../docs/coap-resource-model.md`.

## Board portability

The app references only DeviceTree aliases, so the same sources build for every
supported MCU. Each board provides the pin mapping in `boards/<board>.overlay`:

| alias | meaning |
|-------|---------|
| `light-relay` | relay/SSR output that switches the lamp (required) |
| `led0` | status LED mirroring on/off (optional) |

## Build

```bash
# Nordic nRF54L15
west build -b nrf54l15dk/nrf54l15/cpuapp firmware/light

# Silicon Labs xG24 / EFR32MG24 (MGM240)
west build -b xg24_dk/efr32mg24b310f1536im48 firmware/light

# Espressif
west build -b esp32c6_devkitc firmware/light
west build -b esp32h2_devkitm firmware/light
```

Flash: `west flash`.

## Thread network

`prj.conf` enables `CONFIG_OPENTHREAD_JOINER` for bring-up. In production, drive
commissioning from your border router (e.g. OTBR ephemeral key). The node runs
as an FTD/Router and stays awake to route for sleepy switch nodes.

## Notes / TODO

- `/.well-known/core` lists resource paths via `CONFIG_COAP_SERVER_WELL_KNOWN_CORE`.
  Advertising `rt="light.switch"` for discovery needs a custom link-format
  handler; add if you rely on `?rt=` filtering (see resource model doc).
- Brightness/PWM dimming is stubbed (`bri`): wire a PWM channel in the overlay
  and extend `light_state.c:drive_output()` if the hardware supports dimming.
- OpenThread maturity varies by platform on Zephyr; verify ESP32-H2/C6 against
  your SDK version.
