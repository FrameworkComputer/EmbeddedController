/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Nuvoton M4 EB */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#undef CONFIG_HOST_EVENT64

/* Optional modules */
#define CONFIG_ADC
#define CONFIG_CHIPSET_RK3399
#define CONFIG_CMD_RTC
#define CONFIG_FPU
#define CONFIG_HOSTCMD_RTC
#define CONFIG_HOSTCMD_SPS
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_I2C_VIRTUAL_BATTERY
#define CONFIG_I2C_PASSTHRU_RESTRICTED
#define CONFIG_LED_COMMON
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_POWER_COMMON
#define CONFIG_PWM
#define CONFIG_PWM_DISPLIGHT
#define CONFIG_SPI
#define CONFIG_SPI_MASTER
#define CONFIG_SPI_FLASH_GD25LQ40
#define CONFIG_SPI_FLASH_REGS

#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands for testing */

/*
 * We are code space-constrained on kevin, so take 10K that is normally used
 * as data RAM (was 30K, now 22K) and use it for code RAM (was 96K, now 104K)
 */
#define RAM_SHIFT_SIZE (8 * 1024)
#undef  CONFIG_RO_SIZE
#define CONFIG_RO_SIZE (NPCX_PROGRAM_MEMORY_SIZE + RAM_SHIFT_SIZE)
#undef  CONFIG_RAM_BASE
#define CONFIG_RAM_BASE (0x200C0000 + RAM_SHIFT_SIZE)
#undef  CONFIG_RAM_SIZE
#undef  CONFIG_DATA_RAM_SIZE
#define CONFIG_DATA_RAM_SIZE (0x00008000 - RAM_SHIFT_SIZE)
#define CONFIG_RAM_SIZE (CONFIG_DATA_RAM_SIZE - 0x800)

/* Optional features */
#define CONFIG_BOARD_VERSION_CUSTOM
#define CONFIG_FLASH_SIZE          0x00080000 /* 512KB spi flash */
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_HOSTCMD_SECTION_SORTED /* Host commands are sorted. */
/* By default, set hcdebug to off */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_MKBP /* Instead of 8042 protocol of keyboard */
#define CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2
#define CONFIG_LTO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_VBOOT_HASH
#define CONFIG_VOLUME_BUTTONS

#define CONFIG_CHARGER
#define CONFIG_CHARGER_BD9995X
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_MAINTAIN_VBAT
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 2
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 2
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 15000
#define CONFIG_CHARGER_PROFILE_OVERRIDE
#define CONFIG_USB_CHARGER
#define CONFIG_USB_MUX_VIRTUAL

/* Increase tx buffer size, as we'd like to stream EC log to AP. */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048

/* Sensors */
#define CONFIG_ACCEL_BMA255
#define CONFIG_ACCEL_KX022
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT TASK_EVENT_CUSTOM(4)
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_INVALID_CHECK
#define CONFIG_LID_ANGLE_TABLET_MODE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE    BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID     LID_ACCEL

#ifdef BOARD_GRU
#define CONFIG_ALS_OPT3001
#define OPT3001_I2C_ADDR OPT3001_I2C_ADDR1
#define CONFIG_BARO_BMP280
#endif
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO 128
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO / 3)

/* Sensors without hardware FIFO are in forced mode */
#ifdef BOARD_KEVIN
#define CONFIG_ACCEL_FORCE_MODE_MASK (1 << LID_ACCEL)
#else
#define CONFIG_ACCEL_FORCE_MODE_MASK \
	((1 << LID_ACCEL) | (1 << BASE_BARO))
#endif

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH

/* USB PD config */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP_SW
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DISCHARGE_GPIO
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_TCPM_FUSB302
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_VBUS_DETECT_CHARGER
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_PD_COMM_LOCKED

#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_L
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART

#ifdef BOARD_KEVIN
#define CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
#define CONFIG_USB_PD_GIVE_BACK
#endif

#define PD_OPERATING_POWER_MW 15000
/* Kevin board accommodate 40W input charge current */
#ifdef BOARD_KEVIN
#define PD_MAX_POWER_MW       40000
#else
/* 60W for Gru */
#define PD_MAX_POWER_MW       60000
#endif
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

#define PD_MIN_CURRENT_MA     500
#define PD_MIN_POWER_MW       2500

#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 50000  /* us */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

#define CONFIG_UART_HOST                0

/* Optional feature - used by nuvoton */
#define NPCX_UART_MODULE2    1 /* 0:GPIO10/11 1:GPIO64/65 as UART */
#define NPCX_JTAG_MODULE2    0 /* 0:GPIO21/17/16/20 1:GPIOD5/E2/D4/E5 as JTAG*/
#define NPCX_TACH_SEL2       0 /* 0:GPIO40/73 1:GPIO93/A6 as TACH */
/* Enable SHI PU on transition to S0. Disable the PU otherwise for leakage. */
#define NPCX_SHI_CS_PU
#define NPCX_SHI_BYPASS_OVER_256B

/* Optional for testing */
#undef  CONFIG_PSTORE

/* Reduce code size */
#define CONFIG_COMMON_GPIO_SHORTNAMES
#define GPIO_NAME_BY_PIN(port, index) #port#index
#undef  CONFIG_CONSOLE_VERBOSE

#define CONFIG_HOSTCMD_ALIGNED

/* Modules we want to exclude */
#undef CONFIG_CMD_BATTFAKE
#undef CONFIG_CMD_CRASH
#undef CONFIG_CMD_FLASH
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_HCDEBUG
#undef CONFIG_CMD_I2C_SCAN
#undef CONFIG_CMD_MD
#undef CONFIG_CMD_MMAPINFO
#undef CONFIG_CMD_POWERINDEBUG
#undef CONFIG_CMD_PWR_AVG
#undef CONFIG_CMD_TIMERINFO
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_CONSOLE_HISTORY
#undef CONFIG_EC_CMD_PD_CHIP_INFO

#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_FLASHINFO
#undef CONFIG_CMD_I2C_XFER

/* Gru is especially limited on code space */
#ifdef BOARD_GRU
#undef CONFIG_CMD_IDLE_STATS
#undef CONFIG_USB_PD_LOGGING
#undef CONFIG_CMD_SHMEM
#undef CONFIG_CMD_USBMUX
#undef CONFIG_CMD_TYPEC
#endif

/*
 * Remove task profiling to improve SHI interrupt latency.
 * TODO(crosbug.com/p/55710): Re-define once interrupt latency is within
 * tolerance.
 */
#undef CONFIG_TASK_PROFILING

#define I2C_PORT_TCPC0    NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC1    NPCX_I2C_PORT0_1
#define I2C_PORT_ACCEL    NPCX_I2C_PORT1
#define I2C_PORT_ALS      NPCX_I2C_PORT1
#define I2C_PORT_BARO     NPCX_I2C_PORT1
#define I2C_PORT_CHARGER  NPCX_I2C_PORT2
#define I2C_PORT_BATTERY  NPCX_I2C_PORT3
#define I2C_PORT_VIRTUAL_BATTERY I2C_PORT_BATTERY

/* Enable Accel over SPI */
#define CONFIG_SPI_ACCEL_PORT    0  /* SPI master port (SPIP) form BMI160 */

#define CONFIG_MKBP_EVENT
/* Define the MKBP events which are allowed to wakeup AP in S3. */
#define CONFIG_MKBP_WAKEUP_MASK \
		(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEY_PRESSED) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_RTC))

/*
 * Define the host events which are to be reported to the kernel.
 *
 * Linux 4.4 kernel uses EC_HOST_EVENT_PD_MCU, EC_HOST_EVENT_USB_MUX,
 * and EC_HOST_EVENT_RTC and all enabled WAKE events.
 *
 * Linux 3.18 kernel uses EC_HOST_EVENT_PD_MCU and all enabled WAKE events.
 */
#undef CONFIG_HOST_EVENT_REPORT_MASK
#define CONFIG_HOST_EVENT_REPORT_MASK \
		(CONFIG_MKBP_WAKEUP_MASK |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_PD_MCU) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_RTC) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_USB_MUX))

#ifndef __ASSEMBLER__

enum adc_channel {
	/* Real ADC channels begin here */
	ADC_BOARD_ID = 0,
	ADC_PP900_AP,
	ADC_PP1200_LPDDR,
	ADC_PPVAR_CLOGIC,
	ADC_PPVAR_LOGIC,
	ADC_CH_COUNT
};

enum pwm_channel {
/* don't change the order or add anything between, this is ABI to kernel dts! */
#ifdef BOARD_KEVIN
	PWM_CH_LED_GREEN,
#endif
	PWM_CH_DISPLIGHT,
	PWM_CH_LED_RED,
#ifdef BOARD_KEVIN
	PWM_CH_LED_BLUE,
#endif
	/* Number of PWM channels */
	PWM_CH_COUNT
};

/* power signal definitions */
enum power_signal {
	PP5000_PWR_GOOD = 0,
	SYS_PWR_GOOD,
	AP_PWR_GOOD,
	SUSPEND_DEASSERTED,

	/* Number of signals */
	POWER_SIGNAL_COUNT,
};

/* Light sensors */
#ifdef BOARD_GRU
enum als_id {
	ALS_OPT3001 = 0,
	ALS_COUNT
};
#endif

/* Motion sensors */
enum sensor_id {
	BASE_ACCEL = 0,
	BASE_GYRO,
	LID_ACCEL,
#ifdef BOARD_GRU
	BASE_BARO,
#endif
};

#include "gpio_signal.h"
#include "registers.h"

void board_reset_pd_mcu(void);
int board_get_version(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
