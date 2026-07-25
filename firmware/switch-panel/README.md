# Panel switch firmware

Battery-powered Thread **SSED** in a switch-cover-sized PCB with several buttons
and a rotary encoder. Controls bound lights over CoAP (color, brightness, color
temperature) using the shared binding module (`../common/binding.c`).

## Controls (default mapping)

| Input | Action |
|-------|--------|
| Button KEY_0 | toggle on/off |
| Button KEY_1 | cycle encoder mode (brightness ↔ color temperature) |
| Button KEY_2 | cycle RGB color preset |
| Button KEY_3 | all off |
| Encoder push (KEY_ENTER) | toggle on/off |
| Encoder turn | adjust the active mode's value |

Remap by editing `src/main.c` (`input_cb`) and the board overlay's key codes.

## Local shadow

The SSED doesn't continuously track the light, so it keeps a local shadow of
brightness/color and sends **absolute** values (the CoAP protocol is stateful,
not delta-based). The shadow may drift from the real light; a periodic `GET` or
Observe could tighten sync at a power cost (see `docs/architecture.md`).

## Status display (e-paper, optional)

`src/display.c` renders a status screen on an e-paper panel via the Zephyr
`display` API + CFB (character framebuffer): light on/off, brightness %, encoder
mode, active color/color-temp, and battery %. It shows the **shadow** values.

Design notes (see `../../docs/hardware/panel-switch.md` §4.2):

- Refreshes are **coalesced** — a burst of encoder ticks collapses into one
  update ~400 ms after activity settles — and a **full refresh** runs every N
  updates to clear e-paper ghosting.
- The code **self-disables** unless the board overlay chooses a `zephyr,display`,
  so the default build (no display) still compiles and runs.

To fit an e-paper (e.g. SSD1680):

1. Merge `boards/epd-ssd1680.overlay.example` into your board overlay and set
   the real pins, panel size, and **panel-specific SSD16xx waveform properties**
   from your display's known-good DTS.
2. Enable the display Kconfig options listed in `prj.conf`
   (`CONFIG_DISPLAY`, `CONFIG_CHARACTER_FRAMEBUFFER`, `CONFIG_SSD16XX`,
   `CONFIG_SPI`).
3. Power the panel from an nPM1300 LDO/load-switch rail so it can be gated off.

## Battery gauge (nPM1300, optional)

`src/battery.c` reads the **nPM1300 charger** (voltage/current/temperature) and
runs the **nRF Fuel Gauge** library to produce a state-of-charge %, shown on the
display and refreshed every 30 min. Like the display, it **self-disables** unless
an `npm1300_charger` node exists in DT, so the default build is unaffected.

To enable (nRF Connect SDK):

1. Merge `boards/npm1300.overlay.example` into your board overlay and set the
   real I2C instance/pins and **charger limits/thermistor** for your cell.
2. Enable the nPM1300 + fuel-gauge Kconfig options listed in `prj.conf`
   (`CONFIG_NPM1300_CHARGER`, `CONFIG_NRF_FUEL_GAUGE`, `CONFIG_FPU`, …).
3. Provide your Li-ion cell's **battery model** (`battery_model`) — generate it
   for your cell and link it in; see the NCS `npm1300_fuel_gauge` sample.

Without an nPM1300, `battery_soc_pct()` returns `-1` and the display omits the
battery line.

## Inputs

Built on the Zephyr **input subsystem**: buttons via `gpio-keys`, the encoder
via `gpio-qdec` (reports `INPUT_REL_WHEEL`). The board overlay maps the pins.
Use SENSE-capable pins on the real PCB so inputs can wake the SoC from sleep.

## Build

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp firmware/switch-panel
west flash
```

## Bindings

Same binding model as the inner switch — the lights this panel controls are
stored in NVS. Add via `binding_add()` during bring-up or implement the
commissioning/discovery flow (see `../../docs/coap-resource-model.md`).

## Power (nPM1300 PMIC)

This panel is powered by a rechargeable Li-ion cell managed by an **nPM1300**
PMIC (USB-C charging, dual buck + LDO, fuel-gauge measurements) — see
`../../docs/hardware/panel-switch.md`. Firmware integration (add when running on
the real PCB; the DK overlay here has no nPM1300 so it is left out of the build):

- DeviceTree: add the `nordic,npm1300` MFD node on I2C with its `regulator`,
  `charger` (`npm1300_charger`), and `gpio` child nodes.
- Kconfig: `CONFIG_MFD=y`, `CONFIG_REGULATOR=y`, `CONFIG_REGULATOR_NPM1300=y`,
  `CONFIG_CHARGER=y`, `CONFIG_CHARGER_NPM1300=y`, `CONFIG_GPIO_NPM1300=y`,
  plus the **nRF Fuel Gauge** library for state-of-charge from VBAT/current/temp.
- Report battery % as telemetry (e.g. periodic `POST`), and surface charge/USB
  events from the charger driver callbacks.

## Notes / TODO

- `INPUT_CALLBACK_DEFINE(NULL, input_cb)` subscribes to all input devices; some
  Zephyr versions add a `user_data` argument — adjust if your SDK requires it.
- Encoder acceleration (faster turns = bigger steps) and per-button
  long-press actions are easy extensions in `input_cb`.
