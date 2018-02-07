/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BOARD_CR50_WP_H
#define __EC_BOARD_CR50_WP_H

#include "common.h"

/**
 * Initialize write protect state.
 *
 * Must be called after case-closed debugging is initialized.
 */
void init_wp_state(void);

/**
 * Read the FWMP value from TPM NVMEM and set the console restriction
 * appropriately.
 */
void read_fwmp(void);

/**
 * Set WP as dicated by CCD configuration.
 */
void set_wp_follow_ccd_config(void);

#endif  /* ! __EC_BOARD_CR50_WP_H */
