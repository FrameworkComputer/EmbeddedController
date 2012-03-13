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

/* Informs the power module that the CPU has overheated (too_hot=1) or is
 * no longer too hot (too_hot=0). */
void x86_power_cpu_overheated(int too_hot);

/* Immediately shuts down power to the main processor and chipset.  This is
 * intended for use when the system is too hot or battery power is critical. */
void x86_power_force_shutdown(void);

/* Pulse the reset line to the x86. */
void x86_power_reset(void);

#endif  /* __CROS_EC_X86_POWER_H */
