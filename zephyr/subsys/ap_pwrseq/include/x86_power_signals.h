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

#elif defined(CONFIG_AP_X86_INTEL_MTL)

#define IN_PGOOD_ALL_CORE POWER_SIGNAL_MASK(PWR_RSMRST_PWRGD)
#define PWRSEQ_G3S5_UP_SIGNAL IN_PGOOD_ALL_CORE
#define PWRSEQ_G3S5_UP_VALUE IN_PGOOD_ALL_CORE

#else
#warning("Input power signals state flags not defined");
#endif

#endif /* __X86_POWER_SIGNALS_H__ */
