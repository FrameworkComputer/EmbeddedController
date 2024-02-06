/* Copyright 2017 The ChromiumOS Authors
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

/*
 * Debug on EVB with CONFIG_CHIPSET_DEBUG
 * Keep WDG disabled and JTAG enabled.
 * CONFIG_BOARD_PRE_INIT enables JTAG early
 */
/* #define CONFIG_CHIPSET_DEBUG */

#ifdef CONFIG_CHIPSET_DEBUG
#ifndef CONFIG_BOARD_PRE_INIT
#define CONFIG_BOARD_PRE_INIT
#endif
#endif

/*
 * DEBUG: Add CRC32 in last 4 bytes of EC_RO/RW binaries
 * in SPI. LFW will use DMA CRC32 HW to check data integrity.
 * #define CONFIG_MCHP_LFW_DEBUG
 */

/*
 * EC UART console on UART 0 or 1
 */
#define CONFIG_UART_CONSOLE 0

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
/* #define CONFIG_MEC_GPIO_EC_CMDS */

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
#define CONFIG_BOARD_EC_HANDLES_ALL_SYS_PWRGD

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

/* New eSPI configuration items */

/*
 * Maximum clock frequence eSPI EC advertises
 * Values in MHz are 20, 25, 33, 50, and 66
 */
/* KBL + EVB fly-wire hook up only supports 20MHz */
#define CONFIG_HOST_INTERFACE_ESPI_EC_MAX_FREQ MCHP_ESPI_CAP1_MAX_FREQ_20M

/*
 * EC eSPI advertises IO lanes
 * 0 = Single
 * 1 = Single and Dual
 * 2 = Single and Quad
 * 3 = Single, Dual, and Quad
 */
/* KBL + EVB fly-wire hook up only support Single mode */
#define CONFIG_HOST_INTERFACE_ESPI_EC_MODE MCHP_ESPI_CAP1_SINGLE_MODE

/*
 * Bit map of eSPI channels EC advertises
 * bit[0] = 1 Peripheral channel
 * bit[1] = 1 Virtual Wire channel
 * bit[2] = 1 OOB channel
 * bit[3] = 1 Flash channel
 */
#define CONFIG_HOST_INTERFACE_ESPI_EC_CHAN_BITMAP MCHP_ESPI_CAP0_ALL_CHAN_SUPP

#define CONFIG_MCHP_ESPI_VW_SAVE_ON_SLEEP

/*
 * Allow dangerous commands.
 * TODO(shawnn): Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Optional features */
#define CONFIG_ACCELGYRO_BMI160
/* #define CONFIG_ACCEL_KX022 */
/* #define CONFIG_ALS */
/* #define CONFIG_ALS_OPT3001 */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_BAT_PRESENT_L
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_VERSION_GPIO
#define CONFIG_BUTTON_COUNT 2
/* #define CONFIG_CHARGE_MANAGER */
/* #define CONFIG_CHARGE_RAMP_SW */

/* #define CONFIG_CHARGER */

/* #define CONFIG_CHARGER_DISCHARGE_ON_AC */
/* #define CONFIG_CHARGER_ISL9237 */
/* #define CONFIG_CHARGER_ILIM_PIN_DISABLED */
/* #define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512 */
/* #define CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT 512 */

/* #define CONFIG_CHARGER_NARROW_VDC */
/* #define CONFIG_CHARGER_PROFILE_OVERRIDE */
/* #define CONFIG_CHARGER_SENSE_RESISTOR 10 */
/* #define CONFIG_CHARGER_SENSE_RESISTOR_AC 20 */
/* #define CONFIG_CMD_CHARGER_ADC_AMON_BMON */

#define CONFIG_CHIPSET_SKYLAKE
#define CONFIG_CHIPSET_RESET_HOOK

#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S3
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S4
#define GPIO_PG_EC_RSMRST_ODL GPIO_RSMRST_L_PGOOD

#define CONFIG_CLOCK_CRYSTAL
#define CONFIG_EXTPOWER_GPIO
/* #define CONFIG_HOSTCMD_PD */
/* #define CONFIG_HOSTCMD_PD_PANIC */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_COMMON

#ifdef CONFIG_ACCEL_KX022
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#endif /* CONFIG_ACCEL_KX022 */

#define CONFIG_LID_SWITCH
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
#define CONFIG_MCHP_48MHZ_OUT

/*
 * DEBUG: Save and print out PCR sleep enables,
 * clock required, and interrupt aggregator result
 * registers.
 */
#define CONFIG_MCHP_DEEP_SLP_DEBUG

/*
 * MCHP debug EC code turn off GCC link-time-optimization
 * #define CONFIG_LTO
 */
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30

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
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
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

#define CONFIG_VBOOT_HASH

/*
 * MEC1701H loads firmware using QMSPI controller
 * CONFIG_SPI_FLASH_PORT is the index into
 * spi_devices[] in board.c
 */
#define CONFIG_SPI_FLASH_PORT 0
#define CONFIG_SPI_FLASH
/*
 * Google uses smaller flashes on chromebook boards
 * MCHP SPI test dongle for EVB uses 16MB W25Q128F
 * Configure for smaller flash is OK for testing except
 * for SPI flash lock bit.
 */
#define CONFIG_FLASH_SIZE_BYTES 524288
#define CONFIG_SPI_FLASH_W25X40
/*
 * #define CONFIG_FLASH_SIZE_BYTES 0x1000000
 * #define CONFIG_SPI_FLASH_W25Q128
 */

/*
 * Enable extra SPI flash and generic SPI
 * commands via EC UART
 */
#define CONFIG_CMD_SPI_FLASH
#define CONFIG_CMD_SPI_XFER

/* common software SHA256 required by vboot and rollback */
#define CONFIG_SHA256_SW

/*
 * Enable MCHP SHA256 hardware accelerator module.
 * API is same as software SHA256 but prefixed with "chip_"
 * #define CONFIG_SHA256_HW
 */

/* enable console command to test HW Hash engine
 * #define CONFIG_CMD_SHA256_TEST
 */

/*
 * MEC17xx EVB + SKL/KBL RVP3 does not have
 * BD99992GW PMIC with NCP15WB thermistor.
 * We have connected a Maxim DS1624 I2C temperature
 * sensor. The sensor board has a thermistor on it
 * we connect to an EC ADC channel.
 */
#if 0
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_BD99992GW
#define CONFIG_THERMISTOR_NCP15WB
#define CONFIG_DPTF
#else
#define CONFIG_TEMP_SENSOR
#define CONFIG_DPTF
#endif

/* Enable GPSPI0 controller and port for
 * SPI Accelerometer.
 * bit[0] == 1 GPSPI0
 * bit[1] == 0 board does not use GPSPI1
 * Make sure to not include GPSPI in little-firmware(LFW)
 */
#ifndef LFW
#define CONFIG_MCHP_GPSPI 0x01
#endif

/* SPI Accelerometer
 * CONFIG_SPI_FLASH_PORT is the index into
 * spi_devices[] in board.c
 */
#define CONFIG_SPI_ACCEL_PORT 1

/*
 * Enable EC UART commands to read/write
 * motion sensor.
 */
#define CONFIG_CMD_ACCELS

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

/* LED signals */
#define GPIO_BAT_LED_RED GPIO_CHARGE_LED_1
#define GPIO_BAT_LED_GREEN GPIO_CHARGE_LED_2

/* I2C ports */
#define I2C_CONTROLLER_COUNT 2
#define I2C_PORT_COUNT 2

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

#define I2C_PORT_PMIC MCHP_I2C_PORT10
#define I2C_PORT_USB_CHARGER_1 MCHP_I2C_PORT2
#define I2C_PORT_USB_MUX MCHP_I2C_PORT2
#define I2C_PORT_USB_CHARGER_2 MCHP_I2C_PORT2
#define I2C_PORT_PD_MCU MCHP_I2C_PORT3
#define I2C_PORT_TCPC MCHP_I2C_PORT3
#define I2C_PORT_ALS MCHP_I2C_PORT4
#define I2C_PORT_ACCEL MCHP_I2C_PORT4
#define I2C_PORT_BATTERY MCHP_I2C_PORT5
#define I2C_PORT_CHARGER MCHP_I2C_PORT5

/* Thermal sensors read through PMIC ADC interface */
#if 0
#define I2C_PORT_THERMAL I2C_PORT_PMIC
#else
#define I2C_PORT_THERMAL MCHP_I2C_PORT4
#endif

/* Ambient Light Sensor address */
#define OPT3001_I2C_ADDR_FLAGS OPT3001_I2C_ADDR1_FLAGS

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
	ADC_VBUS,
	ADC_AMON_BMON,
	ADC_PSYS,
	ADC_CASE,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,

	/* These temp sensors are only readable in S0 */
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CASE,
	/*	TEMP_SENSOR_CHARGER, */
	/*	TEMP_SENSOR_DRAM, */
	/*	TEMP_SENSOR_WIFI, */

	TEMP_SENSOR_COUNT
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
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* delay to turn on/off vconn */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 45000
#define PD_MAX_CURRENT_MA 3000

/* Try to negotiate to 20V since i2c noise problems should be fixed. */
#define PD_MAX_VOLTAGE_MV 20000

/*
 * include TFDP macros from mchp chip level
 */
#include "tfdp_chip.h"

/* Map I2C port to controller */
int board_i2c_p2c(int port);

/* Reset PD MCU */
void board_reset_pd_mcu(void);

#ifdef CONFIG_LOW_POWER_IDLE
void board_prepare_for_deep_sleep(void);
void board_resume_from_deep_sleep(void);
#endif

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
