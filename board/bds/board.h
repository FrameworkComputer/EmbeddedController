/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Stellaris EKB-LM4F-EAC board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED  /* Allow dangerous commands */

/* LM4 modules we don't use on link but still want to keep compiling */
#define CONFIG_EEPROM
#define CONFIG_EOPTION
#define CONFIG_PSTORE

/* LM4 modules we want to exclude */
#undef CONFIG_SWITCH

/* Write protect is active high */
#define CONFIG_WP_ACTIVE_HIGH

#ifndef __ASSEMBLER__

enum adc_channel
{
	ADC_CH_EC_TEMP = 0,  /* EC internal die temperature in degrees K. */
	ADC_CH_BDS_POT,      /* BDS pot input. */
	ADC_CH_COUNT
};

/* I2C ports */
#define I2C_PORT_LIGHTBAR 5  // port 5 / PA6:7 on link, but PG6:7 on badger
/* Number of I2C ports used */
#define I2C_PORTS_USED 1

/* GPIOs for second UART port */
#define CONFIG_HOST_UART 1
#define CONFIG_HOST_UART_IRQ LM4_IRQ_UART1
#define CONFIG_HOST_UART1_GPIOS_PB0_1

/* GPIO signal list */
enum gpio_signal {
	GPIO_RECOVERYn = 0,       /* Recovery signal from DOWN button */
	GPIO_DEBUG_LED,           /* Debug LED */
	/* Signals which aren't implemented on BDS but we'll emulate anyway, to
	 * make it more convenient to debug other code. */
	GPIO_WP,                  /* Write protect input */
	GPIO_ENTERING_RW,         /* EC entering RW code */

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

/* EEPROM blocks */
#define EEPROM_BLOCK_EOPTION       1  /* EC persistent options */
#define EEPROM_BLOCK_START_PSTORE 16  /* Host persistent storage */
#define EEPROM_BLOCK_COUNT_PSTORE 16

/* Target value for BOOTCFG.  This currently toggles the polarity bit without
 * enabling the boot loader, simply to prove we can program it. */
#define BOOTCFG_VALUE 0xfffffdfe

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
