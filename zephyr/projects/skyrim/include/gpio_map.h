/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

#include <zephyr/devicetree.h>
#include <gpio_signal.h>

/* Power input signals */
enum power_signal {
	X86_SLP_S3_N, /* SOC  -> SLP_S3_L */
	X86_SLP_S5_N, /* SOC  -> SLP_S5_L */

	X86_S0_PGOOD, /* PMIC -> S0_PWROK_OD */
	X86_S5_PGOOD, /* PMIC -> S5_PWROK */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT,
};

#define GPIO_ENTERING_RW	GPIO_UNIMPLEMENTED
#define GPIO_PCH_SYS_PWROK	GPIO_UNIMPLEMENTED

#endif /* __ZEPHYR_GPIO_MAP_H */
