/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#ifndef __CROS_EC_CLOCK_H
#define __CROS_EC_CLOCK_H

#include "common.h"

/**
 * Set the CPU clocks and PLLs.
 */
void clock_init(void);

/**
 * Return the current clock frequency in Hz.
 */
int clock_get_freq(void);

/**
 * Enable or disable clock for a module.
 *
 * Note that if the module requires a higher system clock speed than the
 * current system clock speed, the entire system clock will be increased
 * to allow the module to operate.
 *
 * When a module is disabled, the system clock will be reduced to the highest
 * clock required by the remaining enabled modules.
 *
 * @param module        The module for which we need to enable/disable its
 *                      clock.
 * @param enable	Enable clock if non-zero; disable if zero.
 */
void clock_enable_module(enum module_id module, int enable);

/**
 * Enable or disable the PLL.
 *
 * @param enable	Enable PLL if non-zero; disable if zero.
 * @param notify	Notify other modules of the PLL change.  This should
 *			be 1 unless you're briefly turning on the PLL to work
 *			around a chip errata at init time.
 */
void clock_enable_pll(int enable, int notify);

/**
 * Wait for a number of clock cycles.
 *
 * Simple busy waiting for use before clocks/timers are initialized.
 *
 * @param cycles	Number of cycles to wait.
 */
void clock_wait_cycles(uint32_t cycles);

/* Low power modes for idle API */

enum {
	SLEEP_MASK_AP_RUN = (1 << 0), /* the main CPU is running */
	SLEEP_MASK_UART   = (1 << 1), /* UART communication on-going */
	SLEEP_MASK_I2C    = (1 << 2), /* I2C master communication on-going */
	SLEEP_MASK_CHARGING = (1 << 3), /* Charging loop on-going */
	SLEEP_MASK_USB_PWR = (1 << 4), /* USB power loop on-going */

	SLEEP_MASK_FORCE  = (1 << 31), /* Force disabling low power modes */
};

void enable_sleep(uint32_t mask);
void disable_sleep(uint32_t mask);

#endif  /* __CROS_EC_CLOCK_H */
