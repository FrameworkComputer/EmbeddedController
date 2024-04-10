/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Casta board-specific configuration */

#include "adc.h"
#include "battery.h"
#include "cbi_ssfc.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/charger/bd9995x.h"
#include "driver/charger/bq25710.h"
#include "driver/charger/isl923x.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

static uint8_t sku_id;

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_PD_C0_INT_ODL:
		nx20p348x_interrupt(0);
		break;

	case GPIO_USB_PD_C1_INT_ODL:
		nx20p348x_interrupt(1);
		break;

	default:
		break;
	}
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_AMB] = { "TEMP_AMB", NPCX_ADC_CH0, ADC_MAX_VOLT,
				  ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_CHARGER] = { "TEMP_CHARGER", NPCX_ADC_CH1,
				      ADC_MAX_VOLT, ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* TODO(b/119872005): Casta: confirm thermistor parts */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_BATTERY] = { .name = "Battery",
				  .type = TEMP_SENSOR_TYPE_BATTERY,
				  .read = charge_get_battery_temp,
				  .idx = 0 },
	[TEMP_SENSOR_AMBIENT] = { .name = "Ambient",
				  .type = TEMP_SENSOR_TYPE_BOARD,
				  .read = get_temp_3v3_51k1_47k_4050b,
				  .idx = ADC_TEMP_SENSOR_AMB },
	[TEMP_SENSOR_CHARGER] = { .name = "Charger",
				  .type = TEMP_SENSOR_TYPE_BOARD,
				  .read = get_temp_3v3_13k7_47k_4050b,
				  .idx = ADC_TEMP_SENSOR_CHARGER },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Charger config.  Start i2c address at isl9238, update during runtime */
struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};
const unsigned int chg_cnt = ARRAY_SIZE(chg_chips);

/*
 * I2C callbacks to ensure bus free time for battery I2C transactions is at
 * least 5ms.
 */
#define BATTERY_FREE_MIN_DELTA_US (5 * MSEC)
static timestamp_t battery_last_i2c_time;

static int is_battery_i2c(const int port, const uint16_t addr_flags)
{
	return (port == I2C_PORT_BATTERY) && (addr_flags == BATTERY_ADDR_FLAGS);
}

static int is_battery_port(int port)
{
	return (port == I2C_PORT_BATTERY);
}

void i2c_start_xfer_notify(const int port, const uint16_t addr_flags)
{
	unsigned int time_delta_us;

	if (!is_battery_i2c(port, addr_flags))
		return;

	time_delta_us = time_since32(battery_last_i2c_time);
	if (time_delta_us >= BATTERY_FREE_MIN_DELTA_US)
		return;

	crec_usleep(BATTERY_FREE_MIN_DELTA_US - time_delta_us);
}

void i2c_end_xfer_notify(const int port, const uint16_t addr_flags)
{
	/*
	 * The bus free time needs to be maintained from last transaction
	 * on I2C bus to any device on it to the next transaction to battery.
	 */
	if (!is_battery_port(port))
		return;

	battery_last_i2c_time = get_time();
}

/* Read CBI from i2c eeprom and initialize variables for board variants */
static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_sku_id(&val) != EC_SUCCESS || val > UINT8_MAX)
		return;
	sku_id = val;
	CPRINTS("SKU: %d", sku_id);
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C);

static void board_init(void)
{
	if (get_cbi_ssfc_charger() != SSFC_CHARGER_BQ25710)
		return;

	chg_chips[0].drv = &bq25710_drv;
	chg_chips[0].i2c_addr_flags = BQ25710_SMBUS_ADDR1_FLAGS;
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_INIT_I2C);

static void set_input_limit_on_ac_removal(void)
{
	if (extpower_is_present())
		return;

	if (get_cbi_ssfc_charger() != SSFC_CHARGER_BQ25710)
		return;

	charger_set_input_current_limit(0,
					CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT);
}
DECLARE_HOOK(HOOK_AC_CHANGE, set_input_limit_on_ac_removal, HOOK_PRIO_DEFAULT);

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Check that port number is valid. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;

	/* Note that the level is inverted because the pin is active low. */
	gpio_set_level(GPIO_USB_C_OC, !is_overcurrented);
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (sku_id == 2)
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}
