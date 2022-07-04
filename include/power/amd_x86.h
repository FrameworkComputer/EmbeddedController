/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_AMD_X86_H_
#define __CROS_EC_POWER_AMD_X86_H_

/*
 * In legacy EC-OS, the power signals are defined as part of
 * the board include headers, but with Zephyr, this is common.
 */
#if defined(CONFIG_ZEPHYR) && defined(CONFIG_AP_X86_AMD)

/* Power input signals */
enum power_signal {
	X86_SLP_S3_N, /* SOC  -> SLP_S3_L */
	X86_SLP_S5_N, /* SOC  -> SLP_S5_L */

	X86_S0_PGOOD, /* PMIC -> S0_PWROK_OD */
	X86_S5_PGOOD, /* PMIC -> S5_PWROK */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT,
};

#endif

#endif /* __CROS_EC_POWER_AMD_X86_H_ */
