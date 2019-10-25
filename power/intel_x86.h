/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel X86 chipset power control module for Chrome EC */


#ifndef __CROS_EC_INTEL_X86_H
#define __CROS_EC_INTEL_X86_H

#include "espi.h"
#include "power.h"

/* Chipset specific header files */
/* Geminilake and apollolake use same power sequencing. */
#ifdef CONFIG_CHIPSET_APL_GLK
#include "apollolake.h"
#elif defined(CONFIG_CHIPSET_CANNONLAKE)
#include "cannonlake.h"
#elif defined(CONFIG_CHIPSET_COMETLAKE)
#include "cometlake.h"
#elif defined(CONFIG_CHIPSET_ICL_TGL)
#include "icelake.h"
#elif defined(CONFIG_CHIPSET_SKYLAKE)
#include "skylake.h"
#endif

/* GPIO for power signal */
#ifdef CONFIG_HOSTCMD_ESPI_VW_SLP_S3
#define SLP_S3_SIGNAL_L VW_SLP_S3_L
#else
#define SLP_S3_SIGNAL_L GPIO_PCH_SLP_S3_L
#endif
#ifdef CONFIG_HOSTCMD_ESPI_VW_SLP_S4
#define SLP_S4_SIGNAL_L VW_SLP_S4_L
#else
#define SLP_S4_SIGNAL_L GPIO_PCH_SLP_S4_L
#endif

/**
 * Handle RSMRST signal.
 *
 * @param state Current chipset state.
 */
void common_intel_x86_handle_rsmrst(enum power_state state);

/**
 * Force chipset to G3 state.
 *
 * @return power_state New chipset state.
 */
enum power_state chipset_force_g3(void);

/**
 * Handle power states.
 *
 * @param state        Current chipset state.
 * @return power_state New chipset state.
 */
enum power_state common_intel_x86_power_handle_state(enum power_state state);

#endif /* __CROS_EC_INTEL_X86_H */
