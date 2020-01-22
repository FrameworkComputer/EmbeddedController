/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Deltaur board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

/* Host command protocol */
#define CONFIG_HOSTCMD_ESPI

/* Increase UART buffer size for better UART dump performance with AP */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/*
 * TODO(b/148160415): Evaluate external flash options
 */
#define CONFIG_SPI_FLASH_PORT 0
#define CONFIG_SPI_FLASH_W25X40
#define CONFIG_FLASH_SIZE (512 * 1024)
#define CONFIG_CHIPSET_RESET_HOOK

/* Keyboard features */

/* Sensors */

/* USB Type C and USB PD defines */

/* USB Type A Features */

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */

/* Unneeded features */
#undef CONFIG_ADC
#undef CONFIG_SWITCH
#undef CONFIG_LID_SWITCH

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
