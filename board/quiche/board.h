/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quiche board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/* Optional features */
/*
 * For MP release, CONFIG_SYSTEM_UNLOCKED must be undefined, and
 * CONFIG_FLASH_PSTATE_LOCKED must be defined in order to enable write protect
 * using option bytes WRP registers.
 */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */
#undef CONFIG_FLASH_PSTATE_LOCKED


/* USB Type C and USB PD defines */
#define USB_PD_PORT_HOST   0
#define USB_PD_PORT_DP     1
#define USB_PD_PORT_USB3   2

/*
 * The host (C0) and display (C1) usbc ports are usb-pd capable. There is
 * also a type-c only port (C2). C2 must be accounted for in PORT_MAX_COUNT so
 * the PPC config table is correctly sized and the PPC driver can be used to
 * control VBUS on/off.
 */
#define CONFIG_USB_PD_PORT_MAX_COUNT 3
#define CONFIG_USB_MUX_PS8822

#define CONFIG_USB_PID 0x5048
#define CONFIG_USB_BCD_DEV 0x0001 /* v 0.01 */
#define CONFIG_USB_PD_IDENTITY_HW_VERS 1
#define CONFIG_USB_PD_IDENTITY_SW_VERS 1

/* I2C port names */
#define I2C_PORT_I2C1	0
#define I2C_PORT_I2C2	1
#define I2C_PORT_I2C3	2

/* Required symbolic I2C port names */
#define I2C_PORT_MP4245 I2C_PORT_I2C3
#define I2C_PORT_EEPROM I2C_PORT_I2C3
#define MP4245_I2C_ADDR_FLAGS MP4245_I2C_ADDR_0_FLAGS

/* Include math_util for bitmask_uint64 used in pd_timers */
#define CONFIG_MATH_UTIL

#ifndef __ASSEMBLER__

#include "registers.h"

#define GPIO_DP_HPD GPIO_DDI_MST_IN_HPD
#define GPIO_USBC_UF_ATTACHED_SRC GPIO_USBC_UF_MUX_VBUS_EN
#define GPIO_BPWR_DET GPIO_TP73
#define GPIO_USB_HUB_OCP_NOTIFY GPIO_USBC_DATA_OCP_NOTIFY
#define GPIO_UFP_PLUG_DET GPIO_MST_UFP_PLUG_DET
#define GPIO_PWR_BUTTON_RED GPIO_EC_STATUS_LED1
#define GPIO_PWR_BUTTON_GREEN GPIO_EC_STATUS_LED2

#define BUTTON_PRESSED_LEVEL 1
#define BUTTON_RELEASED_LEVEL 0

#define GPIO_TRIGGER_1 GPIO_EC_STATUS_LED1
#define GPIO_TRIGGER_2 GPIO_EC_STATUS_LED2

enum  debug_gpio {
	TRIGGER_1 = 0,
	TRIGGER_2,
};

/*
 * Function used to control GPIO signals as a timing marker. This is intended to
 * be used for development/debugging purposes.
 *
 * @param trigger GPIO debug signal selection
 * @param level desired level of the debug gpio signal
 * @param pulse_usec pulse width if non-zero
 */
void board_debug_gpio(enum debug_gpio trigger, int level, int pulse_usec);

/*
 * Function called in power on case to enable usbc related interrupts
 */
void board_enable_usbc_interrupts(void);

/*
 * Function called in power off case to disable usbc related interrupts
 */
void board_disable_usbc_interrupts(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
