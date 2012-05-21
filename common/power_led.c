/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power LED control for Chrome EC */

#include "console.h"
#include "onewire.h"
#include "power_led.h"
#include "timer.h"
#include "util.h"

#define POWERLED_RETRIES 10

static const uint8_t led_masks[POWERLED_COLOR_COUNT] = {0xff, 0xfe, 0xfc, 0xfd};
static const char * const color_names[POWERLED_COLOR_COUNT] = {
	"off", "red", "yellow", "green"};


/* Set the power LED GPIO controller outputs to the specified mask. */
static int powerled_set_mask(int mask)
{
	int rv;

	/* Reset the 1-wire bus */
	rv = onewire_reset();
	if (rv)
		return rv;

	/* Skip ROM, since only one device */
	onewire_write(0xcc);

	/* Write and turn on the LEDs */
	onewire_write(0x5a);
	onewire_write(mask);
	onewire_write(~mask);  /* Repeat inverted */

	rv = onewire_read();   /* Confirmation byte */
	if (rv != 0xaa)
		return EC_ERROR_UNKNOWN;

	/* The next byte is a read-back of the chip status.  Since we're only
	 * using lines as outputs, we can ignore it. */
	return EC_SUCCESS;
}


int powerled_set(enum powerled_color color)
{
	int rv = EC_SUCCESS;
	int i;

	/* 1-wire communication can fail for timing reasons in the current
	 * system.  We have a limited timing window to send/receive bits, but
	 * we can't disable interrupts for the rest of the system to guarantee
	 * we hit that window.  Instead, simply retry the low-level command a
	 * few times. */
	for (i = 0; i < POWERLED_RETRIES; i++) {
		rv = powerled_set_mask(led_masks[color]);
		if (rv == EC_SUCCESS)
			break;

		/* Sleep for a bit between tries.  This gives the 1-wire GPIO
		 * chip time to recover from the failed attempt, and allows
		 * lower-priority tasks a chance to run. */
		usleep(100);
	}

	return rv;
}


/*****************************************************************************/
/* Console commands */

static int command_powerled(int argc, char **argv)
{
	int i;

	/* Pick a color, any color... */
	for (i = 0; i < POWERLED_COLOR_COUNT; i++) {
		if (!strcasecmp(argv[1], color_names[i]))
			return powerled_set(i);
	}
	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(powerled, command_powerled);
