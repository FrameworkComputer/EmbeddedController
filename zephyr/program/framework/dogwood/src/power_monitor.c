/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_adc.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "driver/ina2xx.h"
#include "system.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#define INA236_IDX_PSU_12V INA236_INDEX_ADD_PIN_GND
#define INA236_IDX_PSU_5V  INA236_INDEX_ADD_PIN_VS

#define INA236_SHUNT_RESISTOR	1 /* 1 mohm */
#define INA236_ADC_RANGE	2500 /* 2500 nV */
#define INA236_ALERT_LIMIT(x) ((x * INA236_SHUNT_RESISTOR) * 1000 / INA236_ADC_RANGE)

#define INA236_MONITOR_12V_CURRENT 30000 /* 30 A */
#define INA236_MONITOR_5V_CURRENT  2500  /* 2.5 A */

static int power_monitor_update_configuration(int id, uint16_t flags)
{
	uint16_t data;
	int rv;

	data = ina2xx_read(id, INA2XX_REG_CONFIG);

	if (data == 0x0bad) {
		return EC_ERROR_UNKNOWN;
	}

	data |= flags;

	rv = ina2xx_write(INA236_IDX_PSU_12V, INA2XX_REG_CONFIG, data);

	return rv;
}

static void power_monitor_update_ina236(int idx)
{
	int rv, current;

	if (idx == INA236_IDX_PSU_12V)
		current = INA236_MONITOR_12V_CURRENT;
	else if (idx == INA236_IDX_PSU_5V)
		current = INA236_MONITOR_5V_CURRENT;
	else {
		CPRINTS("%s gets invalid index: %d", __func__, idx);
		return;
	}

	rv = power_monitor_update_configuration(idx, INA2XX_CONFIG_AVG_4);

	if (rv != EC_SUCCESS)
		CPRINTS("INA236 index %d write config fail", idx);

	rv = ina2xx_set_alert(idx, INA236_ALERT_LIMIT(current));

	if (rv != EC_SUCCESS)
		CPRINTS("INA236 index %d write alert fail", idx);

	rv = ina2xx_set_mask(idx, INA2XX_MASK_EN_SOL);

	if (rv != EC_SUCCESS)
		CPRINTS("INA236 index %d mask fail", idx);
}

void power_monitor_init(void)
{
	int index;
	int ina236_max_idx = (board_get_version() >= BOARD_VERSION_8) ? 2 : 1;

	for (index = 0; index < ina236_max_idx; index++)
		power_monitor_update_ina236(index);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, power_monitor_init, HOOK_PRIO_DEFAULT);

static void power_monitor_release_index_0(void)
{
	int rv;

	rv = ina2xx_read(INA236_IDX_PSU_12V, INA2XX_REG_MASK);

	if (rv == 0x0bad)
		CPRINTS("ina236 read mask fail");

}
DECLARE_DEFERRED(power_monitor_release_index_0);

static void power_monitor_release_index_1(void)
{
	int rv;

	rv = ina2xx_read(INA236_IDX_PSU_5V, INA2XX_REG_MASK);

	if (rv == 0x0bad)
		CPRINTS("ina236 read mask fail");

}
DECLARE_DEFERRED(power_monitor_release_index_1);

void power_monitor_interrupt_idx_0(enum gpio_signal signal)
{
	hook_call_deferred(&power_monitor_release_index_0_data, 6 * MSEC);
}

void power_monitor_interrupt_idx_1(enum gpio_signal signal)
{
	hook_call_deferred(&power_monitor_release_index_1_data, 6 * MSEC);
}
