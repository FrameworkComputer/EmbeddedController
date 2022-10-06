/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Define power signals from device tree */

#ifndef __X86_POWER_SIGNALS_H__
#define __X86_POWER_SIGNALS_H__

#define IN_PCH_SLP_S0 POWER_SIGNAL_MASK(PWR_SLP_S0)
#define IN_PCH_SLP_S3 POWER_SIGNAL_MASK(PWR_SLP_S3)
#define IN_PCH_SLP_S4 POWER_SIGNAL_MASK(PWR_SLP_S4)
#define IN_PCH_SLP_S5 POWER_SIGNAL_MASK(PWR_SLP_S5)

/*
 * Define the chipset specific power signal masks and values
 * matching the AP state.
 */
#if defined(CONFIG_AP_X86_INTEL_ADL)

/* Input state flags */
#define IN_PCH_SLP_SUS POWER_SIGNAL_MASK(PWR_SLP_SUS)
#define IN_PGOOD_ALL_CORE POWER_SIGNAL_MASK(PWR_DSW_PWROK)
#define PWRSEQ_G3S5_UP_SIGNAL IN_PCH_SLP_SUS
#define PWRSEQ_G3S5_UP_VALUE 0

#define MASK_ALL_POWER_GOOD                     \
	(POWER_SIGNAL_MASK(PWR_RSMRST) |        \
	 POWER_SIGNAL_MASK(PWR_ALL_SYS_PWRGD) | \
	 POWER_SIGNAL_MASK(PWR_DSW_PWROK) | POWER_SIGNAL_MASK(PWR_PG_PP1P05))

#define MASK_VW_POWER                                                       \
	(POWER_SIGNAL_MASK(PWR_RSMRST) | POWER_SIGNAL_MASK(PWR_DSW_PWROK) | \
	 POWER_SIGNAL_MASK(PWR_SLP_SUS))
#define VALUE_VW_POWER \
	(POWER_SIGNAL_MASK(PWR_RSMRST) | POWER_SIGNAL_MASK(PWR_DSW_PWROK))

#define MASK_S0                                                           \
	(MASK_ALL_POWER_GOOD | POWER_SIGNAL_MASK(PWR_SLP_S0) |            \
	 POWER_SIGNAL_MASK(PWR_SLP_S3) | POWER_SIGNAL_MASK(PWR_SLP_SUS) | \
	 POWER_SIGNAL_MASK(PWR_SLP_S4) | POWER_SIGNAL_MASK(PWR_SLP_S5))
#define VALUE_S0 MASK_ALL_POWER_GOOD

#define MASK_S3 MASK_S0
#define VALUE_S3 (MASK_ALL_POWER_GOOD | POWER_SIGNAL_MASK(PWR_SLP_S3))

#define MASK_S5                                                             \
	(POWER_SIGNAL_MASK(PWR_RSMRST) | POWER_SIGNAL_MASK(PWR_DSW_PWROK) | \
	 POWER_SIGNAL_MASK(PWR_SLP_S3) | POWER_SIGNAL_MASK(PWR_SLP_S4) |    \
	 POWER_SIGNAL_MASK(PWR_SLP_S5))
#define VALUE_S5 MASK_S5

#elif defined(CONFIG_AP_X86_INTEL_MTL)

#define IN_PGOOD_ALL_CORE POWER_SIGNAL_MASK(PWR_RSMRST)
#define PWRSEQ_G3S5_UP_SIGNAL IN_PGOOD_ALL_CORE
#define PWRSEQ_G3S5_UP_VALUE IN_PGOOD_ALL_CORE

#define MASK_ALL_POWER_GOOD \
	(POWER_SIGNAL_MASK(PWR_RSMRST) | POWER_SIGNAL_MASK(PWR_ALL_SYS_PWRGD))

#define MASK_VW_POWER POWER_SIGNAL_MASK(PWR_RSMRST)
#define VALUE_VW_POWER POWER_SIGNAL_MASK(PWR_RSMRST)

#define MASK_S0                                                          \
	(MASK_ALL_POWER_GOOD | POWER_SIGNAL_MASK(PWR_SLP_S0) |           \
	 POWER_SIGNAL_MASK(PWR_SLP_S3) | POWER_SIGNAL_MASK(PWR_SLP_S4) | \
	 POWER_SIGNAL_MASK(PWR_SLP_S5))
#define VALUE_S0 MASK_ALL_POWER_GOOD

#define MASK_S3 MASK_S0
#define VALUE_S3 (MASK_ALL_POWER_GOOD | POWER_SIGNAL_MASK(PWR_SLP_S3))

#define MASK_S5                                                          \
	(POWER_SIGNAL_MASK(PWR_RSMRST) | POWER_SIGNAL_MASK(PWR_SLP_S3) | \
	 POWER_SIGNAL_MASK(PWR_SLP_S4) | POWER_SIGNAL_MASK(PWR_SLP_S5))
#define VALUE_S5 MASK_S5

#else
#warning("Input power signals state flags not defined");
#endif

#endif /* __X86_POWER_SIGNALS_H__ */
