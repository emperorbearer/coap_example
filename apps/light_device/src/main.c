/*
 * LwM2M light (relay) device for the SparkFun Thing Plus Matter MGM240P.
 *
 * Exposes an IPSO Light Control object (3311) to a Leshan LwM2M server.
 * When the server writes the On/Off resource (5850), a relay connected to
 * a GPIO pin is switched, turning the attached light on or off. The
 * current state stays readable/observable on the server side, so Leshan
 * always knows the actual light state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/lwm2m.h>
#include <zephyr/net/openthread.h>
#include <openthread/thread.h>

LOG_MODULE_REGISTER(lwm2m_light, LOG_LEVEL_INF);

/* IPSO Light Control object (3311) */
#define LIGHT_CONTROL_OBJ_ID	3311
#define RES_ON_OFF		5850
#define RES_APPLICATION_TYPE	5750

/* LwM2M Security (0) / Device (3) object resources */
#define SECURITY_SERVER_URI	0
#define SECURITY_MODE		2
#define SECURITY_MODE_NO_SEC	3
#define DEVICE_MANUFACTURER	0
#define DEVICE_MODEL_NUMBER	1

static const struct gpio_dt_spec relay_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(relay0), gpios);
static const struct gpio_dt_spec led_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static struct lwm2m_ctx client_ctx;

static char manufacturer[] = "SparkFun";
static char model_number[] = "Thing Plus Matter MGM240P";
static char app_type[] = "Relay light";

/* Called by the LwM2M engine after the server writes 3311/0/5850. */
static int light_on_off_cb(uint16_t obj_inst_id, uint16_t res_id,
			   uint16_t res_inst_id, uint8_t *data,
			   uint16_t data_len, bool last_block,
			   size_t total_size, size_t offset)
{
	bool on = (data_len > 0) && *(bool *)data;
	int ret;

	ret = gpio_pin_set_dt(&relay_gpio, on);
	if (ret < 0) {
		LOG_ERR("Failed to drive relay (%d)", ret);
		return ret;
	}

	/* The on-board blue LED mirrors the light state. */
	gpio_pin_set_dt(&led_gpio, on);

	LOG_INF("Light -> %s (written by server)", on ? "ON" : "OFF");

	return 0;
}

static int init_relay_gpio(void)
{
	int ret;

	if (!gpio_is_ready_dt(&relay_gpio) || !gpio_is_ready_dt(&led_gpio)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&relay_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	return gpio_pin_configure_dt(&led_gpio, GPIO_OUTPUT_INACTIVE);
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

	/* IPSO Light Control instance driving the relay. */
	ret = lwm2m_create_object_inst(&LWM2M_OBJ(LIGHT_CONTROL_OBJ_ID, 0));
	if (ret < 0) {
		return ret;
	}
	lwm2m_set_res_buf(&LWM2M_OBJ(LIGHT_CONTROL_OBJ_ID, 0, RES_APPLICATION_TYPE),
			  app_type, sizeof(app_type), sizeof(app_type), 0);
	lwm2m_set_bool(&LWM2M_OBJ(LIGHT_CONTROL_OBJ_ID, 0, RES_ON_OFF), false);
	lwm2m_register_post_write_callback(
		&LWM2M_OBJ(LIGHT_CONTROL_OBJ_ID, 0, RES_ON_OFF), light_on_off_cb);

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

	LOG_INF("LwM2M light (relay) device starting");

	ret = init_relay_gpio();
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
