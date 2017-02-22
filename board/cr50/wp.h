/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BOARD_CR50_WP_H
#define __EC_BOARD_CR50_WP_H

#include "common.h"

/**
 * Set the current write protect state in RBOX and long life scratch register.
 *
 * @param asserted: 0 to disable write protect, otherwise enable write protect.
 */
void set_wp_state(int asserted);

/**
 * Read the FWMP value from TPM NVMEM and set the console restriction
 * appropriately.
 */
void read_fwmp(void);

#endif  /* ! __EC_BOARD_CR50_WP_H */
