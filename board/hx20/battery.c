/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "i2c.h"
#include "string.h"
#include "util.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define PARAM_CUT_OFF_LOW  0x10
#define PARAM_CUT_OFF_HIGH 0x00

/* Battery info for BQ40Z50 */
static const struct battery_info info = {
	.voltage_max = 17600,        /* mV */
	.voltage_normal = 15400,
	.voltage_min = 12000,
	.precharge_current = 72,   /* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 47,
	.charging_min_c = 0,
	.charging_max_c = 52,
	.discharging_min_c = 0,
	.discharging_max_c = 62,
};

static int old_btp;

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	int rv;
	uint8_t buf[3];

	/* Ship mode command must be sent twice to take effect */
	buf[0] = SB_MANUFACTURER_ACCESS & 0xff;
	buf[1] = PARAM_CUT_OFF_LOW;
	buf[2] = PARAM_CUT_OFF_HIGH;

	i2c_lock(I2C_PORT_BATTERY, 1);
	rv = i2c_xfer_unlocked(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS,
			       buf, 3, NULL, 0, I2C_XFER_SINGLE);
	rv |= i2c_xfer_unlocked(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS,
				buf, 3, NULL, 0, I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_BATTERY, 0);

	return rv;
}

enum battery_present battery_is_present(void)
{
	enum battery_present bp;
	int mv;

	mv = adc_read_channel(ADC_VCIN1_BATT_TEMP);

	if (mv == ADC_READ_ERROR)
		return -1;

	bp = (mv < 3000 ? BP_YES : BP_NO);

	return bp;
}

#ifdef CONFIG_EMI_REGION1

void battery_customize(struct charge_state_data *emi_info)
{
	char text[32];
	char *str = "LION";
	int value;
	int new_btp;
	static int batt_state;

	/* Update EMI */
	*host_get_customer_memmap(0x03) = (emi_info->batt.temperature - 2731)/10;
	*host_get_customer_memmap(0x06) = emi_info->batt.display_charge/10;
	
	if (emi_info->batt.status & STATUS_FULLY_CHARGED)
		*host_get_customer_memmap(0x07) |= BIT(0);
	else
		*host_get_customer_memmap(0x07) &= ~BIT(0);

	battery_device_chemistry(text, sizeof(text));
	if (!strncmp(text, str, 4))
		*host_get_customer_memmap(0x07) |= BIT(1);
	else
		*host_get_customer_memmap(0x07) &= ~BIT(1);

	battery_get_mode(&value);
	if (value & MODE_CAPACITY)
		*host_get_customer_memmap(0x07) |= BIT(2);
	else
		*host_get_customer_memmap(0x07) &= ~BIT(2);
	
	/* BTP: Notify AP update battery */
	new_btp = *host_get_customer_memmap(0x08) + (*host_get_customer_memmap(0x09) << 8);
	if (new_btp > old_btp)
	{
		if (emi_info->batt.remaining_capacity > new_btp)
		{
			old_btp = new_btp;
			host_set_single_event(EC_HOST_EVENT_BATT_BTP);
			ccprintf ("trigger higher BTP: %d", old_btp);
		}
	}
	else if (new_btp < old_btp)
	{
		if (emi_info->batt.remaining_capacity < new_btp)
		{
			old_btp = new_btp;
			host_set_single_event(EC_HOST_EVENT_BATT_BTP);
			ccprintf ("trigger lower BTP: %d", old_btp);
		}
	}

	/*
	 * When the battery present have change notify AP
	 */
	if (batt_state != emi_info->batt.is_present) {
		host_set_single_event(EC_HOST_EVENT_BATTERY_STATUS);
		batt_state = emi_info->batt.is_present;
	}
}

#endif
