#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#include "scd40.h"

LOG_MODULE_REGISTER(scd40, LOG_LEVEL_INF);

/* ── SCD40 command opcodes ───────────────────────────────────────────────── */
#define CMD_START_PERIODIC_MEAS     0x21B1U
#define CMD_READ_MEASUREMENT        0xEC05U
#define CMD_STOP_PERIODIC_MEAS      0x3F86U
#define CMD_GET_DATA_READY_STATUS   0xE4B8U
#define CMD_REINIT                  0x3646U
#define CMD_SOFT_RESET              0xD304U

/* ── Timing (ms) ─────────────────────────────────────────────────────────── */
#define DELAY_STOP_MEAS_MS      500
#define DELAY_REINIT_MS          20
#define DELAY_CMD_RESPONSE_MS     1

/* ── Sensirion CRC-8 (poly 0x31, init 0xFF) ─────────────────────────────── */
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFFU;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80U) ? ((crc << 1) ^ 0x31U) : (crc << 1);
        }
    }
    return crc;
}

/* ── Low-level helpers ───────────────────────────────────────────────────── */

static int send_cmd(const struct device *dev, uint16_t cmd)
{
    uint8_t buf[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFFU)};

    return i2c_write(dev, buf, sizeof(buf), SCD40_I2C_ADDR);
}

/**
 * Read @p num_words 16-bit words (each followed by a CRC byte) from the
 * sensor.  The caller supplies a buffer of at least num_words * 3 bytes.
 * Returns -EIO on CRC mismatch.
 */
static int read_words(const struct device *dev, uint8_t *buf, size_t num_words)
{
    int ret = i2c_read(dev, buf, num_words * 3U, SCD40_I2C_ADDR);

    if (ret < 0) {
        return ret;
    }

    for (size_t i = 0; i < num_words; i++) {
        uint8_t *w = &buf[i * 3U];

        if (crc8(w, 2U) != w[2]) {
            LOG_ERR("CRC error on word %zu (got 0x%02X, expected 0x%02X)",
                    i, w[2], crc8(w, 2U));
            return -EIO;
        }
    }
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int scd40_init(const struct device *i2c_dev)
{
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device '%s' is not ready", i2c_dev->name);
        return -ENODEV;
    }

    /* Stop any measurement that may still be running (ignore errors here:
     * the sensor may not have been started yet on a cold boot). */
    (void)send_cmd(i2c_dev, CMD_STOP_PERIODIC_MEAS);
    k_sleep(K_MSEC(DELAY_STOP_MEAS_MS));

    /* Reinitialise NVM-loaded calibration values */
    int ret = send_cmd(i2c_dev, CMD_REINIT);

    if (ret < 0) {
        LOG_ERR("REINIT command failed: %d", ret);
        return ret;
    }
    k_sleep(K_MSEC(DELAY_REINIT_MS));

    LOG_INF("SCD40 initialised on %s", i2c_dev->name);
    return 0;
}

int scd40_start_periodic_measurement(const struct device *i2c_dev)
{
    int ret = send_cmd(i2c_dev, CMD_START_PERIODIC_MEAS);

    if (ret < 0) {
        LOG_ERR("START_PERIODIC_MEASUREMENT failed: %d", ret);
        return ret;
    }
    LOG_INF("SCD40 periodic measurement started (5 s interval)");
    return 0;
}

int scd40_stop_periodic_measurement(const struct device *i2c_dev)
{
    int ret = send_cmd(i2c_dev, CMD_STOP_PERIODIC_MEAS);

    if (ret < 0) {
        return ret;
    }
    k_sleep(K_MSEC(DELAY_STOP_MEAS_MS));
    return 0;
}

int scd40_get_data_ready(const struct device *i2c_dev, bool *ready)
{
    int ret = send_cmd(i2c_dev, CMD_GET_DATA_READY_STATUS);

    if (ret < 0) {
        return ret;
    }
    k_sleep(K_MSEC(DELAY_CMD_RESPONSE_MS));

    uint8_t buf[3];

    ret = i2c_read(i2c_dev, buf, sizeof(buf), SCD40_I2C_ADDR);
    if (ret < 0) {
        return ret;
    }
    if (crc8(buf, 2U) != buf[2]) {
        LOG_ERR("CRC error reading data-ready status");
        return -EIO;
    }

    /* Bits [10:0] non-zero means a fresh sample is available */
    uint16_t status = ((uint16_t)buf[0] << 8) | buf[1];

    *ready = (status & 0x07FFU) != 0U;
    return 0;
}

int scd40_read_measurement(const struct device *i2c_dev,
                           struct scd40_data *data)
{
    bool ready = false;
    int  ret   = scd40_get_data_ready(i2c_dev, &ready);

    if (ret < 0) {
        LOG_ERR("Failed to check data-ready: %d", ret);
        return ret;
    }
    if (!ready) {
        return -EAGAIN;
    }

    ret = send_cmd(i2c_dev, CMD_READ_MEASUREMENT);
    if (ret < 0) {
        LOG_ERR("READ_MEASUREMENT command failed: %d", ret);
        return ret;
    }
    k_sleep(K_MSEC(DELAY_CMD_RESPONSE_MS));

    /* Response: [CO2_H, CO2_L, CRC, TEMP_H, TEMP_L, CRC, RH_H, RH_L, CRC] */
    uint8_t buf[9];

    ret = read_words(i2c_dev, buf, 3U);
    if (ret < 0) {
        LOG_ERR("Failed to read measurement data: %d", ret);
        return ret;
    }

    uint16_t raw_co2  = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t raw_temp = ((uint16_t)buf[3] << 8) | buf[4];
    uint16_t raw_rh   = ((uint16_t)buf[6] << 8) | buf[7];

    data->co2_ppm    = raw_co2;
    data->temperature = -45.0 + 175.0 * (double)raw_temp / 65535.0;
    data->humidity    = 100.0 * (double)raw_rh   / 65535.0;

    LOG_INF("SCD40: CO2=%u ppm  Temp=%.2f C  Hum=%.2f %%RH",
            data->co2_ppm, data->temperature, data->humidity);
    return 0;
}
