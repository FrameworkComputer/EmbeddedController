/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "board.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/charger/isl923x_public.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "time.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, "ALS " format, ##args)

#define EEPROM_PAGE_WRITE_MS 5
#define I2C_ADDR_ALS_FLAGS 0x50
#define I2C_PORT_ALS IT83XX_I2C_CH_E

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
	crec_msleep(EEPROM_PAGE_WRITE_MS);

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
	if (!gpio_get_level(GPIO_DOOR_OPEN_EC)) {
		als_data_handler();
	}
}
DECLARE_DEFERRED(als_change_deferred);

void door_open_interrupt(enum gpio_signal s)
{
	if (als_enable) {
		hook_call_deferred(&als_change_deferred_data, 500 * MSEC);
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

		gpio_enable_interrupt(GPIO_DOOR_OPEN_EC);
		hook_call_deferred(&als_change_deferred_data, 500 * MSEC);
	} else {
		gpio_disable_interrupt(GPIO_DOOR_OPEN_EC);
	}
}
DECLARE_HOOK(HOOK_INIT, check_als_status, HOOK_PRIO_DEFAULT);
