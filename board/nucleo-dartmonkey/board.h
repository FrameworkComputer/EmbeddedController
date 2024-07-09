/* Copyright 2020 The ChromiumOS Authors
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

/* Fingerprint needs to store a secrect in the anti-rollback block */
#define CONFIG_ROLLBACK_SECRET_SIZE 32

#define CONFIG_BORINGSSL_CRYPTO

/* SPI configuration for the fingerprint sensor */
#define CONFIG_SPI_CONTROLLER
#define CONFIG_SPI_FP_PORT 2 /* SPI4: third master config */

#define CONFIG_FINGERPRINT_MCU

#ifdef SECTION_IS_RW
/* Select fingerprint sensor */
#define CONFIG_FP_SENSOR_FPC1145
#define CONFIG_CMD_FPSENSOR_DEBUG
/* Special memory regions to store large arrays */
#define FP_FRAME_SECTION __SECTION(ahb4)
#define FP_TEMPLATE_SECTION __SECTION(ahb)
#endif /* SECTION_IS_RW */

#ifndef __ASSEMBLER__
#ifdef __cplusplus
extern "C" {
#endif
void fps_event(enum gpio_signal signal);
#ifdef __cplusplus
}
#endif
#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
