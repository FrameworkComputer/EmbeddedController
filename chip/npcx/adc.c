/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific ADC module for Chrome EC */

#include "adc.h"
#include "adc_chip.h"
#include "atomic.h"
#include "clock.h"
#include "clock_chip.h"
#include "console.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Maximum time we allow for an ADC conversion */
#define ADC_TIMEOUT_US            SECOND
#define ADC_CLK                   2000000
#define ADC_REGULAR_DLY           0x11
#define ADC_REGULAR_ADCCNF2       0x8B07
#define ADC_REGULAR_GENDLY        0x0100
#define ADC_REGULAR_MEAST         0x0001

/* ADC conversion mode */
enum npcx_adc_conversion_mode {
	ADC_CHN_CONVERSION_MODE   = 0,
	ADC_SCAN_CONVERSION_MODE  = 1
};

/* Global variables */
static volatile task_id_t task_waiting;

/**
 * Preset ADC operation clock.
 *
 * @param   none
 * @return  none
 * @notes   changed when initial or HOOK_FREQ_CHANGE command
 */
void adc_freq_changed(void)
{
	uint8_t prescaler_divider    = 0;

	/* Set clock prescaler divider to ADC module*/
	prescaler_divider = (uint8_t)(clock_get_apb1_freq() / ADC_CLK);
	if (prescaler_divider >= 1)
		prescaler_divider = prescaler_divider - 1;
	if (prescaler_divider > 0x3F)
		prescaler_divider = 0x3F;

	/* Set Core Clock Division Factor in order to obtain the ADC clock */
	SET_FIELD(NPCX_ATCTL, NPCX_ATCTL_SCLKDIV_FIELD, prescaler_divider);
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, adc_freq_changed, HOOK_PRIO_DEFAULT);

/**
 * Flush an ADC sequencer and initiate a read.
 *
 * @param   input_ch    operation channel
 * @param   timeout		preset timeout
 * @return  TRUE/FALSE  success/fail
 * @notes   set SW-triggered interrupt conversion and one-shot mode in npcx chip
 */
static int start_single_and_wait(enum npcx_adc_input_channel input_ch
		, int timeout)
{
	int event;

	task_waiting = task_get_current();

	/* Set ADC conversion code to SW conversion mode */
	SET_FIELD(NPCX_ADCCNF, NPCX_ADCCNF_ADCMD_FIELD,
			ADC_CHN_CONVERSION_MODE);

	/* Set conversion type to one-shot type */
	CLEAR_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCRPTC);

	/* Update number of channel to be converted */
	SET_FIELD(NPCX_ASCADD, NPCX_ASCADD_SADDR_FIELD, input_ch);

	/* Clear End-of-Conversion Event status */
	SET_BIT(NPCX_ADCSTS, NPCX_ADCSTS_EOCEV);

	/* Enable ADC End-of-Conversion Interrupt if applicable */
	SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_INTECEN);

	/* Start conversion */
	SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_START);

	/* Wait for interrupt */
	event = task_wait_event_mask(TASK_EVENT_ADC_DONE, timeout);

	task_waiting = TASK_ID_INVALID;

	return (event == TASK_EVENT_ADC_DONE);

}

/**
 * ADC read specific channel.
 *
 * @param   ch    operation channel
 * @return  ADC converted voltage or error message
 */
int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;
	static struct mutex adc_lock;
	int value;
	uint16_t chn_data;

	mutex_lock(&adc_lock);

	/* Forbid ec enter deep sleep during ADC conversion is proceeding. */
	disable_sleep(SLEEP_MASK_ADC);
	/* Turn on ADC */
	SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCEN);

	if (start_single_and_wait(adc->input_ch, ADC_TIMEOUT_US)) {
		chn_data = NPCX_CHNDAT(adc->input_ch);
		if ((adc->input_ch ==
			GET_FIELD(NPCX_ASCADD, NPCX_ASCADD_SADDR_FIELD))
			&& (IS_BIT_SET(chn_data,
					NPCX_CHNDAT_NEW))) {
			value = GET_FIELD(chn_data, NPCX_CHNDAT_CHDAT_FIELD) *
				adc->factor_mul / adc->factor_div + adc->shift;
		} else {
			value = ADC_READ_ERROR;
		}
	} else {
		value = ADC_READ_ERROR;
	}

	/* Turn off ADC */
	CLEAR_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCEN);
	/* Allow ec enter deep sleep */
	enable_sleep(SLEEP_MASK_ADC);

	mutex_unlock(&adc_lock);

	return value;
}

/**
 * ADC interrupt handler
 *
 * @param   none
 * @return  none
 * @notes   Only handle SW-triggered conversion in npcx chip
 */
void adc_interrupt(void)
{
	if (IS_BIT_SET(NPCX_ADCSTS, NPCX_ADCSTS_EOCEV)) {
		/* Disable End-of-Conversion Interrupt */
		CLEAR_BIT(NPCX_ADCCNF, NPCX_ADCCNF_INTECEN);

		/* Stop conversion */
		SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_STOP);

		/* Clear End-of-Conversion Event status */
		SET_BIT(NPCX_ADCSTS, NPCX_ADCSTS_EOCEV);

		/* Wake up the task which was waiting for the interrupt */
		if (task_waiting != TASK_ID_INVALID)
			task_set_event(task_waiting, TASK_EVENT_ADC_DONE, 0);
	}
}
DECLARE_IRQ(NPCX_IRQ_ADC, adc_interrupt, 4);

/**
 * ADC initial.
 *
 * @param none
 * @return none
 */
static void adc_init(void)
{
	/* Configure pins from GPIOs to ADCs */
	gpio_config_module(MODULE_ADC, 1);

	/* Enable ADC clock (bit4 mask = 0x10) */
	clock_enable_peripheral(CGC_OFFSET_ADC, CGC_ADC_MASK,
			CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* Set Core Clock Division Factor in order to obtain the ADC clock */
	adc_freq_changed();

	/* Set regular speed */
	SET_FIELD(NPCX_ATCTL, NPCX_ATCTL_DLY_FIELD, (ADC_REGULAR_DLY - 1));

	/* Set the other ADC settings */
	NPCX_ADCCNF2 = ADC_REGULAR_ADCCNF2;
	NPCX_GENDLY = ADC_REGULAR_GENDLY;
	NPCX_MEAST = ADC_REGULAR_MEAST;

	task_waiting = TASK_ID_INVALID;

	/* Enable IRQs */
	task_enable_irq(NPCX_IRQ_ADC);
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_INIT_ADC);
