/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* I2C arbitration using a pair of GPIO lines */

#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"
#include "util.h"

/* Time between requesting bus and deciding that we have it */
#define BUS_SLEW_DELAY_US 10

/* Time between retrying to see if the AP has released the bus */
#define BUS_WAIT_RETRY_US 3000

/* Time to wait until the bus becomes free */
#define BUS_WAIT_FREE_US (100 * 1000)

/*
 * This reflects the desired value of GPIO_EC_CLAIM to ensure that the
 * GPIO is driven correctly when re-enabled before AP power on.
 */
static char i2c_claimed_by_ec;

int i2c_claim(int port)
{
	timestamp_t start;

	if (port != I2C_PORT_MASTER)
		return EC_SUCCESS;

	/* If AP is off, we have the bus */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		i2c_claimed_by_ec = 1;
		return EC_SUCCESS;
	}

	/* Start a round of trying to claim the bus */
	start = get_time();
	do {
		timestamp_t start_retry;
		int waiting = 0;

		/* Indicate that we want to claim the bus */
		gpio_set_level(GPIO_EC_CLAIM, 0);
		usleep(BUS_SLEW_DELAY_US);

		/* Wait for the AP to release it */
		start_retry = get_time();
		while (time_since32(start_retry) < BUS_WAIT_RETRY_US) {
			if (gpio_get_level(GPIO_AP_CLAIM)) {
				/* We got it, so return */
				i2c_claimed_by_ec = 1;
				return EC_SUCCESS;
			}

			if (!waiting)
				waiting = 1;
		}

		/* It didn't release, so give up, wait, and try again */
		gpio_set_level(GPIO_EC_CLAIM, 1);

		usleep(BUS_WAIT_RETRY_US);
	} while (time_since32(start) < BUS_WAIT_FREE_US);

	gpio_set_level(GPIO_EC_CLAIM, 1);
	usleep(BUS_SLEW_DELAY_US);
	i2c_claimed_by_ec = 0;

	panic_puts("Unable to access I2C bus (arbitration timeout)\n");
	return EC_ERROR_BUSY;
}

void i2c_release(int port)
{
	if (port == I2C_PORT_MASTER) {
		/* Release our claim */
		gpio_set_level(GPIO_EC_CLAIM, 1);
		usleep(BUS_SLEW_DELAY_US);
		i2c_claimed_by_ec = 0;
	}
}

static void i2c_pre_init_hook(void)
{
	gpio_set_flags(GPIO_AP_CLAIM, GPIO_PULL_UP);
	gpio_set_level(GPIO_EC_CLAIM, i2c_claimed_by_ec ? 0 : 1);
	gpio_set_flags(GPIO_EC_CLAIM, GPIO_OUTPUT);
	usleep(BUS_SLEW_DELAY_US);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, i2c_pre_init_hook, HOOK_PRIO_DEFAULT);

static void i2c_shutdown_hook(void)
{
	gpio_set_flags(GPIO_AP_CLAIM, GPIO_INPUT);
	gpio_set_flags(GPIO_EC_CLAIM, GPIO_INPUT);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, i2c_shutdown_hook, HOOK_PRIO_DEFAULT);
