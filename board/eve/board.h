/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Eve board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* EC */
#define CONFIG_ADC
#define CONFIG_BOARD_HAS_BEFORE_RSMRST
#define CONFIG_BOARD_VERSION
#define CONFIG_BOARD_SPECIFIC_VERSION
#define CONFIG_BUTTON_COUNT 2
#define CONFIG_CASE_CLOSED_DEBUG_EXTERNAL
#define CONFIG_DPTF
#define CONFIG_FLASH_SIZE 0x80000
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_LID_SWITCH
#define CONFIG_LTO
#define CONFIG_PWM
#define CONFIG_PWM_KBLIGHT
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25X40
#define CONFIG_UART_HOST 0
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1
#define CONFIG_WATCHDOG_HELP
#define CONFIG_WIRELESS
#define CONFIG_WIRELESS_SUSPEND \
	(EC_WIRELESS_SWITCH_WLAN | EC_WIRELESS_SWITCH_WLAN_POWER)
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_DX_WLAN

/* EC console commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

/* SOC */
#define CONFIG_CHIPSET_SKYLAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_ESPI
#define CONFIG_ESPI_VW_SIGNALS
#define CONFIG_LPC
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_DEVICE_CHEMISTRY "LION"
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_BATTERY_PRESENT_L
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART

/* Charger */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_BD99956
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 1
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 15000
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define BD9995X_IOUT_GAIN_SELECT \
		BD9995X_CMD_PMON_IOUT_CTRL_SET_IOUT_GAIN_SET_20V
#define BD9995X_PSYS_GAIN_SELECT \
		BD9995X_CMD_PMON_IOUT_CTRL_SET_PMON_GAIN_SET_02UAW
#define CONFIG_CMD_CHARGER_ADC_AMON_BMON
#define CONFIG_CMD_CHARGER_PSYS
#define CONFIG_CMD_PD_CONTROL
#define CONFIG_EXTPOWER_GPIO
#undef   CONFIG_EXTPOWER_DEBOUNCE_MS
#define  CONFIG_EXTPOWER_DEBOUNCE_MS 1000
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30

/* Sensor */
#define CONFIG_ACCEL_KXCJ9
#define CONFIG_ALS_ISL29035
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_BD99992GW
#define CONFIG_THERMISTOR_NCP15WB

#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_MAG_BMI160_BMM150
#define BMM150_I2C_ADDRESS BMM150_ADDR0	/* 8-bit address */
#define CONFIG_MAG_CALIBRATE

/* FIFO size is in power of 2. */
/*
 * TODO (crosbug.com/p/59144): Uncomment this when AP is reading sensor
 * data. For now, it's commented so that the data can be read from the EC
 * console.
 */
/*#define CONFIG_ACCEL_FIFO 1024*/

/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO / 3)

/* USB */
#define CONFIG_USB_CHARGER
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DISCHARGE
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 512
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_QUIRK_SLOW_CC_STATUS
#define CONFIG_USB_PD_VBUS_DETECT_CHARGER
#define CONFIG_USB_PD_TCPC_FW_VERSION
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_ANX74XX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* Optional feature to configure npcx chip */
#define NPCX_UART_MODULE2	1 /* 1:GPIO64/65 as UART */
#define NPCX_JTAG_MODULE2	0 /* 0:GPIO21/17/16/20 as JTAG */
#define NPCX_TACH_SEL2		0 /* 0:GPIO40/A4 as TACH */

/* I2C ports */
#define I2C_PORT_TCPC0		NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC1		NPCX_I2C_PORT0_1
#define I2C_PORT_GYRO		NPCX_I2C_PORT1
#define I2C_PORT_ACCEL		I2C_PORT_GYRO
#define I2C_PORT_LID_ACCEL	NPCX_I2C_PORT2
#define I2C_PORT_ALS		NPCX_I2C_PORT2
#define I2C_PORT_PMIC		NPCX_I2C_PORT3
#define I2C_PORT_BATTERY	NPCX_I2C_PORT3
#define I2C_PORT_CHARGER	NPCX_I2C_PORT3
#define I2C_PORT_THERMAL	I2C_PORT_PMIC
#define I2C_PORT_MP2949		NPCX_I2C_PORT3

/* I2C addresses */
#define I2C_ADDR_BD99992	0x60
#define I2C_ADDR_MP2949		0x40

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum board_version_list {
	BOARD_VERSION_P0,
	BOARD_VERSION_P0B,
	BOARD_VERSION_P1,
	BOARD_VERSION_EVT,
	BOARD_VERSION_DVT,
	BOARD_VERSION_PVT,
};

enum power_signal {
	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	X86_RSMRST_L_PGOOD,
	X86_PMIC_DPWROK,
	POWER_SIGNAL_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,	/* BD99956GW TSENSE */
	TEMP_SENSOR_AMBIENT,	/* BD99992GW SYSTHERM0 */
	TEMP_SENSOR_CHARGER,	/* BD99992GW SYSTHERM1 */
	TEMP_SENSOR_DRAM,	/* BD99992GW SYSTHERM2 */
	TEMP_SENSOR_EMMC,	/* BD99992GW SYSTHERM3 */
	TEMP_SENSOR_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT
};

enum als_id {
	ALS_ISL29035,
	ALS_COUNT
};

/*
 * Motion sensors:
 * When reading through IO memory is set up for sensors (LPC is used),
 * the first 2 entries must be accelerometers, then gyroscope.
 * For BMI160, accel, gyro and compass sensors must be next to each other.
 */
enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	BASE_MAG,
};

enum adc_channel {
	ADC_CH_COUNT
};

/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	250000 /* us */

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY		5000   /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW		15000
#define PD_MAX_POWER_MW			45000
#define PD_MAX_CURRENT_MA		3000
#define PD_MAX_VOLTAGE_MV		20000

/* Board specific handlers */
int board_get_version(void);
void board_reset_pd_mcu(void);
void board_set_tcpc_power_mode(int port, int mode);
void board_print_tcpc_fw_version(int port);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
