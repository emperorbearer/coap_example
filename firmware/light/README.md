# Light node firmware

Thread **FTD/Router** for the custom LED+MCU PCB (powered from a KC-certified
SMPS DC rail). Exposes a CoAP-controlled light with **color + dimming** and
notifies observers (switch nodes + Home Assistant bridge) on every state change.

## CoAP resources

| Resource | Methods | Notes |
|----------|---------|-------|
| `/light` | GET / PUT / POST | GET state, PUT partial state, POST toggle |
| `/light/state` | GET + **Observe** | Observable state (RFC 7641) |

Payload is CBOR (`application/cbor`, ct=60):
`{"on":bool,"bri":1..254?,"r":0..255?,"g":?,"b":?,"w":?,"ct":?,"seq":uint}`.
See `../../docs/coap-resource-model.md`.

## Output back end (selected by board overlay)

The app references only DeviceTree aliases, so the same sources build for every
supported MCU. Two back ends are supported:

| aliases | back end |
|---------|----------|
| `pwm-red`, `pwm-green`, `pwm-blue` (+ optional `pwm-white`) | RGBW PWM: color + dimming (custom LED PCB) |
| `light-relay` | single GPIO: on/off only (fallback) |
| `led0` | status LED mirroring on/off (optional) |

If the RGB PWM aliases exist the firmware drives the LEDs by PWM
(`channel × bri / 254`); otherwise it falls back to the relay GPIO. See
`boards/nrf54l15dk_nrf54l15_cpuapp.overlay` for the reference RGBW mapping and
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
- Color is linear RGB(W). Add a gamma/white-balance LUT in
  `light_state.c:set_channel()` for better color fidelity if needed.
- Tunable white (`ct`) is carried in state but mapping to CW/WW channels is
  hardware-specific; wire it in the overlay + `set_channel()` when supported.
- PWM overlays for ESP32/Silabs are examples — confirm the PWM provider node,
  channels, and pins for your board/SoC (Silabs ships on/off by default).
- OpenThread maturity varies by platform on Zephyr; verify ESP32-H2/C6 against
  your SDK version.
