/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_WOL_H
#define __CROS_EC_WOL_H

#include "common.h"

/**
 * Interrupt handler for wake on lan signal.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void wake_on_lan_interrupt(enum gpio_signal signal);

/**
 * @return wake on lan status, true = enabled.
 */
bool wake_on_lan_is_enabled(void);

#endif /* __CROS_EC_WOL_H */
