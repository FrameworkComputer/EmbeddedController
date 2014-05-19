/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_protocol.h"
#include "timer.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)

#define CAPSENSE_I2C_ADDR 0x08
#define CAPSENSE_MASK_BITS 8
#define CAPSENSE_POLL_INTERVAL (20 * MSEC)

static int capsense_read_bitmask(void)
{
	int rv;
	uint8_t val = 0;

	i2c_lock(I2C_PORT_CAPSENSE, 1);
	rv = i2c_xfer(I2C_PORT_CAPSENSE, CAPSENSE_I2C_ADDR,
		      0, 0, &val, 1, I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_CAPSENSE, 0);

	if (rv)
		CPRINTS("%s failed: error %d", __func__, rv);

	return val;
}

static void capsense_init(void)
{
	gpio_enable_interrupt(GPIO_CAPSENSE_INT_L);
}
DECLARE_HOOK(HOOK_INIT, capsense_init, HOOK_PRIO_DEFAULT);

/*
 * Keep checking polling the capsense until all the buttons are released.
 * We're not worrying about debouncing, since the capsense module should do
 * that for us.
 */
static void capsense_change_deferred(void)
{
	static uint8_t cur_val;
	uint8_t new_val;
	int i, n, c;

	new_val = capsense_read_bitmask();
	if (new_val != cur_val) {
		CPRINTF("[%T capsense 0x%02x: ", new_val);
		for (i = 0; i < CAPSENSE_MASK_BITS; i++) {
			/* See what changed */
			n = (new_val >> i) & 0x01;
			c = (cur_val >> i) & 0x01;
			CPRINTF("%s", n ? " X " : " _ ");
			if (n == c)
				continue;
#ifdef HAS_TASK_KEYPROTO
			/* Treat it as a keyboard event. */
			keyboard_update_button(i + KEYBOARD_BUTTON_CAPSENSE_1,
					       n);
#endif
		}
		CPRINTF("]\n");
		cur_val = new_val;
	}

	if (cur_val)
		hook_call_deferred(capsense_change_deferred,
				   CAPSENSE_POLL_INTERVAL);
}
DECLARE_DEFERRED(capsense_change_deferred);

/*
 * Somebody's poking at us.
 */
void capsense_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(capsense_change_deferred, 0);
}
