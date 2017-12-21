/* Copyright 2017 The Chromium OS Authors. All rights reserved.
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
#include "tfdp_chip.h"

/*
 * Conversion on a single channel takes less than 12 ms. Set timeout to
 * 15 ms so that we have a 3-ms margin.
 */
#define ADC_SINGLE_READ_TIME 15000

struct mutex adc_lock;

/*
 * Volatile should not be needed.
 * ADC ISR only reads task_waiting.
 * Two other non-ISR routines only write task_waiting when
 * interrupt is disabled or before starting ADC.
 */
static task_id_t task_waiting;

static int start_single_and_wait(int timeout)
{
	int event;

	task_waiting = task_get_current();

	/* Start conversion */
	MCHP_ADC_CTRL |= 1 << 1;

	/* Wait for interrupt */
	event = task_wait_event(timeout);
	task_waiting = TASK_ID_INVALID;
	return event != TASK_EVENT_TIMER;
}

int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;
	int value;

	trace1(0, ADC, 0, "adc_read_channel %d", ch);

	mutex_lock(&adc_lock);

	trace1(0, ADC, 0,
	       "adc_read_channel acquired mutex. Physical channel = %d",
	       adc->channel);

	MCHP_ADC_SINGLE = 1 << adc->channel;

	if (start_single_and_wait(ADC_SINGLE_READ_TIME))
		value = MCHP_ADC_READ(adc->channel) * adc->factor_mul /
			adc->factor_div + adc->shift;
	else
		value = ADC_READ_ERROR;

	trace11(0, ADC, 0,
		"adc_read_channel value = 0x%08X. Releasing mutex", value);

	mutex_unlock(&adc_lock);
	return value;
}

int adc_read_all_channels(int *data)
{
	int i;
	int ret = EC_SUCCESS;
	const struct adc_t *adc;

	trace0(0, ADC, 0, "adc_read_all_channels");

	mutex_lock(&adc_lock);

	trace0(0, ADC, 0, "adc_read_all_channels acquired mutex");

	MCHP_ADC_SINGLE = 0;
	for (i = 0; i < ADC_CH_COUNT; ++i)
		MCHP_ADC_SINGLE |= 1 << adc_channels[i].channel;

	if (!start_single_and_wait(ADC_SINGLE_READ_TIME * ADC_CH_COUNT)) {
		ret = EC_ERROR_TIMEOUT;
		goto exit_all_channels;
	}

	for (i = 0; i < ADC_CH_COUNT; ++i) {
		adc = adc_channels + i;
		data[i] = MCHP_ADC_READ(adc->channel) * adc->factor_mul /
			  adc->factor_div + adc->shift;
		trace12(0, ADC, 0, "adc all: data[%d] = 0x%08X", i, data[i]);
	}

exit_all_channels:
	mutex_unlock(&adc_lock);
	trace0(0, ADC, 0, "adc_read_all_channels released mutex");

	return ret;
}

/*
 * Using MEC1701 direct mode interrupts. Do not
 * set Interrupt Aggregator Block Enable bit
 * for GIRQ containing ADC.
 */
static void adc_init(void)
{
	/* clear ADC sleep enable */
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_ADC);

	/* Activate ADC module */
	MCHP_ADC_CTRL |= 1 << 0;

	/* Enable interrupt */
	task_waiting = TASK_ID_INVALID;
	MCHP_INT_ENABLE(MCHP_ADC_GIRQ) = MCHP_ADC_GIRQ_SINGLE_BIT;
	task_enable_irq(MCHP_IRQ_ADC_SNGL);
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_INIT_ADC);

void adc_interrupt(void)
{
	/* Clear interrupt status bit */
	MCHP_ADC_CTRL |= 1 << 7;

	MCHP_INT_SOURCE(MCHP_ADC_GIRQ) = MCHP_ADC_GIRQ_SINGLE_BIT;

	if (task_waiting != TASK_ID_INVALID)
		task_wake(task_waiting);
}
DECLARE_IRQ(MCHP_IRQ_ADC_SNGL, adc_interrupt, 2);
