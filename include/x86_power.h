/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* x86 power module for Chrome EC */

#ifndef __CROS_EC_X86_POWER_H
#define __CROS_EC_X86_POWER_H

#include "gpio.h"

#ifdef CONFIG_CHIPSET_X86
/**
 * Interrupt handler for x86 chipset GPIOs.
 */
void x86_power_interrupt(enum gpio_signal signal);
#else
#define x86_power_interrupt NULL
#endif

#endif  /* __CROS_EC_X86_POWER_H */
