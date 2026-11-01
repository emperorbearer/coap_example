#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "display.h"

LOG_MODULE_REGISTER(panel_display, LOG_LEVEL_INF);

#if DT_HAS_CHOSEN(zephyr_display)

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>

/* Coalesce window: wait this long after the last change before refreshing, so
 * a burst of encoder ticks results in a single e-paper update. */
#define REFRESH_COALESCE_MS 400
/* Do a full (ghost-clearing) refresh every N updates; partial otherwise. */
#define FULL_REFRESH_EVERY  20

static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static struct k_work_delayable refresh_work;
static struct k_mutex g_lock;
static struct panel_ui g_ui;
static uint32_t update_count;
static bool ready;

static void render(bool full)
{
	char line[24];

	/* Full refresh: clear the physical panel (drives the clearing waveform)
	 * to remove accumulated ghosting; otherwise just clear the framebuffer. */
	cfb_framebuffer_clear(disp, full);

	snprintf(line, sizeof(line), "Light: %s", g_ui.on ? "ON" : "OFF");
	cfb_print(disp, line, 0, 0);

	snprintf(line, sizeof(line), "Bri: %d%%", (g_ui.bri * 100) / 254);
	cfb_print(disp, line, 0, 16);

	snprintf(line, sizeof(line), "Enc: %s", g_ui.ct_mode ? "TEMP" : "BRIGHT");
	cfb_print(disp, line, 0, 32);

	/* Show color temperature in kelvin (mireds -> K). */
	if (g_ui.ct > 0) {
		snprintf(line, sizeof(line), "CT: %uK", 1000000u / g_ui.ct);
	} else {
		snprintf(line, sizeof(line), "CT: -");
	}
	cfb_print(disp, line, 0, 48);

	if (g_ui.battery_pct >= 0) {
		snprintf(line, sizeof(line), "Batt: %d%%", g_ui.battery_pct);
		cfb_print(disp, line, 0, 64);
	}

	cfb_framebuffer_finalize(disp);
}

static void refresh_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!ready) {
		return;
	}
	k_mutex_lock(&g_lock, K_FOREVER);
	render((update_count % FULL_REFRESH_EVERY) == 1);
	k_mutex_unlock(&g_lock);
}

int panel_display_init(void)
{
	k_mutex_init(&g_lock);
	k_work_init_delayable(&refresh_work, refresh_handler);

	if (!device_is_ready(disp)) {
		LOG_WRN("display not ready; UI disabled");
		return -ENODEV;
	}
	if (cfb_framebuffer_init(disp) != 0) {
		LOG_ERR("cfb init failed");
		return -EIO;
	}
	cfb_framebuffer_set_font(disp, 0);
	display_blanking_off(disp);
	ready = true;
	LOG_INF("e-paper display ready");
	return 0;
}

void panel_display_request(const struct panel_ui *ui)
{
	k_mutex_lock(&g_lock, K_FOREVER);
	g_ui = *ui;
	update_count++;
	k_mutex_unlock(&g_lock);

	/* (Re)schedule a single refresh; bursts collapse into one update. */
	k_work_reschedule(&refresh_work, K_MSEC(REFRESH_COALESCE_MS));
}

#else /* no chosen display: no-op stubs so the app builds without an EPD */

int panel_display_init(void)
{
	return 0;
}

void panel_display_request(const struct panel_ui *ui)
{
	ARG_UNUSED(ui);
}

#endif /* DT_HAS_CHOSEN(zephyr_display) */
