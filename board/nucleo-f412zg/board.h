/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32F412 */

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

#endif /* __BOARD_H */
