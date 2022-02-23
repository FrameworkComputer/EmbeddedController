/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MPS MP3385 LED driver.
 */

#ifndef __CROS_EC_MP3385_H
#define __CROS_EC_MP3385_H

#include "gpio.h"
#include "common.h"

/*
 * Overridable board initialization.  Should be overridden by a board
 * specific function if the default is not appropriate
 */
void mp3385_board_init(void);

/**
 * Update mp3385 configuration array (mp3385_conf).
 *
 * @param offset: Offset of the register to be set.
 * @param data:   Value to be set.
 * @return EC_SUCCESS or EC_ERROR_* for errors.
 */
int mp3385_set_config(int offset, int data);

#ifndef MP3385_POWER_BACKLIGHT_DELAY
#define MP3385_POWER_BACKLIGHT_DELAY (15*MSEC)
#endif

void mp3385_interrupt(enum gpio_signal signal);

#endif
