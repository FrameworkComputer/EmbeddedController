/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adsp_comms.h"
#include "battery_smart.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(adsp_comms, LOG_LEVEL_INF);

static int active_charge_port = CHARGE_PORT_NONE;
static enum led_pwr_state active_charge_state = LED_PWRS_IDLE;
static int active_battery_status;

int charge_manager_get_active_charge_port(void)
{
	return active_charge_port;
}

enum led_pwr_state led_pwr_get_state(void)
{
	return active_charge_state;
}

static int command_chgstate(int argc, const char **argv)
{
	ccprintf("ac = %d\n", extpower_is_present());
	/* TODO: b/493490329 add the rest of the details */
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(chgstate, command_chgstate, NULL,
			"Get charge state machine status");

#ifdef CONFIG_BATTERY_STATUS_CUSTOM
test_mockable int battery_status(int *status)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return sb_read(SB_BATTERY_STATUS, status);
	}

	*status = active_battery_status;
	return EC_SUCCESS;
}
#endif

static void adsp_power_state_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	LOG_INF("ADSP: Power State: 0x%04x", data);
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_DEFAULT, ADSP_POWER_STATE_REG_VAL,
		       adsp_power_state_cb);

static void adsp_oem_magic_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	if (data == ADSP_OEM_CUSTOM_MAGIC_VAL) {
		LOG_INF("ADSP: Comms established");
	} else {
		LOG_INF("ADSP: Incorrect magic packet received (0x%04x)", data);
	}
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_MAGIC,
		       adsp_oem_magic_cb);

static void adsp_oem_version_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	if (data == ADSP_OEM_CUSTOM_VERSION_1) {
		LOG_INF("ADSP: Version 1 identified");
	} else {
		LOG_ERR("ADSP: Incorrect version received: %d (expected %d)",
			data, ADSP_OEM_CUSTOM_VERSION_1);
	}
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_VERSION,
		       adsp_oem_version_cb);

static void adsp_oem_charge_port_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	if (data == ADSP_OEM_CUSTOM_CHARGE_PORT_DISABLED) {
		active_charge_port = CHARGE_PORT_NONE;
		LOG_INF("ADSP: Charging disabled");
	} else if (data >= ADSP_OEM_CUSTOM_CHARGE_PORT_START &&
		   data <= ADSP_OEM_CUSTOM_CHARGE_PORT_COUNT) {
		active_charge_port = data - ADSP_OEM_CUSTOM_CHARGE_PORT_START;
		LOG_INF("ADSP: Charging from USB%d", active_charge_port);
	} else {
		LOG_ERR("ADSP: Invalid charge port: %d", data);
	}
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_PORT,
		       adsp_oem_charge_port_cb);

static void adsp_oem_charge_state_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	switch (data) {
	case ADSP_OEM_CUSTOM_CHARGE_STATE_CHARGE:
		active_charge_state = LED_PWRS_CHARGE;
		break;
	case ADSP_OEM_CUSTOM_CHARGE_STATE_DISCHARGE:
		active_charge_state = LED_PWRS_DISCHARGE;
		break;
	case ADSP_OEM_CUSTOM_CHARGE_STATE_ERROR:
		active_charge_state = LED_PWRS_ERROR;
		break;
	case ADSP_OEM_CUSTOM_CHARGE_STATE_IDLE:
		active_charge_state = LED_PWRS_IDLE;
		break;
	case ADSP_OEM_CUSTOM_CHARGE_STATE_FORCED_IDLE:
		active_charge_state = LED_PWRS_FORCED_IDLE;
		break;
	case ADSP_OEM_CUSTOM_CHARGE_STATE_NEAR_FULL:
		active_charge_state = LED_PWRS_CHARGE_NEAR_FULL;
		break;
	default:
		LOG_ERR("ADSP: Invalid charge state: %d", data);
		return;
	}
	LOG_INF("ADSP: Charge State - %d", data);
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM,
		       ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		       adsp_oem_charge_state_cb);

static void adsp_oem_battery_state_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	active_battery_status = (int)data;
	LOG_INF("ADSP: Battery State: 0x%04x", data);
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM,
		       ADSP_OEM_CUSTOM_REG_BATTERY_STATE,
		       adsp_oem_battery_state_cb);
