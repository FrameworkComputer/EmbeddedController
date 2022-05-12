/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MKBP keyboard protocol
 */

#ifndef __CROS_EC_KEYBOARD_MKBP_H
#define __CROS_EC_KEYBOARD_MKBP_H

#include "common.h"
#include "keyboard_config.h"

/**
 * Add keyboard state into FIFO
 *
 * @return EC_SUCCESS if entry added, EC_ERROR_OVERFLOW if FIFO is full
 */
int mkbp_keyboard_add(const uint8_t *buffp);

#endif  /* __CROS_EC_KEYBOARD_MKBP_H */
