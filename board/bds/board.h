/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Stellaris EKB-LM4F-EAC board configuration */

#ifndef __BOARD_H
#define __BOARD_H

enum adc_channel
{
	ADC_CH_EC_TEMP = 0,  /* EC internal die temperature in degrees K. */
	ADC_CH_BDS_POT,      /* BDS pot input. */
	ADC_CH_COUNT
};

/* I2C ports */
#define I2C_PORT_BATTERY 5  // port 0 / PB2:3 on Link, open on badger
#define I2C_PORT_CHARGER 5  // port 1 / PA6:7 on Link, user LED on badger
#define I2C_PORT_THERMAL 5  // port 5 / PB6:7 on link, but PG6:7 on badger
#define I2C_PORT_LIGHTBAR 5  // port 5 / PA6:7 on link, but PG6:7 on badger
/* I2C port speeds in kbps.  All the same because they all share a port */
#define I2C_SPEED_BATTERY 400
#define I2C_SPEED_CHARGER 400
#define I2C_SPEED_LIGHTBAR 400
#define I2C_SPEED_THERMAL 400

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
