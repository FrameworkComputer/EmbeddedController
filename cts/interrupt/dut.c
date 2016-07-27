/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "task.h"
#include "dut_common.h"
#include "timer.h"
#include "watchdog.h"

static int got_interrupt;
static int wake_me_up;

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
void cts_irq(enum gpio_signal signal)
{
	/* test some APIs */
	got_interrupt = in_interrupt_context();

	/* Wake up the CTS task */
	if (wake_me_up)
		task_wake(TASK_ID_CTS);
}

enum cts_rc test_task_wait_event(void)
{
	uint32_t event;

	wake_me_up = 1;

	/* Sleep and wait for interrupt. This shouldn't time out. */
	event = task_wait_event(CTS_INTERRUPT_TRIGGER_DELAY_US * 2);
	if (event != TASK_EVENT_WAKE) {
		CPRINTS("Woke up by 0x%08x", event);
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
		CPRINTS("Woke up by 0x%08x", event);
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

#include "cts_testlist.h"

void cts_task(void)
{
	enum cts_rc rc;
	int i;

	gpio_enable_interrupt(GPIO_CTS_IRQ);
	interrupt_enable();
	for (i = 0; i < CTS_TEST_ID_COUNT; i++) {
		got_interrupt = 0;
		wake_me_up = 0;
		sync();
		rc = tests[i].run();
		interrupt_enable();
		CPRINTF("\n%s %d\n", tests[i].name, rc);
		cflush();
	}

	CPRINTS("Interrupt test suite finished");
	cflush();

	while (1) {
		watchdog_reload();
		sleep(1);
	}
}
