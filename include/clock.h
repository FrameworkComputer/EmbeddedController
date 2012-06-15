/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*  Clocks and power management settings */

#ifndef __CROS_EC_CLOCK_H
#define __CROS_EC_CLOCK_H

#include "common.h"

/* Set the CPU clocks and PLLs. */
int clock_init(void);

/* Return the current clock frequency in Hz. */
int clock_get_freq(void);

/* Enable or disable the PLL. */
int clock_enable_pll(int enable);

/* Wait <cycles> system clock cycles.  Simple busy waiting for before
 * clocks/timers are initialized. */
void clock_wait_cycles(uint32_t cycles);

/* Low power modes for idle API */

enum {
	SLEEP_MASK_AP_RUN = (1 << 0), /* the main CPU is running */
	SLEEP_MASK_UART   = (1 << 1), /* UART communication on-going */
	SLEEP_MASK_I2C    = (1 << 2), /* I2C master communication on-going */

	SLEEP_MASK_FORCE  = (1 << 31), /* Force disabling low power modes */
};

void enable_sleep(uint32_t mask);
void disable_sleep(uint32_t mask);

#endif  /* __CROS_EC_CLOCK_H */
