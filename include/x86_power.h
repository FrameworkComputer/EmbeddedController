/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* x86 power module for Chrome EC */

#ifndef __CROS_EC_X86_POWER_H
#define __CROS_EC_X86_POWER_H

#include "common.h"
#include "gpio.h"

/* Initializes the module. */
int x86_power_init(void);

/* Interrupt handler for input GPIOs */
void x86_power_interrupt(enum gpio_signal signal);

#endif  /* __CROS_EC_X86_POWER_H */
