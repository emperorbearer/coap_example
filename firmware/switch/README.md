# Switch node firmware

Battery-powered Thread **Sleepy End Device (SSED)** on nRF54L15. Sits behind an
existing wall switch (like an "inner relay" module but with no mains/relay),
senses the switch's dry contact, and drives one or more lights over CoAP.

## Behavior

- Deep sleep by default; a wall-switch edge wakes the SoC via GPIO SENSE.
- After a short debounce it dispatches to every configured **binding**:
  - Maintained switch (`CONFIG_APP_SWITCH_MAINTAINED=y`, default):
    `PUT /light {"on":<pos>,"src":"switch"}`.
  - Momentary switch (`=n`): `POST /light` (toggle).
- Requests are Confirmable when the binding sets `BINDING_FLAG_CONFIRMABLE`.

## Bindings

Which light(s) this switch controls is stored in NVS (survives battery change).
See `../../docs/coap-resource-model.md` section 4. A commissioning button hook
(`sw0`) is stubbed for multicast discovery + bind; for bring-up you can add a
binding programmatically via `binding_add()` or wire the discovery flow.

## Board aliases

| alias | meaning |
|-------|---------|
| `wall-switch` | dry-contact input from the wall switch (required, SENSE-capable) |
| `sw0` | commissioning button (optional) |

## Build

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp firmware/switch
west flash
```

Momentary switch variant:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp firmware/switch -- -DCONFIG_APP_SWITCH_MAINTAINED=n
```

## Power

`prj.conf` configures MTD + SED with CSL for low-latency, low-power operation.
Tune `CONFIG_OPENTHREAD_POLL_PERIOD` / CSL period against your latency and
battery targets, and measure real current draw. See
`../../docs/hardware/switch-node.md` for the power budget.

## Notes / TODO

- Implement the commissioning/discovery flow in `commission_isr()`
  (`GET /.well-known/core?rt=light.switch` over realm-local multicast).
- For lowest power, reduce logging (`CONFIG_LOG=n`) and minimize contact
  pull-up leakage in the real hardware design.
