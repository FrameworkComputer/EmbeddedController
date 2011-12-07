/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#ifndef __CROS_EC_GPIO_H
#define __CROS_EC_GPIO_H

#include "common.h"

/* GPIO signal definitions. */
enum gpio_signal {
	/* Firmware write protect */
	EC_GPIO_WRITE_PROTECT = 0,
	/* Recovery switch */
	EC_GPIO_RECOVERY_SWITCH,
	/* Debug LED */
	EC_GPIO_DEBUG_LED
};


/* Pre-initializes the module.  This occurs before clocks or tasks are
 * set up. */
int gpio_pre_init(void);

/* Initializes the GPIO module. */
int gpio_init(void);

/* Functions should return an error if the requested signal is not
 * supported / not present on the board. */

/* Gets the current value of a signal (0=low, 1=hi). */
int gpio_get(enum gpio_signal signal, int *value_ptr);

/* Sets the current value of a signal.  Returns error if the signal is
 * not supported or is an input signal. */
int gpio_set(enum gpio_signal signal, int value);

#endif  /* __CROS_EC_GPIO_H */
