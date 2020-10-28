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
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

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

#ifndef __ASSEMBLER__

#include "registers.h"

#define GPIO_DP_HPD GPIO_DDI_MST_IN_HPD

#define GPIO_TRIGGER_1 GPIO_TP41
#define GPIO_TRIGGER_2 GPIO_TP73

enum  debug_gpio {
	TRIGGER_1 = 0,
	TRIGGER_2,
};

void board_debug_gpio(int trigger, int enable, int pulse_usec);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
