/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(charger, LOG_LEVEL_INF);

int extpower_is_present(void)
{
	int port;
	int rv;
	bool acok;

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		rv = raa489000_is_acok(port, &acok);
		if ((rv == EC_SUCCESS) && acok)
			return 1;
	}

	return 0;
}

/*
 * Nereid does not have a GPIO indicating whether extpower is present,
 * so detect using the charger(s).
 */
__override void board_check_extpower(void)
{
	static int last_extpower_present;
	int extpower_present = extpower_is_present();

	if (last_extpower_present ^ extpower_present)
		extpower_handle_update(extpower_present);

	last_extpower_present = extpower_present;
}

__override void board_hibernate(void)
{
	/* Shut down the chargers */
	if (board_get_usb_pd_port_count() == 2)
		raa489000_hibernate(CHARGER_SECONDARY, true);
	raa489000_hibernate(CHARGER_PRIMARY, true);
	LOG_INF("Charger(s) hibernated");
	cflush();
}

#ifndef CONFIG_ZTEST
#define MAX_POWER_65W 20000
#define DEFAULT_POWER 15000
#define IS_SKU_ID_IN_RANGE(sku_id) \
	((sku_id >= 0x2A0000) && (sku_id <= 0x2A0010))

static uint32_t calculate_max_voltage(uint32_t val, uint32_t sku_id)
{
	if (val == MAX20 || IS_SKU_ID_IN_RANGE(sku_id)) {
		return MAX_POWER_65W;
	} else {
		return DEFAULT_POWER;
	}
}

static void adapter_voltage_limit(void)
{
	int ret;
	uint32_t val;
	int port;
	uint32_t sku_id;
	uint32_t max_voltage;

	ret = cros_cbi_get_fw_config(ADAPTER_VOLTAGE_LIMIT, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			ADAPTER_VOLTAGE_LIMIT);
	} else {
		ret = cbi_get_sku_id(&sku_id);
		if (ret != 0) {
			LOG_ERR("Error retrieving CBI SKU_ID field");
		}
	}

	if (ret != 0) {
		max_voltage = DEFAULT_POWER;
	} else {
		max_voltage = calculate_max_voltage(val, sku_id);
	}

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		pd_set_external_voltage_limit(port, max_voltage);
	}
}

DECLARE_HOOK(HOOK_INIT, adapter_voltage_limit, HOOK_PRIO_DEFAULT - 1);
#endif
