/*
 * Optional e-paper status display for the panel switch.
 *
 * The panel is a sender-only SSED, so the values shown are the last-commanded
 * (shadow) values, not confirmed light state. Redraws are coalesced and the
 * e-paper is refreshed a short time after activity settles, with an occasional
 * full refresh to clear ghosting (see docs/hardware/panel-switch.md §4.2).
 *
 * If no `zephyr,display` is chosen in the board overlay, all functions here are
 * no-ops so the app still builds and runs without a display.
 */
#ifndef PANEL_DISPLAY_H_
#define PANEL_DISPLAY_H_

#include <stdbool.h>
#include <stdint.h>

struct panel_ui {
	bool on;             /* last-commanded on/off (shadow) */
	uint8_t bri;         /* 1..254 */
	bool ct_mode;        /* encoder adjusts color temp (true) or brightness */
	uint16_t ct;         /* color temperature, mireds */
	int battery_pct;     /* 0..100, or -1 if unknown */
};

/* Initialize the display. Returns 0 on success, negative if unavailable
 * (treated as non-fatal by the caller). No-op without a chosen display. */
int panel_display_init(void);

/* Request a redraw with the given state. Coalesced: the physical e-paper
 * refresh happens once, shortly after the last request. No-op without a
 * chosen display. */
void panel_display_request(const struct panel_ui *ui);

#endif /* PANEL_DISPLAY_H_ */
