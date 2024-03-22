/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Icelake chipset power control module for Chrome EC */

#ifndef __CROS_EC_ICELAKE_H
#define __CROS_EC_ICELAKE_H

#include "stdbool.h"

/* Input state flags. */
#define IN_PCH_SLP_S3_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)
#define IN_PCH_SLP_SUS_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_SUS_DEASSERTED)

#define IN_ALL_PM_SLP_DEASSERTED                               \
	(IN_PCH_SLP_S3_DEASSERTED | IN_PCH_SLP_S4_DEASSERTED | \
	 IN_PCH_SLP_SUS_DEASSERTED)

#define IN_PGOOD_ALL_CORE POWER_SIGNAL_MASK(X86_DSW_DPWROK)

#define IN_ALL_S0 (IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

#define CHIPSET_G3S5_POWERUP_SIGNAL IN_PCH_SLP_SUS_DEASSERTED

/*
 * By default, intel_x86 uses IN_PGOOD_ALL_CORE for power fail detection, which
 * on icelake is defined to DSW_DPWROK. On dedede, there's no hardware signal
 * for DSW_DPWROK, instead it's generated from the level of PP3300_A. When AC is
 * disconnected, PP3300_A does not drop immediately. It won't drop until either
 * the power supply voltage drops below 3.3V (which takes some time), or the EC
 * turns the rail off when entering G3. So the power failure is not detected and
 * the EC performs the full shutdown sequence.
 *
 * So for icelake we use DSW_DPWROK|RSMRST_PWRGD_L for power fail detection. On
 * a clean shutdown, RSMRST_PWRGD_L doesn't drop until the EC disables it when
 * entering G3. But when AC is disconnected it drops immediately since the rails
 * it corresponds to are enabled by SLP_SUS_L, and the AP asserts SLP_SUS_L
 * immediately when there's a power failure.
 */
#define CHIPSET_POWERFAIL_DETECT \
	(IN_PGOOD_ALL_CORE | POWER_SIGNAL_MASK(X86_RSMRST_L_PGOOD))

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

/* Power signals list */
enum power_signal {
	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_SLP_S5_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	X86_RSMRST_L_PGOOD,
	X86_DSW_DPWROK,
	X86_ALL_SYS_PGOOD,
#ifdef CONFIG_CHIPSET_JASPERLAKE
	PP1050_ST_PGOOD,
	DRAM_PGOOD,
	VCCIO_EXT_PGOOD,
#endif

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

struct intel_x86_pwrok_signal {
	enum gpio_signal gpio;
	bool active_low;
	int delay_ms;
};

/*
 * Ice Lake/Tiger Lake/Jasper Lake PWROK Generation
 *
 * The following signals are controlled based on the state of the ALL_SYS_PWRGD
 * signal
 *
 *	VCCIN enable (input to the VCCIN voltage rail controller)
 *	VCCST_PWRGD (input to the SoC)
 *	PCH_PWROK (input to the SoC)
 *	SYS_PWROK (input to the SoC)
 *
 * For any the above signals that are controlled by the EC, create an entry
 * in the pwrok_signal_assert_list[] and pwrok_signal_deassert_list[] arrays.
 * The typical order for asserting the signals is shown above, the deassert
 * order is the reverse.
 *
 * ALL_SYS_PWRGD indicates when all the following are asserted.
 *	RSMRST_PWRGD & DPWROK
 *	S4 voltage rails good (DDR)
 *	VCCST voltage rail good
 *	S0 voltage rails good
 *
 * ALL_SYS_PWRGD can be implemented as a single GPIO if the platform power logic
 * combines the above power good signals. Otherwise your board can override
 * power_signal_get_level() to check multiple power good signals.
 */
extern const struct intel_x86_pwrok_signal pwrok_signal_assert_list[];
extern const int pwrok_signal_assert_count;
extern const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[];
extern const int pwrok_signal_deassert_count;

#endif /* __CROS_EC_ICELAKE_H */
