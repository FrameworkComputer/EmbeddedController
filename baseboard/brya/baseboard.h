/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/*
 * By default, enable all console messages excepted HC
 */
#define CC_DEFAULT     (CC_ALL & ~(BIT(CC_HOSTCMD)))

/* NPCX9 config */
#define NPCX9_PWM1_SEL    1  /* GPIO C2 is used as PWM1. */
/*
 * This defines which pads (GPIO10/11 or GPIO64/65) are connected to
 * the "UART1" (NPCX_UART_PORT0) controller when used for
 * CONSOLE_UART.
 */
#define NPCX_UART_MODULE2	1 /* 1:GPIO64/65 for UART1 */

/* EC Defines */
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8

/* Host communication */
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S4

/* Common battery defines */
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_HW_PRESENT_CUSTOM
#define CONFIG_BATTERY_REVIVE_DISCONNECT

/* Chipset config */
#define CONFIG_CHIPSET_ALDERLAKE_SLG4BD44540

#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_SLEEP_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

#define CONFIG_BOARD_HAS_RTC_RESET

/* Physical Volume Buttons */
#define CONFIG_VOLUME_BUTTONS

/* Common Keyboard Defines */
#define CONFIG_CMD_KEYBOARD
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_KEYPAD
#define CONFIG_KEYBOARD_PROTOCOL_8042
#ifdef CONFIG_KEYBOARD_VIVALDI
#define CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2
#else
#define CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI3
#endif

/* Thermal features */
#define CONFIG_THROTTLE_AP
#define CONFIG_CHIPSET_CAN_THROTTLE

#define CONFIG_PWM

/* Enable I2C Support */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER

#ifndef __ASSEMBLER__

#include <stdbool.h>

#include "common.h"
#include "baseboard_usbc_config.h"
#include "extpower.h"

/**
 * Configure run-time data structures and operation based on CBI data. This
 * typically includes customization for changes in the BOARD_VERSION and
 * FW_CONFIG fields in CBI. This routine is called from the baseboard after
 * the CBI data has been initialized.
 */
__override_proto void board_cbi_init(void);

/*
 * Check battery disconnect state.
 * This function will return if battery is initialized or not.
 * @return true - initialized. false - not.
 */
__override_proto bool board_battery_is_initialized(void);

/*
 * Return the board revision number.
 */
uint8_t get_board_id(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
