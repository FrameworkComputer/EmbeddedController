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

/* Common EC defines */
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_EVENTS
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_I2C
#define CONFIG_VBOOT_HASH
#define CONFIG_CRC8
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_BOARD_HAS_RTC_RESET

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BASEBOARD_H */
