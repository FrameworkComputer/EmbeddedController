/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* cappy2 board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_KEEBY_EC_NPCX797FC
#include "baseboard.h"

#undef	GPIO_VOLUME_UP_L
#undef	GPIO_VOLUME_DOWN_L
#undef	CONFIG_VOLUME_BUTTONS

/* System unlocked in early development */
#define CONFIG_SYSTEM_UNLOCKED

/*
 * The RAM and flash size combination on the the NPCX797FC does not leave
 * any unused flash space that can be used to store the .init_rom section.
 */
#undef CONFIG_CHIP_INIT_ROM_REGION

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1
#define CONFIG_HOSTCMD_BATTERY_V2

/* Charger */
#define CONFIG_CHARGER_RAA489000
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SINGLE_CHIP
#define CONFIG_CHARGER_PROFILE_OVERRIDE
#undef	CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (100 * MSEC)

/* Keyboard */
#undef	CONFIG_PWM_KBLIGHT

/* LED defines */
#define CONFIG_LED_COMMON
#define CONFIG_LED_ONOFF_STATES
#define GPIO_BAT_LED_RED_L GPIO_LED_R_ODL
#define GPIO_BAT_LED_GREEN_L GPIO_LED_G_ODL
#define GPIO_PWR_LED_BLUE_L GPIO_LED_B_ODL

/* PWM */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is used as PWM1. */

/******************************************************************************/

/* USB PD */
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPM_RAA489000

/* USB defines specific to external TCPCs */
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_TCPC_LOW_POWER

/* Variant references the TCPCs to determine Vbus sourcing */
#define CONFIG_USB_PD_5V_EN_CUSTOM

/* BC1.2 */
#define CONFIG_BC12_DETECT_PI3USB9201

/* MUX */
#define CONFIG_USB_MUX_PS8743

/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B
#define CONFIG_TEMP_SENSOR_POWER_GPIO GPIO_EN_PP3300_A

/* I2C configuration */
#define I2C_PORT_EEPROM     NPCX_I2C_PORT7_0
#define I2C_PORT_BATTERY    NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR     NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0     NPCX_I2C_PORT1_0
#define I2C_PORT_USB_MUX    I2C_PORT_USB_C0

#define I2C_ADDR_EEPROM_FLAGS 0x50 /* 7b address */

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1,     /* ADC0 */
	ADC_TEMP_SENSOR_2,     /* ADC1 */
	ADC_SUB_ANALOG,	       /* ADC2 */
	ADC_TEMP_SENSOR_3,     /* ADC6 */
	ADC_VSNS_PP3300_A,     /* ADC9 */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1,
	TEMP_SENSOR_2,
	TEMP_SENSOR_3,
	TEMP_SENSOR_COUNT
};

enum battery_type {
	BATTERY_SDI,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
