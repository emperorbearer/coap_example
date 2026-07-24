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

## Notes / TODO

- `INPUT_CALLBACK_DEFINE(NULL, input_cb)` subscribes to all input devices; some
  Zephyr versions add a `user_data` argument — adjust if your SDK requires it.
- Encoder acceleration (faster turns = bigger steps) and per-button
  long-press actions are easy extensions in `input_cb`.
