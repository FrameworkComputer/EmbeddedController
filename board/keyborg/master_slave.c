/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Master/slave identification */

#include "config.h"
#include "debug.h"
#include "master_slave.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

#define SYNC1 (1 << 1) /* PI1 */
#define SYNC2 (1 << 2) /* PI2 */

static int is_master = -1;

int master_slave_is_master(void)
{
	return is_master;
}

static int wait_sync_signal(int mask, int v, int timeout_ms)
{
	uint32_t start = get_time().le.lo;

	while ((!!(STM32_GPIO_IDR(GPIO_I) & mask)) != v) {
		if ((get_time().le.lo - start) >= timeout_ms * MSEC)
			return EC_ERROR_TIMEOUT;
	}
	return EC_SUCCESS;
}

int master_slave_sync_impl(const char *filename, int line, int timeout_ms)
{
	int err = EC_SUCCESS;
	if (is_master) {
		STM32_GPIO_BSRR(GPIO_I) = SYNC1 << 0;
		if (wait_sync_signal(SYNC2, 1, timeout_ms))
			err = EC_ERROR_TIMEOUT;
		STM32_GPIO_BSRR(GPIO_I) = SYNC1 << 16;
		if (wait_sync_signal(SYNC2, 0, 5))
			err = EC_ERROR_TIMEOUT;
	} else {
		if (wait_sync_signal(SYNC1, 1, timeout_ms))
			err = EC_ERROR_TIMEOUT;
		STM32_GPIO_BSRR(GPIO_I) = SYNC2 << 0;
		if (wait_sync_signal(SYNC1, 0, 5))
			err = EC_ERROR_TIMEOUT;
		STM32_GPIO_BSRR(GPIO_I) = SYNC2 << 16;
	}
	if (err != EC_SUCCESS)
		debug_printf("Sync failed at %s:%d\n", filename, line);
	return err;
}

static int master_handshake(void)
{
	uint32_t val;
	int err;

	/* SYNC2 is the sync signal from the slave. Set it to input. */
	val = STM32_GPIO_CRL(GPIO_I);
	val &= ~0x00000f00;
	val |=  0x00000400;
	STM32_GPIO_CRL(GPIO_I) = val;

	err = master_slave_sync(1000);
	err |= master_slave_sync(20);
	err |= master_slave_sync(20);

	return err;
}

static int slave_handshake(void)
{
	uint32_t val;
	int err;

	/*
	 * N_CHG is used to drive SPI_NSS on the master. Set it to
	 * output low.
	 */
	val = STM32_GPIO_CRL(GPIO_A);
	val &= ~0x000000f0;
	val |= 0x00000010;
	STM32_GPIO_CRL(GPIO_A) = val;
	STM32_GPIO_BSRR(GPIO_A) = 1 << (1 + 16);

	/* SYNC1 is the sync signal from the master. Set it to input. */
	val = STM32_GPIO_CRL(GPIO_I);
	val &= ~0x000000f0;
	val |=  0x00000040;
	STM32_GPIO_CRL(GPIO_I) = val;

	err = master_slave_sync(1000);
	err |= master_slave_sync(20);
	err |= master_slave_sync(20);

	return err;
}

static void master_slave_check(void)
{
	/*
	 * Master slave identity check:
	 *   - Master has USB_PU connected to N_CHG through 1.5K
	 *     resistor. USB_PU is initially low, so N_CHG is low.
	 *   - Slave has N_CHG connected to master NSS with a 20K
	 *     pull-up. Master NSS is initially Hi-Z, so N_CHG is
	 *     high.
	 */

	if (STM32_GPIO_IDR(GPIO_A) & (1 << 1) /* N_CHG */) {
		debug_printf("I'm slave\n");
		is_master = 0;
	} else {
		debug_printf("I'm master\n");
		is_master = 1;
	}
}

int master_slave_init(void)
{
	int handshake_err;

	master_slave_check();

	if (is_master)
		handshake_err = master_handshake();
	else
		handshake_err = slave_handshake();

	if (handshake_err != EC_SUCCESS)
		debug_printf("handshake error\n");
	else
		debug_printf("handshake done\n");

	return handshake_err;
}

