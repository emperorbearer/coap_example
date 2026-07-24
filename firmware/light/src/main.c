#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>
#include <openthread/thread.h>

#include "light_state.h"

LOG_MODULE_REGISTER(light_main, LOG_LEVEL_INF);

/* Defined in light_resource.c: wires CoAP resources to the state module. */
void light_resource_init(void);

int main(void)
{
	LOG_INF("CoAP-over-Thread light node starting");

	/* With CONFIG_NET_L2_OPENTHREAD the Thread stack is initialized and
	 * (unless MANUAL_START) attempts to attach automatically using the
	 * commissioned/joined dataset. As an FTD/router it stays awake.
	 */
	struct openthread_context *ot = openthread_get_default_context();

	if (ot == NULL) {
		LOG_ERR("no OpenThread context");
		return -1;
	}

	/* Bring up CoAP resources + restore/drive the relay output. */
	light_resource_init();

	LOG_INF("light node ready; role=%d",
		otThreadGetDeviceRole(ot->instance));

	/* Application work is interrupt/CoAP driven; nothing to poll here. */
	return 0;
}
