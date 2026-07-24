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
 *   - Custom LED PCB (this project's light): PWM channels pwm-red/green/blue
 *     (+ optional pwm-white) drive the LEDs, giving color + dimming.
 *   - Simple on/off fixture: a single "light-relay" GPIO.
 *
 * If the PWM aliases are present we use them; otherwise we fall back to the
 * relay GPIO. See docs/hardware/light-node.md.
 */
#if DT_NODE_EXISTS(DT_ALIAS(pwm_red)) && \
	DT_NODE_EXISTS(DT_ALIAS(pwm_green)) && \
	DT_NODE_EXISTS(DT_ALIAS(pwm_blue))
#define HAVE_RGB_PWM 1
static const struct pwm_dt_spec pwm_r = PWM_DT_SPEC_GET(DT_ALIAS(pwm_red));
static const struct pwm_dt_spec pwm_g = PWM_DT_SPEC_GET(DT_ALIAS(pwm_green));
static const struct pwm_dt_spec pwm_b = PWM_DT_SPEC_GET(DT_ALIAS(pwm_blue));
#if DT_NODE_EXISTS(DT_ALIAS(pwm_white))
#define HAVE_WHITE_PWM 1
static const struct pwm_dt_spec pwm_w = PWM_DT_SPEC_GET(DT_ALIAS(pwm_white));
#endif
#else
#define HAVE_RGB_PWM 0
#define LIGHT_RELAY_NODE DT_ALIAS(light_relay)
#if !DT_NODE_EXISTS(LIGHT_RELAY_NODE)
#error "Board overlay must define RGB pwm aliases or a 'light-relay' GPIO alias"
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
#if HAVE_RGB_PWM
	.bri = LIGHT_BRI_MAX, /* dimmable LED fixture: default full brightness */
	.r = 255, .g = 255, .b = 255,
#else
	.bri = 0,             /* on/off fixture: brightness unsupported */
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

#if HAVE_RGB_PWM
/* Set one channel: duty = channel(0..255) * bri(0..254) / (255*254) of period.
 * When off, duty is 0. */
static void set_channel(const struct pwm_dt_spec *ch, uint8_t value)
{
	uint32_t period = ch->period;
	uint32_t bri = g_state.on ? g_state.bri : 0;
	/* scale: value/255 * bri/254 */
	uint32_t pulse = (uint32_t)((uint64_t)period * value * bri /
				    (255u * LIGHT_BRI_MAX));

	(void)pwm_set_pulse_dt(ch, pulse);
}
#endif

static void drive_output(void)
{
#if HAVE_RGB_PWM
	set_channel(&pwm_r, g_state.r);
	set_channel(&pwm_g, g_state.g);
	set_channel(&pwm_b, g_state.b);
#if defined(HAVE_WHITE_PWM)
	set_channel(&pwm_w, g_state.w);
#endif
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

#if HAVE_RGB_PWM
	if (!pwm_is_ready_dt(&pwm_r) || !pwm_is_ready_dt(&pwm_g) ||
	    !pwm_is_ready_dt(&pwm_b)) {
		return -ENODEV;
	}
#if defined(HAVE_WHITE_PWM)
	if (!pwm_is_ready_dt(&pwm_w)) {
		return -ENODEV;
	}
#endif
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
	LOG_INF("light init: on=%d bri=%d seq=%u", g_state.on, g_state.bri,
		g_state.seq);
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
	if (upd->has_rgb &&
	    (upd->r != g_state.r || upd->g != g_state.g || upd->b != g_state.b)) {
		g_state.r = upd->r;
		g_state.g = upd->g;
		g_state.b = upd->b;
		g_state.ct = 0; /* leaving color-temp mode */
		if (!g_state.on) {
			g_state.on = true; /* color change implies on */
		}
		changed = true;
	}
	if (upd->has_w && upd->w != g_state.w) {
		g_state.w = upd->w;
		changed = true;
	}
	if (upd->has_ct && upd->ct != g_state.ct) {
		g_state.ct = upd->ct;
		if (!g_state.on) {
			g_state.on = true;
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
