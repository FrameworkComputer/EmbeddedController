/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "dptf.h"
#include "fan.h"
#include "gpio/gpio.h"
#include "hooks.h"
#include "task.h"
#include "time.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#define ROLLING_PERCENT 50
#define PUJJOTEEN_FAN_PRESENT 0x4

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

enum override_status {
	OVERRIDE_NONE,
	OVERRIDE_CHECK,
	OVERRIDE_15W,
};

struct sku_id_map {
	uint32_t ori_sku_id;
	uint32_t remap_sku_id;
};

/* SKU ID Map */
const struct sku_id_map sku_ids[] = {
	{ .ori_sku_id = 0xa0012, .remap_sku_id = 0xa0054 },
	{ .ori_sku_id = 0xa0013, .remap_sku_id = 0xa0056 },
	{ .ori_sku_id = 0xa0015, .remap_sku_id = 0xa005a },
	{ .ori_sku_id = 0xa0016, .remap_sku_id = 0xa005c },
	{ .ori_sku_id = 0xa002a, .remap_sku_id = 0xa0055 },
	{ .ori_sku_id = 0xa002b, .remap_sku_id = 0xa0057 },
	{ .ori_sku_id = 0xa002d, .remap_sku_id = 0xa005b },
	{ .ori_sku_id = 0xa002e, .remap_sku_id = 0xa005d },
};

static uint32_t sku_id;
static uint32_t fw_config;
static int override_flag = OVERRIDE_NONE;

void check_fan_status(void)
{
	override_flag = OVERRIDE_NONE;

	/* Force set duty as ROLLING_PERCENT to avoid that EC can't get RPM when
	 * duty cycle is zero.
	 */
	fan_set_duty(0, ROLLING_PERCENT);

	if (fan_get_rpm_actual(0) != 0) {
		override_flag = OVERRIDE_15W;
		fw_config |= PUJJOTEEN_FAN_PRESENT;
		for (int i = 0; i < (sizeof(sku_ids) / sizeof(sku_ids[0]));
		     i++) {
			if (sku_id == sku_ids[i].ori_sku_id) {
				sku_id = sku_ids[i].remap_sku_id;
				break;
			}
		}
	} else {
		/* Disable the fan */
		fan_set_count(0);
		fan_set_enabled(0, 0);
	}
	/* Configure to Thermal Table Control */
	dptf_set_fan_duty_target(-1);
}

K_MUTEX_DEFINE(check_fan_lock);
int cbi_board_override(enum cbi_data_tag tag, uint8_t *buf, uint8_t *size)
{
	mutex_lock(&check_fan_lock);
	if (override_flag == OVERRIDE_CHECK)
		check_fan_status();
	mutex_unlock(&check_fan_lock);
	if (override_flag == OVERRIDE_15W) {
		switch (tag) {
		case CBI_TAG_SKU_ID:
			memcpy(buf, &sku_id, *size);
			break;
		case CBI_TAG_FW_CONFIG:
			memcpy(buf, &fw_config, *size);
			break;
		default:
			break;
		}
	}

	return EC_SUCCESS;
}

void set_fan_status(void)
{
	override_flag = OVERRIDE_CHECK;

	fan_set_enabled(0, 1);
	dptf_set_fan_duty_target(ROLLING_PERCENT);
}
DECLARE_DEFERRED(set_fan_status);

/*
 * Pujjo fan support
 */
test_export_static void fan_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * Retrieve the fan config.
	 */
	ret = cros_cbi_get_fw_config(FW_FAN, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_FAN);
		return;
	}

	if (val != FW_FAN_PRESENT) {
		/* Disable the fan */
		fan_set_count(0);

		ret = cbi_get_board_version(&val);
		if (ret != 0) {
			LOG_ERR("Error retrieving CBI BOARD_VERSION field");
			return;
		}
		if (val == 3) {
			ret = cbi_get_sku_id(&sku_id);
			if (ret != 0) {
				LOG_ERR("Error retrieving CBI SKU_ID field");
				return;
			}
			ret = cbi_get_fw_config(&fw_config);
			if (ret != 0) {
				LOG_ERR("Error retrieving CBI FW_CONFIG field");
				return;
			}

			for (int i = 0;
			     i < (sizeof(sku_ids) / sizeof(sku_ids[0])); i++) {
				if (sku_id == sku_ids[i].ori_sku_id) {
					/* Enable the fan */
					fan_set_count(1);

					/* Configure the fan enable GPIO */
					gpio_pin_configure_dt(
						GPIO_DT_FROM_NODELABEL(
							gpio_fan_enable),
						GPIO_OUTPUT);

					/* Trigger setup of fan duty cycle in
					 * 100ms
					 */
					hook_call_deferred(&set_fan_status_data,
							   100 * MSEC);
					break;
				}
			}
		}
	} else {
		/* Configure the fan enable GPIO */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_fan_enable),
				      GPIO_OUTPUT);
	}
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_POST_FIRST);
