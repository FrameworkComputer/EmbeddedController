/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Extra hooks for board and chip initialization/configuration
 */

#ifndef __CROS_EC_BOARD_CONFIG_H
#define __CROS_EC_BOARD_CONFIG_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_BOARD_PRE_INIT
/**
 * Configure board before any inits are called.
 *
 * Note that this is in general a hacky place to do configuration.  Most config
 * is actually chip-specific or module-specific and not board-specific, so
 * putting it here hides dependencies between module inits and board init.
 * Think very hard before putting code here.
 */
void board_config_pre_init(void);
#endif

#ifdef CONFIG_BOARD_POST_GPIO_INIT
/**
 * Configure board after GPIOs are initialized.
 *
 * Note that this is in general a hacky place to do configuration.  Most config
 * is actually chip-specific or module-specific and not board-specific, so
 * putting it here hides dependencies between module inits and board init.
 * Think very hard before putting code here.
 */
void board_config_post_gpio_init(void);
#endif

/**
 * Configure board before RSMRST# state change
 *
 * This board function allows workarounds to be applied to a board after all
 * power rails are up but before the AP is out of reset.
 *
 * Most workarounds for power sequencing can go in board init hooks, but for
 * devices where the power sequencing is driven by external PMIC the EC may
 * not get interrupts in time to handle workarounds.  For x86 platforms and
 * boards which support RSMRST# passthrough this hook will allow the board
 * to apply workarounds despite the PMIC sequencing.
 *
 * The default implementation does nothing.
 */
__override_proto void board_before_rsmrst(int rsmrst);

/**
 * Configure board after RSMRST# state change
 *
 * Similar to board_before_rsmrst, except this is called after passing RSMRST#
 * to the AP.
 *
 * The default implementation does nothing.
 */
__override_proto void board_after_rsmrst(int rsmrst);

/**
 * Configure chip early in main(), just after board_config_pre_init().
 *
 * Most chip configuration is not particularly timing critical and can be done
 * in other chip driver initialization such as system_pre_init() or HOOK_INIT
 * handlers.  Chip pre-init should be reserved for small amounts of critical
 * functionality that can't wait that long.  Think very hard before putting
 * code here.
 */
void chip_pre_init(void);

/*
 * Allow board to override the feature bitmap provided through host command
 * and ACPI.
 */
__override_proto uint32_t board_override_feature_flags0(uint32_t flags0);
__override_proto uint32_t board_override_feature_flags1(uint32_t flags1);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_BOARD_CONFIG_H */
