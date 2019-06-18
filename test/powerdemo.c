/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power state machine demo module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "powerdemo.h"
#include "task.h"
#include "timer.h"
#include "registers.h"

static volatile enum {
	POWER_STATE_IDLE = 0,    /* Idle */
	POWER_STATE_DOWN1,       /* Assert output for 1ms */
	POWER_STATE_UP1,         /* Deassert output for 1ms */
	POWER_STATE_DOWN10,      /* Assert output for 10ms */
	POWER_STATE_UP5,         /* Deassert output for 5ms */
	POWER_STATE_DOWN15,      /* Assert output for 15ms */
	POWER_STATE_WAIT,        /* Wait for button to be released */
	POWER_STATE_DOWN2        /* Assert output for 2ms */
} state = POWER_STATE_IDLE;


/* Stops the timer. */
static void __stop_timer(void)
{
	/* Disable timer A */
	LM4_TIMER_CTL(7) &= ~0x01;
	/* Clear any pending interrupts */
	LM4_TIMER_ICR(7) = LM4_TIMER_RIS(7);
}


/* Starts the timer with the specified delay.  If the timer is already
 * started, resets it. */
static void __start_timer(int usec)
{
	/* Stop the timer, if it was started */
	__stop_timer();
	/* Set the delay, counting function overhead */
	LM4_TIMER_TAILR(7) = usec;
	/* Start timer A */
	LM4_TIMER_CTL(7) |= 0x01;
}


static void __set_state(int new_state, int pin_value, int timeout)
{
	LM4_GPIO_DATA(LM4_GPIO_D, 0x08) = (pin_value ? 0x08 : 0);
	if (timeout)
		__start_timer(timeout);
	else
		__stop_timer();
	state = new_state;
}


int power_demo_init(void)
{
	volatile uint32_t scratch __attribute__((unused));

	/* Set up TIMER1 as our state timer */
	/* Enable TIMER1 clock */
	LM4_SYSTEM_RCGCWTIMER |= 0x02;
	/* wait 3 clock cycles before using the module */
	scratch = LM4_SYSTEM_RCGCWTIMER;
	/* Ensure timer is disabled : TAEN = TBEN = 0 */
	LM4_TIMER_CTL(7) &= ~0x101;
	/* 32-bit timer mode */
	LM4_TIMER_CFG(7) = 4;
	/* Set the prescaler to increment every microsecond */
	LM4_TIMER_TAPR(7) = clock_get_freq() / SECOND;
	/* One-shot, counting down */
	LM4_TIMER_TAMR(7) = 0x01;
	/* Set overflow interrupt */
	LM4_TIMER_IMR(7) = 0x1;

	/* Enable clock to GPIO module D */
	LM4_SYSTEM_RCGCGPIO |= 0x0008;

	/* Clear GPIOAFSEL and enable digital function for pins 0-3 */
	LM4_GPIO_AFSEL(LM4_GPIO_D) &= ~0x0f;
	LM4_GPIO_DEN(LM4_GPIO_D) |= 0x0f;

	/* Set pins 0-2 as input, pin 3 as output */
	LM4_GPIO_DIR(LM4_GPIO_D) = (LM4_GPIO_DIR(LM4_GPIO_D) & ~0x0f) | 0x08;

	/* Set pin 0 to edge-sensitive, both edges, pull-up */
	LM4_GPIO_IS(LM4_GPIO_D) &= ~0x01;
	LM4_GPIO_IBE(LM4_GPIO_D) |= 0x01;
	LM4_GPIO_PUR(LM4_GPIO_D) |= 0x01;

	/* Move to idle state */
	__set_state(POWER_STATE_IDLE, 1, 0);

	/* Enable interrupt on pin 0 */
	LM4_GPIO_IM(LM4_GPIO_D) |= 0x01;

	return EC_SUCCESS;
}


/* GPIO interrupt handler */
static void __gpio_d_interrupt(void)
{
	uint32_t mis = LM4_GPIO_MIS(LM4_GPIO_D);

	/* Clear the interrupt bits we're handling */
	LM4_GPIO_ICR(LM4_GPIO_D) = mis;

	/* Handle edges */
	if (mis & 0x01) {
		if (LM4_GPIO_DATA(LM4_GPIO_D, 0x01)) {
			if (state == POWER_STATE_WAIT)
				__set_state(POWER_STATE_DOWN2, 0, 2000 - 28);
		} else {
			if (state == POWER_STATE_IDLE)
				__set_state(POWER_STATE_DOWN1, 0, 1000 - 28);
		}
	}
}

DECLARE_IRQ(LM4_IRQ_GPIOD, __gpio_d_interrupt, 1);


/* Timer interrupt handler */
static void __timer_w1_interrupt(void)
{
	uint32_t mis = LM4_TIMER_RIS(7);
	/* Clear the interrupt reasons we're handling */
	LM4_TIMER_ICR(7) = mis;

	/* Transition to next state */
	switch (state) {
	case POWER_STATE_IDLE:
	case POWER_STATE_WAIT:
		/* Ignore timer events when waiting for GPIO edges */
		break;
	case POWER_STATE_DOWN1:
		__set_state(POWER_STATE_UP1, 1, 1000 - 28);
		break;
	case POWER_STATE_UP1:
		__set_state(POWER_STATE_DOWN10, 0, 10000 - 228);
		break;
	case POWER_STATE_DOWN10:
		__set_state(POWER_STATE_UP5, 1, 5000 - 128);
		break;
	case POWER_STATE_UP5:
		__set_state(POWER_STATE_DOWN15, 0, 15000 - 328);
		break;
	case POWER_STATE_DOWN15:
		if (LM4_GPIO_DATA(LM4_GPIO_D, 0x01)) {
			/* Button has already been released; go straight to
			 * idle */
			__set_state(POWER_STATE_IDLE, 1, 0);
		} else {
			/* Wait for button release */
			__set_state(POWER_STATE_WAIT, 1, 0);
		}
		break;
	case POWER_STATE_DOWN2:
		__set_state(POWER_STATE_IDLE, 1, 0);
		break;
	}
}

DECLARE_IRQ(LM4_IRQ_TIMERW1A, __timer_w1_interrupt, 1);

int power_demo_task(void)
{
	/* Initialize the peripherals */
	power_demo_init();

	/* suspend this task forever */
	task_wait_event(-1);

	return EC_SUCCESS;
}
