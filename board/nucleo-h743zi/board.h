/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * STM32H743 MCU configuration
 */

#ifndef __BOARD_H
#define __BOARD_H

/* Baseboard features */
#include "base-board.h"

#undef CONFIG_SYSTEM_UNLOCKED

/*
 * These allow console commands to be flagged as restricted.
 * Restricted commands will only be permitted to run when
 * console_is_restricted() returns false.
 * See console_is_restricted's definition in board.c.
 */
#define CONFIG_CONSOLE_COMMAND_FLAGS
#define CONFIG_RESTRICTED_CONSOLE_COMMANDS

/*
 * Enable the blink example that exercises the LEDs.
 */
#define CONFIG_BLINK
#define CONFIG_BLINK_LEDS    GPIO_LED1, GPIO_LED2, GPIO_LED3

#endif /* __BOARD_H */
