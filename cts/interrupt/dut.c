/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include "common.h"
#include "cts_common.h"
#include "gpio.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

static int got_interrupt;
static int wake_me_up;
static int state_index;
static char state[4];

/*
 * Raw busy loop. Returns 1 if loop finishes before interrupt is triggered.
 * Loop length is controlled by busy_loop_timeout. It has to be set to the
 * value which makes the loop last longer than CTS_INTERRUPT_TRIGGER_DELAY_US.
 */
static int busy_loop(void)
{
	/* TODO: Derive a proper value from clock speed */
	const uint32_t busy_loop_timeout = 0xfffff;
	uint32_t counter = 0;

	while (counter++ < busy_loop_timeout) {
		if (got_interrupt)
			break;
		watchdog_reload();
	}
	if (counter > busy_loop_timeout)
		return 1;

	return 0;
}

/*
 * Interrupt handler.
 */
void cts_irq1(enum gpio_signal signal)
{
	state[state_index++] = 'B';

	got_interrupt = in_interrupt_context();

	/* Wake up the CTS task */
	if (wake_me_up)
		task_wake(TASK_ID_CTS);

	busy_loop();

	state[state_index++] = 'C';
}

void cts_irq2(enum gpio_signal signal)
{
	state[state_index++] = 'A';
	busy_loop();
	state[state_index++] = 'D';
}

void clean_state(void)
{
	uint32_t *event;

	interrupt_enable();
	got_interrupt = 0;
	wake_me_up = 0;
	state_index = 0;
	memset(state, '_', sizeof(state));
	event = task_get_event_bitmap(TASK_ID_CTS);
	*event = 0;
}

enum cts_rc test_task_wait_event(void)
{
	uint32_t event;

	wake_me_up = 1;

	/* Sleep and wait for interrupt. This shouldn't time out. */
	event = task_wait_event(CTS_INTERRUPT_TRIGGER_DELAY_US * 2);
	if (event != TASK_EVENT_WAKE) {
		CPRINTS("Woken up by unexpected event: 0x%08x", event);
		return CTS_RC_FAILURE;
	}
	if (!got_interrupt) {
		CPRINTS("Interrupt context not detected");
		return CTS_RC_TIMEOUT;
	}

	return CTS_RC_SUCCESS;
}

enum cts_rc test_task_disable_irq(void)
{
	uint32_t event;

	wake_me_up = 1;

	task_disable_irq(CTS_IRQ_NUMBER);
	/* Sleep and wait for interrupt. This should time out. */
	event = task_wait_event(CTS_INTERRUPT_TRIGGER_DELAY_US * 2);
	if (event != TASK_EVENT_TIMER) {
		CPRINTS("Woken up by unexpected event: 0x%08x", event);
		return CTS_RC_FAILURE;
	}
	task_enable_irq(CTS_IRQ_NUMBER);

	return CTS_RC_SUCCESS;
}

enum cts_rc test_interrupt_enable(void)
{
	if (busy_loop()) {
		CPRINTS("Timeout before interrupt");
		return CTS_RC_TIMEOUT;
	}
	return CTS_RC_SUCCESS;
}

enum cts_rc test_interrupt_disable(void)
{
	interrupt_disable();
	if (!busy_loop()) {
		CPRINTS("Expected timeout but didn't");
		return CTS_RC_FAILURE;
	}
	return CTS_RC_SUCCESS;
}

enum cts_rc test_nested_interrupt_low_high(void)
{
	uint32_t event;

	event = task_wait_event(CTS_INTERRUPT_TRIGGER_DELAY_US * 4);
	if (event != TASK_EVENT_TIMER) {
		CPRINTS("Woken up by unexpected event: 0x%08x", event);
		return CTS_RC_FAILURE;
	}
	if (!got_interrupt) {
		CPRINTS("Interrupt context not detected");
		return CTS_RC_TIMEOUT;
	}
	if (memcmp(state, "ABCD", sizeof(state))) {
		CPRINTS("State transition differs from expectation");
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}

enum cts_rc test_nested_interrupt_high_low(void)
{
	uint32_t event;

	event = task_wait_event(CTS_INTERRUPT_TRIGGER_DELAY_US * 4);
	if (event != TASK_EVENT_TIMER) {
		CPRINTS("Woken up by unexpected event: 0x%08x", event);
		return CTS_RC_FAILURE;
	}

	if (memcmp(state, "BCAD", sizeof(state))) {
		CPRINTS("State transition differs from expectation");
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	gpio_enable_interrupt(GPIO_CTS_IRQ1);
	gpio_enable_interrupt(GPIO_CTS_IRQ2);
	cts_main_loop(tests, "Interrupt");
	task_wait_event(-1);
}
