/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* x86 power module for Chrome EC */

#ifndef __CROS_EC_X86_POWER_H
#define __CROS_EC_X86_POWER_H

#include "common.h"
#include "gpio.h"

/* Interrupt handler for input GPIOs */
void x86_power_interrupt(enum gpio_signal signal);

/* Informs the power module that the CPU has overheated (too_hot=1) or is
 * no longer too hot (too_hot=0). */
void x86_power_cpu_overheated(int too_hot);

/* Immediately shuts down power to the main processor and chipset.  This is
 * intended for use when the system is too hot or battery power is critical. */
void x86_power_force_shutdown(void);

/* Reset the x86.  If cold_reset!=0, forces a cold reset by sending
 * power-not-ok; otherwise, just pulses the reset line to the x86. */
void x86_power_reset(int cold_reset);

#endif  /* __CROS_EC_X86_POWER_H */
