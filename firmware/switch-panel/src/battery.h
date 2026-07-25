/*
 * Battery state-of-charge for the panel switch, via the nPM1300 charger
 * (voltage/current/temperature measurements) + the nRF Fuel Gauge library.
 *
 * If no `npm1300_charger` node exists in the board devicetree, these are
 * no-ops returning "unknown", so the app builds/runs without an nPM1300.
 */
#ifndef PANEL_BATTERY_H_
#define PANEL_BATTERY_H_

/* Initialize the charger sensor + fuel gauge. Returns 0 on success, negative
 * if unavailable (non-fatal). */
int battery_init(void);

/* Current state of charge in percent (0..100), or -1 if unavailable. */
int battery_soc_pct(void);

#endif /* PANEL_BATTERY_H_ */
