/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* External power detection API for Chrome EC */

#ifndef __CROS_EC_EXTPOWER_H
#define __CROS_EC_EXTPOWER_H

#include "common.h"

enum gpio_signal; /* from gpio_signal.h */

/**
 * Run board specific code to update extpower status.  The default
 * implementation does nothing, but a board may override it.
 */
__override_proto void board_check_extpower(void);

/**
 * Return non-zero if external power is present.
 */
test_mockable int extpower_is_present(void);

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

#endif /* __CROS_EC_EXTPOWER_H */
