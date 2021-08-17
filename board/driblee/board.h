/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lalala board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_KEEBY_EC_NPCX797FC
#include "baseboard.h"

/*
 * The RAM and flash size combination on the the NPCX797FC does not leave
 * any unused flash space that can be used to store the .init_rom section.
 */
#undef CONFIG_CHIP_INIT_ROM_REGION

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE

/* Charger */
#define CONFIG_CHARGER_RAA489000
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#undef CONFIG_CMD_CHARGER_DUMP
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (100 * MSEC)
#define CONFIG_MATH_UTIL

/*
 * GPIO for C1 interrupts, for baseboard use
 *
 * Note this line might already have its pull up disabled for HDMI DBs, but
 * it should be fine to set again before z-state.
 */
#define GPIO_EC_HDMI_EN_ODL GPIO_EC_I2C_SBU_USB_C1_SCL
#define GPIO_HDMI_PP3300_EN GPIO_SUB_USB_C1_INT_ODL

/* Keyboard */

#define CONFIG_KEYBOARD_KEYPAD

/* LED defines */
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LED_COMMON
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

/* PWM */
#define CONFIG_PWM
#define NPCX7_PWM1_SEL    1  /* GPIO C2 is used as PWM1. */

/* Temp sensor */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THROTTLE_AP
#define CONFIG_THERMISTOR_NCP15WB
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* USB */
#define CONFIG_BC12_DETECT_PI3USB9201
#define CONFIG_USBC_RETIMER_PS8802

/* Common USB-A defines */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_SMART
#undef CONFIG_USB_PORT_POWER_SMART_PORT_COUNT
#define CONFIG_USB_PORT_POWER_SMART_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_CDP
#define CONFIG_USB_PORT_POWER_SMART_INVERTED
#define GPIO_USB1_ILIM_SEL GPIO_USB_A0_CHARGE_EN_L

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

/* I2C configuration */
#define I2C_PORT_EEPROM     NPCX_I2C_PORT7_0
#define I2C_PORT_BATTERY    NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR     NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0     NPCX_I2C_PORT1_0
#define I2C_PORT_SUB_USB_C1 NPCX_I2C_PORT2_0
#define I2C_PORT_USB_MUX    I2C_PORT_USB_C0
/* TODO(b:147440290): Need to handle multiple charger ICs */
#define I2C_PORT_CHARGER    I2C_PORT_USB_C0

#define I2C_PORT_ACCEL      I2C_PORT_SENSOR

#define I2C_ADDR_EEPROM_FLAGS 0x50 /* 7b address */

/* Volume Button feature */
#define CONFIG_ADC_BUTTONS
#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_VOLDN_BTN_ODL

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1,     /* ADC0 */
	ADC_TEMP_SENSOR_2,     /* ADC1 */
	ADC_SUB_ANALOG,	       /* ADC2 */
	ADC_VSNS_PP3300_A,     /* ADC9 */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1,
	TEMP_SENSOR_2,
	TEMP_SENSOR_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT,
};

/* List of possible batteries */
enum battery_type {
	BATTERY_BYD_1VX1H,
	BATTERY_BYD_YT39X,
	BATTERY_BYD_X0Y5M,
	BATTERY_LGC_FDRHM,
	BATTERY_LGC_8GHCX,
	BATTERY_SWD_ATL_WJPC4,
	BATTERY_SWD_ATL_CTGKT,
	BATTERY_SWD_COS_WJPC4,
	BATTERY_SWD_COS_CTGKT,
	BATTERY_SMP_ATL_VM732,
	BATTERY_SMP_ATL_26JGK,
	BATTERY_SMP_ATL_RF9H3,
	BATTERY_SMP_COS_VM732,
	BATTERY_SMP_COS_26JGK,
	BATTERY_SMP_COS_RF9H3,
	BATTERY_BYD16,
	BATTERY_LGC3,
	BATTERY_SIMPLO,
	BATTERY_SIMPLO_LS,
	BATTERY_TYPE_COUNT,
};

int board_is_sourcing_vbus(int port);
#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
