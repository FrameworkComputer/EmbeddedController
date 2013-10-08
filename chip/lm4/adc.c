/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LM4-specific ADC module for Chrome EC */

#include "adc.h"
#include "clock.h"
#include "console.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "lm4_adc.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

static task_id_t task_waiting_on_ss[LM4_ADC_SEQ_COUNT];

static void configure_gpio(void)
{
	int i, port, mask;

	/* Use analog function for AIN */
	for (i = 0; i < ADC_CH_COUNT; ++i) {
		if (adc_channels[i].gpio_mask) {
			mask = adc_channels[i].gpio_mask;
			port = adc_channels[i].gpio_port;
			LM4_GPIO_DEN(port) &= ~mask;
			LM4_GPIO_AMSEL(port) |= mask;
		}
	}
}

/**
 * Flush an ADC sequencer and initiate a read.
 *
 * @param seq		Sequencer to read
 * @return Raw ADC value.
 */
static int lm4_adc_flush_and_read(enum lm4_adc_sequencer seq)
{
	/*
	 * TODO: right now we have only a single channel so this is simple.
	 * When we have multiple channels, should we...
	 *
	 * 1) Read them all using a timer interrupt, and then return the most
	 * recent value?  This is lowest-latency for the caller, but won't
	 * return accurate data if read frequently.
	 *
	 * 2) Reserve SS3 for reading a single value, and configure it on each
	 * read?  Needs mutex if we could have multiple callers; doesn't matter
	 * if just used for debugging.
	 *
	 * 3) Both?
	 */
	volatile uint32_t scratch  __attribute__((unused));
	int event;

	/* Empty the FIFO of any previous results */
	while (!(LM4_ADC_SSFSTAT(seq) & 0x100))
		scratch = LM4_ADC_SSFIFO(seq);

	/*
	 * This assumes we don't have multiple tasks accessing the same
	 * sequencer. Add mutex lock if needed.
	 */
	task_waiting_on_ss[seq] = task_get_current();

	/* Clear the interrupt status */
	LM4_ADC_ADCISC |= 0x01 << seq;

	/* Initiate sample sequence */
	LM4_ADC_ADCPSSI |= 0x01 << seq;

	/* Wait for interrupt */
	event = task_wait_event(SECOND);
	task_waiting_on_ss[seq] = TASK_ID_INVALID;
	if (event == TASK_EVENT_TIMER)
		return ADC_READ_ERROR;

	/* Read the FIFO and convert to temperature */
	return LM4_ADC_SSFIFO(seq);
}

/**
 * Configure an ADC sequencer to be dedicated for an ADC input.
 *
 * @param seq		Sequencer to configure
 * @param ain_id	ADC input to use
 * @param ssctl		Value for sampler sequencer control register
 *
 */
static void lm4_adc_configure(const struct adc_t *adc)
{
	const enum lm4_adc_sequencer seq = adc->sequencer;

	/* Configure sample sequencer */
	LM4_ADC_ADCACTSS &= ~(0x01 << seq);

	/* Trigger sequencer by processor request */
	LM4_ADC_ADCEMUX = (LM4_ADC_ADCEMUX & ~(0xf << (seq * 4))) | 0x00;

	/* Sample internal temp sensor */
	if (adc->channel == LM4_AIN_NONE) {
		LM4_ADC_SSMUX(seq) = 0x00;
		LM4_ADC_SSEMUX(seq) = 0x00;
	} else {
		LM4_ADC_SSMUX(seq) = adc->channel & 0xf;
		LM4_ADC_SSEMUX(seq) = adc->channel >> 4;
	}
	LM4_ADC_SSCTL(seq) = adc->flag;

	/* Enable sample sequencer */
	LM4_ADC_ADCACTSS |= 0x01 << seq;
}

int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;
	int rv = lm4_adc_flush_and_read(adc->sequencer);

	if (rv == ADC_READ_ERROR)
		return ADC_READ_ERROR;
	return rv * adc->factor_mul / adc->factor_div + adc->shift;
}

/*****************************************************************************/
/* Interrupt handlers */

/**
 * Handle an interrupt on the specified sample sequencer.
 */
static void handle_interrupt(int ss)
{
	int id = task_waiting_on_ss[ss];

	/* Clear the interrupt status */
	LM4_ADC_ADCISC = (0x1 << ss);

	/* Wake up the task which was waiting on the interrupt, if any */
	if (id != TASK_ID_INVALID)
		task_wake(id);
}

static void ss0_interrupt(void) { handle_interrupt(0); }
static void ss1_interrupt(void) { handle_interrupt(1); }
static void ss2_interrupt(void) { handle_interrupt(2); }
static void ss3_interrupt(void) { handle_interrupt(3); }

DECLARE_IRQ(LM4_IRQ_ADC0_SS0, ss0_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_ADC0_SS1, ss1_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_ADC0_SS2, ss2_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_ADC0_SS3, ss3_interrupt, 2);

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_ECTEMP
static int command_ectemp(int argc, char **argv)
{
	int t = adc_read_channel(ADC_CH_EC_TEMP);
	ccprintf("EC temperature is %d K = %d C\n", t, K_TO_C(t));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ectemp, command_ectemp,
			NULL,
			"Print EC temperature",
			NULL);
#endif

static int command_adc(int argc, char **argv)
{
	int i;

	for (i = 0; i < ADC_CH_COUNT; ++i)
		ccprintf("ADC channel \"%s\" = %d\n",
			 adc_channels[i].name, adc_read_channel(i));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(adc, command_adc,
			NULL,
			"Print ADC channels",
			NULL);

/*****************************************************************************/
/* Initialization */

static void adc_init(void)
{
	int i;

	/* Configure GPIOs */
	configure_gpio();

	/*
	 * Temporarily enable the PLL when turning on the clock to the ADC
	 * module, to work around chip errata (10.4).  No need to notify
	 * other modules; the PLL isn't enabled long enough to matter.
	 */
	clock_enable_pll(1, 0);

	/* Enable ADC0 module in run and sleep modes. */
	clock_enable_peripheral(CGC_OFFSET_ADC, 0x1,
			CGC_MODE_RUN | CGC_MODE_SLEEP);

	/*
	 * Use external voltage references (VREFA+, VREFA-) instead of
	 * VDDA and GNDA.
	 */
	LM4_ADC_ADCCTL = 0x01;

	/* Use internal oscillator */
	LM4_ADC_ADCCC = 0x1;

	/* Disable the PLL now that the ADC is using the internal oscillator */
	clock_enable_pll(0, 0);

	/* No tasks waiting yet */
	for (i = 0; i < LM4_ADC_SEQ_COUNT; i++)
		task_waiting_on_ss[i] = TASK_ID_INVALID;

	/* Enable interrupt */
	LM4_ADC_ADCIM = 0xF;
	task_enable_irq(LM4_IRQ_ADC0_SS0);
	task_enable_irq(LM4_IRQ_ADC0_SS1);
	task_enable_irq(LM4_IRQ_ADC0_SS2);
	task_enable_irq(LM4_IRQ_ADC0_SS3);

	/* 2**6 = 64x oversampling */
	LM4_ADC_ADCSAC = 6;

	/* Initialize ADC sequencer */
	for (i = 0; i < ADC_CH_COUNT; ++i)
		lm4_adc_configure(adc_channels + i);
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_DEFAULT);
