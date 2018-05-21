/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kahlee board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* EC console commands  */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_BATT_MFG_ACCESS
#define CONFIG_CHARGER_SENSE_RESISTOR		10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC	10
#define CONFIG_CHARGER_PSYS_READ

/* Battery */
#define CONFIG_BATTERY_DEVICE_CHEMISTRY  "LION"
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_L
#define CONFIG_BATTERY_SMART

/* Charger */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP_SW
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_ISL9237
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 2137
#undef CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT
#undef CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW
#undef CONFIG_CHARGER_MAINTAIN_VBAT
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_USB_CHARGER
#undef CONFIG_CHARGER_PROFILE_OVERRIDE
#undef CONFIG_CHARGER_PROFILE_OVERRIDE_COMMON
#define CONFIG_CHARGE_MANAGER_EXTERNAL_POWER_LIMIT

/* USB-A config */
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_CDP
#define CONFIG_USB_PORT_POWER_SMART_SIMPLE
#undef CONFIG_USB_PORT_POWER_SMART_PORT_COUNT
#define CONFIG_USB_PORT_POWER_SMART_PORT_COUNT 1
#define GPIO_USB1_ILIM_SEL GPIO_USB_A_CHARGE_EN_L
#define GPIO_USB_CTL1 GPIO_USB_A_CHARGE_EN_L

/* USB PD config */
#define CONFIG_CMD_PD_CONTROL
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_PORT_COUNT 2
#undef CONFIG_USB_PD_VBUS_DETECT_CHARGER
#define ADC_VBUS -1
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_PS8751
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_COMM_LOCKED

#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* SoC / PCH */
#define CONFIG_HOSTCMD_LPC
#define CONFIG_CHIPSET_STONEY
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
#define GPIO_PCH_WAKE_L	0

/* EC */
#define CONFIG_ADC
#define CONFIG_BOARD_VERSION_CUSTOM
#define CONFIG_EXTPOWER_GPIO
#undef   CONFIG_EXTPOWER_DEBOUNCE_MS
#define  CONFIG_EXTPOWER_DEBOUNCE_MS 1000
#define CONFIG_FPU
#define CONFIG_HOSTCMD_FLASH_SPI_INFO
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2
#define CONFIG_LED_COMMON
#define CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LTO
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30
#define CONFIG_PWM
#define CONFIG_FANS 1
#undef CONFIG_FAN_INIT_SPEED
#define CONFIG_FAN_INIT_SPEED 50
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_G781
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L
#define CONFIG_VBOOT_HASH
#define CONFIG_BACKLIGHT_LID
#define CONFIG_WIRELESS
#define CONFIG_WIRELESS_SUSPEND EC_WIRELESS_SWITCH_WLAN_POWER
#define CONFIG_WLAN_POWER_ACTIVE_LOW
#define WIRELESS_GPIO_WLAN_POWER GPIO_WIRELESS_GPIO_WLAN_POWER
#define CONFIG_BC12_DETECT_PI3USB9281
#define CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT 2

/*
 * During shutdown sequence TPS65094x PMIC turns off the sensor rails
 * asynchronously to the EC. If we access the sensors when the sensor power
 * rails are off we get I2C errors. To avoid this issue, defer switching
 * the sensors rate if in S3. By the time deferred function is serviced if
 * the chipset is in S5 we can back out from switching the sensor rate.
 *
 * Time taken by V1P8U rail to go down from S3 is 30ms to 60ms hence defer
 * the sensor switching after 60ms.
 */
#undef CONFIG_MOTION_SENSE_SUSPEND_DELAY_US
#define CONFIG_MOTION_SENSE_SUSPEND_DELAY_US (MSEC * 60)

#define CONFIG_FLASH_SIZE 524288
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25X40

/*
 * Enable 1 slot of secure temporary storage to support
 * suspend/resume with read/write memory training.
 */
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Optional feature - used by nuvoton */
#define NPCX_UART_MODULE2    1 /* 0:GPIO10/11 1:GPIO64/65 as UART */
#define NPCX_JTAG_MODULE2    0 /* 0:GPIO21/17/16/20 1:GPIOD5/E2/D4/E5 as JTAG*/
#define NPCX_TACH_SEL2       1 /* 0:GPIO40/73 1:GPIO93/A6 as TACH */

/* I2C ports */
#define I2C_PORT_THERMAL		NPCX_I2C_PORT1
#define I2C_PORT_GYRO			NPCX_I2C_PORT2
#define I2C_PORT_LID_ACCEL		NPCX_I2C_PORT2
#define I2C_PORT_ALS			NPCX_I2C_PORT2
#define I2C_PORT_BARO			NPCX_I2C_PORT2
#define I2C_PORT_BATTERY		NPCX_I2C_PORT3
#define I2C_PORT_CHARGER		NPCX_I2C_PORT3
/* Accelerometer and Gyroscope are the same device. */
#define I2C_PORT_ACCEL			I2C_PORT_GYRO

/* Sensors */
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HOST_EVENT
#define CONFIG_ACCEL_KXCJ9
#define CONFIG_ALS_AL3010
#define AL3010_I2C_ADDR AL3010_I2C_ADDR1
#undef CONFIG_LID_ANGLE
#undef CONFIG_LID_ANGLE_UPDATE
#undef CONFIG_LID_ANGLE_SENSOR_BASE
#undef CONFIG_LID_ANGLE_SENSOR_LID

/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO 512

/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO / 3)


#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* ADC signal */
enum adc_channel {
	ADC_IMON1,		/* ADC0 */
	ADC_IMON2,		/* ADC1 */
	ADC_BOARD_ID,		/* ADC2 */
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_FAN,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum fan_channel {
	FAN_CH_0,
	/* Number of FAN channels */
	FAN_CH_COUNT
};

enum mft_channel {
	MFT_CH_0,
	/* Number of MFT channels */
	MFT_CH_COUNT
};

enum power_signal {
	X86_SLP_S3_N = 0,
	X86_SLP_S5_N,
	X86_S5_PGOOD,
	X86_ALW_PG,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_I2C_G781_LOCAL = 0,
	TEMP_SENSOR_I2C_G781_REMOTE1,
	TEMP_SENSOR_BATTERY,
	TEMP_SENSOR_COUNT
};

/* Light sensors */
enum als_id {
	ALS_AL3010 = 0,

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
};

enum board_version {
	BOARD_VERSION_UNKNOWN = -1,
	BOARD_VERSION_1,
	BOARD_VERSION_2,
	BOARD_VERSION_3,
	BOARD_VERSION_4,
	BOARD_VERSION_5,
	BOARD_VERSION_6,
	BOARD_VERSION_7,
	BOARD_VERSION_8,
	BOARD_VERSION_COUNT,
};

/* TODO: determine the following board specific type-C power constants */
/* FIXME(dhendrix): verify all of the below PD_* numbers */
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

/* Request the max voltage for type-c adapter */
#define PD_PREFER_HIGH_VOLTAGE

/* Reset PD MCU */
void board_reset_pd_mcu(void);

int board_get_version(void);

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (1 << LID_ACCEL)

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
