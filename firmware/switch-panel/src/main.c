/*
 * Panel switch: a switch-cover-sized, battery-powered controller with several
 * buttons and a rotary encoder. Runs as a Thread SSED and drives bound lights
 * over CoAP (shared binding module).
 *
 * Because the SSED does not continuously track the light, it keeps a local
 * "shadow" of brightness/color and sends absolute values. The shadow can drift
 * from the real light; for tight sync a periodic GET/Observe could be added
 * (at a power cost).
 *
 * Default control mapping (buttons wired via the board overlay as input keys):
 *   KEY_0        : toggle on/off
 *   KEY_1        : cycle encoder mode (brightness <-> color temperature)
 *   KEY_2        : cycle color preset (RGB)
 *   KEY_3        : all off
 *   KEY_ENTER    : encoder push  -> toggle on/off
 *   encoder turn : adjust the active mode's value (brightness or color temp)
 */
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include "binding.h"
#include "display.h"

LOG_MODULE_REGISTER(panel_main, LOG_LEVEL_INF);

/* Steps and limits for encoder adjustments. */
#define BRI_STEP   16
#define CT_STEP    20
#define CT_MIN     153   /* ~6500K in mireds */
#define CT_MAX     500   /* ~2000K in mireds */

enum enc_mode { ENC_MODE_BRIGHTNESS = 0, ENC_MODE_CT, ENC_MODE_COUNT };

/* Local shadow of what we last commanded (also drives the optional display). */
static bool sh_on;
static uint8_t sh_bri = LIGHT_BRI_MAX;
static uint16_t sh_ct = 320;
static enum enc_mode enc_mode = ENC_MODE_BRIGHTNESS;

/* A few RGB presets cycled by KEY_2. First is plain white. */
static const struct { uint8_t r, g, b; const char *name; } presets[] = {
	{ 255, 255, 255, "White" },
	{ 255, 90, 20, "Warm" },
	{ 40, 120, 255, "Blue" },
	{ 40, 255, 90, "Green" },
	{ 255, 40, 120, "Pink" },
};
static int preset_idx;

/* Push the current shadow to the (optional) e-paper display. */
static void ui_push(void)
{
	struct panel_ui ui = {
		.on = sh_on,
		.bri = sh_bri,
		.ct_mode = (enc_mode == ENC_MODE_CT),
		.color = presets[preset_idx].name,
		.ct = sh_ct,
		.battery_pct = -1, /* TODO: read from nPM1300 fuel gauge */
	};

	panel_display_request(&ui);
}

static int clampi(int v, int lo, int hi)
{
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

/* --- actions --- */

static void action_toggle(void)
{
	struct light_cmd cmd = { .toggle = true };

	sh_on = !sh_on; /* shadow only; the light is the real source of truth */
	LOG_INF("toggle");
	binding_send(&cmd);
	ui_push();
}

static void action_all_off(void)
{
	struct light_cmd cmd = { .has_on = true, .on = false };

	sh_on = false;
	LOG_INF("all off");
	binding_send(&cmd);
	ui_push();
}

static void action_cycle_mode(void)
{
	enc_mode = (enc_mode + 1) % ENC_MODE_COUNT;
	LOG_INF("encoder mode -> %s",
		enc_mode == ENC_MODE_BRIGHTNESS ? "brightness" : "color-temp");
	ui_push();
}

static void action_cycle_preset(void)
{
	struct light_cmd cmd = { .has_on = true, .on = true, .has_rgb = true };

	preset_idx = (preset_idx + 1) % ARRAY_SIZE(presets);
	cmd.r = presets[preset_idx].r;
	cmd.g = presets[preset_idx].g;
	cmd.b = presets[preset_idx].b;
	sh_on = true;
	LOG_INF("preset %d", preset_idx);
	binding_send(&cmd);
	ui_push();
}

static void action_rotate(int delta)
{
	struct light_cmd cmd = { .has_on = true, .on = true };

	if (enc_mode == ENC_MODE_BRIGHTNESS) {
		sh_bri = clampi(sh_bri + delta * BRI_STEP, LIGHT_BRI_MIN,
				LIGHT_BRI_MAX);
		cmd.has_bri = true;
		cmd.bri = sh_bri;
		LOG_INF("brightness -> %u", sh_bri);
	} else {
		sh_ct = clampi(sh_ct + delta * CT_STEP, CT_MIN, CT_MAX);
		cmd.has_ct = true;
		cmd.ct = sh_ct;
		LOG_INF("color-temp -> %u mireds", sh_ct);
	}
	sh_on = true;
	binding_send(&cmd);
	ui_push();
}

/* --- input handling --- */

static void input_cb(struct input_event *evt)
{
	if (evt->type == INPUT_EV_KEY && evt->value) {
		switch (evt->code) {
		case INPUT_KEY_0:
			action_toggle();
			break;
		case INPUT_KEY_1:
			action_cycle_mode();
			break;
		case INPUT_KEY_2:
			action_cycle_preset();
			break;
		case INPUT_KEY_3:
			action_all_off();
			break;
		case INPUT_KEY_ENTER: /* encoder push */
			action_toggle();
			break;
		default:
			break;
		}
	} else if (evt->type == INPUT_EV_REL && evt->code == INPUT_REL_WHEEL) {
		action_rotate(evt->value);
	}
}

/* Subscribe to all input devices. (Some Zephyr versions take a 3rd user_data
 * argument; add NULL if your SDK requires it.) */
INPUT_CALLBACK_DEFINE(NULL, input_cb);

int main(void)
{
	LOG_INF("CoAP-over-Thread panel switch starting (SSED)");

	if (binding_init() != 0) {
		LOG_ERR("binding init failed");
		return -1;
	}

	/* Optional e-paper status display (no-op if none is fitted). */
	panel_display_init();
	ui_push(); /* draw the initial screen */

	LOG_INF("panel ready; sleeping between events");
	/* All control is input-driven; the SSED sleeps otherwise. */
	return 0;
}
