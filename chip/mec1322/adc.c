/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/*
 * Conversion on a single channel takes less than 12 ms. Set timeout to
 * 15 ms so that we have a 3-ms margin.
 */
#define ADC_SINGLE_READ_TIME 15000

struct mutex adc_lock;

static volatile task_id_t task_waiting;

static int start_single_and_wait(int timeout)
{
	int event;

	task_waiting = task_get_current();

	/* Start conversion */
	MEC1322_ADC_CTRL |= BIT(1);

	/* Wait for interrupt */
	event = task_wait_event(timeout);
	task_waiting = TASK_ID_INVALID;
	return event != TASK_EVENT_TIMER;
}

int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;
	int value;

	mutex_lock(&adc_lock);

	MEC1322_ADC_SINGLE = 1 << adc->channel;

	if (start_single_and_wait(ADC_SINGLE_READ_TIME))
		value = MEC1322_ADC_READ(adc->channel) * adc->factor_mul /
			adc->factor_div + adc->shift;
	else
		value = ADC_READ_ERROR;

	mutex_unlock(&adc_lock);
	return value;
}

static void adc_init(void)
{
	/* Activate ADC module */
	MEC1322_ADC_CTRL |= BIT(0);

	/* Enable interrupt */
	task_waiting = TASK_ID_INVALID;
	MEC1322_INT_ENABLE(17) |= BIT(10);
	MEC1322_INT_BLK_EN |= BIT(17);
	task_enable_irq(MEC1322_IRQ_ADC_SNGL);
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_INIT_ADC);

void adc_interrupt(void)
{
	/* Clear interrupt status bit */
	MEC1322_ADC_CTRL |= BIT(7);

	if (task_waiting != TASK_ID_INVALID)
		task_wake(task_waiting);
}
DECLARE_IRQ(MEC1322_IRQ_ADC_SNGL, adc_interrupt, 2);
