#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>

#include "binding.h"

LOG_MODULE_REGISTER(switch_main, LOG_LEVEL_INF);

/* Wall-switch dry contact input. Provided by the board overlay as the
 * "wall-switch" alias. Maintained (rocker) type by default: the pin level
 * reflects the physical on/off position.
 */
#define WALL_SWITCH_NODE DT_ALIAS(wall_switch)
#if !DT_NODE_EXISTS(WALL_SWITCH_NODE)
#error "Board overlay must define a 'wall-switch' GPIO alias"
#endif
static const struct gpio_dt_spec wall_switch =
	GPIO_DT_SPEC_GET(WALL_SWITCH_NODE, gpios);

/* Set at build time: true = maintained (send state), false = momentary (toggle). */
#define SWITCH_MAINTAINED IS_ENABLED(CONFIG_APP_SWITCH_MAINTAINED)

/* Optional commissioning button to (re)bind this switch to a light. */
#define BUTTON_NODE DT_ALIAS(sw0)
#if DT_NODE_EXISTS(BUTTON_NODE)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static struct gpio_callback button_cb;
#endif

static struct gpio_callback wall_cb;
static struct k_work_delayable debounce_work;
static int last_reported = -1;

#define DEBOUNCE_MS 30

static void debounce_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int level = gpio_pin_get_dt(&wall_switch); /* logical: 1 = "on" */

	if (level < 0) {
		return;
	}
	if (SWITCH_MAINTAINED) {
		if (level == last_reported) {
			return; /* no net change after bounce */
		}
		last_reported = level;
		LOG_INF("wall switch -> %s", level ? "ON" : "OFF");
		binding_dispatch(true, level != 0);
	} else {
		LOG_INF("wall switch pressed -> toggle");
		binding_dispatch(false, false);
	}
}

static void wall_switch_isr(const struct device *dev, struct gpio_callback *cb,
			    uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* Wake from sleep and debounce before acting. */
	k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_MS));
}

#if DT_NODE_EXISTS(BUTTON_NODE)
static void commission_isr(const struct device *dev, struct gpio_callback *cb,
			   uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	/* TODO: trigger multicast discovery (GET /.well-known/core?rt=light.switch)
	 * and bind to the responding light. See docs/architecture.md section 5.
	 */
	LOG_INF("commissioning button: start discovery/binding");
}
#endif

static int setup_inputs(void)
{
	int err;

	if (!gpio_is_ready_dt(&wall_switch)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&wall_switch, GPIO_INPUT);
	if (err) {
		return err;
	}
	/* Interrupt on both edges so we catch on->off and off->on, and it can
	 * wake the SoC from deep sleep via GPIO SENSE. */
	err = gpio_pin_interrupt_configure_dt(&wall_switch,
					      GPIO_INT_EDGE_BOTH);
	if (err) {
		return err;
	}
	gpio_init_callback(&wall_cb, wall_switch_isr, BIT(wall_switch.pin));
	gpio_add_callback(wall_switch.port, &wall_cb);

#if DT_NODE_EXISTS(BUTTON_NODE)
	if (gpio_is_ready_dt(&button)) {
		gpio_pin_configure_dt(&button, GPIO_INPUT);
		gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
		gpio_init_callback(&button_cb, commission_isr, BIT(button.pin));
		gpio_add_callback(button.port, &button_cb);
	}
#endif
	return 0;
}

int main(void)
{
	LOG_INF("CoAP-over-Thread switch node starting (SSED)");

	if (binding_init() != 0) {
		LOG_ERR("binding init failed");
		return -1;
	}

	k_work_init_delayable(&debounce_work, debounce_handler);

	if (setup_inputs() != 0) {
		LOG_ERR("input setup failed");
		return -1;
	}

	/* Report initial position so the light matches the wall switch at boot. */
	if (SWITCH_MAINTAINED) {
		k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_MS));
	}

	LOG_INF("switch ready; sleeping between events");
	/* Everything else is interrupt-driven; the SSED sleeps otherwise. */
	return 0;
}
