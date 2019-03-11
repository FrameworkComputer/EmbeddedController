/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
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

/*
 * Start ADC single-shot conversion.
 * 1. Disable ADC interrupt.
 * 2. Clear sticky hardware status.
 * 3. Start conversion.
 * 4. Enable interrupt.
 * 5. Wait with timeout for ADC ISR to
 *    to set TASK_EVENT_TIMER.
 */
static int start_single_and_wait(int timeout)
{
	int event;

	MCHP_INT_DISABLE(MCHP_ADC_GIRQ) = MCHP_ADC_GIRQ_SINGLE_BIT;
	task_waiting = task_get_current();

	/* clear all R/W1C channel status */
	MCHP_ADC_STS = 0xffffu;
	/* clear R/W1C single done status */
	MCHP_ADC_CTRL |= BIT(7);
	/* clear GIRQ single status */
	MCHP_INT_SOURCE(MCHP_ADC_GIRQ) = MCHP_ADC_GIRQ_SINGLE_BIT;
	/* make sure all writes are issued before starting conversion */
	asm volatile ("dsb");

	/* Start conversion */
	MCHP_ADC_CTRL |= BIT(1);

	MCHP_INT_ENABLE(MCHP_ADC_GIRQ) = MCHP_ADC_GIRQ_SINGLE_BIT;

	/* Wait for interrupt, ISR disables interrupt */
	event = task_wait_event(timeout);
	task_waiting = TASK_ID_INVALID;
	return event != TASK_EVENT_TIMER;
}

int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;
	int value;

	mutex_lock(&adc_lock);

	MCHP_ADC_SINGLE = 1 << adc->channel;

	if (start_single_and_wait(ADC_SINGLE_READ_TIME))
		value = (MCHP_ADC_READ(adc->channel) * adc->factor_mul) /
			adc->factor_div + adc->shift;
	else
		value = ADC_READ_ERROR;

	mutex_unlock(&adc_lock);
	return value;
}

int adc_read_all_channels(int *data)
{
	int i;
	int ret = EC_SUCCESS;
	const struct adc_t *adc;

	mutex_lock(&adc_lock);

	MCHP_ADC_SINGLE = 0;
	for (i = 0; i < ADC_CH_COUNT; ++i)
		MCHP_ADC_SINGLE |= 1 << adc_channels[i].channel;

	if (!start_single_and_wait(ADC_SINGLE_READ_TIME * ADC_CH_COUNT)) {
		ret = EC_ERROR_TIMEOUT;
		goto exit_all_channels;
	}

	for (i = 0; i < ADC_CH_COUNT; ++i) {
		adc = adc_channels + i;
		data[i] = (MCHP_ADC_READ(adc->channel) * adc->factor_mul) /
			  adc->factor_div + adc->shift;
	}

exit_all_channels:
	mutex_unlock(&adc_lock);

	return ret;
}

/*
 * Enable GPIO pins.
 * Using MEC17xx direct mode interrupts. Do not
 * set Interrupt Aggregator Block Enable bit
 * for GIRQ containing ADC.
 */
static void adc_init(void)
{
	trace0(0, ADC, 0, "adc_init");

	gpio_config_module(MODULE_ADC, 1);

	/* clear ADC sleep enable */
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_ADC);

	/* Activate ADC module */
	MCHP_ADC_CTRL |= BIT(0);

	/* Enable interrupt */
	task_waiting = TASK_ID_INVALID;
	MCHP_INT_ENABLE(MCHP_ADC_GIRQ) = MCHP_ADC_GIRQ_SINGLE_BIT;
	task_enable_irq(MCHP_IRQ_ADC_SNGL);
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_INIT_ADC);

void adc_interrupt(void)
{
	MCHP_INT_DISABLE(MCHP_ADC_GIRQ) = MCHP_ADC_GIRQ_SINGLE_BIT;

	/* clear individual chan conversion status */
	MCHP_ADC_STS = 0xffffu;

	/* Clear interrupt status bit */
	MCHP_ADC_CTRL |= BIT(7);

	MCHP_INT_SOURCE(MCHP_ADC_GIRQ) = MCHP_ADC_GIRQ_SINGLE_BIT;

	if (task_waiting != TASK_ID_INVALID)
		task_wake(task_waiting);
}
DECLARE_IRQ(MCHP_IRQ_ADC_SNGL, adc_interrupt, 2);
