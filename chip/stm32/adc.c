/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "board.h"
#include "console.h"
#include "dma.h"
#include "hooks.h"
#include "registers.h"
#include "stm32_adc.h"
#include "task.h"
#include "timer.h"
#include "util.h"

struct mutex adc_lock;

static const struct dma_option dma_adc_option = {
	DMAC_ADC, (void *)&STM32_ADC_DR,
	DMA_MSIZE_HALF_WORD | DMA_PSIZE_HALF_WORD
};

static inline void adc_set_channel(int sample_id, int channel)
{
	uint32_t mask, val;
	volatile uint32_t *sqr_reg;

	if (sample_id < 6) {
		mask = 0x1f << (sample_id * 5);
		val = channel << (sample_id * 5);
		sqr_reg = &STM32_ADC_SQR3;
	} else if (sample_id < 12) {
		mask = 0x1f << ((sample_id - 6) * 5);
		val = channel << ((sample_id - 6) * 5);
		sqr_reg = &STM32_ADC_SQR2;
	} else {
		mask = 0x1f << ((sample_id - 12) * 5);
		val = channel << ((sample_id - 12) * 5);
		sqr_reg = &STM32_ADC_SQR1;
	}

	*sqr_reg = (*sqr_reg & ~mask) | val;
}

static void adc_configure(int ain_id)
{
	/* Set ADC channel */
	adc_set_channel(0, ain_id);

	/* Disable DMA */
	STM32_ADC_CR2 &= ~(1 << 8);

	/* Disable scan mode */
	STM32_ADC_CR1 &= ~(1 << 8);
}

static void adc_configure_all(void)
{
	int i;

	/* Set ADC channels */
	STM32_ADC_SQR1 = (ADC_CH_COUNT - 1) << 20;
	for (i = 0; i < ADC_CH_COUNT; ++i)
		adc_set_channel(i, adc_channels[i].channel);

	/* Enable DMA */
	STM32_ADC_CR2 |= (1 << 8);

	/* Enable scan mode */
	STM32_ADC_CR1 |= (1 << 8);
}

static inline int adc_powered(void)
{
	return STM32_ADC_CR2 & (1 << 0);
}

static inline int adc_conversion_ended(void)
{
	return STM32_ADC_SR & (1 << 1);
}

int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;
	int value;

	if (!adc_powered())
		return EC_ERROR_UNKNOWN;

	mutex_lock(&adc_lock);

	adc_configure(adc->channel);

	/* Clear EOC bit */
	STM32_ADC_SR &= ~(1 << 1);

	/* Start conversion */
	STM32_ADC_CR2 |= (1 << 0); /* ADON */

	/* Wait for EOC bit set */
	while (!adc_conversion_ended())
		;
	value = STM32_ADC_DR & ADC_READ_MAX;

	mutex_unlock(&adc_lock);
	return value * adc->factor_mul / adc->factor_div + adc->shift;
}

int adc_read_all_channels(int *data)
{
	int i;
	int16_t raw_data[ADC_CH_COUNT];
	const struct adc_t *adc;

	if (!adc_powered())
		return EC_ERROR_UNKNOWN;

	mutex_lock(&adc_lock);

	adc_configure_all();

	dma_start_rx(&dma_adc_option, ADC_CH_COUNT, raw_data);

	/* Start conversion */
	STM32_ADC_CR2 |= (1 << 0); /* ADON */

	if (dma_wait(DMAC_ADC)) {
		mutex_unlock(&adc_lock);
		return EC_ERROR_UNKNOWN;
	}

	for (i = 0; i < ADC_CH_COUNT; ++i) {
		adc = adc_channels + i;
		data[i] = raw_data[i] * adc->factor_mul / adc->factor_div +
			  adc->shift;
	}

	mutex_unlock(&adc_lock);
	return EC_SUCCESS;
}

static int adc_init(void)
{
	/*
	 * Enable ADC clock.
	 * APB2 clock is 16MHz. ADC clock prescaler is /2.
	 * So the ADC clock is 8MHz.
	 */
	STM32_RCC_APB2ENR |= (1 << 9);

	if (!adc_powered()) {
		/* Power on ADC module */
		STM32_ADC_CR2 |= (1 << 0);  /* ADON */

		/* Reset calibration */
		STM32_ADC_CR2 |= (1 << 3);  /* RSTCAL */
		while (STM32_ADC_CR2 & (1 << 3))
			;

		/* A/D Calibrate */
		STM32_ADC_CR2 |= (1 << 2);  /* CAL */
		while (STM32_ADC_CR2 & (1 << 2))
			;
	}

	/* Set right alignment */
	STM32_ADC_CR2 &= ~(1 << 11);

	/*
	 * Set sample time of all channels to 1.5 cycles.
	 * Conversion takes 1.75 us.
	 */
	STM32_ADC_SMPR1 = 0;
	STM32_ADC_SMPR2 = 0;

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_DEFAULT);

static int command_adc(int argc, char **argv)
{
	int i;
	int data[ADC_CH_COUNT];

	if (adc_read_all_channels(data))
		return EC_ERROR_UNKNOWN;
	for (i = 0; i < ADC_CH_COUNT; ++i)
		ccprintf("ADC channel \"%s\" = %d\n",
			 adc_channels[i].name, data[i]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(adc, command_adc,
			NULL,
			"Print ADC channels",
			NULL);
