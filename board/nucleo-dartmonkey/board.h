/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * STM32H743 + FPC 1145 Fingerprint MCU configuration
 *
 * This board is designed to have nucleo-h743zi support (uart+btn+leds) with
 * dartmonkey configuration (fingerprint support).
 * This allows for proxy testing of dartmonkey on the Nucleo-H743ZI.
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

#ifndef __ASSEMBLER__

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
