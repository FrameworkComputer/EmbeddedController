/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_DEDEDE_EC_NPCX796FC
#include "baseboard.h"

#define CONFIG_USB_PD_DEBUG_LEVEL 2

#ifdef BOARD_MAGOLOR_LEGACY
/* this change saves 1656 bytes of RW flash space */
#define CONFIG_CHIP_INIT_ROM_REGION
#define CONFIG_DEBUG_ASSERT_BRIEF
#else
/*
 * The RAM and flash size combination on the the NPCX797FC does not leave
 * any unused flash space that can be used to store the .init_rom section.
 */
#undef CONFIG_CHIP_INIT_ROM_REGION
#endif

/* Remove default commands to free flash space */
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_BATTFAKE

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1
#define CONFIG_HOSTCMD_BATTERY_V2

/* Charger */
/* Set trickle charge current by taking integer value */
#undef CONFIG_RAA489000_TRICKLE_CHARGE_CURRENT
#define CONFIG_RAA489000_TRICKLE_CHARGE_CURRENT 256
#define CONFIG_CHARGER_RAA489000
#define PD_MAX_VOLTAGE_MV 20000
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_OCPC_DEF_RBATT_MOHMS               \
	22 /* R_DS(on) 11.6mOhm + 10mOhm sns rstr \
	    */
#define CONFIG_OCPC
#undef CONFIG_CHARGER_SINGLE_CHIP
#undef CONFIG_CMD_CHARGER_DUMP
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (100 * MSEC)
/*
 * b/147463641: The charger IC seems to overdraw ~4%, therefore we
 * reduce our target accordingly.
 */
#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 4

/* GPIO for C1 interrupts, for baseboard use */
#define GPIO_USB_C1_INT_ODL GPIO_SUB_C1_INT_EN_RAILS_ODL

/* Keyboard */
#define CONFIG_KEYBOARD_FACTORY_TEST
#ifdef BOARD_MAGOLOR

#define CONFIG_KEYBOARD_KEYPAD
#endif
#define CONFIG_PWM_KBLIGHT

/* LED defines */
#define CONFIG_LED_ONOFF_STATES

/* PWM */
#define CONFIG_PWM
#define NPCX7_PWM1_SEL 1 /* GPIO C2 is used as PWM1. */

/* Temp sensor */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THROTTLE_AP
#define CONFIG_THERMISTOR_NCP15WB
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* USB */
#define CONFIG_BC12_DETECT_PI3USB9201
#define CONFIG_USBC_RETIMER_PS8802
#define CONFIG_USB_MUX_PS8743
#define CONFIG_USB_MUX_RUNTIME_CONFIG

/* Common USB-A defines */
#define USB_PORT_COUNT 2
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_CDP
#define CONFIG_USB_PORT_POWER_SMART_INVERTED
#define GPIO_USB1_ILIM_SEL GPIO_USB_A0_CHARGE_EN_L
#define GPIO_USB2_ILIM_SEL GPIO_USB_A1_CHARGE_EN_L

/*******************************************************************************/

/* USB PD */
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPM_RAA489000

/* USB defines specific to external TCPCs */
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_TCPC_LOW_POWER

/* Variant references the TCPCs to determine Vbus sourcing */
#define CONFIG_USB_PD_5V_EN_CUSTOM

/* I2C configuration */
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0
#define I2C_PORT_BATTERY NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0 NPCX_I2C_PORT1_0
#define I2C_PORT_SUB_USB_C1 NPCX_I2C_PORT2_0
#define I2C_PORT_USB_MUX I2C_PORT_USB_C0
/* TODO(b:147440290): Need to handle multiple charger ICs */
#define I2C_PORT_CHARGER I2C_PORT_USB_C0

#define I2C_PORT_ACCEL I2C_PORT_SENSOR

#define I2C_ADDR_EEPROM_FLAGS 0x50 /* 7b address */

/*
 * I2C pin names for baseboard
 *
 * Note: these lines will be set as i2c on start-up, but this should be
 * okay since they're ODL.
 */
#define GPIO_EC_I2C_SUB_USB_C1_SCL GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL
#define GPIO_EC_I2C_SUB_USB_C1_SDA GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL

/* Sensors */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

#define CONFIG_ACCEL_BMA255 /* Lid accel */
#define CONFIG_ACCELGYRO_BMI160 /* Base accel */

#ifdef BOARD_MAGOLOR
#define CONFIG_ACCEL_KX022 /* Lid accel */
#define CONFIG_ACCELGYRO_ICM426XX /* Base accel second source*/
#endif

/* Lid operates in forced mode, base in FIFO */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)
#define CONFIG_ACCEL_FIFO
#define CONFIG_ACCEL_FIFO_SIZE 256 /* Must be a power of 2 */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#ifdef BOARD_MAGOLOR
#define CONFIG_ACCELGYRO_ICM426XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#endif

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

/* Volume Button feature */
#define CONFIG_ADC_BUTTONS
#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_VOLDN_BTN_ODL

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum chg_id {
	CHARGER_PRIMARY,
	CHARGER_SECONDARY,
	CHARGER_NUM,
};

enum adc_channel {
	ADC_TEMP_SENSOR_1, /* ADC0 */
	ADC_TEMP_SENSOR_2, /* ADC1 */
	ADC_SUB_ANALOG, /* ADC2 */
	ADC_VSNS_PP3300_A, /* ADC9 */
	ADC_CH_COUNT
};

enum temp_sensor_id { TEMP_SENSOR_1, TEMP_SENSOR_2, TEMP_SENSOR_COUNT };

enum sensor_id { LID_ACCEL, BASE_ACCEL, BASE_GYRO, SENSOR_COUNT };

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT,
};

enum battery_type {
	BATTERY_AP19B8M,
	BATTERY_AP18C7M,
	BATTERY_LGC_AP18C8K,
	BATTERY_MURATA_AP18C4K,
	BATTERY_COSMX_AP20CBL,
	BATTERY_COSMX_AP20CBL_004,
	BATTERY_TYPE_COUNT,
};

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

void motion_interrupt(enum gpio_signal signal);
void pen_detect_interrupt(enum gpio_signal s);
#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
