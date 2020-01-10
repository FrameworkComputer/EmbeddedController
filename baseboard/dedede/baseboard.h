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
#ifdef VARIANT_DEDEDE_EC_NPCX796FC
	/* NPCX7 config */
	#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
	#define NPCX_TACH_SEL2    0  /* No tach. */
	#define NPCX7_PWM1_SEL    1  /* GPIO C2 is used as PWM1. */

	/* Internal SPI flash on NPCX7 */
	#define CONFIG_FLASH_SIZE (512 * 1024)
	#define CONFIG_SPI_FLASH_REGS
	#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */
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
#define GPIO_PCH_DSW_PWROK	GPIO_EC_AP_DPWROK
#define GPIO_PCH_PWRBTN_L	GPIO_EC_AP_PWR_BTN_ODL
#define GPIO_PCH_RSMRST_L	GPIO_EC_AP_RSMRST_L
#define GPIO_PCH_RTCRST		GPIO_EC_AP_RTCRST
#define GPIO_PCH_SLP_S0_L	GPIO_SLP_S0_L
#define GPIO_PCH_SLP_S3_L	GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S4_L	GPIO_SLP_S4_L
#define GPIO_PCH_SYS_PWROK	GPIO_EC_AP_SYS_PWROK
#define GPIO_PCH_WAKE_L		GPIO_EC_AP_WAKE_ODL
#define GPIO_PG_EC_RSMRST_ODL	GPIO_RSMRST_PWRGD_L
#define GPIO_POWER_BUTTON_L	GPIO_H1_EC_PWR_BTN_ODL
#define GPIO_RSMRST_L_PGOOD	GPIO_RSMRST_PWRGD_L
#define GPIO_SYS_RESET_L	GPIO_SYS_RST_ODL
#define GPIO_VOLUME_UP_L	GPIO_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L	GPIO_VOLDN_BTN_ODL
#define GPIO_WP			GPIO_EC_WP_OD
#define GMR_TABLET_MODE_GPIO_L	GPIO_LID_360_L

/* Common EC defines */

/* EC Modules */
#define CONFIG_ADC
#define CONFIG_CRC8
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_EVENTS
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Buttons / Switches */
#define CONFIG_SWITCH
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_WP_ACTIVE_HIGH

/* CBI */
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_BOARD_VERSION_CBI

/* SoC */
#define CONFIG_BOARD_HAS_RTC_RESET
#define CONFIG_CHIPSET_JASPERLAKE
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON


#ifndef __ASSEMBLER__

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BASEBOARD_H */
