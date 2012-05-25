/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Stellaris EKB-LM4F-EAC board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_CONSOLE_CMDHELP
#define CONFIG_SYSTEM_UNLOCKED  /* Allow dangerous commands */

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

/* GPIO signal list */
enum gpio_signal {
	GPIO_RECOVERYn = 0,       /* Recovery signal from DOWN button */
	GPIO_DEBUG_LED,           /* Debug LED */
	/* Signals which aren't implemented on BDS but we'll emulate anyway, to
	 * make it more convenient to debug other code. */
	GPIO_WRITE_PROTECT,       /* Write protect input */

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

/* Target value for BOOTCFG.  This currently toggles the polarity bit without
 * enabling the boot loader, simply to prove we can program it. */
#define BOOTCFG_VALUE 0xfffffdfe

void configure_board(void);

#endif /* __BOARD_H */
