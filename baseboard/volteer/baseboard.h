/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H


/* NPCX7 config */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
/* Internal SPI flash on NPCX796FC is 512 kB */
#define CONFIG_FLASH_SIZE (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* EC Defines */
#define CONFIG_CRC8
#define CONFIG_HIBERNATE_PSL
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Host communication */

/* Chipset config */

/* Common Keyboard Defines */

/* Sensors */

/* Common charger defines */

/* Common battery defines */

/* USB Type C and USB PD defines */

/* BC 1.2 */

/* I2C Bus Configuration */


#ifndef __ASSEMBLER__

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
