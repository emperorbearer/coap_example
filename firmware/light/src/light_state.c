#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include "light_state.h"

LOG_MODULE_REGISTER(light_state, LOG_LEVEL_INF);

/* Relay/SSR output is board-agnostic via the "light-relay" DT alias.
 * Each board overlay maps it to the real pin. See docs/hardware/light-node.md.
 */
#define LIGHT_RELAY_NODE DT_ALIAS(light_relay)
#if !DT_NODE_EXISTS(LIGHT_RELAY_NODE)
#error "Board overlay must define a 'light-relay' GPIO alias"
#endif
static const struct gpio_dt_spec relay = GPIO_DT_SPEC_GET(LIGHT_RELAY_NODE, gpios);

/* Optional status LED mirrors the on/off state. */
#define LED0_NODE DT_ALIAS(led0)
#if DT_NODE_EXISTS(LED0_NODE)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#endif

static struct light_state g_state = {
	.on = false,
	.bri = 0, /* 0 => brightness unsupported by default; set in overlay/Kconfig */
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

static void drive_output(void)
{
	gpio_pin_set_dt(&relay, g_state.on ? 1 : 0);
#if DT_NODE_EXISTS(LED0_NODE)
	gpio_pin_set_dt(&led, g_state.on ? 1 : 0);
#endif
	/* Brightness/PWM dimming would be applied here if supported. */
}

int light_state_init(light_state_changed_cb cb)
{
	int err;

	k_mutex_init(&g_lock);
	g_cb = cb;

	if (!gpio_is_ready_dt(&relay)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&relay, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
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
		/* A brightness change implies the light is on. */
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
