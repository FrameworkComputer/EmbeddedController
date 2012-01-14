/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LM4-specific ADC module for Chrome EC */

#include "lm4_adc.h"
#include "console.h"
#include "adc.h"
#include "timer.h"
#include "registers.h"
#include "uart.h"
#include "util.h"

extern const struct adc_t adc_channels[ADC_CH_COUNT];

static void configure_gpio(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable GPIOE module and delay a few clocks */
	LM4_SYSTEM_RCGCGPIO |= 0x0010;
	scratch = LM4_SYSTEM_RCGCGPIO;

	/* Use analog function for PE3 (AIN0) */
	LM4_GPIO_DEN(LM4_GPIO_E) &= ~0x08;
	LM4_GPIO_AMSEL(LM4_GPIO_E) |= 0x08;
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

	/* Empty the FIFO of any previous results */
	while (!(LM4_ADC_SSFSTAT(seq) & 0x100))
		scratch = LM4_ADC_SSFIFO(seq);

	/* Clear the interrupt status */
	LM4_ADC_ADCISC |= 0x01 << seq;

	/* Initiate sample sequence */
	LM4_ADC_ADCPSSI |= 0x01 << seq;

	/* Wait for interrupt */
	/* TODO: use a real interrupt */
	/* TODO: timeout */
	while (!(LM4_ADC_ADCRIS & (0x01 << seq)));

	/* Read the FIFO and convert to temperature */
	return LM4_ADC_SSFIFO(seq);
}

int lm4_adc_configure(enum lm4_adc_sequencer seq,
		      int ain_id,
		      int ssctl)
{
	volatile uint32_t scratch  __attribute__((unused));
	/* TODO: set up clock using ADCCC register? */
	/* Configure sample sequencer */
	LM4_ADC_ADCACTSS &= ~(0x01 << seq);
	/* Trigger sequencer by processor request */
	LM4_ADC_ADCEMUX = (LM4_ADC_ADCEMUX & ~(0xf << (seq * 4))) | 0x00;
	/* Sample internal temp sensor */
	if (ain_id != LM4_AIN_NONE) {
		LM4_ADC_SSMUX(seq) = ain_id & 0xf;
		LM4_ADC_SSEMUX(seq) = ain_id >> 4;
	}
	else {
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
	return rv * adc->factor_mul / adc->factor_div + adc->shift;
}

/*****************************************************************************/
/* Console commands */

static int command_ectemp(int argc, char **argv)
{
	int t = adc_read_channel(ADC_CH_EC_TEMP);
	uart_printf("EC temperature is %d K = %d C\n", t, t-273);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ectemp, command_ectemp);

static int command_adc(int argc, char **argv)
{
	int i;

	for (i = 0; i < ADC_CH_COUNT; ++i)
		uart_printf("ADC channel \"%s\" = %d\n",
			     adc_channels[i].name,
			     adc_read_channel(i));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(adc, command_adc);

/*****************************************************************************/
/* Initialization */

int adc_init(void)
{
	int i;
	const struct adc_t *adc;

        /* Enable ADC0 module and delay a few clocks */
	LM4_SYSTEM_RCGCADC |= 0x01;
	udelay(1);

	/* Configure GPIOs */
	configure_gpio();

	/* Use external voltage references (VREFA+, VREFA-) instead of
	 * VDDA and GNDA. */
	LM4_ADC_ADCCTL = 0x01;

	/* Initialize ADC sequencer */
	for (i = 0; i < ADC_CH_COUNT; ++i) {
		adc = adc_channels + i;
		lm4_adc_configure(adc->sequencer, adc->channel, adc->flag);
	}

	return EC_SUCCESS;
}
