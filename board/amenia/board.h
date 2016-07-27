/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Amenia board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Optional features */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCEL_KX022
#define CONFIG_ADC
#define CONFIG_ALS
#define CONFIG_ALS_ISL29035
#define CONFIG_BACKLIGHT_LID
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_VERSION
#define CONFIG_BUTTON_COUNT 2
#define CONFIG_CHARGE_MANAGER

#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2

#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_BD99955
#define CONFIG_CHARGER_ILIM_PIN_DISABLED
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 1
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 15000
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_NARROW_VDC
#define CONFIG_USB_CHARGER

#define CONFIG_CMD_CHARGER_ADC_AMON_BMON
#define CONFIG_CHARGER_SENSE_RESISTOR		10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC	10
#define BD99955_IOUT_GAIN_SELECT \
		BD99955_CMD_PMON_IOUT_CTRL_SET_IOUT_GAIN_SET_20V

#define CONFIG_CMD_CHARGER_PSYS
#define BD99955_PSYS_GAIN_SELECT \
		BD99955_CMD_PMON_IOUT_CTRL_SET_PMON_GAIN_SET_02UAW

#define CONFIG_CHIPSET_APOLLOLAKE
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_ALS
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_LPC
#define CONFIG_UART_HOST                0
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_COMMON
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE	BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID	LID_ACCEL
#define CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LOW_POWER_S0
#define CONFIG_LTO
#define CONFIG_MAG_BMI160_BMM150
#define   BMM150_I2C_ADDRESS BMM150_ADDR0
#define CONFIG_MAG_CALIBRATE
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 512
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_TCPM_ANX74XX
#define  TCPC0_I2C_ADDR 0x50
#define CONFIG_USB_PD_TCPM_PS8751
#define CONFIG_USB_PD_TCPM_TCPCI
#define  TCPC1_I2C_ADDR 0x16
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_CHARGER
#define CONFIG_USB_PORT_POWER_SMART
#define  GPIO_USB1_CTL1 GPIO_USB_CTL1
#define  GPIO_USB1_CTL2 GPIO_UNIMPLEMENTED
#define  GPIO_USB1_CTL3 GPIO_UNIMPLEMENTED
#define  GPIO_USB2_CTL1 GPIO_USB_CTL1
#define  GPIO_USB2_CTL2 GPIO_UNIMPLEMENTED
#define  GPIO_USB2_CTL3 GPIO_UNIMPLEMENTED
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_CDP
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_VBOOT_HASH

/*
 * Enable 1 slot of secure temporary storage to support
 * suspend/resume with read/write memory training.
 */
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

#define CONFIG_FLASH_SIZE 0x80000 /* 512 KB Flash used for EC */
#define CONFIG_SPI_FLASH_W25X40

#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_G782
#define CONFIG_DPTF

/* Optional feature - used by nuvoton */
#define NPCX_I2C0_BUS2       0 /* 0:GPIOB4/B5 1:GPIOB2/B3 as I2C0 */
#define NPCX_UART_MODULE2    1 /* 0:GPIO10/11 1:GPIO64/65 as UART */
#define NPCX_JTAG_MODULE2    0 /* 0:GPIO21/17/16/20 1:GPIOD5/E2/D4/E5 as JTAG*/
#define NPCX_TACH_SEL2       0 /* 0:GPIO40/A4 1:GPIO93/D3 as TACH */

#define CONFIG_WATCHDOG_HELP

#define CONFIG_WIRELESS
#define CONFIG_WIRELESS_SUSPEND \
	(EC_WIRELESS_SWITCH_WLAN | EC_WIRELESS_SWITCH_WLAN_POWER)

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WLAN_POWER GPIO_WLAN_EN

/* LED signals */
#define GPIO_BAT_LED_RED GPIO_CHARGE_LED_1
#define GPIO_BAT_LED_GREEN GPIO_CHARGE_LED_2

/* I2C ports */
#define I2C_PORT_TCPC0                  NPCX_I2C_PORT0_1
#define I2C_PORT_TCPC1                  NPCX_I2C_PORT0_1
#define I2C_PORT_ACCELGYRO              NPCX_I2C_PORT1
#define I2C_PORT_ALS                    NPCX_I2C_PORT2
#define I2C_PORT_ACCEL                  NPCX_I2C_PORT2
#define I2C_PORT_BATTERY                NPCX_I2C_PORT3
#define I2C_PORT_CHARGER                NPCX_I2C_PORT3
#define I2C_PORT_THERMAL                NPCX_I2C_PORT3

/* Modules we want to exclude */
#undef CONFIG_PECI

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* ADC signal */
enum adc_channel {
	ADC_VBUS,
	ADC_AMON_BMON,
	ADC_PSYS,
	ADC_THERM_SYS0,
	ADC_THERM_SYS1,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* power signal definitions */
enum power_signal {
	X86_RSMRST_N = 0,
	X86_ALL_SYS_PG,
	X86_SLP_S0_N,
	X86_SLP_S3_N,
	X86_SLP_S4_N,
	X86_SUSPWRDNACK,
	X86_SUS_STAT_N,
	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum temp_sensor_id {
	/* G782 local and remote sensors */
	TEMP_SENSOR_I2C_G782_LOCAL,
	TEMP_SENSOR_I2C_G782_REMOTE1,
	TEMP_SENSOR_I2C_G782_REMOTE2,

	/* Battery temperature sensor */
	TEMP_SENSOR_BATTERY,

	TEMP_SENSOR_COUNT
};

/* Light sensors */
enum als_id {
	ALS_ISL29035 = 0,

	ALS_COUNT
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL = 0,
	LID_GYRO,
	LID_MAG,
	BASE_ACCEL,
};

/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* TODO: determine the following board specific type-C power constants */
/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       45000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

/* Reset PD MCU */
void board_reset_pd_mcu(void);

void board_set_tcpc_power_mode(int port, int normal_mode);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
