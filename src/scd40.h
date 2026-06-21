#ifndef SCD40_H_
#define SCD40_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/device.h>

/** SCD40 I2C 7-bit address */
#define SCD40_I2C_ADDR  0x62U

/**
 * Measurement data from a single SCD40 reading.
 * Populated by scd40_read_measurement().
 */
struct scd40_data {
    uint16_t co2_ppm;    /* CO2 concentration [ppm] */
    double   temperature; /* Temperature [°C] */
    double   humidity;    /* Relative humidity [%RH] */
};

/**
 * @brief  Stop any ongoing measurement, reinitialise the sensor.
 * @return 0 on success, negative errno on I2C error.
 */
int scd40_init(const struct device *i2c_dev);

/**
 * @brief  Start periodic measurements (one sample every ~5 s).
 * @return 0 on success, negative errno on I2C error.
 */
int scd40_start_periodic_measurement(const struct device *i2c_dev);

/**
 * @brief  Stop periodic measurements.
 *         Required before issuing single-shot commands or reconfiguring.
 * @return 0 on success, negative errno on I2C error.
 */
int scd40_stop_periodic_measurement(const struct device *i2c_dev);

/**
 * @brief  Check whether a fresh measurement is available.
 * @param  ready  Set to true when new data is ready to read.
 * @return 0 on success, negative errno on I2C / CRC error.
 */
int scd40_get_data_ready(const struct device *i2c_dev, bool *ready);

/**
 * @brief  Read the latest CO2, temperature, and humidity values.
 *         Checks data-ready status internally; returns -EAGAIN if
 *         the sensor has no fresh sample yet.
 * @param  data   Output struct filled with measurement values.
 * @return 0 on success, -EAGAIN if data not ready, negative errno on error.
 */
int scd40_read_measurement(const struct device *i2c_dev,
                           struct scd40_data *data);

#endif /* SCD40_H_ */
