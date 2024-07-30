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
#define EEPROM_DATA_VERIFY 0xaa
#define I2C_ADDR_ALS_FLAGS 0x50
#define I2C_PORT_ALS IT83XX_I2C_CH_E

#define ALS_ENABLE BIT(0)
#define FACTORY_CLEAR BIT(1)

static int als_enable = 0;
static int als_det_enable = 1;

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
	uint8_t data[4];

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

static void check_als_status(void)
{
	uint8_t data[3];

	als_eeprom_read(0x00, data, 3);
	CPRINTS("data:%d, %d, %d ", data[0], data[1], data[2]);

	/* Check if the first three bytes are "CBI", otherwise we need
	 * disable als function and wait factory clear eeprom data.
	 */
	if ((data[0] == 0x43) && (data[1] == 0x42) && (data[2] == 0x49)) {
		CPRINTS("als eeprom need clear! disable als function");
		als_enable = 0;
	} else {
		/* Enable als function */
		if ((data[0] & ALS_ENABLE) || (data[1] != EEPROM_DATA_VERIFY)) {
			als_enable = 1;
		}
	}
}
DECLARE_HOOK(HOOK_INIT, check_als_status, HOOK_PRIO_DEFAULT);

int als_enable_status(void)
{
	return als_enable;
}

static void als_change_deferred(void)
{
	static bool debouncing;
	int out;

	out = gpio_get_level(GPIO_DOOR_OPEN_EC);

	if (out == 0) {
		if (!debouncing)
			debouncing = true;

		debouncing = false;
		als_data_handler();
		chipset_force_shutdown(CHIPSET_SHUTDOWN_BOARD_CUSTOM);
		if (extpower_is_present()) {
			CPRINTS("AC off!");
			tcpc_write(0, TCPC_REG_COMMAND,
				   TCPC_REG_COMMAND_SNK_CTRL_LOW);
			raa489000_enable_asgate(0, false);
		}
		cflush();
		als_det_enable = 0;
		if (battery_is_present()) {
			CPRINTS("cut off!");
			board_cut_off_battery();
		}
	}
}
DECLARE_DEFERRED(als_change_deferred);

static void check_als(void)
{
	if (als_enable && als_det_enable) {
		hook_call_deferred(&als_change_deferred_data, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, check_als, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_SECOND, check_als, HOOK_PRIO_DEFAULT);
