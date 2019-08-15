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
#define CONFIG_HOSTCMD_ESPI

/* Chipset config */
/* TODO - replace with TigerLake once the changes land */
#define CONFIG_CHIPSET_ICELAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_S0IX_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

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
