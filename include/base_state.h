/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BASE_STATE_H
#define __CROS_EC_BASE_STATE_H

#include "ec_commands.h"

/**
 * Return 1 if base attached, 0 otherwise.
 */
int base_get_state(void);

/**
 * Sets the current state of the base, with 0 meaning detached,
 * and non-zero meaning attached.
 */
void base_set_state(int state);

/**
 * Call board specific base_force_state function.
 * Force the current state of the base, with 0 meaning detached,
 * 1 meaning attached and 2 meaning reset to the original state.
 */
void base_force_state(enum ec_set_base_state_cmd state);

#endif /* __CROS_EC_BASE_STATE_H */
