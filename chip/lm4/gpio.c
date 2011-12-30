/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "gpio.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "uart.h"

enum debounce_isr_id
{
	DEBOUNCE_LID,
	DEBOUNCE_PWRBTN,
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

enum power_button_state {
	PWRBTN_STATE_STOPPED = 0,
	PWRBTN_STATE_START = 1,
	PWRBTN_STATE_T0 = 2,
	PWRBTN_STATE_T1 = 3,
	PWRBTN_STATE_T2 = 4,
	PWRBTN_STATE_STOPPING = 5,
};
static enum power_button_state pwrbtn_state = PWRBTN_STATE_STOPPED;
/* The next timestamp to move onto next state if power button is still pressed.
 */
static timestamp_t pwrbtn_next_ts = {0};

#define PWRBTN_DELAY_T0 32000  // 32ms
#define PWRBTN_DELAY_T1 (4000000 - PWRBTN_DELAY_T0)  // 4 secs - t0
#define PWRBTN_DELAY_T2 4000000  // 4 secs


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


/* Power button state machine.
 *
 *   PWRBTN#   ---                      ----
 *     to EC     |______________________|
 *
 *
 *   PWRBTN#   ---  ---------           ----
 *    to PCH     |__|       |___________|
 *                t0    t1       t2
 */
static void set_pwrbtn_to_pch(int high)
{
#if defined(EVT)
	LM4_GPIO_DATA(LM4_GPIO_G, 0x80) = high ? 0x80 : 0;  // PG7 - R_PBTN_OUT#
#else
	uart_printf("[%d] set_pwrbtn_to_pch(%s)\n", get_time().le.lo, high ? "HIGH" : "LOW");
#endif
}

static void pwrbtn_sm_start(void)
{
	pwrbtn_state = PWRBTN_STATE_START;
	pwrbtn_next_ts = get_time();  // execute action now!
}

static void pwrbtn_sm_stop(void)
{
	pwrbtn_state = PWRBTN_STATE_STOPPING;
	pwrbtn_next_ts = get_time();  // execute action now!
}

static void pwrbtn_sm_handle(timestamp_t current)
{
	// Not the time to move onto next state.
	if (pwrbtn_state == PWRBTN_STATE_STOPPED ||
	    current.val < pwrbtn_next_ts.val) return;

	switch (pwrbtn_state) {
	case PWRBTN_STATE_START:
		pwrbtn_next_ts.val = current.val + PWRBTN_DELAY_T0;
		pwrbtn_state = PWRBTN_STATE_T0;
		set_pwrbtn_to_pch(0);
		break;
	case PWRBTN_STATE_T0:
		pwrbtn_next_ts.val = current.val + PWRBTN_DELAY_T1;
		pwrbtn_state = PWRBTN_STATE_T1;
		set_pwrbtn_to_pch(1);
		break;
	case PWRBTN_STATE_T1:
		pwrbtn_next_ts.val = current.val + PWRBTN_DELAY_T2;
		pwrbtn_state = PWRBTN_STATE_T2;
		set_pwrbtn_to_pch(0);
		break;
	case PWRBTN_STATE_T2:
		/* T2 has passed */
	case PWRBTN_STATE_STOPPING:
		set_pwrbtn_to_pch(1);
		pwrbtn_state = PWRBTN_STATE_STOPPED;
		break;
	default:
		break;
	}
}

static void power_button_isr(void)
{
#if defined(EVT)
	uint32_t val = LM4_GPIO_DATA(LM4_GPIO_K, 0x80);  // PK7
#else
	uint32_t val = LM4_GPIO_DATA(LM4_GPIO_C, 0x20);  // PC5
#endif

	if (!val) {
		/* pressed */
		pwrbtn_sm_start();
		/* TODO: implement after chip/lm4/x86_power.c is completed. */
		// if system is in S5, power_on_system()
		// elif system is in S3, resume_system()
		// else S0 i8042_send_host(make_code);
	} else {
		/* released */
		pwrbtn_sm_stop();
		/* TODO: implement after chip/lm4/x86_power.c is completed. */
		// if system in S0, i8042_send_host(break_code);
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

	/* Setup power button input and output pins */
#if defined(EVT)
	/* input: PK7 */
	LM4_GPIO_PCTL(LM4_GPIO_K) &= ~0xf0000000;
	LM4_GPIO_DIR(LM4_GPIO_K) &= ~0x80;
	LM4_GPIO_PUR(LM4_GPIO_K) |= 0x80;
	LM4_GPIO_DEN(LM4_GPIO_K) |= 0x80;
	LM4_GPIO_IM(LM4_GPIO_K) |= 0x80;
	LM4_GPIO_IBE(LM4_GPIO_K) |= 0x80;
	/* output: PG7 */
	LM4_GPIO_PCTL(LM4_GPIO_G) &= ~0xf0000000;
	LM4_GPIO_DATA(LM4_GPIO_G, 0x80) = 0x80;
	LM4_GPIO_DIR(LM4_GPIO_G) |= 0x80;
	LM4_GPIO_DEN(LM4_GPIO_G) |= 0x80;
#else
	/* input: PC5 */
	LM4_GPIO_PCTL(LM4_GPIO_C) &= ~0x00f00000;
	LM4_GPIO_DIR(LM4_GPIO_C) &= ~0x20;
	LM4_GPIO_PUR(LM4_GPIO_C) |= 0x20;
	LM4_GPIO_DEN(LM4_GPIO_C) |= 0x20;
	LM4_GPIO_IM(LM4_GPIO_C) |= 0x20;
	LM4_GPIO_IBE(LM4_GPIO_C) |= 0x20;
#endif

	return EC_SUCCESS;
}


int gpio_init(void)
{
	debounce_isr[DEBOUNCE_LID].started = 0;
	debounce_isr[DEBOUNCE_LID].callback = lid_switch_isr;
	debounce_isr[DEBOUNCE_PWRBTN].started = 0;
	debounce_isr[DEBOUNCE_PWRBTN].callback = power_button_isr;

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

	/* Handle power button */
#if defined(EVT)
	if (port == LM4_GPIO_K && (mis & 0x80)) {  // PK7
#else
	if (port == LM4_GPIO_C && (mis & 0x20)) {  // PC5
#endif
		debounce_isr[DEBOUNCE_PWRBTN].tstamp = timelimit;
		debounce_isr[DEBOUNCE_PWRBTN].started = 1;
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

#if !defined(EVT)
static void __gpio_c_interrupt(void)
{
	uint32_t mis = LM4_GPIO_MIS(LM4_GPIO_C);

	/* Clear the interrupt bits we received */
	LM4_GPIO_ICR(LM4_GPIO_C) = mis;

	gpio_interrupt(LM4_GPIO_C, mis);
}
DECLARE_IRQ(LM4_IRQ_GPIOC, __gpio_c_interrupt, 1);
#endif

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

		pwrbtn_sm_handle(ts);
	}

	return EC_SUCCESS;
}
