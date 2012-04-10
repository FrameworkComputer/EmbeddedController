/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CHIP_CONFIG_H
#define __CROS_EC_CHIP_CONFIG_H

/* 16.000 Mhz internal oscillator frequency (PIOSC) */
#define INTERNAL_CLOCK 16000000

/* Memory mapping */
#define CONFIG_FLASH_BASE       0x00000000
#define CONFIG_FLASH_SIZE       0x00040000
#define CONFIG_FLASH_BANK_SIZE  0x00000800  /* Protect bank size */
#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00008000

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (80 * 1024)
#define CONFIG_FW_RO_OFF        0
#define CONFIG_FW_A_OFF         CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_B_OFF         (2 * CONFIG_FW_IMAGE_SIZE)

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 240

/* Debug UART parameters for panic message */
#define CONFIG_UART_ADDRESS    0x4000c000
#define CONFIG_UART_DR_OFFSET  0x00
#define CONFIG_UART_SR_OFFSET  0x18
#define CONFIG_UART_SR_TXEMPTY 0x80

/* System stack size */
#define CONFIG_STACK_SIZE 4096

/* build with assertions and debug messages */
#define CONFIG_DEBUG

/* Optional features present on this chip */
#define CONFIG_ADC
#define CONFIG_EEPROM
#define CONFIG_FLASH
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_LPC
#define CONFIG_PWM

/* Compile for running from RAM instead of flash */
/* #define COMPILE_FOR_RAM */

#endif  /* __CROS_EC_CHIP_CONFIG_H */
