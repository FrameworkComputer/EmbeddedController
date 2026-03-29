/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* External power detection API for Chrome EC */

#ifndef __CROS_EC_EXTPOWER_H
#define __CROS_EC_EXTPOWER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum gpio_signal; /* from gpio_signal.h */

/**
 * Run board specific code to update extpower status.  The default
 * implementation does nothing, but a board may override it.
 */
__override_proto void board_check_extpower(void);

/**
 * Return non-zero if external power is present.
 */
int extpower_is_present(void);

/**
 * Return non-zero if external power is present.
 *
 * This function is used when CONFIG_EXTPOWER_GPIO_CUSTOM is defined.
 * It should return the current external power presence status.
 */
__override_proto int board_extpower_is_present(void);

/**
 * Enable external power detection interrupt.
 */
void extpower_enable_interrupt(void);

/**
 * Disable external power detection interrupt.
 */
void extpower_disable_interrupt(void);

/**
 * Board specific implementation to enable external power detection interrupt.
 */
__override_proto void board_extpower_enable_interrupt(void);

/**
 * Board specific implementation to disable external power detection interrupt.
 */
__override_proto void board_extpower_disable_interrupt(void);

/**
 * Stub signal for extpower_interrupt() when the signal is unknown or
 * irrelevant.
 */
#define GPIO_SIGNAL_ANY ((enum gpio_signal)0)

/**
 * Interrupt handler for external power GPIOs.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void extpower_interrupt(enum gpio_signal signal);

/**
 * Routine to trigger actions based on external power state change.
 *
 * @param is_present	State of external power (1 = present, 0 = not present)
 */
void extpower_handle_update(int is_present);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_EXTPOWER_H */
