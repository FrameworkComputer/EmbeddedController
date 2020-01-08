/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* A place to organize workarounds for legacy RO */

#include <assert.h>
#include <stdbool.h>

#include "bkpdata.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h" /* Reset cause */
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "task.h"
#include "watchdog.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/*
 * We only patch RW to ensure that future ROs have correct behavior.
 */
#if defined(APPLY_RESET_LOOP_FIX) && defined(SECTION_IS_RW)

/*
 * Add in ap-off flag to be able to detect on next boot.
 * No other code in this build uses this ap-off reset flag.
 */
#define FORGE_PORFLAG_FLAGS (EC_RESET_FLAG_POWER_ON|EC_RESET_FLAG_AP_OFF)

static void wp_change_deferred(void)
{
	/*
	 * The normal state of the reset backup register is 0, but
	 * we know that our override version of bkpdata_write_reset_flags
	 * will adjust it based on GPIO_WP's status.
	 */
	bkpdata_write_reset_flags(0);
}
DECLARE_DEFERRED(wp_change_deferred);

/*
 * We respond to changes in the hardware write protect line in order to
 * ensure this workaround is installed when it is needed and uninstalled
 * when it is not needed. This ensures that we are protected during
 * unexpected resets, such as pin resets or double faults.
 *
 * Furthermore, installing and uninstalling when needed minimizes the
 * difference between our normal operating conditions and normal operating
 * conditions with this ro_workaround source being included. That is to say,
 * the system behavior is only altered in the less likely state, when hardware
 * write protect deasserted.
 */
void wp_event(enum gpio_signal signal)
{
	/*
	 * We must use a deferred function to call bkpdata_write_reset_flags,
	 * since the underlying bkpdata_write uses a mutex.
	 */
	hook_call_deferred(&wp_change_deferred_data, 0);
}

/*
 * We intercept all changes to the reset backup register to ensure that
 * our reset loop patch stays in place.
 *
 * This function will be called once in check_reset_cause during
 * startup, which ensures proper behavior even when unexpected
 * resets occurs (pin reset or exception).
 *
 * This function is also called from system_reset to set the final save
 * reset flags, before an actual planned reset.
 */
__override
void bkpdata_write_reset_flags(uint32_t save_flags)
{
	/* Preserve flags in case a reset pulse occurs */
	if (!gpio_get_level(GPIO_WP))
		save_flags |= FORGE_PORFLAG_FLAGS;

#ifdef CONFIG_STM32_RESET_FLAGS_EXTENDED
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, save_flags & 0xffff);
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS_2, save_flags >> 16);
#else
	/* Reset flags are 32-bits, but BBRAM entry is only 16 bits. */
	ASSERT(!(save_flags >> 16));
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, save_flags);
#endif
}

/*
 * We do not need to explicitly invoke bkpdata_write_reset_flags
 * on boot, since check_reset_cause will already invoke it once on boot.
 */
static void board_init_workarounds(void)
{
	gpio_disable_interrupt(GPIO_WP);
	gpio_clear_pending_interrupt(GPIO_WP);

	/*
	 * Detect our forged power-on flag and correct the current
	 * system reset flags.
	 * This does not ensure that all init functions will see
	 * the corrected system reset flags, so care should be taken.
	 */
	if ((system_get_reset_flags() & FORGE_PORFLAG_FLAGS) ==
	    FORGE_PORFLAG_FLAGS) {
		CPRINTS("WARNING: Reset flags power-on + ap-off were forged.");
		system_clear_reset_flags(FORGE_PORFLAG_FLAGS);
	}

	gpio_enable_interrupt(GPIO_WP);
}
/* Run one priority level higher than the main board_init in board.c */
DECLARE_HOOK(HOOK_INIT, board_init_workarounds, HOOK_PRIO_DEFAULT - 1);

#endif /* APPLY_RESET_LOOP_FIX && SECTION_IS_RW */
