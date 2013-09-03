/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tegra power module for Chrome EC */

#ifndef __CROS_EC_TEGRA_POWER_H
#define __CROS_EC_TEGRA_POWER_H

#include "gpio.h"

#ifdef CONFIG_CHIPSET_TEGRA

/**
 * Interrupt handlers for Tegra chipset GPIOs.
 */
void tegra_power_event(enum gpio_signal signal);
void tegra_suspend_event(enum gpio_signal signal);

#else

#define tegra_power_event NULL
#define tegra_suspend_event NULL

#endif

#endif  /* __CROS_EC_TEGRA_POWER_H */
