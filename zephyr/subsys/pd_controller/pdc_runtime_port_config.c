/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/pdc_power_mgmt.h"
#include "usbc/pdc_runtime_port_config.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pdc_port_config, LOG_LEVEL_INF);

/*
 * When runtime port config is used, all PDC nodes in the devicetree must be
 * marked with zephyr,deferred-init so that they do not automatically
 * initialize.
 */
#define PDC_ENSURE_DEFERRED_INIT(node_id, prop, idx)             \
	BUILD_ASSERT(DT_PROP(DT_PROP_BY_IDX(node_id, prop, idx), \
			     zephyr_deferred_init) == 1,         \
		     "When using runtime port config, each PDC " \
		     "must have zephyr,deferred-init set");

#define CHECK_NAMED_USBC_PORT(node_id) \
	DT_FOREACH_PROP_ELEM(node_id, pdc, PDC_ENSURE_DEFERRED_INIT)

DT_FOREACH_STATUS_OKAY(named_usbc_port, CHECK_NAMED_USBC_PORT)

/**
 * Indicates if we have queried and cached the board's USB-C configuration
 */
static bool board_config_loaded = false;

/** Cached USB-C port count */
static uint8_t port_count = 0;
/** Cached driver pointers for each port */
static const struct device *port_pdc_drivers[CONFIG_USB_PD_PORT_MAX_COUNT];

/**
 * @brief Call board code to determine USB-C port configuration and cache
 *        the data for future reference.
 */
static void query_board_port_config(void)
{
	int rv;
	uint8_t count = 0;
	const struct device *drv = NULL;

	LOG_INF("Querying USB-C port config");

	for (count = 0; count < CONFIG_USB_PD_PORT_MAX_COUNT; count++) {
		rv = board_get_pdc_for_port(count, &drv);

		if (rv) {
			/* Error occurred on this port */
			LOG_ERR("Error querying port config for C%u: %d. "
				"Port will be disabled.",
				count, rv);

			/* Discontinuous port ranges are not supported. Do not
			 * look for further ports. */
			break;
		}

		if (drv == NULL) {
			/* Port not used on this board/SKU */
			LOG_INF("No PDC for C%u. Port will be disabled.",
				count);

			/* Discontinuous port ranges are not supported. Do not
			 * look for further ports. */
			break;
		}

		port_pdc_drivers[count] = drv;

		LOG_INF("Port C%u: using driver %s", count,
			drv->name ? drv->name : "<no name>");
	}

	LOG_INF("Final USB-C count: %u", count);

	port_count = count;
	board_config_loaded = true;
}

uint8_t pdc_power_mgmt_get_usb_pd_port_count(void)
{
	if (!board_config_loaded) {
		query_board_port_config();
	}

	return port_count;
}

const struct device *pdc_power_mgmt_get_port_pdc_driver(int port)
{
	if (!board_config_loaded) {
		query_board_port_config();
	}

	if (port < 0 || port >= port_count) {
		/* Port number is invalid or the port is inactive */
		return NULL;
	}

	return port_pdc_drivers[port];
}
