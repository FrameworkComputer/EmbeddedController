/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dedede board configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#define CONFIG_SUPPRESSED_HOST_COMMANDS \
	EC_CMD_CONSOLE_SNAPSHOT, EC_CMD_CONSOLE_READ, EC_CMD_USB_PD_DISCOVERY,\
	EC_CMD_USB_PD_POWER_INFO, EC_CMD_PD_GET_LOG_ENTRY, \
	EC_CMD_MOTION_SENSE_CMD, EC_CMD_GET_NEXT_EVENT

/*
 * Variant EC defines. Pick one:
 * VARIANT_DEDEDE_EC_NPCX796FC
 */
#if defined(VARIANT_DEDEDE_EC_NPCX796FC)
	/* NPCX7 config */
	#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
	#define NPCX_TACH_SEL2    0  /* No tach. */

	/* Internal SPI flash on NPCX7 */
	#define CONFIG_FLASH_SIZE (512 * 1024)
	#define CONFIG_SPI_FLASH_REGS
	#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */
#elif defined(VARIANT_DEDEDE_EC_IT8320)
	#define I2C_PORT_EEPROM		IT83XX_I2C_CH_A
	#define I2C_PORT_BATTERY	IT83XX_I2C_CH_B
	#define I2C_PORT_SENSOR		IT83XX_I2C_CH_C
	#define I2C_PORT_SUB_USB_C1	IT83XX_I2C_CH_E
	#define I2C_PORT_USB_C0		IT83XX_I2C_CH_F

	#define I2C_ADDR_EEPROM_FLAGS	0x50

	#define CONFIG_ADC_VOLTAGE_COMPARATOR	/* ITE ADC thresholds */

	#undef CONFIG_UART_TX_BUF_SIZE		/* UART */
	#define CONFIG_UART_TX_BUF_SIZE 4096
#else
#error "Must define a VARIANT_DEDEDE_EC!"
#endif

/*
 * Remapping of schematic GPIO names to common GPIO names expected (hardcoded)
 * in the EC code base.
 */
#define GPIO_CPU_PROCHOT	GPIO_EC_PROCHOT_ODL
#define GPIO_EC_INT_L		GPIO_EC_AP_MKBP_INT_L
#define GPIO_EN_PP5000		GPIO_EN_PP5000_U
#define GPIO_ENTERING_RW	GPIO_EC_ENTERING_RW
#define GPIO_KBD_KSO2		GPIO_EC_KSO_02_INV
#define GPIO_PACKET_MODE_EN	GPIO_ECH1_PACKET_MODE
#define GPIO_PCH_DSW_PWROK	GPIO_EC_AP_DPWROK
#define GPIO_PCH_PWRBTN_L	GPIO_EC_AP_PWR_BTN_ODL
#define GPIO_PCH_RSMRST_L	GPIO_EC_AP_RSMRST_L
#define GPIO_PCH_RTCRST		GPIO_EC_AP_RTCRST
#define GPIO_PCH_SLP_S0_L	GPIO_SLP_S0_L
#define GPIO_PCH_SLP_S3_L	GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S4_L	GPIO_SLP_S4_L
#define GPIO_PCH_WAKE_L		GPIO_EC_AP_WAKE_ODL
#define GPIO_PG_EC_RSMRST_ODL	GPIO_RSMRST_PWRGD_L
#define GPIO_POWER_BUTTON_L	GPIO_H1_EC_PWR_BTN_ODL
#define GPIO_RSMRST_L_PGOOD	GPIO_RSMRST_PWRGD_L
#define GPIO_SYS_RESET_L	GPIO_SYS_RST_ODL
#define GPIO_USB_C0_DP_HPD	GPIO_EC_AP_USB_C0_HPD
#define GPIO_USB_C1_DP_HPD	GPIO_EC_AP_USB_C1_HDMI_HPD
#define GPIO_VOLUME_UP_L	GPIO_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L	GPIO_VOLDN_BTN_ODL
#define GPIO_WP			GPIO_EC_WP_OD
#define GMR_TABLET_MODE_GPIO_L	GPIO_LID_360_L

/* Common EC defines */

/* Work around double CR50 reset by waiting in initial power on. */
#define CONFIG_BOARD_RESET_AFTER_POWER_ON

/* Optional console commands */
#define CONFIG_CMD_CHARGER_DUMP

/* Enable AP Reset command for TPM with old firmware version to detect it. */
#define CONFIG_CMD_AP_RESET_LOG
#define CONFIG_HOSTCMD_AP_RESET

/* Enable i2ctrace command */
#define CONFIG_I2C_DEBUG

/* EC Modules */
#define CONFIG_ADC
#define CONFIG_CRC8
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_EVENTS
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_I2C_BUS_MAY_BE_UNPOWERED
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_POWER_PP5000_CONTROL
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1
#define CONFIG_VBOOT_EFS2

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATTERY_PRES_ODL
#define CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART

/* Buttons / Switches */
#define CONFIG_SWITCH
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_WP_ACTIVE_HIGH

/* CBI */
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_BOARD_VERSION_CBI

/* Charger */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 256
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_USB_CHARGER
#define CONFIG_TRICKLE_CHARGING

/* Keyboard */
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042

/* Backlight */
#define CONFIG_BACKLIGHT_LID
#define GPIO_ENABLE_BACKLIGHT   GPIO_EN_BL_OD

/* LED */
#define CONFIG_LED_COMMON

/* SoC */
#define CONFIG_BOARD_HAS_RTC_RESET
#define CONFIG_CHIPSET_JASPERLAKE
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_SLEEP_FAILURE_DETECTION
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW

/* USB Type-C */
#define CONFIG_USB_MUX_PI3USB31532
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* USB PD */
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DP_HPD_GPIO
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_REV30
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
/* #define CONFIG_USB_PD_VBUS_DETECT_CHARGER */
#define CONFIG_USB_PD_VBUS_MEASURE_CHARGER
#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_USB_PID 0x5042
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_HOSTCMD_PD_CONTROL

/* UART COMMAND */
#define CONFIG_CMD_CHARGEN

/* Define typical operating power and max power. */
#define PD_MAX_VOLTAGE_MV     20000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_POWER_MW       45000
#define PD_OPERATING_POWER_MW 15000

/* TODO(b:147314141): Verify these timings */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000	/* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	250000	/* us */
#define PD_VCONN_SWAP_DELAY		5000	/* us */

#ifndef __ASSEMBLER__

#include "common.h"
#include "gpio_signal.h"

/* Common enums */
#if defined(VARIANT_DEDEDE_EC_NPCX796FC)
#elif defined(VARIANT_DEDEDE_EC_IT8320)
	enum board_vcmp {
		VCMP_SNS_PP3300_LOW,
		VCMP_SNS_PP3300_HIGH,
		VCMP_COUNT
	};
#else
#error "Must define a VARIANT_DEDEDE_EC!"
#endif

/* Interrupt handler for signals that are used to generate ALL_SYS_PGOOD. */
void baseboard_all_sys_pgood_interrupt(enum gpio_signal signal);

/* Reset all TCPCs */
void board_reset_pd_mcu(void);

/*
 * Bit to indicate if the PP3000_A rail's power is good. Will be updated by ADC
 * interrupt.
 */
extern uint32_t pp3300_a_pgood;

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BASEBOARD_H */
