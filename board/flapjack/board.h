/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Kukui */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional modules */
#define CONFIG_ADC
#undef  CONFIG_ADC_WATCHDOG
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CHIPSET_MT8183
#define CONFIG_CMD_ACCELS
#define CONFIG_CRC8
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_EMULATED_SYSRQ
#undef  CONFIG_HIBERNATE
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_I2C_VIRTUAL_BATTERY
#define CONFIG_I2C_PASSTHRU_RESTRICTED
#define CONFIG_LED_COMMON
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_POWER_COMMON
#define CONFIG_SPI
#define CONFIG_SPI_MASTER
#define CONFIG_STM_HWTIMER32
#define CONFIG_SWITCH
#define CONFIG_WATCHDOG_HELP

#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands for testing */

#undef  CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1
#define CONFIG_UART_RX_DMA

/* Bootblock */
#ifdef SECTION_IS_RO
#define CONFIG_BOOTBLOCK

#define EMMC_SPI_PORT 2
#endif

/* Optional features */
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_BUTTON_TRIGGERED_RECOVERY
#define CONFIG_CHARGER_ILIM_PIN_DISABLED
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_CMD_AP_RESET_LOG

/* Required for FAFT */
#define CONFIG_CMD_BUTTON

/* By default, set hcdebug to off */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF
#define CONFIG_LTO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_IGNORE_LID
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_SOFTWARE_PANIC
#define CONFIG_VBOOT_HASH
#define CONFIG_VOLUME_BUTTONS

#define CONFIG_CHARGER
#define CONFIG_CHARGER_MT6370
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 2
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 2
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 15000
#define CONFIG_CHARGER_PROFILE_OVERRIDE
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_OTG
#define CONFIG_USB_CHARGER
#define CONFIG_USB_MUX_VIRTUAL

/* Increase tx buffer size, as we'd like to stream EC log to AP. */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Motion Sensors */
#ifdef SECTION_IS_RW
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)

#define CONFIG_ALS
#define ALS_COUNT 1

/* TSC3400 ALS */
#define CONFIG_ALS_TCS3400
#define CONFIG_ALS_TCS3400_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(CLEAR_ALS)
#define CONFIG_TCS_USE_LUX_TABLE


/* ALS needs to be polled */
#define CONFIG_ACCEL_FORCE_MODE_MASK    BIT(CLEAR_ALS)

/* Camera VSYNC */
#define CONFIG_SYNC
#define CONFIG_SYNC_COMMAND
#define CONFIG_SYNC_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)
#endif /* SECTION_IS_RW */

/* To be able to indicate the device is in tablet mode. */
#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH

/*
 * Only include the sensor fifo in the RW section (since the motion task is only
 * included there).
 */
#ifdef SECTION_IS_RW
/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs. */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)
#endif /* SECTION_IS_RW */

/* Wireless Power Charger Config */
#ifdef SECTION_IS_RW
#define CONFIG_WIRELESS_CHARGER_P9221_R7
#endif


/* USB PD config */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_MT6370
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_5V_CHARGER_CTRL
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USBC_DISABLE_CHARGE_FROM_RP_DEF
#ifdef SECTION_IS_RO
#define CONFIG_USB_PD_DEBUG_LEVEL 0
#endif

#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_MAX17055
#define CONFIG_BATTERY_MAX17055_ALERT
#define CONFIG_BATTERY_MAX17055_FULL_MODEL

/* Battery parameters for max17055 ModelGauge m5 algorithm. */
#define BATTERY_MAX17055_RSENSE             10    /* m-ohm */
#define BATTERY_DESIRED_CHARGING_CURRENT    2000  /* mA */

#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_VOLTAGE_MV     9000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_POWER_MW       18000

#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 50000  /* us */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* Timer selection */
#define TIM_CLOCK32  2
#define TIM_WATCHDOG 7

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Optional for testing */
#undef  CONFIG_PECI
#undef  CONFIG_PSTORE

/* Modules we want to exclude */
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_APTHROTTLE
#undef CONFIG_CMD_BATTFAKE
#undef CONFIG_CMD_DEVICE_EVENT
#undef CONFIG_CMD_FASTCHARGE
#undef CONFIG_CMD_FLASH
#undef CONFIG_CMD_GETTIME
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_HCDEBUG
#undef CONFIG_CMD_MD
#undef CONFIG_CMD_MEM
#undef CONFIG_CMD_POWERINDEBUG
#undef CONFIG_CMD_TIMERINFO

#ifdef SECTION_IS_RO
#undef CONFIG_CMD_ADC
#undef CONFIG_CMD_APTHROTTLE
#undef CONFIG_CMD_CBI
#undef CONFIG_CMD_I2C_SCAN
#undef CONFIG_CMD_I2C_XFER
#undef CONFIG_CMD_IDLE_STATS
#undef CONFIG_CMD_INA
#undef CONFIG_CMD_MMAPINFO
#undef CONFIG_CMD_PD
#undef CONFIG_CMD_PWR_AVG
#undef CONFIG_CMD_REGULATOR
#undef CONFIG_CMD_RW
#undef CONFIG_CMD_SHMEM
/* TODO: Consider put these back when FSI is (about to be) done. */
#undef CONFIG_CMD_SLEEPMASK
#undef CONFIG_CMD_SLEEPMASK_SET
#undef CONFIG_CMD_SYSLOCK
#endif

#define CONFIG_TASK_PROFILING

/* I2C ports */
#define I2C_PORT_CHARGER  0
#define I2C_PORT_TCPC0    0
#define I2C_PORT_WPC      0
#define I2C_PORT_BATTERY  1
#define I2C_PORT_VIRTUAL_BATTERY I2C_PORT_BATTERY
#define I2C_PORT_ACCEL    1
#define I2C_PORT_ALS      1
#define I2C_PORT_EEPROM		1

/* I2C addresses */
#define I2C_ADDR_EEPROM_FLAGS		0x50

/* Route sbs host requests to virtual battery driver */
#define VIRTUAL_BATTERY_ADDR_FLAGS	0x0B

/* Enable Accel over SPI */
#define CONFIG_SPI_ACCEL_PORT    0  /* The first SPI master port (SPI2) */

#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO
/* Define the host events which are allowed to wakeup AP in S3. */
#define CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK \
		(EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_RTC))

/* Define panel size mask according to skuid */
#define SKU_ID_PANEL_SIZE_MASK  BIT(1)

#ifndef __ASSEMBLER__

enum oem_id {
	PROJECT_FLAPJACK = 0,
	PROJECT_COUNT,
};

enum adc_channel {
	/* Real ADC channels begin here */
	ADC_LCM_ID = 0,
	ADC_EC_SKU_ID,
	ADC_BATT_ID,
	ADC_USBC_THERM,
	ADC_CH_COUNT
};

/* Panel ID bit position inside sku_id */
#define PANEL_ID_BIT_POSITION  16

/* Refer to coreboot/src/mainboard/google/kukui/display.h */
enum panel_id {
	PANEL_DEFAULT = 0,
	PANEL_BOE_TV101WUM_NG0,
	PANEL_BOE_TV080WUM_NG0,
	PANEL_STA_10P,
	PANEL_STA_08P,
	PANEL_COUNT,
	PANEL_UNINITIALIZED = 0xf,
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
	CLEAR_ALS,
	RGB_ALS,
	VSYNC,
	SENSOR_COUNT,
};

/* Batteries */
enum battery_type {
	BATTERY_UNKNOWN = 0,
	BATTERY_C18_ATL,
	BATTERY_C19_ATL,
	BATTERY_C18_SUNWODA,
	BATTERY_C19_SUNWODA,
	BATTERY_COUNT,
};

#include "gpio_signal.h"
#include "registers.h"

#ifdef SECTION_IS_RO
/* Interrupt handler for emmc task */
void emmc_cmd_interrupt(enum gpio_signal signal);
#endif

void board_reset_pd_mcu(void);

#define ADC_MARGIN_MV 56 /* Simply assume 1800/16/2 */

struct mv_to_id {
	int id;
	int median_mv;
};

int board_read_id(enum adc_channel, const struct mv_to_id *table, int size);

extern uint8_t board_version;
extern uint8_t oem;
extern uint32_t sku;

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
