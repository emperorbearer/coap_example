#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include "light_state.h"

LOG_MODULE_REGISTER(light_state, LOG_LEVEL_INF);

/*
 * Output back end is selected at build time from the board overlay:
 *
 *   - Tunable-white LED PCB (this project's light): two PWM channels
 *     pwm-cw (cool white) and pwm-ww (warm white) give brightness + color
 *     temperature. The CW/WW split is derived from the color temperature and
 *     both are scaled by the master brightness.
 *   - Simple on/off fixture: a single "light-relay" GPIO.
 *
 * See docs/hardware/light-node.md.
 */
#if DT_NODE_EXISTS(DT_ALIAS(pwm_cw)) && DT_NODE_EXISTS(DT_ALIAS(pwm_ww))
#define HAVE_CCT_PWM 1
static const struct pwm_dt_spec pwm_cw = PWM_DT_SPEC_GET(DT_ALIAS(pwm_cw));
static const struct pwm_dt_spec pwm_ww = PWM_DT_SPEC_GET(DT_ALIAS(pwm_ww));
#else
#define HAVE_CCT_PWM 0
#define LIGHT_RELAY_NODE DT_ALIAS(light_relay)
#if !DT_NODE_EXISTS(LIGHT_RELAY_NODE)
#error "Board overlay must define pwm-cw/pwm-ww aliases or a 'light-relay' GPIO"
#endif
static const struct gpio_dt_spec relay =
	GPIO_DT_SPEC_GET(LIGHT_RELAY_NODE, gpios);
#endif

/* Optional status LED mirrors the on/off state. */
#define LED0_NODE DT_ALIAS(led0)
#if DT_NODE_EXISTS(LED0_NODE)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#endif

static struct light_state g_state = {
	.on = false,
#if HAVE_CCT_PWM
	.bri = LIGHT_BRI_MAX,               /* tunable-white: full brightness */
	.ct = (LIGHT_CT_MIN + LIGHT_CT_MAX) / 2, /* neutral color temp */
#else
	.bri = 0,                           /* on/off fixture: no dimming */
#endif
	.seq = 0,
};
static struct k_mutex g_lock;
static light_state_changed_cb g_cb;

#define SETTINGS_KEY "light/state"

/* --- persistence via the settings subsystem --- */

static int state_settings_set(const char *name, size_t len,
			      settings_read_cb read_cb, void *cb_arg)
{
	if (settings_name_steq(name, "state", NULL)) {
		struct light_state loaded;
		ssize_t n = read_cb(cb_arg, &loaded, sizeof(loaded));

		if (n == sizeof(loaded)) {
			g_state = loaded;
		}
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(light, "light", NULL, state_settings_set,
			      NULL, NULL);

static void state_persist(void)
{
	int err = settings_save_one(SETTINGS_KEY, &g_state, sizeof(g_state));

	if (err) {
		LOG_WRN("settings_save_one failed: %d", err);
	}
}

/* --- output --- */

#if HAVE_CCT_PWM
/* Set one channel: duty = value(0..255) * bri(0..254) / (255*254) of period.
 * When off, duty is 0. */
static void set_channel(const struct pwm_dt_spec *ch, uint8_t value)
{
	uint32_t period = ch->period;
	uint32_t bri = g_state.on ? g_state.bri : 0;
	uint32_t pulse = (uint32_t)((uint64_t)period * value * bri /
				    (255u * LIGHT_BRI_MAX));

	(void)pwm_set_pulse_dt(ch, pulse);
}
#endif

static void drive_output(void)
{
#if HAVE_CCT_PWM
	/* Map color temperature (mireds) to a cool/warm split. Higher mired =
	 * warmer, so warm_frac rises toward LIGHT_CT_MAX. cw + ww values sum to
	 * 255 so overall output stays ~constant across the CT range. */
	uint16_t ct = g_state.ct;

	if (ct < LIGHT_CT_MIN) {
		ct = LIGHT_CT_MIN;
	}
	if (ct > LIGHT_CT_MAX) {
		ct = LIGHT_CT_MAX;
	}
	uint32_t warm = (uint32_t)(ct - LIGHT_CT_MIN) * 255u /
			(LIGHT_CT_MAX - LIGHT_CT_MIN);
	uint8_t ww_val = (uint8_t)warm;
	uint8_t cw_val = (uint8_t)(255u - warm);

	set_channel(&pwm_cw, cw_val);
	set_channel(&pwm_ww, ww_val);
#else
	gpio_pin_set_dt(&relay, g_state.on ? 1 : 0);
#endif
#if DT_NODE_EXISTS(LED0_NODE)
	gpio_pin_set_dt(&led, g_state.on ? 1 : 0);
#endif
}

int light_state_init(light_state_changed_cb cb)
{
	k_mutex_init(&g_lock);
	g_cb = cb;

#if HAVE_CCT_PWM
	if (!pwm_is_ready_dt(&pwm_cw) || !pwm_is_ready_dt(&pwm_ww)) {
		return -ENODEV;
	}
#else
	if (!gpio_is_ready_dt(&relay)) {
		return -ENODEV;
	}
	int err = gpio_pin_configure_dt(&relay, GPIO_OUTPUT_INACTIVE);

	if (err) {
		return err;
	}
#endif
#if DT_NODE_EXISTS(LED0_NODE)
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}
#endif

	/* Restore persisted state (default off if none). */
	settings_subsys_init();
	settings_load_subtree("light");

	drive_output();
	if (g_cb) {
		g_cb(&g_state, LIGHT_SRC_BOOT);
	}
	LOG_INF("light init: on=%d bri=%d ct=%u seq=%u", g_state.on,
		g_state.bri, g_state.ct, g_state.seq);
	return 0;
}

static void commit_change(const char *src)
{
	g_state.seq++;
	drive_output();
	state_persist();
	if (g_cb) {
		g_cb(&g_state, src);
	}
}

void light_state_apply(const struct light_update *upd, const char *src)
{
	bool changed = false;

	k_mutex_lock(&g_lock, K_FOREVER);
	if (upd->has_on && upd->on != g_state.on) {
		g_state.on = upd->on;
		changed = true;
	}
	if (upd->has_bri && g_state.bri != 0 && upd->bri != g_state.bri) {
		g_state.bri = upd->bri;
		if (!g_state.on) {
			g_state.on = true; /* brightness change implies on */
		}
		changed = true;
	}
	if (upd->has_ct && upd->ct != g_state.ct) {
		g_state.ct = upd->ct;
		if (!g_state.on) {
			g_state.on = true; /* CT change implies on */
		}
		changed = true;
	}
	if (changed) {
		commit_change(src);
	}
	k_mutex_unlock(&g_lock);
}

void light_state_toggle(const char *src)
{
	k_mutex_lock(&g_lock, K_FOREVER);
	g_state.on = !g_state.on;
	commit_change(src);
	k_mutex_unlock(&g_lock);
}

void light_state_get(struct light_state *out)
{
	k_mutex_lock(&g_lock, K_FOREVER);
	*out = g_state;
	k_mutex_unlock(&g_lock);
}
