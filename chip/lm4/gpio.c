/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "gpio.h"
#include "registers.h"
#include "task.h"
#include "timer.h"

enum debounce_isr_id
{
	DEBOUNCE_LID,
	DEBOUNCE_ISR_ID_MAX
};

struct debounce_isr_t
{
	/* TODO: Add a carry bit to indicate timestamp overflow */
	timestamp_t tstamp;
	int started;
	void (*callback)(void);
};

struct debounce_isr_t debounce_isr[DEBOUNCE_ISR_ID_MAX];

static void lid_switch_isr(void)
{
	/* TODO: Currently we pass through the LID_SW# pin to R_EC_LID_OUT#
	         directly. Modify this if we need to consider more conditions.
		 */
	uint32_t val = LM4_GPIO_DATA(LM4_GPIO_K, 0x20);
	if (val) {
		LM4_GPIO_DATA(LM4_GPIO_F, 0x1) = 0x1;
	}
	else {
		LM4_GPIO_DATA(LM4_GPIO_F, 0x1) = 0x0;
	}
}

int gpio_pre_init(void)
{
	/* Enable clock to GPIO block A */
	LM4_SYSTEM_RCGCGPIO |= 0x0001;

	/* Turn off the LED before we make it an output */
	gpio_set(EC_GPIO_DEBUG_LED, 0);

	/* Clear GPIOAFSEL bits for block A pin 7 */
	LM4_GPIO_AFSEL(LM4_GPIO_A) &= ~(0x80);

	/* Set GPIO to digital enable, output */
	LM4_GPIO_DEN(LM4_GPIO_A) |= 0x80;
	LM4_GPIO_DIR(LM4_GPIO_A) |= 0x80;

	/* Set up LID switch input (block K pin 5) */
	LM4_GPIO_PCTL(LM4_GPIO_K) &= ~(0xf00000);
	LM4_GPIO_DIR(LM4_GPIO_K) &= ~(0x20);
	LM4_GPIO_PUR(LM4_GPIO_K) |= 0x20;
	LM4_GPIO_DEN(LM4_GPIO_K) |= 0x20;
	LM4_GPIO_IM(LM4_GPIO_K) |= 0x20;
	LM4_GPIO_IBE(LM4_GPIO_K) |= 0x20;

	/* Block F pin 0 is NMI pin, so we have to unlock GPIO Lock register and
	   set the bit in GPIOCR register first. */
	LM4_GPIO_LOCK(LM4_GPIO_F) = 0x4c4f434b;
	LM4_GPIO_CR(LM4_GPIO_F) |= 0x1;
	LM4_GPIO_LOCK(LM4_GPIO_F) = 0x0;

	/* Set up LID switch output (block F pin 0) */
	LM4_GPIO_PCTL(LM4_GPIO_F) &= ~(0xf);
	LM4_GPIO_DATA(LM4_GPIO_F, 0x1) =
		(LM4_GPIO_DATA(LM4_GPIO_K, 0x20) ? 1 : 0);
	LM4_GPIO_DIR(LM4_GPIO_F) |= 0x1;
	LM4_GPIO_DEN(LM4_GPIO_F) |= 0x1;

	return EC_SUCCESS;
}


int gpio_init(void)
{
	debounce_isr[DEBOUNCE_LID].started = 0;
	debounce_isr[DEBOUNCE_LID].callback = lid_switch_isr;

	return EC_SUCCESS;
}


int gpio_get(enum gpio_signal signal, int *value_ptr)
{
	switch (signal) {
	case EC_GPIO_DEBUG_LED:
		*value_ptr = (LM4_GPIO_DATA(LM4_GPIO_A, 0x80) & 0x80 ? 1 : 0);
		return EC_SUCCESS;
	default:
		return EC_ERROR_UNKNOWN;
	}
}


int gpio_set(enum gpio_signal signal, int value)
{
	switch (signal) {
	case EC_GPIO_DEBUG_LED:
		LM4_GPIO_DATA(LM4_GPIO_A, 0x80) = (value ? 0x80 : 0);
		return EC_SUCCESS;
	default:
		return EC_ERROR_UNKNOWN;
	}
}

static void gpio_interrupt(int port, uint32_t mis)
{
	timestamp_t timelimit;

	/* Set 30 ms debounce timelimit */
	timelimit = get_time();
	timelimit.val += 30000;

	/* Handle interrupts */
	if (port == LM4_GPIO_K && (mis & 0x20)) {
		debounce_isr[DEBOUNCE_LID].tstamp = timelimit;
		debounce_isr[DEBOUNCE_LID].started = 1;
	}
}

static void __gpio_k_interrupt(void)
{
	uint32_t mis = LM4_GPIO_MIS(LM4_GPIO_K);

	/* Clear the interrupt bits we received */
	LM4_GPIO_ICR(LM4_GPIO_K) = mis;

	gpio_interrupt(LM4_GPIO_K, mis);
}

DECLARE_IRQ(LM4_IRQ_GPIOK, __gpio_k_interrupt, 1);

int gpio_task(void)
{
	int i;
	timestamp_t ts;

	while (1) {
		usleep(1000);
		ts = get_time();
		for (i = 0; i < DEBOUNCE_ISR_ID_MAX; ++i) {
			if (debounce_isr[i].started &&
				ts.val >= debounce_isr[i].tstamp.val) {
				debounce_isr[i].started = 0;
				debounce_isr[i].callback();
			}
		}
	}

	return EC_SUCCESS;
}
