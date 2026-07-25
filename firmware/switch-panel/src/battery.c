#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "battery.h"

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_NODELABEL(npm1300_charger))

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <nrf_fuel_gauge.h>

/*
 * This targets nRF Connect SDK: the nPM1300 charger is exposed as a Zephyr
 * sensor, and the nRF Fuel Gauge library turns its voltage/current/temperature
 * readings into a state-of-charge estimate. The exact fuel-gauge API and the
 * battery model are SDK-version and cell specific — see the NCS sample
 * `samples/pmic/native/npm1300_fuel_gauge` and adjust if your SDK differs.
 */

static const struct device *const charger =
	DEVICE_DT_GET(DT_NODELABEL(npm1300_charger));

/* Discharge/charge model for the specific Li-ion cell. Provide this from your
 * cell's generated nRF Fuel Gauge battery model (linked at build time). */
extern const struct battery_model battery_model;

static int64_t ref_time;

static int read_cell(float *voltage, float *current, float *temp)
{
	struct sensor_value value;
	int err;

	err = sensor_sample_fetch(charger);
	if (err) {
		return err;
	}
	if (sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &value)) {
		return -EIO;
	}
	*voltage = (float)sensor_value_to_double(&value);
	if (sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value)) {
		return -EIO;
	}
	*current = (float)sensor_value_to_double(&value);
	if (sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &value)) {
		return -EIO;
	}
	*temp = (float)sensor_value_to_double(&value);
	return 0;
}

int battery_init(void)
{
	struct nrf_fuel_gauge_init_parameters params = {
		.model = &battery_model,
	};
	float v, i, t;
	int err;

	if (!device_is_ready(charger)) {
		LOG_WRN("nPM1300 charger not ready; battery %% unavailable");
		return -ENODEV;
	}
	err = read_cell(&v, &i, &t);
	if (err) {
		LOG_ERR("initial cell read failed: %d", err);
		return err;
	}
	params.v0 = v;
	params.i0 = i;
	params.t0 = t;
	nrf_fuel_gauge_init(&params, NULL);
	ref_time = k_uptime_get();
	LOG_INF("fuel gauge init: %.2f V, %.3f A, %.1f C", (double)v,
		(double)i, (double)t);
	return 0;
}

int battery_soc_pct(void)
{
	float v, i, t, soc, delta;
	int64_t now;

	if (!device_is_ready(charger)) {
		return -1;
	}
	if (read_cell(&v, &i, &t)) {
		return -1;
	}
	now = k_uptime_get();
	delta = (float)(now - ref_time) / 1000.0f;
	ref_time = now;

	/* vbus flag (last arg group) can be derived from the charger status for
	 * better accuracy while charging; false is a safe default on battery. */
	soc = nrf_fuel_gauge_process(v, i, t, delta, false, NULL);
	if (soc < 0.0f) {
		soc = 0.0f;
	} else if (soc > 100.0f) {
		soc = 100.0f;
	}
	return (int)(soc + 0.5f);
}

#else /* no nPM1300 charger in DT: report "unknown" */

int battery_init(void)
{
	return -ENODEV;
}

int battery_soc_pct(void)
{
	return -1;
}

#endif /* DT_NODE_EXISTS(DT_NODELABEL(npm1300_charger)) */
