/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cts_common.h"
#include "gpio.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

void clean_state(void)
{
	gpio_set_level(GPIO_OUTPUT_TEST, 1);
	gpio_set_level(GPIO_CTS_IRQ2, 1);
}

static void trigger_interrupt1(void)
{
	crec_usleep(CTS_INTERRUPT_TRIGGER_DELAY_US);
	gpio_set_level(GPIO_OUTPUT_TEST, 0);
	crec_usleep(CTS_INTERRUPT_TRIGGER_DELAY_US);
}

static void trigger_interrupt2(void)
{
	crec_usleep(CTS_INTERRUPT_TRIGGER_DELAY_US);
	gpio_set_level(GPIO_CTS_IRQ2, 0);
	crec_usleep(CTS_INTERRUPT_TRIGGER_DELAY_US);
}

enum cts_rc test_task_wait_event(void)
{
	trigger_interrupt1();
	return CTS_RC_SUCCESS;
}

enum cts_rc test_task_disable_irq(void)
{
	trigger_interrupt1();
	return CTS_RC_SUCCESS;
}

enum cts_rc test_interrupt_enable(void)
{
	trigger_interrupt1();
	return CTS_RC_SUCCESS;
}

enum cts_rc test_interrupt_disable(void)
{
	trigger_interrupt1();
	return CTS_RC_SUCCESS;
}

enum cts_rc test_nested_interrupt_low_high(void)
{
	trigger_interrupt2();
	trigger_interrupt1();
	return CTS_RC_SUCCESS;
}

enum cts_rc test_nested_interrupt_high_low(void)
{
	trigger_interrupt1();
	trigger_interrupt2();
	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	gpio_set_flags(GPIO_OUTPUT_TEST, GPIO_ODR_HIGH);
	cts_main_loop(tests, "Interrupt");
	task_wait_event(-1);
}
