/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Button API for Chrome EC */

#ifndef __CROS_EC_BUTTON_H
#define __CROS_EC_BUTTON_H

#include "common.h"
#include "gpio.h"

#define BUTTON_FLAG_ACTIVE_HIGH (1 << 0)

enum keyboard_button_type {
	KEYBOARD_BUTTON_POWER = 0,
	KEYBOARD_BUTTON_VOLUME_DOWN,
	KEYBOARD_BUTTON_VOLUME_UP,

	KEYBOARD_BUTTON_COUNT
};

struct button_config {
	const char *name;
	enum keyboard_button_type type;
	enum gpio_signal gpio;
	uint32_t debounce_us;
	int flags;
};

/*
 * Defined in board.c. Should be CONFIG_BUTTON_COUNT elements long.
 */
extern const struct button_config buttons[];

/*
 * Interrupt handler for button.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void button_interrupt(enum gpio_signal signal);

#endif  /* __CROS_EC_BUTTON_H */
