/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_DEDEDE_EC_NPCX796FC
#include "baseboard.h"

/*
 * Keep the system unlocked in early development.
 * TODO(b/151264302): Make sure to remove this before production!
 */
#define CONFIG_SYSTEM_UNLOCKED

/*
 * The RAM and flash size combination on the the NPCX797FC does not leave
 * any unused flash space that can be used to store the .init_rom section.
 */
#undef CONFIG_CHIP_INIT_ROM_REGION

/* Save some flash space */
#define CONFIG_DEBUG_ASSERT_BRIEF
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_BATTFAKE

/* EC console commands */
#define CONFIG_CMD_TCPC_DUMP
#define CONFIG_CMD_CHARGER_DUMP

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE

/* Charger */
#define CONFIG_CHARGER_RAA489000
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_OCPC_DEF_RBATT_MOHMS 22 /* R_DS(on) 11.6mOhm + 10mOhm sns rstr */
#define CONFIG_OCPC
#define CONFIG_CHARGE_RAMP_HW
#undef  CONFIG_CHARGER_SINGLE_CHIP
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (100 * MSEC)
#define CONFIG_BATTERY_CHECK_CHARGE_TEMP_LIMITS
#define CONFIG_CHARGER_PROFILE_OVERRIDE

/*
 * GPIO for C1 interrupts, for baseboard use
 *
 * Note this line might already have its pull up disabled for HDMI DBs, but
 * it should be fine to set again before z-state.
 */
#define GPIO_USB_C1_INT_ODL GPIO_SUB_C1_INT_EN_RAILS_ODL

/* Keyboard */

/* LED */
#define CONFIG_LED_COMMON
#define CONFIG_LED_ONOFF_STATES
#define GPIO_BAT_LED_RED_L GPIO_LED_R_ODL
#define GPIO_BAT_LED_GREEN_L GPIO_LED_G_ODL
#define GPIO_PWR_LED_BLUE_L GPIO_LED_B_ODL


/* PWM */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */

/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* USB */
#define CONFIG_BC12_DETECT_PI3USB9201
#define CONFIG_USB_MUX_PS8743

/* USB PD */
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPM_RAA489000
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (100 * MSEC)
#define CONFIG_USB_PD_COMM_LOCKED

/* USB defines specific to external TCPCs */
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_TCPC_LOW_POWER

/* USB Type A Features */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_DUMB

/* Variant references the TCPCs to determine Vbus sourcing */
#define CONFIG_USB_PD_5V_EN_CUSTOM

#undef PD_POWER_SUPPLY_TURN_ON_DELAY
#undef PD_POWER_SUPPLY_TURN_OFF_DELAY
#undef CONFIG_USBC_VCONN_SWAP_DELAY_US
/* 20% margin added for these timings */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	13080	/* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	16080	/* us */
#undef CONFIG_USBC_VCONN_SWAP_DELAY_US
#define CONFIG_USBC_VCONN_SWAP_DELAY_US		787	/* us */

/* I2C configuration */
#define I2C_PORT_EEPROM     NPCX_I2C_PORT7_0
#define I2C_PORT_BATTERY    NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR     NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0     NPCX_I2C_PORT1_0
#define I2C_PORT_SUB_USB_C1 NPCX_I2C_PORT2_0
#define I2C_PORT_USB_MUX    I2C_PORT_USB_C0
#define I2C_PORT_LCD	    NPCX_I2C_PORT3_0
/* TODO(b:147440290): Need to handle multiple charger ICs */
#define I2C_PORT_CHARGER    I2C_PORT_USB_C0

#define I2C_PORT_ACCEL      I2C_PORT_SENSOR

#define I2C_ADDR_EEPROM_FLAGS 0x50 /* 7b address */
#define I2C_ADDR_ISL98607_FLAGS 0x29
#define I2C_ADDR_MP3372_FLAGS 0x28

/* ISL98607 registers and value */
/* VBST Voltage Adjustment */
#define ISL98607_REG_VBST_OUT	0x06
#define ISL98607_VBST_OUT_5P65	0x0a

/* VN Voltage Adjustment */
#define ISL98607_REG_VN_OUT	0x08
#define ISL98607_VN_OUT_5P5	0x0a

/* VP Voltage Adjustment */
#define ISL98607_REG_VP_OUT	0x09
#define ISL98607_VP_OUT_5P5	0x0a

/* MP3372 registers and value */
/* ISET & CHEN */
#define MP3372_REG_ISET_CHEN		0x00
#define MP3372_ISET_21P8_CHEN_ALL	0x70ff
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

#define CONFIG_ACCELGYRO_BMI160		/* Base accel */

/* Lid operates in forced mode, base in FIFO */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)
#define CONFIG_ACCEL_FIFO
#define CONFIG_ACCEL_FIFO_SIZE 256	/* Must be a power of 2 */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH

/* LIS2DS Lid accel */
#define CONFIG_ACCEL_LIS2DS
#define CONFIG_ACCEL_LIS2DS_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum chg_id {
	CHARGER_PRIMARY,
	CHARGER_SECONDARY,
	CHARGER_NUM,
};

enum temp_sensor_id {
	TEMP_SENSOR_1,
	TEMP_SENSOR_2,
	TEMP_SENSOR_3,
	TEMP_SENSOR_4,
	TEMP_SENSOR_COUNT
};

enum adc_channel {
	ADC_TEMP_SENSOR_1,     /* ADC0 */
	ADC_TEMP_SENSOR_2,     /* ADC1 */
	ADC_TEMP_SENSOR_3,     /* ADC5 */
	ADC_TEMP_SENSOR_4,     /* ADC6 */
	ADC_SUB_ANALOG,	       /* ADC2 */
	ADC_VSNS_PP3300_A,     /* ADC9 */
	ADC_CH_COUNT
};

enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_SDI,
	BATTERY_TYPE_COUNT,
};

void panel_power_change_interrupt(enum gpio_signal signal);
#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
