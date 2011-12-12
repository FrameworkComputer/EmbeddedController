/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADC module for Chrome EC */

#include "board.h"
#include "console.h"
#include "adc.h"
#include "timer.h"
#include "registers.h"
#include "uart.h"
#include "util.h"


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


int adc_read(enum adc_channel ch)
{
	volatile uint32_t scratch  __attribute__((unused));

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
	if (ch != ADC_CH_POT)
		return ADC_READ_ERROR;

	/* Empty the FIFO of any previous results */
	while (!(LM4_ADC_SSFSTAT(0) & 0x100))
		scratch = LM4_ADC_SSFIFO(0);

	/* Clear the interrupt status */
	LM4_ADC_ADCISC |= 0x01;

	/* Initiate sample sequence */
	LM4_ADC_ADCPSSI |= 0x01;

	/* Wait for interrupt */
	/* TODO: use a real interrupt */
	while (!(LM4_ADC_ADCRIS & 0x01));

	/* Read the FIFO */
	return LM4_ADC_SSFIFO(0);
}


int adc_read_ec_temperature(void)
{
	volatile uint32_t scratch  __attribute__((unused));
	int a;

	/* Empty the FIFO of any previous results */
	while (!(LM4_ADC_SSFSTAT(3) & 0x100))
		scratch = LM4_ADC_SSFIFO(3);

	/* Clear the interrupt status */
	LM4_ADC_ADCISC |= 0x08;

	/* Initiate sample sequence */
	LM4_ADC_ADCPSSI |= 0x08;

	/* Wait for interrupt */
	/* TODO: use a real interrupt */
	/* TODO: timeout */
	while (!(LM4_ADC_ADCRIS & 0x08));

	/* Read the FIFO and convert to temperature */
	a = LM4_ADC_SSFIFO(3);
	return 273 + (295 - (225 * 2 * a) / ADC_READ_MAX) / 2;
}


/*****************************************************************************/
/* Console commands */

static int command_adc(int argc, char **argv)
{
	uart_printf("ADC POT channel = 0x%03x\n",adc_read(ADC_CH_POT));
	return EC_SUCCESS;
}


static int command_ectemp(int argc, char **argv)
{
	int t = adc_read_ec_temperature();
	uart_printf("EC temperature is %d K = %d C\n", t, t-273);
	return EC_SUCCESS;
}


static const struct console_command console_commands[] = {
	{"adc", command_adc},
	{"ectemp", command_ectemp},
};
static const struct console_group command_group = {
	"ADC", console_commands, ARRAY_SIZE(console_commands)
};


/*****************************************************************************/
/* Initialization */

int adc_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

        /* Enable ADC0 module and delay a few clocks */
	LM4_SYSTEM_RCGCADC |= 0x01;
	scratch = LM4_SYSTEM_RCGCADC;

	/* Configure GPIOs */
	configure_gpio();

	/* Use external voltage references (VREFA+, VREFA-) instead of
	 * VDDA and GNDA. */
	LM4_ADC_ADCCTL = 0x01;

	/* TODO: set up clock using ADCCC register? */

	/* Configure sample sequencer 0 */
	LM4_ADC_ADCACTSS &= ~0x01;
	/* Trigger SS0 by processor request */
	LM4_ADC_ADCEMUX = (LM4_ADC_ADCEMUX & 0xfffffff0) | 0x00;
	/* Sample AIN0 only */
	LM4_ADC_SSMUX(0) = ADC_IN_POT & 0x0f;
	LM4_ADC_SSEMUX(0) = (ADC_IN_POT >> 4) & 0x0f;
	LM4_ADC_SSCTL(0) = 0x06;  /* IE0 | END0 */
	/* Enable sample sequencer 0 */
	LM4_ADC_ADCACTSS |= 0x01;

	/* Configure sample sequencer 3 */
	LM4_ADC_ADCACTSS &= ~0x08;
	/* Trigger SS3 by processor request */
	LM4_ADC_ADCEMUX = (LM4_ADC_ADCEMUX & 0xffffff0f) | 0x00;
	/* Sample internal temp sensor */
	LM4_ADC_SSMUX(3) = 0x00;
	LM4_ADC_SSEMUX(3) = 0x00;
	LM4_ADC_SSCTL(3) = 0x0e;  /* TS0 | IE0 | END0 */
	/* Enable sample sequencer 3 */
	LM4_ADC_ADCACTSS |= 0x08;

	console_register_commands(&command_group);
	return EC_SUCCESS;
}
