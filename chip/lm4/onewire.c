/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* 1-wire interface module for Chrome EC */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"

#if !defined(CONFIG_ONEWIRE_BANK) || !defined(CONFIG_ONEWIRE_PIN)
#error Unsupported board. CONFIG_ONEWIRE_BANK/PIN need to be defined.
#endif

/*
 * Standard speed; all timings padded by 2 usec for safety.
 *
 * Note that these timing are actually _longer_ than legacy 1-wire standard
 * speed because we're running the 1-wire bus at 3.3V instead of 5V.
 */
#define T_RSTL 602  /* Reset low pulse; 600-960 us */
#define T_MSP   72  /* Presence detect sample time; 70-75 us */
#define T_RSTH (68 + 260 + 5 + 2) /* Reset high; tPDHmax + tPDLmax + tRECmin */
#define T_SLOT  70  /* Timeslot; >67 us */
#define T_W0L   63  /* Write 0 low; 62-120 us */
#define T_W1L    7  /* Write 1 low; 5-15 us */
#define T_RL     7  /* Read low; 5-15 us */
#define T_MSR    9  /* Read sample time; <15 us.  Must be at least 200 ns after
		     * T_RL since that's how long the signal takes to be pulled
		     * up on our board.  */

/**
 * Output low on the bus for <usec> us, then switch back to open-drain input.
 */
static void output0(int usec)
{
	LM4_GPIO_DIR(CONFIG_ONEWIRE_BANK) |= CONFIG_ONEWIRE_PIN;
	LM4_GPIO_DATA(CONFIG_ONEWIRE_BANK, CONFIG_ONEWIRE_PIN) = 0;
	udelay(usec);
	LM4_GPIO_DIR(CONFIG_ONEWIRE_BANK) &= ~CONFIG_ONEWIRE_PIN;
}

/**
 *  Read the signal line.
 */
static int readline(void)
{
	return LM4_GPIO_DATA(CONFIG_ONEWIRE_BANK, CONFIG_ONEWIRE_PIN) ? 1 : 0;
}

/**
 * Read a bit.
 */
static int readbit(void)
{
	int bit;

	/*
	 * The delay between sending the output pulse and reading the bit is
	 * extremely timing sensitive, so disable interrupts.
	 */
	interrupt_disable();

	/* Output low */
	output0(T_RL);

	/* Delay to let slave release the line if it wants to send a 1-bit */
	udelay(T_MSR - T_RL);

	/* Read bit */
	bit = readline();

	/*
	 * Enable interrupt as soon as we've read the bit.  The delay to the
	 * end of the timeslot is a lower bound, so additional latency here is
	 * harmless.
	 */
	interrupt_enable();

	/* Delay to end of timeslot */
	udelay(T_SLOT - T_MSR);
	return bit;
}

/**
 * Write a bit.
 */
static void writebit(int bit)
{
	/*
	 * The delays in the output-low signal for sending 0 and 1 bits are
	 * extremely timing sensitive, so disable interrupts during that time.
	 * Interrupts can be enabled again as soon as the output is switched
	 * back to open-drain, since the delay for the rest of the timeslot is
	 * a lower bound.
	 */
	if (bit) {
		interrupt_disable();
		output0(T_W1L);
		interrupt_enable();
		udelay(T_SLOT - T_W1L);
	} else {
		interrupt_disable();
		output0(T_W0L);
		interrupt_enable();
		udelay(T_SLOT - T_W0L);
	}

}

int onewire_reset(void)
{
	/* Start transaction with master reset pulse */
	output0(T_RSTL);

	/* Wait for presence detect sample time.
	 *
	 * (Alternately, we could poll waiting for a 1-bit indicating our pulse
	 * has let go, then poll up to max time waiting for a 0-bit indicating
	 * the slave has responded.)
	 */
	udelay(T_MSP);

	if (readline() != 0)
		return EC_ERROR_UNKNOWN;

	/*
	 * Wait for end of presence pulse.
	 *
	 * (Alternately, we could poll waiting for a 1-bit.)
	 */
	udelay(T_RSTH - T_MSP);

	return EC_SUCCESS;
}

int onewire_read(void)
{
	int data = 0;
	int i;

	for (i = 0; i < 8; i++)
		data |= readbit() << i;  /* LSB first */

	return data;
}

void onewire_write(int data)
{
	int i;

	for (i = 0; i < 8; i++)
		writebit((data >> i) & 0x01);  /* LSB first */
}

static void onewire_init(void)
{
	/* Configure 1-wire pin as open-drain GPIO */
	gpio_set_alternate_function(CONFIG_ONEWIRE_BANK,
				    CONFIG_ONEWIRE_PIN, -1);
	LM4_GPIO_ODR(CONFIG_ONEWIRE_BANK) |= CONFIG_ONEWIRE_PIN;
}
DECLARE_HOOK(HOOK_INIT, onewire_init, HOOK_PRIO_DEFAULT);
