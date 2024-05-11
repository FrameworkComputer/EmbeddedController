/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel X86 chipset power control module for Chrome EC */

#ifndef __CROS_EC_INTEL_X86_H
#define __CROS_EC_INTEL_X86_H

#include "espi.h"
#include "power.h"

/* Chipset specific header files */
#if defined(CONFIG_CHIPSET_ALDERLAKE_SLG4BD44540)
#include "alderlake_slg4bd44540.h"
/* Geminilake and apollolake use same power sequencing. */
#elif defined(CONFIG_CHIPSET_APL_GLK)
#include "apollolake.h"
#elif defined(CONFIG_CHIPSET_CANNONLAKE)
#include "cannonlake.h"
#elif defined(CONFIG_CHIPSET_COMETLAKE)
#include "cometlake.h"
#elif defined(CONFIG_CHIPSET_COMETLAKE_DISCRETE)
#include "cometlake-discrete.h"
#elif defined(CONFIG_CHIPSET_ICELAKE)
#include "icelake.h"
#elif defined(CONFIG_CHIPSET_SKYLAKE)
#include "skylake.h"

#ifdef __cplusplus
extern "C" {
#endif
#endif

/* GPIO for power signal */
#ifdef CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S3
#define SLP_S3_SIGNAL_L VW_SLP_S3_L
#else
#define SLP_S3_SIGNAL_L GPIO_PCH_SLP_S3_L
#endif
#ifdef CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S4
#define SLP_S4_SIGNAL_L VW_SLP_S4_L
#else
#define SLP_S4_SIGNAL_L GPIO_PCH_SLP_S4_L
#endif
/*
 * The SLP_S5 signal has not traditionally been connected to the EC. If virtual
 * wire support is enabled, then SLP_S5 will be available that way. Otherwise,
 * use SLP_S4's GPIO as a proxy for SLP_S5. This matches old behavior and
 * effectively prevents S4 residency.
 */
#ifdef CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S5
#define SLP_S5_SIGNAL_L VW_SLP_S5_L
#else
#define SLP_S5_SIGNAL_L SLP_S4_SIGNAL_L
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

/**
 * Wait for power-up to be allowed based on available power.
 *
 * This delays G3->S5 until there is enough power to boot the AP, waiting
 * first until the charger (if any) is ready, then for there to be sufficient
 * power.
 *
 * In case of error, the caller should not allow power-up past G3.
 *
 * @return EC_SUCCESS if OK.
 */
enum ec_error_list intel_x86_wait_power_up_ok(void);

/**
 * Introduces SYS_RESET_L Debounce time delay
 *
 * The default implementation is to wait for a duration of 32 ms.
 * If board needs a different debounce time delay, they may override
 * this function
 */
__override_proto void intel_x86_sys_reset_delay(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_INTEL_X86_H */
