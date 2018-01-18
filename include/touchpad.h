/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TOUCHPAD_H
#define __CROS_EC_TOUCHPAD_H

void touchpad_interrupt(enum gpio_signal signal);

/* Reset the touchpad, mainly used to recover it from malfunction. */
void board_touchpad_reset(void);

#endif
