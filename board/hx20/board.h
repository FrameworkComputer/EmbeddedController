/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Microchip Evaluation Board (EVB) with
 * MEC1701H 144-pin processor card.
 * EVB connected to Intel SKL RVP3 configured
 * for eSPI with Kabylake silicon.
 */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Initial board bringup and prevent power button task from
 * generating event to exit G3 state.
 *
 * #define CONFIG_BRINGUP
 */

/* Keyboard features */
#define CONFIG_KEYBOARD_CUSTOMIZATION
/* #define CONFIG_PWM_KBLIGHT */
/* #define CONFIG_KEYBOARD_DEBUG */


/* #define CONFIG_CUSTOMER_PORT80 */

/*
 * Mouse emulation
 */

#define CONFIG_8042_AUX

#define CONFIG_CUSTOMER_PORT80
#define CONFIG_IGNORED_BTN_SCANCODE

/*
 * Combination key
 */
#define CONFIG_KEYBOARD_CUSTOMIZATION_COMBINATION_KEY

/* The Fn key function not ready yet undefined it until the function finish */
#define CONFIG_KEYBOARD_SCANCODE_CALLBACK

#define CONFIG_KEYBOARD_BACKLIGHT
/*Assume we should move to CONFIG_PWM_KBLIGHT later*/
/*
 * Debug on EVB with CONFIG_CHIPSET_DEBUG
 * Keep WDG disabled and JTAG enabled.
 * CONFIG_BOARD_PRE_INIT enables JTAG early
 */
/* #define CONFIG_CHIPSET_DEBUG */
#define CONFIG_BOARD_PRE_INIT

/* Add commands to read/write ec serial data structure */
#ifdef CONFIG_CHIPSET_DEBUG
#define CONFIG_SYSTEMSERIAL_DEBUG
#endif

/*
 * DEBUG: Add CRC32 in last 4 bytes of EC_RO/RW binaries
 * in SPI. LFW will use DMA CRC32 HW to check data integrity.
 * #define CONFIG_MCHP_LFW_DEBUG
 */


/*
 * Override Boot-ROM JTAG mode
 * 0x01 = 4-pin standard JTAG
 * 0x03 = ARM 2-pin SWD + 1-pin SWV
 * 0x05 = ARM 2-pin SWD no SWV
 */
#define CONFIG_MCHP_JTAG_MODE 0x03

/*
 * Enable Trace FIFO Debug port
 * When this is undefined all TRACEn() and tracen()
 * macros are defined as blank.
 * Uncomment this define to enable these messages.
 * Only enable if GPIO's 0171 & 0171 are available therefore
 * define this at the board level.
 */
/* #define CONFIG_MCHP_TFDP */

/*
 * Enable MCHP specific GPIO EC UART commands
 * for debug.
 */
#ifdef CONFIG_CHIPSET_DEBUG
#define CONFIG_MEC_GPIO_EC_CMDS
#endif
/*
 * Enable CPRINT in chip eSPI module
 * and EC UART test command.
 */
/* #define CONFIG_MCHP_ESPI_DEBUG */

/*
 * Enable board specific ISR on ALL_SYS_PWRGD signal.
 * Requires for handling Kabylake/Skylake RVP3 board's
 * ALL_SYS_PWRGD signal.
 */
/* #define CONFIG_BOARD_EC_HANDLES_ALL_SYS_PWRGD */

/*
 * EVB eSPI test mode (no eSPI master connected)
 */
/*
 * #define EVB_NO_ESPI_TEST_MODE
 */


/*
 * DEBUG
 * Disable ARM Cortex-M4 write buffer so
 * exceptions become synchronous.
 *
 * #define CONFIG_DEBUG_DISABLE_WRITE_BUFFER
 */

/* New eSPI slave configuration items */

/*
 * Maximum clock frequence eSPI EC slave advertises
 * Values in MHz are 20, 25, 33, 50, and 66
 */
/* KBL + EVB fly-wire hook up only supports 20MHz */
#define CONFIG_HOSTCMD_ESPI_EC_MAX_FREQ		20

/*
 * EC eSPI slave advertises IO lanes
 * 0 = Single
 * 1 = Single and Dual
 * 2 = Single and Quad
 * 3 = Single, Dual, and Quad
 */
/* KBL + EVB fly-wire hook up only support Single mode */
#define CONFIG_HOSTCMD_ESPI_EC_MODE		0

/*
 * Bit map of eSPI channels EC advertises
 * bit[0] = 1 Peripheral channel
 * bit[1] = 1 Virtual Wire channel
 * bit[2] = 1 OOB channel
 * bit[3] = 1 Flash channel
 */
#define CONFIG_HOSTCMD_ESPI_EC_CHAN_BITMAP	0x0F

#define CONFIG_MCHP_ESPI_VW_SAVE_ON_SLEEP

/*
 * Allow dangerous commands.
 * TODO(shawnn): Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Optional features */

#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BOARD_VERSION_CUSTOM
#define CONFIG_CHARGE_MANAGER
/* #define CONFIG_CHARGE_RAMP_SW */

#undef CONFIG_HOSTCMD_LOCATE_CHIP

#define CONFIG_CHARGER
#define CONFIG_USB_PD_PORT_MAX_COUNT 4
#define CONFIG_USB_PD_REV30
#define CONFIG_USB_PD_EXTENDED_MESSAGES
#define CONFIG_CHARGER_DISCHARGE_ON_AC

/* Charger parameter */
#define CONFIG_CHARGER_ISL9241
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20 /* BOARD_RS1 */
#define CONFIG_CHARGER_SENSE_RESISTOR 10    /* BOARD_RS2 */
#define CONFIG_CHARGER_INPUT_CURRENT 500	/* Minimum for USB - will negociate higher */
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 55000 /* only if battery is not present*/
#define CONFIG_CHARGER_CUSTOMER_SETTING
#define CONFIG_CMD_CHARGER_DUMP
/*
 * MCHP disable this for Kabylake eSPI bring up
 * #define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
 */

/* #define CONFIG_CHIPSET_SKYLAKE */
/* #define CONFIG_CHIPSET_TIGERLAKE */
#define CONFIG_CHIPSET_RESET_HOOK

#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S3
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S4
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S5

#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

#define CONFIG_CLOCK_CRYSTAL
#define CONFIG_EXTPOWER_GPIO
/* #define CONFIG_HOSTCMD_PD */
/* #define CONFIG_HOSTCMD_PD_PANIC */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_SIMULATE_KEYCODE

/* i2c hid interface for HID mediakeys (brightness, airplane mode) */
#define CONFIG_I2C_SLAVE
#define CONFIG_I2C_HID_MEDIAKEYS

/* Leds configuration */
#define CONFIG_LED_COMMON
#define CONFIG_CMD_LEDTEST
#define CONFIG_LED_PWM_COUNT 3
#define CONFIG_LED_PWM_TASK_DISABLED
#define CONFIG_CAPSLED_SUPPORT

#ifdef CONFIG_ACCEL_KX022
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#endif /* CONFIG_ACCEL_KX022 */

#define CONFIG_LID_SWITCH
#define LID_DEBOUNCE_US (200 * MSEC)
#define CONFIG_POWER_BUTTON_IGNORE_LID
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
/*
 * Enable MCHP Low Power Idle support
 * and API to power down pins
 * #define CONFIG_LOW_POWER_IDLE
 */


/* #define CONFIG_GPIO_POWER_DOWN */

/*
 * Turn off pin modules during deep sleep.
 * Requires CONFIG_GPIO_POWER_DOWN
 */
/* #define CONFIG_MCHP_DEEP_SLP_GPIO_PWR_DOWN */

/*
 * DEBUG: Configure MEC17xx GPIO060 as 48MHZ_OUT to
 * verify & debug clock is shutdown in heavy sleep.
 */
/*#define CONFIG_MCHP_48MHZ_OUT*/

/*
 * DEBUG: Save and print out PCR sleep enables,
 * clock required, and interrupt aggregator result
 * registers.
 */
#define CONFIG_MCHP_DEEP_SLP_DEBUG

#ifdef CONFIG_CHIPSET_DEBUG
/* if we are built with debug mode flags the chip
 * will never halt, so never properly sleep
 * otherwise the ec will stop responding to commands
 */
#undef CONFIG_HIBERNATE_DELAY_SEC
#define CONFIG_HIBERNATE_DELAY_SEC (60*60*24*365)
#endif /* CONFIG_CHIPSET_DEBUG */
/*
 * MCHP debug EC code turn off GCC link-time-optimization
 * #define CONFIG_LTO
 */
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_CUSTOM
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30

/*
 * Use for customer boot from G3
 */
#define CONFIG_CUSTOM_BOOT_G3

/*
 * MEC1701H SCI is virtual wire on eSPI
 *#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L
 */

#if 0 /* MCHP EVB + KBL/SKL RVP3 no USB charging hardware */
#define CONFIG_USB_CHARGER
#define CONFIG_USB_MUX_PI3USB30532
#define CONFIG_USB_MUX_PS8740
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DP_HPD_GPIO
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_TCPM_TCPCI
#endif
/*
 * #define CONFIG_USB_PD_TCPC
 * #define CONFIG_USB_PD_TCPM_STUB
 */
#if 0
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#define CONFIG_BC12_DETECT_PI3USB9281
#define CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT 2
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#endif
/* TODO FRAMEWORK 
#define CONFIG_VBOOT_HASH
*/
/*
 * MEC1701H loads firmware using QMSPI controller
 * CONFIG_SPI_FLASH_PORT is the index into
 * spi_devices[] in board.c
 */
#define CONFIG_SPI_FLASH_PORT 0
#define CONFIG_SPI_FLASH

/*
 * MB use W25Q80 SPI ROM
 * Size : 1M
 */
#define CONFIG_FLASH_SIZE 0x100000
#define CONFIG_SPI_FLASH_W25Q80

/*
 * Enable extra SPI flash and generic SPI
 * commands via EC UART
 */
#define CONFIG_CMD_SPI_FLASH
#define CONFIG_CMD_SPI_XFER

/* common software SHA256 required by vboot and rollback */
#define CONFIG_SHA256

/* Enable EMI0 Region 1 */
#define CONFIG_EMI_REGION1
#ifdef CONFIG_EMI_REGION1
#define EC_EMEMAP_ER1_POWER_STATE			0x01 /* Power state from host*/
#define EC_MEMMAP_ER1_BATT_AVER_TEMP		0x03 /* Battery Temp */
#define EC_MEMMAP_ER1_BATT_PERCENTAGE		0x06 /* Battery Percentage */
#define EC_MEMMAP_ER1_BATT_STATUS			0x07 /* Battery information */
#define EC_MEMMAP_ER1_BATT_MANUF_DAY		0x44 /* Manufacturer date - day */
#define EC_MEMMAP_ER1_BATT_MANUF_MONTH		0x45 /* Manufacturer date - month */
#define EC_MEMMAP_ER1_BATT_MANUF_YEAR		0x46 /* Manufacturer date - year */

#define EC_BATT_FLAG_FULL		BIT(0) /* Full Charged */
#define EC_BATT_TYPE			BIT(1) /* (0: NiMh,1: LION) */
#define EC_BATT_MODE			BIT(2) /* (0=mW, 1=mA) */

#define EC_PS_ENTER_S3			BIT(0)
#define EC_PS_RESUME_S3			BIT(1)
#define EC_PS_ENTER_S4			BIT(2)
#define EC_PS_RESUME_S4			BIT(3)
#define EC_PS_ENTER_S5			BIT(4)
#define EC_PS_RESUME_S5			BIT(5)
#define EC_PS_ENTER_S0ix		BIT(6)
#define EC_PS_RESUME_S0ix		BIT(7)

#endif

/*
 * Battery Protect
 */
#define CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
/* EC's thresholds. 3%: boot, 2%: no boot. Required for soft sync. */
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON		3
#define CONFIG_BATTERY_CHECK_CHARGE_TEMP_LIMITS
#undef  CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT
#define CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT 5
#define CHARGE_MAX_SLEEP_USEC (100 * MSEC)

/*
 * Enable MCHP SHA256 hardware accelerator module.
 * API is same as software SHA256 but prefixed with "chip_"
 * #define CONFIG_SHA256_HW
 */

/* enable console command to test HW Hash engine
 * #define CONFIG_CMD_SHA256_TEST
 */

/* Support PWM */
#define CONFIG_PWM

/* Support FAN */
#define CONFIG_FANS 1
#undef CONFIG_FAN_INIT_SPEED
#define CONFIG_FAN_INIT_SPEED 15
#define FAN_HARDARE_MAX 7100
#define CONFIG_TEMP_SENSOR
#define CONFIG_DPTF
#define CONFIG_TEMP_SENSOR_F75303
#define F75303_I2C_ADDR_FLAGS 0x4D
#define CONFIG_CHIPSET_CAN_THROTTLE		/* Enable EC_PROCHOT_L control */

#define CONFIG_THROTTLE_AP

/* Factory mode support */
#define CONFIG_FACTORY_SUPPORT


#define CONFIG_PECI
#define CONFIG_PECI_COMMON
#define CONFIG_PECI_TJMAX 100

/* SPI Accelerometer
 * CONFIG_SPI_FLASH_PORT is the index into
 * spi_devices[] in board.c
 */
/*#define CONFIG_SPI_ACCEL_PORT 1*/

/*
 * Enable EC UART commands to read/write
 * motion sensor.
 */
/*#define CONFIG_CMD_ACCELS*/

/*
 * Enable 1 slot of secure temporary storage to support
 * suspend/resume with read/write memory training.
 */
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

#define CONFIG_WATCHDOG_HELP

#if 0 /* TODO - No wireless on EVB */
#define CONFIG_WIRELESS
#define CONFIG_WIRELESS_SUSPEND \
	(EC_WIRELESS_SWITCH_WLAN | EC_WIRELESS_SWITCH_WLAN_POWER)

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_WLAN_EN
#endif

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */


#define CONFIG_WP_ACTIVE_HIGH

/* LED signals */
/*
#define GPIO_BAT_LED_RED    GPIO_BATT_LOW_LED_L
#define GPIO_BAT_LED_GREEN  GPIO_BATT_CHG_LED_L
*/
/* Power signals */
#define GPIO_AC_PRESENT     GPIO_ADP_IN
#define GPIO_POWER_BUTTON_L GPIO_ON_OFF_FP_L
#define GPIO_PCH_SLP_SUS_L  GPIO_SLP_SUS_L
#define GPIO_PCH_SLP_S3_L   GPIO_PM_SLP_S3_L
#define GPIO_PCH_SLP_S4_L   GPIO_PM_SLP_S4_L
#define GPIO_PCH_PWRBTN_L   GPIO_PBTN_OUT_L
#define GPIO_PCH_ACOK       GPIO_AC_PRESENT_OUT
#define GPIO_PCH_RSMRST_L   GPIO_EC_RSMRST_L
#define GPIO_CPU_PROCHOT    GPIO_VCOUT1_PROCHOT_L
#define GPIO_LID_OPEN       GPIO_LID_SW_L
#define GPIO_ENABLE_BACKLIGHT   GPIO_EC_BKOFF_L

/* SMBus signals */
#define GPIO_I2C_0_SDA      GPIO_EC_SMB_SDA0
#define GPIO_I2C_0_SCL      GPIO_EC_SMB_CLK0
#define GPIO_I2C_1_SDA      GPIO_EC_SMB_SDA1
#define GPIO_I2C_1_SCL      GPIO_EC_SMB_CLK1
#define GPIO_I2C_2_SDA      GPIO_EC_I2C_3_SDA
#define GPIO_I2C_2_SCL      GPIO_EC_I2C_3_SCL
#define GPIO_I2C_3_SDA      GPIO_EC_SMB_SDA3
#define GPIO_I2C_3_SCL      GPIO_EC_SMB_CLK3
#define GPIO_I2C_6_SDA      GPIO_EC_I2C06_PD_SDA
#define GPIO_I2C_6_SCL      GPIO_EC_I2C06_PD_CLK


/* EVT - DVT cover */
#define GPIO_EC_KBL_PWR_EN		    GPIO_TYPEC_G_DRV2_EN


/* I2C ports */
#define I2C_CONTROLLER_COUNT	5
#define I2C_SLAVE_CONTROLLER_COUNT	1
#define I2C_PORT_COUNT		5


/*
 * Map I2C Ports to Controllers for this board.
 *
 * I2C Controller 0 ---- Port 0 -> PMIC, USB Charger 2
 *                   |-- Port 2 -> USB Charger 1, USB Mux
 *
 * I2C Controller 1 ---- Port 3 -> PD MCU, TCPC
 * I2C Controller 2 ---- Port 4 -> ALS, Accel
 * I2C Controller 3 ---- Port 5 -> Battery, Charger
 *
 * All other ports set to 0xff (not used)
 */

#define I2C_PORT_TOUCHPAD		MCHP_I2C_PORT2
#define I2C_PORT_PD_MCU         MCHP_I2C_PORT6
#define I2C_PORT_TCPC           MCHP_I2C_PORT3
#define I2C_PORT_BATTERY        MCHP_I2C_PORT1
#define I2C_PORT_CHARGER        MCHP_I2C_PORT1
#define I2C_PORT_THERMAL		MCHP_I2C_PORT3

/* GPIO for power signal */
#ifdef CONFIG_HOSTCMD_ESPI_VW_SLP_S3
#define SLP_S3_SIGNAL_L VW_SLP_S3_L
#else
#define SLP_S3_SIGNAL_L GPIO_PCH_SLP_S3_L
#endif
#ifdef CONFIG_HOSTCMD_ESPI_VW_SLP_S4
#define SLP_S4_SIGNAL_L VW_SLP_S4_L
#else
#define SLP_S4_SIGNAL_L GPIO_PCH_SLP_S4_L
#endif
#ifdef CONFIG_HOSTCMD_ESPI_VW_SLP_S5
#define SLP_S5_SIGNAL_L VW_SLP_S5_L
#else
#define SLP_S5_SIGNAL_L GPIO_PCH_SLP_S5_L
#endif

#define IN_PGOOD_PWR_VR           POWER_SIGNAL_MASK(X86_VR_PWRGD)
#define IN_PGOOD_PWR_3V5V         POWER_SIGNAL_MASK(X86_PWR_3V5V_PG)
#define IN_PGOOD_VCCIN_AUX_VR     POWER_SIGNAL_MASK(X86_VCCIN_AUX_VR_PG)

#define IN_PCH_SLP_S3_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)
#define IN_PCH_SLP_S5_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S5_DEASSERTED)
#define IN_PCH_SLP_SUS_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_SUS_DEASSERTED)

#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S3_DEASSERTED | \
				  IN_PCH_SLP_S4_DEASSERTED | \
				  IN_PCH_SLP_SUS_DEASSERTED)

/* Thermal sensors read through PMIC ADC interface */

#define SCI_HOST_EVENT_MASK			\
	(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED) |			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) |			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_LOW) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_CRITICAL) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY)	|			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_SHUTDOWN) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_REBOOT) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_UCSI) |			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATT_BTP))

#define SCI_HOST_WAKE_EVENT_MASK			\
	(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED) |			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) |			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATT_BTP)	|			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEY_PRESSED))

/* Ambient Light Sensor address */
#define OPT3001_I2C_ADDR_FLAGS	OPT3001_I2C_ADDR1_FLAGS

/* Modules we want to exclude */
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_TEMP_SENSOR
#undef CONFIG_CMD_TIMERINFO
/* #undef CONFIG_CONSOLE_CMDHELP */

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* ADC signal */
enum adc_channel {
	ADC_I_ADP,
	ADC_I_SYS,
	ADC_VCIN1_BATT_TEMP,
	ADC_TP_BOARD_ID,
	ADC_AD_BID,
	ADC_AUDIO_BOARD_ID,
	ADC_PROCHOT_L,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum hx20_board_version {
	BOARD_VERSION_UNKNOWN = -1,
	BOARD_VERSION_0,
	BOARD_VERSION_1,
	BOARD_VERSION_2,
	BOARD_VERSION_3,
	BOARD_VERSION_4,
	BOARD_VERSION_5,
	BOARD_VERSION_6,
	BOARD_VERSION_7,
	BOARD_VERSION_8,
	BOARD_VERSION_9,
	BOARD_VERSION_10,
	BOARD_VERSION_11,
	BOARD_VERSION_12,
	BOARD_VERSION_13,
	BOARD_VERSION_14,
	BOARD_VERSION_15,
	BOARD_VERSION_COUNT,
};

enum pwm_channel {
	PWM_CH_FAN,
	PWM_CH_KBL,
	PWM_CH_DB0_LED_RED,
	PWM_CH_DB0_LED_GREEN,
	PWM_CH_DB0_LED_BLUE,
	PWM_CH_DB1_LED_RED,
	PWM_CH_DB1_LED_GREEN,
	PWM_CH_DB1_LED_BLUE,
	PWM_CH_FPR_LED_RED_EVT,
	PWM_CH_FPR_LED_GREEN_EVT,
	PWM_CH_FPR_LED_RED,
	PWM_CH_FPR_LED_GREEN,
	PWM_CH_FPR_LED_BLUE,
	PWM_CH_COUNT
};

enum fan_channel {
	FAN_CH_0 = 0,
	/* Number of FAN channels */
	FAN_CH_COUNT,
};

enum temp_sensor_id {
	TEMP_SENSOR_LOCAL,
	TEMP_SENSOR_CPU,
	TEMP_SENSOR_DDR,
	TEMP_SENSOR_BATTERY,
#ifdef CONFIG_PECI
	TEMP_SENSOR_PECI,
#endif /* CONFIG_PECI */
	TEMP_SENSOR_COUNT
};

/* Power signals list */
enum power_signal {
#ifdef CONFIG_POWER_S0IX
	X86_SLP_S0_DEASSERTED,
#endif
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_SLP_S5_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	X86_PWR_3V5V_PG,
	X86_VCCIN_AUX_VR_PG,
	X86_VR_PWRGD,
	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum sensor_id {
	BASE_ACCEL,
	BASE_GYRO,
#ifdef CONFIG_ACCEL_KX022
	LID_ACCEL,
#endif
	SENSOR_COUNT,
};

/* Light sensors */
enum als_id {
	ALS_OPT3001 = 0,

	ALS_COUNT
};

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
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     5000
#define PD_MAX_VOLTAGE_MV     20000


/* #define PD_VERBOSE_LOGGING */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE	2048

/*
 * include TFDP macros from mchp chip level
 */
#include "tfdp_chip.h"


/* Map I2C port to controller */
int board_i2c_p2c(int port);

/* Return the two slave addresses the specified
 * controller will respond to when controller
 * is acting as a slave.
 * b[6:0]  = b[7:1] of I2C address 1
 * b[14:8] = b[7:1] of I2C address 2
 */
uint16_t board_i2c_slave_addrs(int controller);

/* Reset PD MCU */
void board_reset_pd_mcu(void);

/* P sensor */
void psensor_interrupt(enum gpio_signal signal);


/* SOC */
void soc_signal_interrupt(enum gpio_signal signal);

/* chassis function */
void chassis_control_interrupt(enum gpio_signal signal);

/* Touchpad process */
void touchpad_interrupt(enum gpio_signal signal);
void touchpad_i2c_interrupt(enum gpio_signal signal);

/* Mainboard power button handler*/
void mainboard_power_button_interrupt(enum gpio_signal signal);

/* fingerprint power button handler*/
void fingerprint_power_button_interrupt(enum gpio_signal signal);

void board_power_off(void);
void cancel_board_power_off(void);

/* power sequence */
int board_chipset_power_on(void);

int board_get_version(void);

void boot_ap_on_g3(void);

void power_button_enable_led(int enable);
void s5_power_up_control(int control);

int pos_get_state(void);

void me_gpio_change(uint32_t flags);

int get_hardware_id(enum adc_channel channel);

int ac_boot_status(void);

void update_me_change(int change);

int poweron_reason_powerbtn(void);

#ifdef CONFIG_LOW_POWER_IDLE
void board_prepare_for_deep_sleep(void);
void board_resume_from_deep_sleep(void);
#endif

void charge_gate_onoff(uint8_t enable);
void charger_psys_enable(uint8_t enable);
#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
