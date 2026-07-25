# Light node firmware

Thread **FTD/Router** for the tunable-white LED+MCU PCB (powered from a
KC-certified SMPS DC rail). Exposes a CoAP-controlled light with **color
temperature + dimming** and notifies observers (switch nodes + Home Assistant
bridge) on every state change.

## CoAP resources

| Resource | Methods | Notes |
|----------|---------|-------|
| `/light` | GET / PUT / POST | GET state, PUT partial state, POST toggle |
| `/light/state` | GET + **Observe** | Observable state (RFC 7641) |

Payload is CBOR (`application/cbor`, ct=60):
`{"on":bool,"bri":1..254?,"ct":153..370?,"seq":uint}` (ct in mireds).
See `../../docs/coap-resource-model.md`.

## Output back end (selected by board overlay)

The app references only DeviceTree aliases, so the same sources build for every
supported MCU. Two back ends are supported:

| aliases | back end |
|---------|----------|
| `pwm-cw`, `pwm-ww` | tunable white: cool/warm PWM → color temperature + dimming |
| `light-relay` | single GPIO: on/off only (fallback) |
| `led0` | status LED mirroring on/off (optional) |

With the CW/WW aliases the firmware derives a cool/warm split from `ct` and
scales both by `bri`; otherwise it falls back to the relay GPIO. See
`boards/nrf54l15dk_nrf54l15_cpuapp.overlay` for the reference CW/WW mapping and
`../../docs/hardware/light-node.md` for the driver design.

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
- The CW/WW split from `ct` is a simple linear mix; add a calibration/gamma LUT
  in `light_state.c:drive_output()` for better CCT accuracy if needed.
- PWM overlays for ESP32/Silabs are examples — confirm the PWM provider node,
  channels, and pins for your board/SoC (Silabs ships on/off by default).
- OpenThread maturity varies by platform on Zephyr; verify ESP32-H2/C6 against
  your SDK version.
