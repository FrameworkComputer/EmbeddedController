/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, "ALS " format, ##args)

#define EEPROM_PAGE_WRITE_MS 5
#define I2C_ADDR_ALS_FLAGS 0x50
#define I2C_PORT_ALS I2C_PORT_BATTERY

#define ALS_ENABLE BIT(0)
#define FACTORY_CLEAR BIT(1)

static int als_enable = 0;

static int als_eeprom_read(uint8_t offset, uint8_t *data, int len)
{
	return i2c_read_block(I2C_PORT_ALS, I2C_ADDR_ALS_FLAGS, offset, data,
			      len);
}

static int als_eeprom_write(uint8_t offset, uint8_t *data, int len)
{
	int rv;

	rv = i2c_write_block(I2C_PORT_ALS, I2C_ADDR_ALS_FLAGS, offset, data,
			     len);
	if (rv) {
		CPRINTS("Failed to write for %d", rv);
		return rv;
	}
	/* Wait for internal write cycle completion */
	k_msleep(EEPROM_PAGE_WRITE_MS);

	return EC_SUCCESS;
}

/* The number of disassembly count is stored in
 * the position 0x02--0x05
 */
static void als_data_handler(void)
{
	int als_data = 0;
	uint8_t data[4] = { 0 };

	als_eeprom_read(0x02, data, 4);
	als_data = data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0];
	als_data++;

	data[0] = als_data & 0xFF;
	data[1] = (als_data >> 8) & 0xFF;
	data[2] = (als_data >> 16) & 0xFF;
	data[3] = (als_data >> 24) & 0xFF;
	als_eeprom_write(0x02, data, 4);
	CPRINTS(" %d", als_data);
}

static void als_change_deferred(void)
{
	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(ec_als_odl))) {
		als_data_handler();
	}
}
DECLARE_DEFERRED(als_change_deferred);

void door_open_interrupt(enum gpio_signal s)
{
	if (als_enable) {
		hook_call_deferred(&als_change_deferred_data,
				   500 * USEC_PER_MSEC);
	} else {
		hook_call_deferred(&als_change_deferred_data, -1);
	}
}

static void check_als_status(void)
{
	uint8_t data[3] = { 0 };

	als_eeprom_read(0x00, data, 3);
	CPRINTS("data:%d, %d, %d ", data[0], data[1], data[2]);

	/* check als function status
	 * Bit6 is reserved for judging whether the CBI file is pre-burned.
	 * Normally, we will not set the Bit6 position. */
	if ((data[0] & ALS_ENABLE) && (data[0] != 0x43)) {
		als_enable = 1;
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_als_status));
		hook_call_deferred(&als_change_deferred_data,
				   500 * USEC_PER_MSEC);
	} else {
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_als_status));
		hook_call_deferred(&als_change_deferred_data, -1);
	}
}
DECLARE_HOOK(HOOK_INIT, check_als_status, HOOK_PRIO_DEFAULT);
