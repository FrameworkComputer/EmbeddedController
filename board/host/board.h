/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
/* Default-yes, override to no by including fake_battery module. */
#define CONFIG_BATTERY_PRESENT_CUSTOM
#undef CONFIG_CMD_PD
#define CONFIG_CBI_EEPROM
#define CONFIG_EXTPOWER_GPIO
#undef CONFIG_FMAP
#define CONFIG_POWER_BUTTON
#undef CONFIG_WATCHDOG
#define CONFIG_SWITCH
#define CONFIG_INDUCTIVE_CHARGING

#undef CONFIG_CONSOLE_HISTORY
#define CONFIG_CONSOLE_HISTORY 4

#define CONFIG_WP_ACTIVE_HIGH

#define CONFIG_LIBCRYPTOC

#define CONFIG_USB_PD_CUSTOM_PDO
#define CONFIG_USB_PD_DUAL_ROLE

#define CONFIG_CMD_AP_RESET_LOG

#include "gpio_signal.h"

enum temp_sensor_id {
	TEMP_SENSOR_CPU = 0,
	TEMP_SENSOR_BOARD,
	TEMP_SENSOR_CASE,
	TEMP_SENSOR_BATTERY,

	TEMP_SENSOR_COUNT
};

enum adc_channel {
	ADC_CH_CHARGER_CURRENT,
	ADC_AC_ADAPTER_ID_VOLTAGE,
	ADC_VBUS,

	ADC_CH_COUNT
};

enum cec_port { CEC_PORT_0, CEC_PORT_COUNT };

/* Fake test charge suppliers */
enum {
	CHARGE_SUPPLIER_TEST1,
	CHARGE_SUPPLIER_TEST2,
	CHARGE_SUPPLIER_TEST3,
	CHARGE_SUPPLIER_TEST4,
	CHARGE_SUPPLIER_TEST5,
	CHARGE_SUPPLIER_TEST6,
	CHARGE_SUPPLIER_TEST7,
	CHARGE_SUPPLIER_TEST8,
	CHARGE_SUPPLIER_TEST9,
	CHARGE_SUPPLIER_TEST10,
	CHARGE_SUPPLIER_TEST_COUNT
};

/* Standard-current Rp */
#define PD_SRC_VNC PD_SRC_DEF_VNC_MV
#define PD_SRC_RD_THRESHOLD PD_SRC_DEF_RD_THRESH_MV

/* delay necessary for the voltage transition on the power supply */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 20000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 20000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 60000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_VOLTAGE_MV 20000

#define PD_MIN_CURRENT_MA 500
#define PD_MIN_POWER_MW 7500

/* Configuration for fake Fingerprint Sensor */
#define CONFIG_SPI_CONTROLLER
#define CONFIG_SPI_FP_PORT 1 /* SPI1: third master config */

#define CONFIG_RNG
#ifdef __cplusplus
extern "C" {
#endif
void fps_event(enum gpio_signal signal);
#ifdef __cplusplus
}
#endif

#define CONFIG_CRC8
#define CONFIG_SHA256_SW

#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define I2C_PORT_EEPROM 0
#define I2C_ADDR_EEPROM_FLAGS 0x50

#endif /* __CROS_EC_BOARD_H */
