/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * O2 Micro OZ554 LED driver.
 */

#ifndef __CROS_EC_OZ554_H
#define __CROS_EC_OZ554_H

#include "gpio.h"
#include "common.h"

/*
 * Overridable board initialization.  Should be overridden by a board
 * specific function if the default is not appropriate
 */
__override_proto void oz554_board_init(void);

/**
 * Update oz554 configuration array (oz554_conf).
 *
 * @param offset: Offset of the register to be set.
 * @param data:   Value to be set.
 * @return EC_SUCCESS or EC_ERROR_* for errors.
 */
int oz554_set_config(int offset, int data);

#ifndef OZ554_POWER_BACKLIGHT_DELAY
#define OZ554_POWER_BACKLIGHT_DELAY SECOND
#endif

void backlight_enable_interrupt(enum gpio_signal signal);

#endif
