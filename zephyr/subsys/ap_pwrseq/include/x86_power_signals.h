/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Define power signals from device tree */

#ifndef __X86_POWER_SIGNALS_H__
#define __X86_POWER_SIGNALS_H__

#if defined(CONFIG_AP_X86_INTEL_ADL)

/* Input state flags */
#define IN_PCH_SLP_S0  POWER_SIGNAL_MASK(PWR_SLP_S0)
#define IN_PCH_SLP_S3  POWER_SIGNAL_MASK(PWR_SLP_S3)
#define IN_PCH_SLP_S4  POWER_SIGNAL_MASK(PWR_SLP_S4)
#define IN_PCH_SLP_S5  POWER_SIGNAL_MASK(PWR_SLP_S5)
#define IN_PCH_SLP_SUS POWER_SIGNAL_MASK(PWR_SLP_SUS)
#define IN_ALL_PM_SLP  (IN_PCH_SLP_S3 | \
				  IN_PCH_SLP_S4 | \
				  IN_PCH_SLP_SUS)
#define IN_PGOOD_ALL_CORE POWER_SIGNAL_MASK(PWR_DSW_PWROK)
#define IN_ALL_S0_MASK (IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP)
#define IN_ALL_S0_VALUE IN_PGOOD_ALL_CORE
#define PWRSEQ_G3S5_UP_SIGNAL IN_PCH_SLP_SUS
#define PWRSEQ_G3S5_UP_VALUE 0

#else
#warning("Input power signals state flags not defined");
#endif

#endif /* __X86_POWER_SIGNALS_H__ */
