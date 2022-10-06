/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Kukui */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_KUKUI_JACUZZI
#define VARIANT_KUKUI_BATTERY_SMART
#define VARIANT_KUKUI_CHARGER_ISL9238
#define VARIANT_KUKUI_EC_STM32F098

#define VARIANT_KUKUI_NO_SENSORS

#include "baseboard.h"

#undef CONFIG_CHIPSET_POWER_SEQ_VERSION
#define CONFIG_CHIPSET_POWER_SEQ_VERSION 1

#define CONFIG_BATTERY_HW_PRESENT_CUSTOM
#define CONFIG_BATTERY_VENDOR_PARAM
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1

#define CONFIG_CHARGER_PSYS

#define CONFIG_BC12_DETECT_PI3USB9201

#define CONFIG_EXTPOWER_GPIO
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200

#define CONFIG_I2C_BITBANG
#define I2C_BITBANG_PORT_COUNT 1
#undef CONFIG_I2C_NACK_RETRY_COUNT
#define CONFIG_I2C_NACK_RETRY_COUNT 10
#define CONFIG_SMBUS_PEC

#define CONFIG_USB_PD_TCPM_FUSB302
#define CONFIG_USB_PD_DISCHARGE_GPIO
#define CONFIG_USB_PD_TCPC_LOW_POWER

#define CONFIG_USB_MUX_IT5205

#undef CONFIG_GMR_TABLET_MODE
#undef CONFIG_TABLET_MODE
#undef CONFIG_TABLET_MODE_SWITCH

/* I2C ports */
#define I2C_PORT_BC12 0
#define I2C_PORT_TCPC0 0
#define I2C_PORT_USB_MUX 0
#define I2C_PORT_BATTERY 2
#define I2C_PORT_CHARGER 1
#define I2C_PORT_KB_DISCRETE 1
#define I2C_PORT_VIRTUAL_BATTERY I2C_PORT_BATTERY

/* IT8801 I2C address */
#define KB_DISCRETE_I2C_ADDR_FLAGS IT8801_I2C_ADDR1

#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO

#ifndef __ASSEMBLER__

enum adc_channel {
	/* Real ADC channels begin here */
	ADC_BOARD_ID = 0,
	ADC_EC_SKU_ID,
	ADC_CH_COUNT
};

/* power signal definitions */
enum power_signal {
	AP_IN_S3_L,
	PMIC_PWR_GOOD,

	/* Number of signals */
	POWER_SIGNAL_COUNT,
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL = 0,
	LID_GYRO,
	LID_MAG,
	CLEAR_ALS,
	RGB_ALS,
	VSYNC,
	SENSOR_COUNT,
};

enum charge_port {
	CHARGE_PORT_USB_C,
};

enum battery_type {
	BATTERY_DYNAPACK_HIGHPOWER,
	BATTERY_DYNAPACK_COS,
	BATTERY_LGC,
	BATTERY_TYPE_COUNT,
};

#include "gpio_signal.h"
#include "registers.h"

#ifdef SECTION_IS_RO
/* Interrupt handler for emmc task */
void emmc_cmd_interrupt(enum gpio_signal signal);
#endif

void bc12_interrupt(enum gpio_signal signal);
void board_reset_pd_mcu(void);
int board_get_version(void);

/* returns the i2c port number of charger */
int board_get_charger_i2c(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
