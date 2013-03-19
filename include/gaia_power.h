/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Gaia power module for Chrome EC */

#ifndef __CROS_EC_GAIA_POWER_H
#define __CROS_EC_GAIA_POWER_H

#include "gpio.h"

#ifdef CONFIG_CHIPSET_GAIA

/**
 * Interrupt handlers for Gaia chipset GPIOs.
 */
void gaia_power_event(enum gpio_signal signal);
void gaia_lid_event(enum gpio_signal signal);
void gaia_suspend_event(enum gpio_signal signal);

#else

#define gaia_power_event NULL
#define gaia_suspend_event NULL
#define gaia_lid_event NULL

#endif

#endif  /* __CROS_EC_GAIA_POWER_H */
