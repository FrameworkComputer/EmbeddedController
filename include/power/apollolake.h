/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Apollolake chipset power control module for Chrome EC */

#ifndef __CROS_EC_APOLLOLAKE_H
#define __CROS_EC_APOLLOLAKE_H

/*
 * Input state flags.
 * TODO: Normalize the power signal masks from board defines to SoC headers.
 */
#define IN_RSMRST_N	POWER_SIGNAL_MASK(X86_RSMRST_N)
#define IN_ALL_SYS_PG	POWER_SIGNAL_MASK(X86_ALL_SYS_PG)
#define IN_SLP_S3_N	POWER_SIGNAL_MASK(X86_SLP_S3_N)
#define IN_SLP_S4_N	POWER_SIGNAL_MASK(X86_SLP_S4_N)
#define IN_PCH_SLP_S4_DEASSERTED IN_SLP_S4_N
#define IN_SUSPWRDNACK	POWER_SIGNAL_MASK(X86_SUSPWRDNACK)
#define IN_SUS_STAT_N	POWER_SIGNAL_MASK(X86_SUS_STAT_N)

#define IN_ALL_PM_SLP_DEASSERTED (IN_SLP_S3_N | \
				  IN_SLP_S4_N)

#define IN_PGOOD_ALL_CORE (IN_RSMRST_N)

#define IN_ALL_S0 (IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

#define CHIPSET_G3S5_POWERUP_SIGNAL IN_PGOOD_ALL_CORE

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

enum power_signal {
#ifdef CONFIG_POWER_S0IX
	X86_SLP_S0_N,		/* PCH  -> SLP_S0_L */
#endif
	X86_SLP_S3_N,		/* PCH  -> SLP_S3_L */
	X86_SLP_S4_N,		/* PCH  -> SLP_S4_L */
	X86_SUSPWRDNACK,	/* PCH  -> SUSPWRDNACK */

	X86_ALL_SYS_PG,		/* PMIC -> PMIC_EC_PWROK_OD */
	X86_RSMRST_N,		/* PMIC -> PMIC_EC_RSMRST_ODL */
	X86_PGOOD_PP3300,	/* PMIC -> PP3300_PG_OD */
	X86_PGOOD_PP5000,	/* PMIC -> PP5000_PG_OD */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

#endif /* __CROS_EC_APOLLOLAKE_H */
