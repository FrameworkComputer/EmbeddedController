/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LM4-specific ADC module for Chrome EC */

#include "adc.h"
#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "lm4_adc.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

extern const struct adc_t adc_channels[ADC_CH_COUNT];

static task_id_t task_waiting_on_ss[LM4_ADC_SEQ_COUNT];

/* GPIO port and mask for AINs. */
const uint32_t ain_port[24][2] = {
	{LM4_GPIO_E, (1<<3)},
	{LM4_GPIO_E, (1<<2)},
	{LM4_GPIO_E, (1<<1)},
	{LM4_GPIO_E, (1<<0)},
	{LM4_GPIO_D, (1<<7)},
	{LM4_GPIO_D, (1<<6)},
	{LM4_GPIO_D, (1<<5)},
	{LM4_GPIO_D, (1<<4)},
	{LM4_GPIO_E, (1<<5)},
	{LM4_GPIO_E, (1<<4)},
	{LM4_GPIO_B, (1<<4)},
	{LM4_GPIO_B, (1<<5)},
	{LM4_GPIO_D, (1<<3)},
	{LM4_GPIO_D, (1<<2)},
	{LM4_GPIO_D, (1<<1)},
	{LM4_GPIO_D, (1<<0)},
	{LM4_GPIO_K, (1<<0)},
	{LM4_GPIO_K, (1<<1)},
	{LM4_GPIO_K, (1<<2)},
	{LM4_GPIO_K, (1<<3)},
	{LM4_GPIO_E, (1<<7)},
	{LM4_GPIO_E, (1<<6)},
	{LM4_GPIO_N, (1<<1)},
	{LM4_GPIO_N, (1<<0)},
};


static void configure_gpio(void)
{
	int i;

	/* Use analog function for AIN */
	for (i = 0; i < ADC_CH_COUNT; ++i) {
		int id = adc_channels[i].channel;
		if (id != LM4_AIN_NONE)
			gpio_set_alternate_function(ain_port[id][0],
						    ain_port[id][1],
						    1);
	}
}


int lm4_adc_flush_and_read(enum lm4_adc_sequencer seq)
{
	/* TODO: right now we have only a single channel so this is
	 * simple.  When we have multiple channels, should we...
	 *
	 * 1) Read them all using a timer interrupt, and then return
	 * the most recent value?  This is lowest-latency for the
	 * caller, but won't return accurate data if read frequently.
	 *
	 * 2) Reserve SS3 for reading a single value, and configure it
	 * on each read?  Needs mutex if we could have multiple
	 * callers; doesn't matter if just used for debugging.
	 *
	 * 3) Both? */
	volatile uint32_t scratch  __attribute__((unused));
	int event;

	/* Empty the FIFO of any previous results */
	while (!(LM4_ADC_SSFSTAT(seq) & 0x100))
		scratch = LM4_ADC_SSFIFO(seq);

	/* TODO: This assumes we don't have multiple tasks accessing
	 * the same sequencer. Add mutex lock if needed. */
	task_waiting_on_ss[seq] = task_get_current();

	/* Clear the interrupt status */
	LM4_ADC_ADCISC |= 0x01 << seq;

	/* Initiate sample sequence */
	LM4_ADC_ADCPSSI |= 0x01 << seq;

	/* Wait for interrupt */
	event = task_wait_event(1000000);
	task_waiting_on_ss[seq] = TASK_ID_INVALID;
	if (event == TASK_EVENT_TIMER)
		return ADC_READ_ERROR;

	/* Read the FIFO and convert to temperature */
	return LM4_ADC_SSFIFO(seq);
}


int lm4_adc_configure(enum lm4_adc_sequencer seq,
		      int ain_id,
		      int ssctl)
{
	/* Configure sample sequencer */
	LM4_ADC_ADCACTSS &= ~(0x01 << seq);

	/* Trigger sequencer by processor request */
	LM4_ADC_ADCEMUX = (LM4_ADC_ADCEMUX & ~(0xf << (seq * 4))) | 0x00;

	/* Sample internal temp sensor */
	if (ain_id != LM4_AIN_NONE) {
		LM4_ADC_SSMUX(seq) = ain_id & 0xf;
		LM4_ADC_SSEMUX(seq) = ain_id >> 4;
	} else {
		LM4_ADC_SSMUX(seq) = 0x00;
		LM4_ADC_SSEMUX(seq) = 0x00;
	}
	LM4_ADC_SSCTL(seq) = ssctl;

	/* Enable sample sequencer */
	LM4_ADC_ADCACTSS |= 0x01 << seq;

	return EC_SUCCESS;
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

/* Handles an interrupt on the specified sample sequencer. */
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

#ifdef CONSOLE_COMMAND_ECTEMP
static int command_ectemp(int argc, char **argv)
{
	int t = adc_read_channel(ADC_CH_EC_TEMP);
	ccprintf("EC temperature is %d K = %d C\n", t, t-273);
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

static int adc_init(void)
{
	int i;
	const struct adc_t *adc;

	/* Configure GPIOs */
	configure_gpio();

	/*
	 * Temporarily enable the PLL when turning on the clock to the ADC
	 * module, to work around chip errata (10.4).  No need to notify
	 * other modules; the PLL isn't enabled long enough to matter.
	 */
	clock_enable_pll(1, 0);

	/* Enable ADC0 module and delay a few clocks. */
	LM4_SYSTEM_RCGCADC = 1;
	clock_wait_cycles(3);

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

	/* Initialize ADC sequencer */
	for (i = 0; i < ADC_CH_COUNT; ++i) {
		adc = adc_channels + i;
		lm4_adc_configure(adc->sequencer, adc->channel, adc->flag);
	}

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_DEFAULT);
