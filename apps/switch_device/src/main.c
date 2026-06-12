/*
 * LwM2M switch device for the SparkFun Thing Plus Matter MGM240P.
 *
 * Reads an external switch on a GPIO pin and reports its on/off state to
 * a Leshan LwM2M server through the IPSO On/Off Switch object (3342).
 * Connectivity is provided by OpenThread (802.15.4); the device joins the
 * Thread network configured in prj.conf and then registers with the
 * server given by CONFIG_APP_LWM2M_SERVER_URL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/lwm2m.h>
#include <zephyr/net/openthread.h>
#include <openthread/thread.h>

LOG_MODULE_REGISTER(lwm2m_switch, LOG_LEVEL_INF);

/* IPSO On/Off Switch object (3342) */
#define ONOFF_SWITCH_OBJ_ID	3342
#define RES_DIGITAL_INPUT_STATE	5500
#define RES_APPLICATION_TYPE	5750

/* LwM2M Security (0) / Device (3) object resources */
#define SECURITY_SERVER_URI	0
#define SECURITY_MODE		2
#define SECURITY_MODE_NO_SEC	3
#define DEVICE_MANUFACTURER	0
#define DEVICE_MODEL_NUMBER	1

#define SWITCH_DEBOUNCE_MS	50

static const struct gpio_dt_spec sw_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec led_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static struct lwm2m_ctx client_ctx;
static struct gpio_callback sw_cb_data;
static struct k_work_delayable sw_work;
static bool switch_state;

static char manufacturer[] = "SparkFun";
static char model_number[] = "Thing Plus Matter MGM240P";
static char app_type[] = "Light switch";

static void publish_switch_state(bool state)
{
	switch_state = state;

	/* The on-board blue LED mirrors the reported state. */
	gpio_pin_set_dt(&led_gpio, state);

	lwm2m_set_bool(&LWM2M_OBJ(ONOFF_SWITCH_OBJ_ID, 0, RES_DIGITAL_INPUT_STATE),
		       state);

	LOG_INF("Switch state -> %s", state ? "ON" : "OFF");
}

static void sw_work_handler(struct k_work *work)
{
	int level = gpio_pin_get_dt(&sw_gpio);

	if (level < 0) {
		LOG_ERR("Failed to read switch pin (%d)", level);
		return;
	}

	if (IS_ENABLED(CONFIG_APP_SWITCH_TOGGLE_MODE)) {
		/* Push button: toggle once per press. */
		if (level) {
			publish_switch_state(!switch_state);
		}
	} else {
		/* Slide/rocker switch: state follows the pin level. */
		if ((bool)level != switch_state) {
			publish_switch_state(level);
		}
	}
}

static void sw_isr(const struct device *dev, struct gpio_callback *cb,
		   uint32_t pins)
{
	/* Debounce: coalesce edges, then sample the level. */
	k_work_reschedule(&sw_work, K_MSEC(SWITCH_DEBOUNCE_MS));
}

static int init_switch_gpio(void)
{
	int ret;

	if (!gpio_is_ready_dt(&sw_gpio) || !gpio_is_ready_dt(&led_gpio)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&sw_gpio, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&sw_gpio,
		IS_ENABLED(CONFIG_APP_SWITCH_TOGGLE_MODE) ?
			GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		return ret;
	}

	k_work_init_delayable(&sw_work, sw_work_handler);
	gpio_init_callback(&sw_cb_data, sw_isr, BIT(sw_gpio.pin));
	gpio_add_callback(sw_gpio.port, &sw_cb_data);

	return 0;
}

static int lwm2m_setup(void)
{
	int ret;

	/* Security object: plain CoAP (NoSec) towards Leshan. */
	ret = lwm2m_set_string(&LWM2M_OBJ(0, 0, SECURITY_SERVER_URI),
			       CONFIG_APP_LWM2M_SERVER_URL);
	if (ret < 0) {
		return ret;
	}
	lwm2m_set_u8(&LWM2M_OBJ(0, 0, SECURITY_MODE), SECURITY_MODE_NO_SEC);

	/* Device object metadata. */
	lwm2m_set_res_buf(&LWM2M_OBJ(3, 0, DEVICE_MANUFACTURER), manufacturer,
			  sizeof(manufacturer), sizeof(manufacturer),
			  LWM2M_RES_DATA_FLAG_RO);
	lwm2m_set_res_buf(&LWM2M_OBJ(3, 0, DEVICE_MODEL_NUMBER), model_number,
			  sizeof(model_number), sizeof(model_number),
			  LWM2M_RES_DATA_FLAG_RO);

	/* IPSO On/Off Switch instance reporting the physical switch. */
	ret = lwm2m_create_object_inst(&LWM2M_OBJ(ONOFF_SWITCH_OBJ_ID, 0));
	if (ret < 0) {
		return ret;
	}
	lwm2m_set_res_buf(&LWM2M_OBJ(ONOFF_SWITCH_OBJ_ID, 0, RES_APPLICATION_TYPE),
			  app_type, sizeof(app_type), sizeof(app_type), 0);
	lwm2m_set_bool(&LWM2M_OBJ(ONOFF_SWITCH_OBJ_ID, 0, RES_DIGITAL_INPUT_STATE),
		       switch_state);

	return 0;
}

static void rd_client_event(struct lwm2m_ctx *client,
			    enum lwm2m_rd_client_event event)
{
	switch (event) {
	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
		LOG_INF("Registered with LwM2M server as '%s'",
			CONFIG_APP_LWM2M_ENDPOINT);
		break;
	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
		LOG_ERR("Registration failed");
		break;
	case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
		LOG_DBG("Registration update complete");
		break;
	case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
		LOG_INF("Disconnected from LwM2M server");
		break;
	case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
		LOG_ERR("LwM2M network error");
		break;
	default:
		LOG_DBG("RD client event %d", (int)event);
		break;
	}
}

static void wait_for_thread_attach(void)
{
	otInstance *instance = openthread_get_default_instance();

	LOG_INF("Waiting for Thread network attach...");

	while (true) {
		otDeviceRole role = otThreadGetDeviceRole(instance);

		if (role == OT_DEVICE_ROLE_CHILD ||
		    role == OT_DEVICE_ROLE_ROUTER ||
		    role == OT_DEVICE_ROLE_LEADER) {
			LOG_INF("Thread attached (role: %d)", (int)role);
			return;
		}

		k_sleep(K_SECONDS(1));
	}
}

int main(void)
{
	int ret;

	LOG_INF("LwM2M switch device starting");

	ret = init_switch_gpio();
	if (ret < 0) {
		LOG_ERR("Failed to initialize GPIO (%d)", ret);
		return ret;
	}

	ret = lwm2m_setup();
	if (ret < 0) {
		LOG_ERR("Failed to set up LwM2M objects (%d)", ret);
		return ret;
	}

	wait_for_thread_attach();

	client_ctx.srv_obj_inst = 0;
	client_ctx.sec_obj_inst = 0;

	lwm2m_rd_client_start(&client_ctx, CONFIG_APP_LWM2M_ENDPOINT, 0,
			      rd_client_event, NULL);

	return 0;
}
