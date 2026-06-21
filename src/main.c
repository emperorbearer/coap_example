/*
 * nRF54 CoAP/LWM2M Air Quality Monitor
 *
 * Reads CO2, temperature, and relative humidity from a Sensirion SCD40
 * sensor via I2C and publishes the values to an LWM2M server over CoAP/UDP.
 *
 * LWM2M object mapping:
 *   3303/0  – Temperature        (°C)
 *   3304/0  – Humidity           (%RH)
 *   3300/0  – Generic Sensor     (CO2, ppm)
 *
 * Build targets:
 *   west build -b nrf54l15dk/nrf54l15/cpuapp
 *   west build -b nrf54h20dk/nrf54h20/cpuapp
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/lwm2m.h>

#include "scd40.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ── I2C device (alias defined in board overlay) ────────────────────────── */
#define I2C_NODE DT_ALIAS(scd40_i2c)
BUILD_ASSERT(DT_NODE_HAS_STATUS(I2C_NODE, okay),
             "scd40-i2c alias not enabled. Check your board overlay.");

static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

/* ── Network ready synchronisation ─────────────────────────────────────── */
static K_SEM_DEFINE(net_ready_sem, 0, 1);
static struct net_mgmt_event_callback net_mgmt_cb;

static void on_net_event(struct net_mgmt_event_callback *cb,
                         uint32_t event, struct net_if *iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);

    if (event == NET_EVENT_IPV4_ADDR_ADD) {
        LOG_INF("Network: IPv4 address obtained");
        k_sem_give(&net_ready_sem);
    } else if (event == NET_EVENT_IPV6_ADDR_ADD) {
        LOG_INF("Network: IPv6 address obtained");
        k_sem_give(&net_ready_sem);
    }
}

/* ── LWM2M client context ───────────────────────────────────────────────── */
static struct lwm2m_ctx lwm2m_client;

static void lwm2m_event_handler(struct lwm2m_ctx *client,
                                enum lwm2m_rd_client_event event)
{
    ARG_UNUSED(client);

    switch (event) {
    case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
        LOG_INF("LWM2M: registered with server");
        break;
    case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
        LOG_INF("LWM2M: registration updated");
        break;
    case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
        LOG_ERR("LWM2M: registration failed");
        break;
    case LWM2M_RD_CLIENT_EVENT_DEREGISTER_FAILURE:
        LOG_ERR("LWM2M: deregistration failed");
        break;
    case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
        LOG_WRN("LWM2M: disconnected");
        break;
    case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE:
        LOG_ERR("LWM2M: bootstrap registration failed");
        break;
    case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_COMPLETE:
        LOG_INF("LWM2M: bootstrap complete");
        break;
    default:
        break;
    }
}

/* ── LWM2M setup ────────────────────────────────────────────────────────── */
static int lwm2m_setup(void)
{
    int ret;

    /* ── Security object (Object 0, instance 0) ───────────────────────── */
    /* Server URI */
    ret = lwm2m_engine_set_string("0/0/0", CONFIG_APP_LWM2M_SERVER_URI);
    if (ret < 0) {
        LOG_ERR("Failed to set server URI: %d", ret);
        return ret;
    }
    /* Not a bootstrap server */
    ret = lwm2m_engine_set_bool("0/0/1", false);
    if (ret < 0) {
        return ret;
    }
    /* Security mode: NoSec (3) */
    ret = lwm2m_engine_set_u8("0/0/2", 3U);
    if (ret < 0) {
        return ret;
    }
    /* Short Server ID */
    ret = lwm2m_engine_set_u16("0/0/10", 1U);
    if (ret < 0) {
        return ret;
    }

    /* ── Server object (Object 1, instance 0) ─────────────────────────── */
    ret = lwm2m_engine_set_u16("1/0/0", 1U);   /* Short Server ID */
    if (ret < 0) {
        return ret;
    }
    ret = lwm2m_engine_set_u32("1/0/1",         /* Lifetime (s) */
                               CONFIG_LWM2M_ENGINE_DEFAULT_LIFETIME);
    if (ret < 0) {
        return ret;
    }
    ret = lwm2m_engine_set_u8("1/0/6", 0U);    /* Notification storing off */
    if (ret < 0) {
        return ret;
    }
    ret = lwm2m_engine_set_string("1/0/7", "U"); /* UDP binding */
    if (ret < 0) {
        return ret;
    }

    /* ── IPSO Temperature sensor (Object 3303, instance 0) ───────────── */
    ret = lwm2m_engine_create_obj_inst("3303/0");
    if (ret < 0 && ret != -EEXIST) {
        LOG_ERR("Failed to create 3303/0: %d", ret);
        return ret;
    }
    lwm2m_engine_set_string("3303/0/5701", "Cel"); /* Units */

    /* ── IPSO Humidity sensor (Object 3304, instance 0) ──────────────── */
    ret = lwm2m_engine_create_obj_inst("3304/0");
    if (ret < 0 && ret != -EEXIST) {
        LOG_ERR("Failed to create 3304/0: %d", ret);
        return ret;
    }
    lwm2m_engine_set_string("3304/0/5701", "%RH"); /* Units */

    /* ── IPSO Generic sensor (Object 3300, instance 0) – CO2 ─────────── */
    ret = lwm2m_engine_create_obj_inst("3300/0");
    if (ret < 0 && ret != -EEXIST) {
        LOG_ERR("Failed to create 3300/0: %d", ret);
        return ret;
    }
    lwm2m_engine_set_string("3300/0/5701", "ppm"); /* Units */

    LOG_INF("LWM2M objects created: 3303/0 (temp), 3304/0 (hum), 3300/0 (CO2)");
    return 0;
}

/* ── Push current sensor data into LWM2M resources ─────────────────────── */
static void update_lwm2m_values(const struct scd40_data *d)
{
    double temp = d->temperature;
    double hum  = d->humidity;
    double co2  = (double)d->co2_ppm;

    lwm2m_engine_set_float("3303/0/5700", &temp);
    lwm2m_engine_set_float("3304/0/5700", &hum);
    lwm2m_engine_set_float("3300/0/5700", &co2);

    LOG_INF("LWM2M updated → CO2=%u ppm  Temp=%.1f C  Hum=%.1f %%RH",
            d->co2_ppm, d->temperature, d->humidity);
}

/* ── Main ───────────────────────────────────────────────────────────────── */
int main(void)
{
    int ret;
    struct scd40_data sensor;

    LOG_INF("=== nRF54 CoAP/LWM2M SCD40 Air Quality Monitor ===");
    LOG_INF("Endpoint: %s", CONFIG_APP_LWM2M_ENDPOINT_NAME);
    LOG_INF("Server:   %s", CONFIG_APP_LWM2M_SERVER_URI);

    /* ── 1. Initialise SCD40 ─────────────────────────────────────────── */
    ret = scd40_init(i2c_dev);
    if (ret < 0) {
        LOG_ERR("SCD40 init failed (%d). Check I2C wiring.", ret);
        return ret;
    }

    ret = scd40_start_periodic_measurement(i2c_dev);
    if (ret < 0) {
        LOG_ERR("SCD40 start measurement failed: %d", ret);
        return ret;
    }

    /* SCD40 needs at least 5 s to produce the first valid measurement */
    LOG_INF("Waiting 5 s for first SCD40 measurement...");
    k_sleep(K_SECONDS(5));

    /* ── 2. Wait for IP connectivity ─────────────────────────────────── */
    net_mgmt_init_event_callback(&net_mgmt_cb, on_net_event,
                                 NET_EVENT_IPV4_ADDR_ADD |
                                 NET_EVENT_IPV6_ADDR_ADD);
    net_mgmt_add_event_callback(&net_mgmt_cb);

    /* If an address was already assigned before we registered the callback,
     * the semaphore would never be given. Check now and skip the wait. */
    struct net_if *iface = net_if_get_default();

    if (iface && net_if_flag_is_set(iface, NET_IF_DORMANT)) {
        LOG_INF("Network: waiting for link...");
    } else {
        k_sem_give(&net_ready_sem); /* already up */
    }

    LOG_INF("Network: waiting for IP address (timeout 60 s)...");
    ret = k_sem_take(&net_ready_sem, K_SECONDS(60));
    if (ret < 0) {
        LOG_ERR("Network: timed out waiting for IP address");
        return ret;
    }

    /* Short delay to let DHCP/SLAAC settle */
    k_sleep(K_SECONDS(2));

    /* ── 3. Setup LWM2M objects ──────────────────────────────────────── */
    ret = lwm2m_setup();
    if (ret < 0) {
        LOG_ERR("LWM2M setup failed: %d", ret);
        return ret;
    }

    /* ── 4. Start LWM2M RD client ────────────────────────────────────── */
    memset(&lwm2m_client, 0, sizeof(lwm2m_client));
    lwm2m_client.sock_fd = -1;

    ret = lwm2m_rd_client_start(&lwm2m_client,
                                CONFIG_APP_LWM2M_ENDPOINT_NAME,
                                0,                    /* flags */
                                lwm2m_event_handler,
                                NULL);                /* observe_cb */
    if (ret < 0) {
        LOG_ERR("LWM2M RD client start failed: %d", ret);
        return ret;
    }

    /* ── 5. Periodic sensor read & LWM2M update loop ─────────────────── */
    LOG_INF("Entering sensor loop (interval: %d s)",
            CONFIG_APP_SENSOR_READ_INTERVAL_SEC);

    while (true) {
        k_sleep(K_SECONDS(CONFIG_APP_SENSOR_READ_INTERVAL_SEC));

        ret = scd40_read_measurement(i2c_dev, &sensor);
        if (ret == -EAGAIN) {
            LOG_WRN("SCD40 data not ready yet, retrying next cycle");
            continue;
        }
        if (ret < 0) {
            LOG_ERR("SCD40 read error: %d", ret);
            continue;
        }

        update_lwm2m_values(&sensor);
    }

    /* unreachable */
    return 0;
}
